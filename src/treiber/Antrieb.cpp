/*
 * Antrieb.cpp - siehe Antrieb.h
 *
 * Schritterzeugung: PWM-Kanal am PUL-Pin, fester Takt 1 µs, Impulsbreite 5 µs.
 *   Die Periode (TOP) übernimmt der RP2040 erst am Periodenende, dadurch gibt es
 *   beim Beschleunigen keine zerschnittenen Impulse. Jede Periode beginnt mit dem
 *   Impuls; gezählt wird beim Beginn (Freigabe bzw. PWM-Überlauf-Interrupt).
 *
 * Alle Funktionen hier laufen auf Kern 1. Kern 0 greift nur über das Postfach
 * (sende/stopp) und die Ereignis-Warteschlange zu.
 */
#include "Antrieb.h"
#include <hardware/pwm.h>
#include <hardware/gpio.h>
#include <hardware/irq.h>
#include <hardware/clocks.h>
#include <hardware/sync.h>
#include <hardware/timer.h>
#include <hardware/structs/pwm.h>

namespace antrieb {

static const uint16_t PULS_TAKTE          = 5;       // 5 µs Impulsbreite (Treiber: >= 2,5 µs)
static const uint32_t DIR_VORLAUF_US      = 20;      // DIR so lange vor dem ersten Impuls setzen
static const float    F_MIN_HZ            = 16.0f;   // TOP max. 65535 bei 1 µs
static const float    F_MAX_HZ            = 20000.0f;
static const uint32_t MESSWERT_TIMEOUT_US = 20000;   // ohne Messwerte keine Bewegung
static const uint32_t RAMPE_US            = 1000;    // Rampe 1000x pro Sekunde nachführen
static const uint32_t ENTPRELL_MS         = 50;

// ---------------------------------------------------------------------------
// Zustand (Kern 1)
// ---------------------------------------------------------------------------
static uint8_t pPuls, pDir, pEna, pAlm, pStart, pStopp;
static uint    slice = 0, kanal = 0;
static bool    initOk = false;

static Param P;                                  // aktuelle Parameter

// Bewegung (mit Interrupts geteilt)
static volatile bool    laeuft = false;
static volatile int32_t pos = 0;
static volatile int32_t ziel = 0;
static volatile int32_t richtung = 1;            // +1 ab, -1 auf
static volatile Stopp   stoppGrund = Stopp::KEIN;
static volatile Stopp   letzterStopp = Stopp::KEIN;

// Rampe (nur loop1)
static float    vIst = 0.0f, vMax = 0.0f, vStart = 0.0f, acc = 100.0f;   // Schritte/s, Schritte/s²
static uint32_t letzteRampeUs = 0;

// Kraft (aus dem ADC-Interrupt)
static int32_t           puf4[4] = {0, 0, 0, 0};
static uint8_t           pufIndex = 0;
static int32_t           puf4Summe = 0;
static volatile int32_t  lastMittel = 0;         // Last über Tara, Mittel über 4 Werte
static volatile int32_t  emaQ4 = 0;              // langsames Mittel (x16) als Bezug für den Kontakt
static volatile bool     kontaktScharf = false;
static volatile int32_t  kontaktReferenz = 0;
static volatile uint32_t letzterMesswertUs = 0;
static volatile bool     messwerteDa = false;

// Ablauf
static volatile Zustand zustand = Zustand::BEREIT;
static int32_t startPos = 0, kontaktPos = 0, endPos = 0, endLast = 0;
static Stopp   ueberGrund = Stopp::KEIN;
static Stopp    presseGrund = Stopp::KEIN;          // Ende des Pressens: ZIEL oder KRAFT
static int32_t  presseEndPos = 0;
static uint32_t haltStartMs = 0;
static bool     presseFehlerNachRueck = false;      // Kraft in der Anfahrt: nach der Rückfahrt als Fehler melden
static volatile bool referenziert = false;
static volatile bool alarmAktiv = false;
static volatile bool enaEin = false;

// Taster
static bool     startAlt = false;
static uint32_t startWechselMs = 0;
static bool     startEntprellt = false;

// Postfach Kern 0 -> Kern 1
static Param              befehlParam;
static int32_t            befehlSchritte = 0;
static volatile BefehlTyp befehlTyp = BefehlTyp::KEIN;
static volatile bool      stoppAnfrage = false;

// Aufzeichnung des Pressvorgangs Kern 1 -> Kern 0 (ein Erzeuger im ADC-Interrupt, ein Verbraucher)
static const uint32_t     PROBEN_N = 2048;               // Zweierpotenz, ca. 1 s bei 2000 SPS
static Probe              proben[PROBEN_N];
static volatile uint32_t  probenKopf = 0, probenEnde = 0;
static volatile uint32_t  probenVerloren = 0;
static volatile uint8_t   aufzeichnungPhase = 0;         // 0 = aus, sonst PHASE_*

// Simulation (Kern 1; Modell wird im ADC-Interrupt gelesen)
static const float SIM_WERKZEUG_G_PRO_SCHRITT = 20.0f;   // Steifigkeit des virtuellen Werkzeugs
static int32_t     simWerkzeug = 0;                      // Position der Werkzeugoberkante (Schritte)
static int32_t     simOben = 0;                          // Oberkante des Werkstücks
static float       simInvWeg = 1.0f;                     // 1 / (simWerkzeug - simOben)
static float       simFspitze = 0.0f;                    // Spitzenkraft dieser Pressung (g)
static volatile bool simWerkstueck = false;
static float       simRissX = 2.0f;                      // Lage eines simulierten Risses (0..1, > 1 = kein Riss)
static uint32_t    simZufall = 1;

// Ereignisse Kern 1 -> Kern 0 (ein Erzeuger, ein Verbraucher)
static const uint32_t     EREIGNIS_N = 8;
static Ereignis           ereignisse[EREIGNIS_N];
static volatile uint32_t  ereignisKopf = 0, ereignisEnde = 0;

// ---------------------------------------------------------------------------
// Hilfsfunktionen
// ---------------------------------------------------------------------------
static void melde(EreignisTyp t, Stopp g, int32_t a = 0, int32_t b = 0, int32_t c = 0) {
  const uint32_t k = ereignisKopf;
  if (k - ereignisEnde >= EREIGNIS_N) return;            // voll: verwerfen
  Ereignis &e = ereignisse[k % EREIGNIS_N];
  e.typ = t; e.grund = g; e.a = a; e.b = b; e.c = c;
  __dmb();
  ereignisKopf = k + 1;
}

// Kommen noch Messwerte? Zuerst den Zeitstempel lesen, dann die Uhr: Andersherum kann der
// ADC-Interrupt dazwischen einen neueren Zeitstempel setzen, die Differenz wird negativ und
// läuft (vorzeichenlos) über -> falscher Stopp "keine Messwerte".
static inline bool messwerteVeraltet() {
  if (!messwerteDa) return true;
  const uint32_t t = letzterMesswertUs;
  return (int32_t)(time_us_32() - t) > (int32_t)MESSWERT_TIMEOUT_US;
}

// logischer Pegel -> Pin (bei invertierter Verdrahtung umgekehrt)
static inline void setzeAusgang(uint8_t pin, bool logisch) {
  gpio_put(pin, logisch != (P.invertiert != 0));
}

static inline void pulsRuhepegel() {
  gpio_set_outover(pPuls, P.invertiert ? GPIO_OVERRIDE_HIGH : GPIO_OVERRIDE_LOW);
}

// ENA-Ausgang: Modus 1 = Optokoppler an schaltet den Motor AUS (übliche Logik), 2 = schaltet EIN
static void enaAnwenden() {
  if (P.enaModus == 0) {
    gpio_set_dir(pEna, GPIO_IN);                         // nicht angeschlossen: hochohmig
    return;
  }
  gpio_set_dir(pEna, GPIO_OUT);
  const bool optoAn = (P.enaModus == 1) ? !enaEin : enaEin;
  setzeAusgang(pEna, optoAn);
}

// ----- Simulation -----
static inline float simZufallF() {                       // xorshift32, 0..1
  simZufall ^= simZufall << 13;
  simZufall ^= simZufall >> 17;
  simZufall ^= simZufall << 5;
  return (simZufall >> 8) * (1.0f / 16777216.0f);
}

// Simulierte Last in Counts über Tara (aus dem ADC-Interrupt)
static int32_t simLast() {
  const int32_t p = pos;
  float g = 0.0f;
  if (simWerkstueck && p > simOben) {
    // Werkstück wird bis zur Werkzeugoberkante eingepresst: steiler Anstieg, dann flacher
    float x = (float)(p - simOben) * simInvWeg;
    if (x > 1.0f) x = 1.0f;
    const float r = 1.0f - x;
    float s = 0.35f * x + 0.65f * (1.0f - r * r);
    // Einführspitze an der Fase (bei 8 % des Wegs, glatter Buckel)
    const float b = (x - 0.08f) * (1.0f / 0.06f);
    if (b > -1.0f && b < 1.0f) { const float q = 1.0f - b * b; s += P.simSpitze * q * q; }
    // kleine, glatte Reibungs-Welligkeit (Dreieck, Periode 64 Schritte), am Ende 0
    const float dreieck = 1.0f - (float)abs((int)(p & 63) - 32) * (1.0f / 16.0f);
    s += 0.004f * r * dreieck;
    // simulierter Riss: Kraft fällt schlagartig um 15 % und erholt sich danach
    if (x > simRissX) s *= 1.0f - 0.15f / (1.0f + 80.0f * (x - simRissX));
    g += simFspitze * s;
  }
  if (p > simWerkzeug) g += SIM_WERKZEUG_G_PRO_SCHRITT * (float)(p - simWerkzeug);
  g += P.simRauschenG * (simZufallF() + simZufallF() - 1.0f);
  return (int32_t)lroundf(g * P.countsProGramm);
}

// Neues virtuelles Werkstück mit etwas anderer Höhe und zufälliger Spitzenkraft
static void simWerkstueckEinlegen() {
  float hoehe = P.simHoeheMm * P.schritteProMm * (0.97f + 0.06f * simZufallF());
  // nie über den Anfahrpunkt hinaus, sonst stößt der Eilgang schon auf das Werkstück
  if (P.presseAnfahrt < 0 && hoehe > -0.98f * (float)P.presseAnfahrt) hoehe = -0.98f * (float)P.presseAnfahrt;
  float fmax = P.simFmaxG - 500.0f;                      // Platz für Werkzeugkontakt (ca. 16 Schritte) und Rauschen
  if (fmax < P.simFminG) fmax = P.simFminG;
  const float fspitze = P.simFminG + (fmax - P.simFminG) * simZufallF();
  // ein Anteil simRissAnteil der Pressungen bekommt einen Riss zwischen 30 und 80 % des Wegs
  const float riss = (simZufallF() < P.simRissAnteil) ? 0.3f + 0.5f * simZufallF() : 2.0f;
  const uint32_t irq = save_and_disable_interrupts();
  simRissX = riss;
  simOben = simWerkzeug - (int32_t)lroundf(hoehe < 1.0f ? 1.0f : hoehe);
  simInvWeg = 1.0f / (float)(simWerkzeug - simOben);
  simFspitze = fspitze;
  simWerkstueck = true;
  restore_interrupts(irq);
}

// Parameter übernehmen (nur im Stillstand)
static void parameterUebernehmen(const Param &neu) {
  // Simulation eingeschaltet oder Werkzeugabstand geändert: virtuelles Werkzeug neu setzen
  const bool simStart = neu.simulation && (!P.simulation || neu.simWerkzeugMm != P.simWerkzeugMm);
  const uint32_t irq = save_and_disable_interrupts();    // ISR liest tara/maxCounts
  if (simStart) {                                        // virtuelles Werkzeug unter der aktuellen Position
    simWerkzeug = pos + (int32_t)lroundf(neu.simWerkzeugMm * neu.schritteProMm);
    simWerkstueck = false;
    simZufall = time_us_32() | 1u;
  }
  P = neu;
  restore_interrupts(irq);
  if (!initOk) return;
  pwm_set_output_polarity(slice, kanal == PWM_CHAN_A && P.invertiert, kanal == PWM_CHAN_B && P.invertiert);
  pulsRuhepegel();
  setzeAusgang(pDir, P.dirAb);
  enaAnwenden();
  if (P.almModus != 0) gpio_pull_up(pAlm);
}

static void setzeFrequenz(float f) {
  if (f < F_MIN_HZ) f = F_MIN_HZ;
  if (f > F_MAX_HZ) f = F_MAX_HZ;
  uint32_t top = (uint32_t)(1.0e6f / f + 0.5f);          // Takte pro Periode (1 µs)
  if (top < (uint32_t)PULS_TAKTE + 3u) top = PULS_TAKTE + 3u;
  if (top > 65536u) top = 65536u;
  pwm_set_wrap(slice, (uint16_t)(top - 1u));             // wird am Periodenende übernommen
}

// Sofort anhalten. Aufrufbar aus loop1, PWM- und ADC-Interrupt (alle Kern 1).
static void anhalten(Stopp s) {
  const uint32_t irq = save_and_disable_interrupts();
  if (laeuft) {
    // Läuft gerade ein Impuls, ihn zu Ende ausgeben (max. 5 µs), damit der Treiber ihn sicher zählt
    const uint32_t t0 = time_us_32();
    while (pwm_get_counter(slice) < PULS_TAKTE && time_us_32() - t0 < 10) { }
    pulsRuhepegel();                                     // ab jetzt kein Impuls mehr am Pin
    pwm_set_enabled(slice, false);
    // Überlauf passiert, aber noch nicht gezählt (PWM-Interrupt war blockiert)?
    if (pwm_hw->intr & (1u << slice)) {
      pwm_clear_irq(slice);
      if (pos != ziel) pos = pos + richtung;
    }
    laeuft = false;
    stoppGrund = s;
    letzterStopp = s;
  }
  restore_interrupts(irq);
}

static void pwmIsr() {
  if (!(pwm_hw->ints & (1u << slice))) return;          // geteilter Interrupt: nicht unserer
  pwm_clear_irq(slice);
  if (!laeuft) return;
  pos = pos + richtung;                                  // ein neuer Impuls hat begonnen
  if (pos == ziel) anhalten(Stopp::ZIEL);                // wartet, bis dieser letzte Impuls fertig ist
}

// Fahrt um 'schritte' (+ = ab) mit Höchstgeschwindigkeit vMaxMmS starten.
// 0 Schritte: sofort "Ziel erreicht" (ohne Bewegung).
static bool starten(int32_t schritte, float vMaxMmS) {
  if (laeuft || !initOk) return false;
  if (schritte == 0) { stoppGrund = Stopp::ZIEL; return true; }
  if (P.schritteProMm <= 0.0f) return false;
  if (P.enaModus != 0 && !enaEin) return false;
  if (messwerteVeraltet()) {
    stoppGrund = Stopp::MESSWERTE;
    letzterStopp = Stopp::MESSWERTE;
    return false;
  }

  richtung = (schritte > 0) ? 1 : -1;
  setzeAusgang(pDir, (richtung > 0) ? P.dirAb : !P.dirAb);
  busy_wait_us_32(DIR_VORLAUF_US);

  vMax = vMaxMmS * P.schritteProMm;
  if (vMax < F_MIN_HZ) vMax = F_MIN_HZ;
  if (vMax > F_MAX_HZ) vMax = F_MAX_HZ;
  acc = P.accMmS2 * P.schritteProMm;
  if (acc < 10.0f) acc = 10.0f;
  vStart = sqrtf(acc);                                   // ca. Geschwindigkeit nach einem halben Schritt
  if (vStart < F_MIN_HZ) vStart = F_MIN_HZ;
  if (vStart > vMax) vStart = vMax;
  vIst = vStart;
  setzeFrequenz(vIst);

  const uint32_t irq = save_and_disable_interrupts();
  ziel = pos + schritte;
  stoppGrund = Stopp::KEIN;
  pwm_set_counter(slice, 0);
  pwm_clear_irq(slice);
  laeuft = true;
  gpio_set_outover(pPuls, GPIO_OVERRIDE_NORMAL);
  pwm_set_enabled(slice, true);                          // Impuls 1 beginnt sofort
  pos = pos + richtung;
  if (pos == ziel) anhalten(Stopp::ZIEL);
  restore_interrupts(irq);
  letzteRampeUs = time_us_32();
  return true;
}

// Trapezrampe: beschleunigen bis vMax, rechtzeitig vor dem Ziel abbremsen
static void rampe() {
  const uint32_t jetzt = time_us_32();
  const uint32_t dtUs = jetzt - letzteRampeUs;
  if (dtUs < RAMPE_US) return;
  letzteRampeUs = jetzt;
  const float dt = dtUs * 1.0e-6f;
  const int32_t rest = (ziel - pos) * richtung;          // verbleibende Schritte
  const float bremsweg = vIst * vIst / (2.0f * acc);
  if ((float)rest <= bremsweg) vIst -= acc * dt;
  else if (vIst < vMax)        vIst += acc * dt;
  if (vIst < vStart) vIst = vStart;
  if (vIst > vMax)   vIst = vMax;
  const uint32_t irq = save_and_disable_interrupts();
  if (laeuft) setzeFrequenz(vIst);
  restore_interrupts(irq);
}

static void rueckfahrt() {
  zustand = Zustand::ZURUECK;
  if (!starten(startPos - pos, P.vFahrtMmS)) {
    zustand = Zustand::BEREIT;
    melde(EreignisTyp::KAL_FEHLER, stoppGrund == Stopp::KEIN ? Stopp::BEFEHL : stoppGrund);
  }
}

// ----- Pressvorgang: Teilschritte -----
static void presseAbbruch(Stopp g) {
  aufzeichnungPhase = 0;
  zustand = Zustand::BEREIT;
  melde(EreignisTyp::PRESSE_FEHLER, g == Stopp::KEIN ? Stopp::BEFEHL : g, pos, 0);
}

static void presseRueckfahrt() {
  aufzeichnungPhase = 0;
  zustand = Zustand::PR_ZURUECK;
  if (!starten(startPos - pos, P.vFahrtMmS)) presseAbbruch(stoppGrund);
}

static void presseDruecken() {
  zustand = Zustand::PR_DRUECKEN;
  aufzeichnungPhase = PHASE_DRUECKEN;
  if (!starten(P.presseEnde - pos, P.vPressMmS)) presseAbbruch(stoppGrund);
}

// Reaktion auf das Ende einer Teilfahrt
static void ablauf() {
  if (laeuft || zustand == Zustand::BEREIT) return;
  const Stopp g = stoppGrund;
  switch (zustand) {
    case Zustand::FAHRT:
      zustand = Zustand::BEREIT;
      melde(EreignisTyp::FAHRT_ENDE, g, pos);
      break;

    case Zustand::SUCHE:
      kontaktScharf = false;
      if (g == Stopp::KONTAKT) {
        kontaktPos = pos;
        // Überlauf: bis Kontakt + N Schritte, die Maximalkraft stoppt vorher
        zustand = Zustand::UEBERLAUF;
        if (!starten(P.ueberSchritte, P.vSuchMmS)) {
          zustand = Zustand::BEREIT;
          melde(EreignisTyp::KAL_FEHLER, stoppGrund);
        }
      } else {
        zustand = Zustand::BEREIT;
        melde(EreignisTyp::KAL_FEHLER, g == Stopp::ZIEL ? Stopp::KEIN_KONTAKT : g);
      }
      break;

    case Zustand::UEBERLAUF:
      if (g == Stopp::ZIEL || g == Stopp::KRAFT) {
        endPos = pos;
        endLast = lastMittel;
        ueberGrund = g;
        rueckfahrt();
      } else {
        zustand = Zustand::BEREIT;
        melde(EreignisTyp::KAL_FEHLER, g);
      }
      break;

    case Zustand::ZURUECK:
      zustand = Zustand::BEREIT;
      if (g == Stopp::ZIEL) {
        // Nullpunkt auf den Werkzeug-Endpunkt (erster Kontakt) legen
        const uint32_t irq = save_and_disable_interrupts();
        pos = pos - kontaktPos;
        simWerkzeug -= kontaktPos;                       // Simulation: Welt mitverschieben
        simOben -= kontaktPos;
        restore_interrupts(irq);
        referenziert = true;
        melde(EreignisTyp::KAL_FERTIG, ueberGrund, kontaktPos - startPos, endPos - kontaktPos, endLast);
      } else {
        melde(EreignisTyp::KAL_FEHLER, g);
      }
      break;

    // ----- Pressvorgang -----
    case Zustand::PR_ANFAHRT:
      if (g == Stopp::ZIEL) {
        presseDruecken();
      } else if (g == Stopp::KRAFT) {
        presseFehlerNachRueck = true;                    // Werkstück höher als der Anfahrpunkt
        presseGrund = g;
        presseRueckfahrt();
      } else {
        presseAbbruch(g);
      }
      break;

    case Zustand::PR_DRUECKEN:
      if (g == Stopp::ZIEL || g == Stopp::KRAFT) {
        presseGrund = g;
        presseEndPos = pos;
        if (P.haltMs > 0) {
          zustand = Zustand::PR_HALTEN;
          aufzeichnungPhase = PHASE_HALTEN;
          stoppGrund = Stopp::KEIN;                      // ab hier: Stopp-Befehl/Taster erkennen
          haltStartMs = millis();
        } else {
          presseRueckfahrt();
        }
      } else {
        presseAbbruch(g);
      }
      break;

    case Zustand::PR_HALTEN:
      if (g != Stopp::KEIN) presseAbbruch(g);
      else if (millis() - haltStartMs >= P.haltMs) presseRueckfahrt();
      break;

    case Zustand::PR_ZURUECK:
      zustand = Zustand::BEREIT;
      if (g != Stopp::ZIEL) {
        melde(EreignisTyp::PRESSE_FEHLER, g, pos, 0);
      } else if (presseFehlerNachRueck) {
        melde(EreignisTyp::PRESSE_FEHLER, presseGrund, pos, 1);
      } else {
        melde(EreignisTyp::PRESSE_FERTIG, presseGrund, presseEndPos);
      }
      break;

    default:
      zustand = Zustand::BEREIT;
      break;
  }
}

static void ausfuehren(BefehlTyp t, int32_t schritte, const Param &p) {
  const bool still = !laeuft && zustand == Zustand::BEREIT;
  if (still) parameterUebernehmen(p);
  switch (t) {
    case BefehlTyp::PARAM:
      break;

    case BefehlTyp::ENA_EIN:
    case BefehlTyp::ENA_AUS:
      if (!still) break;
      enaEin = (t == BefehlTyp::ENA_EIN);
      enaAnwenden();
      break;

    case BefehlTyp::NULLPUNKT:
      if (!still) break;
      {
        const uint32_t irq = save_and_disable_interrupts();
        simWerkzeug -= pos;                              // Simulation: Welt mitverschieben
        simOben -= pos;
        pos = 0;
        restore_interrupts(irq);
      }
      referenziert = true;
      break;

    case BefehlTyp::FAHRE:
      if (!still) break;
      if (alarmAktiv) { melde(EreignisTyp::FAHRT_ENDE, Stopp::ALARM, pos); break; }
      zustand = Zustand::FAHRT;
      if (!starten(schritte, P.vFahrtMmS)) {
        zustand = Zustand::BEREIT;
        melde(EreignisTyp::FAHRT_ENDE, stoppGrund == Stopp::KEIN ? Stopp::BEFEHL : stoppGrund, pos);
      }
      break;

    case BefehlTyp::KALIBRIEREN: {
      if (!still) break;
      if (alarmAktiv) { melde(EreignisTyp::KAL_FEHLER, Stopp::ALARM); break; }
      referenziert = false;
      simWerkstueck = false;                             // Simulation: Kalibrieren ohne Werkstück
      startPos = pos;
      kontaktReferenz = emaQ4 >> 4;                      // Last vor Fahrtbeginn (Werkzeug in der Luft)
      kontaktScharf = true;
      zustand = Zustand::SUCHE;
      const int32_t weg = (int32_t)lroundf(P.hubMm * P.schritteProMm);
      if (!starten(weg, P.vSuchMmS)) {
        kontaktScharf = false;
        zustand = Zustand::BEREIT;
        melde(EreignisTyp::KAL_FEHLER, stoppGrund == Stopp::KEIN ? Stopp::BEFEHL : stoppGrund);
      }
      break;
    }

    case BefehlTyp::PRESSEN: {
      if (!still) break;
      if (alarmAktiv) { melde(EreignisTyp::PRESSE_FEHLER, Stopp::ALARM, pos, 0); break; }
      if (!referenziert || P.presseEnde <= pos) { melde(EreignisTyp::PRESSE_FEHLER, Stopp::BEFEHL, pos, 0); break; }
      startPos = pos;
      presseGrund = Stopp::KEIN;
      presseFehlerNachRueck = false;
      if (P.simulation) simWerkstueckEinlegen();         // Simulation: neues Werkstück je Pressung
      if (pos < P.presseAnfahrt && P.presseAnfahrt < P.presseEnde) {
        zustand = Zustand::PR_ANFAHRT;                   // Eilgang bis zum Anfahrpunkt
        if (!starten(P.presseAnfahrt - pos, P.vFahrtMmS)) presseAbbruch(stoppGrund);
      } else {
        presseDruecken();                                // schon unterhalb des Anfahrpunkts
      }
      break;
    }

    default:
      break;
  }
}

static void tasterUndAlarm() {
  // STOPP-Taster (gegen GND, Pull-up): sofort, ohne Entprellen
  if (!gpio_get(pStopp)) {
    if (laeuft) anhalten(Stopp::TASTER);
    kontaktScharf = false;
    if (!laeuft && zustand != Zustand::BEREIT && stoppGrund == Stopp::KEIN) stoppGrund = Stopp::TASTER;
  }

  // START-Taster (gegen GND, Pull-up): entprellt, meldet nur; Kern 0 prüft und startet
  const bool start = !gpio_get(pStart);
  const uint32_t ms = millis();
  if (start != startAlt) { startAlt = start; startWechselMs = ms; }
  if (ms - startWechselMs >= ENTPRELL_MS && start != startEntprellt) {
    startEntprellt = start;
    if (start) melde(EreignisTyp::START_TASTE, Stopp::KEIN);
  }

  // Alarmausgang des Treibers (z. B. Schleppfehler, Überstrom)
  bool alarm = false;
  if (P.almModus == 1) alarm = !gpio_get(pAlm);
  else if (P.almModus == 2) alarm = gpio_get(pAlm);
  if (alarm && laeuft) anhalten(Stopp::ALARM);
  if (alarm && !alarmAktiv) melde(EreignisTyp::ALARM, Stopp::ALARM, pos);
  alarmAktiv = alarm;
}

// ---------------------------------------------------------------------------
// Öffentliche Funktionen (Kern 1)
// ---------------------------------------------------------------------------
void init(uint8_t pinPuls, uint8_t pinDir, uint8_t pinEna, uint8_t pinAlm,
          uint8_t pinStart, uint8_t pinStopp) {
  pPuls = pinPuls; pDir = pinDir; pEna = pinEna; pAlm = pinAlm; pStart = pinStart; pStopp = pinStopp;

  gpio_init(pDir);   gpio_set_dir(pDir, GPIO_OUT);
  gpio_init(pEna);   gpio_set_dir(pEna, GPIO_IN);
  gpio_init(pAlm);   gpio_set_dir(pAlm, GPIO_IN);   gpio_pull_up(pAlm);
  gpio_init(pStart); gpio_set_dir(pStart, GPIO_IN); gpio_pull_up(pStart);
  gpio_init(pStopp); gpio_set_dir(pStopp, GPIO_IN); gpio_pull_up(pStopp);

  // PWM: 1 Takt = 1 µs, Impuls am Periodenanfang
  gpio_set_function(pPuls, GPIO_FUNC_PWM);
  slice = pwm_gpio_to_slice_num(pPuls);
  kanal = pwm_gpio_to_channel(pPuls);
  pwm_config c = pwm_get_default_config();
  pwm_config_set_clkdiv(&c, (float)clock_get_hz(clk_sys) / 1.0e6f);
  pwm_config_set_wrap(&c, 999);
  pwm_init(slice, &c, false);
  pwm_set_chan_level(slice, kanal, PULS_TAKTE);
  initOk = true;
  parameterUebernehmen(P);

  pwm_clear_irq(slice);
  pwm_set_irq_enabled(slice, true);
  irq_add_shared_handler(PWM_IRQ_WRAP, pwmIsr, PICO_SHARED_IRQ_HANDLER_DEFAULT_ORDER_PRIORITY);
  irq_set_enabled(PWM_IRQ_WRAP, true);                   // auf Kern 1 (aufrufender Kern)
}

void schritt() {
  if (!initOk) return;

  if (stoppAnfrage) {
    stoppAnfrage = false;
    kontaktScharf = false;
    if (laeuft) anhalten(Stopp::BEFEHL);
    else if (zustand != Zustand::BEREIT) stoppGrund = Stopp::BEFEHL;
  }

  const BefehlTyp t = befehlTyp;
  if (t != BefehlTyp::KEIN) {
    __dmb();
    const Param p = befehlParam;
    const int32_t s = befehlSchritte;
    __dmb();
    befehlTyp = BefehlTyp::KEIN;
    ausfuehren(t, s, p);
  }

  tasterUndAlarm();

  if (laeuft) {
    if (messwerteVeraltet()) anhalten(Stopp::MESSWERTE);
    else rampe();
  }
  ablauf();
}

void messwertIsr(uint32_t tMikros, int32_t counts) {
  letzterMesswertUs = tMikros;
  messwerteDa = true;
  const int32_t last = P.simulation ? simLast()                   // Simulation: Last aus der Position
                                    : (counts - P.tara) * P.vorzeichen;   // > 0 = Last
  puf4Summe += last - puf4[pufIndex];
  puf4[pufIndex] = last;
  pufIndex = (uint8_t)((pufIndex + 1) & 3);
  const int32_t m = puf4Summe / 4;
  lastMittel = m;
  emaQ4 = emaQ4 + (((last << 4) - emaQ4) / 32);                   // ca. 16 ms bei 2000 SPS

  const uint8_t phase = aufzeichnungPhase;
  if (phase != 0) {
    const uint32_t k = probenKopf;
    if (k - probenEnde < PROBEN_N) {
      Probe &pr = proben[k & (PROBEN_N - 1)];
      pr.tMikros = tMikros; pr.position = pos; pr.last = last; pr.phase = phase;
      __dmb();
      probenKopf = k + 1;
    } else {
      probenVerloren = probenVerloren + 1;
    }
  }

  if (laeuft && richtung > 0) {                                   // nur Abwärtsfahrten begrenzen
    if (last >= P.maxCounts) {
      anhalten(Stopp::KRAFT);
    } else if (kontaktScharf && m - kontaktReferenz >= P.kontaktCounts) {
      kontaktScharf = false;
      anhalten(Stopp::KONTAKT);
    }
  }
}

// ---------------------------------------------------------------------------
// Öffentliche Funktionen (Kern 0)
// ---------------------------------------------------------------------------
bool sende(BefehlTyp typ, int32_t schritte, const Param &p) {
  if (befehlTyp != BefehlTyp::KEIN) return false;
  befehlParam = p;
  befehlSchritte = schritte;
  __dmb();
  befehlTyp = typ;
  return true;
}

void stopp() { stoppAnfrage = true; }

// Notfall (Kern 1 hängt): nur Registerzugriffe über die atomaren Aliase, kein Warten auf Kern 1.
// Position und Zustand stimmen danach nicht mehr - nur vor einem Neustart verwenden.
void notHalt() {
  if (!initOk) return;
  pulsRuhepegel();                 // Pin sofort auf Ruhepegel, auch mitten im Impuls
  pwm_set_enabled(slice, false);
}

Status status() {
  Status s;
  s.zustand      = zustand;
  s.laeuft       = laeuft;
  s.referenziert = referenziert;
  s.alarm        = alarmAktiv;
  s.enaEin       = enaEin;
  s.position     = pos;
  s.lastCounts   = lastMittel;
  s.letzterStopp = letzterStopp;
  return s;
}

bool ereignisHolen(Ereignis &e) {
  const uint32_t ende = ereignisEnde;
  if (ereignisKopf == ende) return false;
  __dmb();
  e = ereignisse[ende % EREIGNIS_N];
  __dmb();
  ereignisEnde = ende + 1;
  return true;
}

bool aufzeichnungHolen(Probe &p) {
  const uint32_t ende = probenEnde;
  if (probenKopf == ende) return false;
  __dmb();
  p = proben[ende & (PROBEN_N - 1)];
  __dmb();
  probenEnde = ende + 1;
  return true;
}

uint32_t aufzeichnungVerloren() { return probenVerloren; }

bool beschaeftigt() {
  return laeuft || zustand != Zustand::BEREIT || befehlTyp != BefehlTyp::KEIN;
}

const char *zustandText(Zustand z) {
  switch (z) {
    case Zustand::BEREIT:    return "bereit";
    case Zustand::FAHRT:     return "Fahrt";
    case Zustand::SUCHE:     return "Kontaktsuche";
    case Zustand::UEBERLAUF: return "Ueberlauf";
    case Zustand::ZURUECK:   return "Rueckfahrt";
    case Zustand::PR_ANFAHRT:  return "Presse: Anfahrt";
    case Zustand::PR_DRUECKEN: return "Presse: Druecken";
    case Zustand::PR_HALTEN:   return "Presse: Halten";
    case Zustand::PR_ZURUECK:  return "Presse: Rueckfahrt";
  }
  return "?";
}

const char *stoppText(Stopp s) {
  switch (s) {
    case Stopp::KEIN:         return "-";
    case Stopp::ZIEL:         return "Ziel erreicht";
    case Stopp::KONTAKT:      return "Kontakt erkannt";
    case Stopp::KRAFT:        return "Maximalkraft erreicht";
    case Stopp::ALARM:        return "Alarm vom Treiber";
    case Stopp::TASTER:       return "STOPP-Taster";
    case Stopp::BEFEHL:       return "Stopp-Befehl";
    case Stopp::MESSWERTE:    return "keine Messwerte";
    case Stopp::KEIN_KONTAKT: return "kein Kontakt im Hub gefunden";
  }
  return "?";
}

}  // namespace antrieb
