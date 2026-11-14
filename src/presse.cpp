#include "presse.h"

#include <math.h>
#include "konfig.h"
#include "meldung.h"
#include "einstellungen.h"
#include "adc.h"
#include "auftraege.h"
#include "motor.h"
#include "statistik.h"

static constexpr uint8_t ENDE_N = 64;            // Proben für End- und Haltekraft (max.)

// ----- Ablauf -----
static bool     aktiv = false;
static uint32_t startMs = 0;
static uint32_t liveId = 0;
static float    fa = 1.0f;                       // |Faktor| beim Start
static float    spmm = 1.0f;
static int32_t  startSchritt = 0;                // Beginn des Pressens (Schritte)
static uint32_t abschnittSchritte = 1;           // Schritte je Punkt
static uint16_t abschnitte = 1;
static uint32_t verlorenStart = 0;

// ----- Kurve -----
static PressPunkt punkte[PRESSE_PUNKTE_MAX];
static uint16_t   punktAnzahl = 0;               // fertige Punkte (inkl. leerer)
static int32_t    aktIndex = -1;                 // Abschnitt, der gerade gesammelt wird
static double     aktSumme = 0.0;
static uint32_t   aktN = 0;
static uint32_t   aktTms = 0;

// ----- Kennwerte -----
static bool     ersteProbe = true;
static uint32_t t0Us = 0, tLetztUs = 0, tKontaktUs = 0;
static bool     kontakt = false;
static int32_t  kontaktSchritt = 0;
static float    kontaktN = 1.0f;
static Welford  wf;
static float    fMin = 0, fMax = 0;
static float    fMaxGesamt = -1e9f;
static int32_t  fMaxSchritt = 0;
static double   arbeitNmm = 0.0;
static int32_t  letzterSchritt = 0;
static float    ende[ENDE_N], halt[ENDE_N];
static uint8_t  endeN = 0, endeI = 0, haltN = 0, haltI = 0, mittelN = 20;
static uint32_t proben = 0;

bool presseLaeuft() { return aktiv; }
uint16_t          presseLiveAnzahl()  { return punktAnzahl; }
const PressPunkt *presseLivePunkte()  { return punkte; }
float             presseLiveStartMm() { return (startSchritt + 0.5f * abschnittSchritte) / spmm; }
float             presseLivePunktMm() { return abschnittSchritte / spmm; }
uint32_t          presseLiveId()      { return liveId; }

// ===========================================================================
// Voraussetzungen
// ===========================================================================
bool presseBereit(char *grund, size_t n) {
  const antrieb::Status s = antrieb::status();
  const char *g = nullptr;
  if (!adsOk || !ads.isContinuous())             g = "ADC misst nicht ('init')";
  else if (ads.sampleRate() < 500.0f)            g = "Datenrate unter 500 SPS ('r 2000')";
  else if (!cfg.kalibriert && !motorSimulation()) g = "Waage nicht kalibriert ('k <g>')";
  else if (mot.schritteProMm <= 0.0f)            g = "Antrieb nicht eingerichtet ('mp spmm')";
  else if (!motorEndpunktAktuell() || !s.referenziert) g = "Hub nicht kalibriert (nach jedem Einschalten noetig)";
  else if (!sitzungAktiv())                      g = "keine Sitzung aktiv";
  else if (!speicherReichtFuerPresse())          g = "Speicher voll - Sitzungen exportieren und loeschen";
  else if (auftragLaeuft())                      g = "Auftrag laeuft (Tara, Kalibrierung, Rauschtest)";
  else if (aktiv || antrieb::beschaeftigt())     g = "Antrieb faehrt";
  else if (s.alarm)                              g = "Treiber meldet Alarm";
  else if (s.position >= mot.kalEnde)            g = "Antrieb steht nicht ueber dem Endpunkt ('mot auf <mm>')";
  if (g && grund) snprintf(grund, n, "%s", g);
  return g == nullptr;
}

// ===========================================================================
// Start
// ===========================================================================
bool presseStarten(const char *quelle) {
  char grund[80];
  if (!presseBereit(grund, sizeof(grund))) {
    meld.printf("# Pressen nicht moeglich: %s.\n", grund);
    return false;
  }
  const antrieb::Status s = antrieb::status();
  spmm = mot.schritteProMm;
  fa = fabsf(cfg.faktor);
  kontaktN = mot.kontaktG * N_PRO_GRAMM;

  // Pressbereich wie auf Kern 1: ab Anfahrpunkt bzw. ab der aktuellen Position
  const int32_t anfahrt = -(int32_t)lroundf(pres.anfahrtMm * spmm);
  startSchritt = (s.position < anfahrt && anfahrt < mot.kalEnde) ? anfahrt : s.position;
  const uint32_t weg = (uint32_t)(mot.kalEnde - startSchritt);
  abschnittSchritte = (weg + PRESSE_PUNKTE_MAX - 1) / PRESSE_PUNKTE_MAX;
  if (abschnittSchritte < 1) abschnittSchritte = 1;
  abschnitte = (uint16_t)((weg + abschnittSchritte - 1) / abschnittSchritte);
  if (abschnitte < 1) abschnitte = 1;

  // Puffer leeren und Kennwerte zurücksetzen
  antrieb::Probe p;
  while (antrieb::aufzeichnungHolen(p)) { }
  verlorenStart = antrieb::aufzeichnungVerloren();
  punktAnzahl = 0; aktIndex = -1; aktSumme = 0.0; aktN = 0;
  ersteProbe = true; kontakt = false; wf.reset();
  fMaxGesamt = -1e9f; arbeitNmm = 0.0; endeN = endeI = haltN = haltI = 0; proben = 0;
  mittelN = (uint8_t)constrain(lroundf(ads.sampleRate() * 0.01f), 1L, (long)ENDE_N);   // ca. 10 ms

  if (!antriebSenden(antrieb::BefehlTyp::PRESSEN, 0)) return false;
  aktiv = true;
  startMs = millis();
  liveId++;
  meld.printf("# Pressung %lu gestartet (%s): bis %.3f mm mit %.2f mm/s%s%s.\n",
              (unsigned long)(sitzungAktivAnzahl() + 1), quelle, mot.kalEnde / spmm, pres.vPressMmS,
              startSchritt > s.position ? ", davor Eilgang" : "", motorSimulation() ? " - SIMULATION" : "");
  return true;
}

// ===========================================================================
// Proben verarbeiten
// ===========================================================================
static void abschnittAbschliessen() {
  if (aktIndex < 0) return;
  while (punktAnzahl < (uint16_t)aktIndex && punktAnzahl < PRESSE_PUNKTE_MAX) {   // übersprungene Abschnitte
    punkte[punktAnzahl].kraft = PUNKT_LEER;
    punkte[punktAnzahl].tMs = 0;
    punktAnzahl++;
  }
  if (punktAnzahl < PRESSE_PUNKTE_MAX && aktN > 0) {
    const float f = (float)(aktSumme / aktN);
    long k = lroundf(f / KRAFT_N_PRO_STUFE);
    if (k > INT16_MAX) k = INT16_MAX;
    if (k < INT16_MIN + 1) k = INT16_MIN + 1;
    punkte[punktAnzahl].kraft = (int16_t)k;
    punkte[punktAnzahl].tMs = (uint16_t)(aktTms > 65535 ? 65535 : aktTms);
    punktAnzahl++;
  }
  aktSumme = 0.0;
  aktN = 0;
}

static void probeVerarbeiten(const antrieb::Probe &p) {
  const float f = (float)p.last / fa * N_PRO_GRAMM;   // Kraft in N
  proben++;
  if (ersteProbe) { ersteProbe = false; t0Us = p.tMikros; letzterSchritt = p.position; }

  if (f > fMaxGesamt) { fMaxGesamt = f; fMaxSchritt = p.position; }

  if (p.phase == antrieb::PHASE_HALTEN) {
    halt[haltI] = f; haltI = (uint8_t)((haltI + 1) % mittelN); if (haltN < mittelN) haltN++;
    return;
  }

  // ----- Drücken -----
  tLetztUs = p.tMikros;
  if (!kontakt && f >= kontaktN) { kontakt = true; kontaktSchritt = p.position; tKontaktUs = p.tMikros; }
  if (kontakt) {
    if (wf.n == 0) { fMin = fMax = f; }
    if (f < fMin) fMin = f;
    if (f > fMax) fMax = f;
    wf.add(f);
    arbeitNmm += (double)f * (double)(p.position - letzterSchritt) / spmm;
  }
  letzterSchritt = p.position;
  ende[endeI] = f; endeI = (uint8_t)((endeI + 1) % mittelN); if (endeN < mittelN) endeN++;

  // Wegabschnitt
  int32_t idx = (int32_t)((p.position - startSchritt) / (int32_t)abschnittSchritte);
  if (idx < 0) idx = 0;
  if (idx >= abschnitte) idx = abschnitte - 1;
  if (idx != aktIndex) {
    abschnittAbschliessen();
    aktIndex = idx;
    aktTms = (p.tMikros - t0Us) / 1000u;
  }
  aktSumme += f;
  aktN++;
}

void presseSchritt() {
  if (!aktiv) return;
  antrieb::Probe p;
  uint16_t n = 0;
  while (n < 1024 && antrieb::aufzeichnungHolen(p)) { probeVerarbeiten(p); n++; }
}

// ===========================================================================
// Ende: Kennwerte, Meldung, Speichern
// ===========================================================================
static float mittel(const float *v, uint8_t n) {
  if (n == 0) return 0.0f;
  double s = 0;
  for (uint8_t i = 0; i < n; i++) s += v[i];
  return (float)(s / n);
}

static void presseAbschliessen(PressStatus status, antrieb::Stopp grund, int32_t endSchritt) {
  antrieb::Probe p;
  while (antrieb::aufzeichnungHolen(p)) probeVerarbeiten(p);   // Rest aus dem Puffer
  abschnittAbschliessen();
  aktiv = false;
  liveId++;

  // Sofort abgelehnt (z. B. Alarm) und nichts gemessen: nicht als Pressung speichern
  if (proben == 0 && status == PRESS_ABBRUCH) {
    meld.printf("# Pressung nicht gestartet: %s\n", antrieb::stoppText(grund));
    return;
  }

  PressKopf k;
  memset(&k, 0, sizeof(k));
  k.zeit       = zeitJetzt();
  k.endGrund   = (uint8_t)grund;
  k.status     = status;
  k.kontakt    = kontakt ? 1 : 0;
  k.simuliert  = motorSimulation() ? 1 : 0;
  k.startMm    = presseLiveStartMm();
  k.punktMm    = presseLivePunktMm();
  k.kontaktMm  = kontakt ? kontaktSchritt / spmm : NAN;
  k.endMm      = endSchritt / spmm;
  k.tiefeMm    = kontakt ? (endSchritt - kontaktSchritt) / spmm : NAN;
  k.fmaxN      = proben ? fMaxGesamt : NAN;
  k.fmaxMm     = fMaxSchritt / spmm;
  k.fendN      = endeN ? mittel(ende, endeN) : NAN;
  k.fhaltN     = haltN ? mittel(halt, haltN) : NAN;
  k.fmittelN   = wf.n ? (float)wf.mittel : NAN;
  k.fsigmaN    = wf.n > 1 ? (float)wf.stdAbw() : NAN;
  k.fminN      = wf.n ? fMin : NAN;
  k.fspanneN   = wf.n ? fMax - fMin : NAN;
  k.arbeitJ    = (float)(arbeitNmm / 1000.0);
  k.presszeitS = proben ? (tLetztUs - (kontakt ? tKontaktUs : t0Us)) * 1e-6f : NAN;
  k.zykluszeitS = (millis() - startMs) / 1000.0f;
  k.proben     = proben;
  k.verloren   = antrieb::aufzeichnungVerloren() - verlorenStart;
  k.tara       = (float)cfg.tara;
  k.faktor     = cfg.faktor;
  k.schritteProMm = spmm;
  k.sps        = ads.sampleRate();
  k.kalTiefe   = mot.kalTiefe;
  k.kalEnde    = mot.kalEnde;
  k.vPressMmS  = pres.vPressMmS;
  k.anfahrtMm  = pres.anfahrtMm;
  k.haltMs     = pres.haltMs;

  const bool gespeichert = sitzungPresseSpeichern(k, punkte, punktAnzahl);
  const unsigned long nr = gespeichert ? (unsigned long)k.nr : 0UL;
  if (status == PRESS_OK) {
    meld.printf("# Pressung %lu: Fmax %.1f N bei %.3f mm, Fend %.1f N, Kontakt %s, Tiefe %.3f mm, "
                "Arbeit %.3f J, %.2f s (%s)\n", nr, k.fmaxN, k.fmaxMm, k.fendN,
                kontakt ? String(k.kontaktMm, 3).c_str() : "-", k.tiefeMm, k.arbeitJ, k.presszeitS,
                antrieb::stoppText(grund));
  } else {
    meld.printf("# Pressung %lu ABGEBROCHEN: %s%s\n", nr, antrieb::stoppText(grund),
                status == PRESS_KRAFT_ANFAHRT ? " schon im Eilgang - Anfahrpunkt ('mp anfahrt') hoeher setzen" : "");
  }
  if (k.verloren) meld.printf("# Hinweis: %lu Proben gingen verloren (Probenpuffer voll).\n", (unsigned long)k.verloren);
}

void presseEreignis(const antrieb::Ereignis &e) {
  if (!aktiv) return;
  if (e.typ == antrieb::EreignisTyp::PRESSE_FERTIG) {
    presseAbschliessen(PRESS_OK, e.grund, e.a);
  } else {
    const PressStatus st = (e.grund == antrieb::Stopp::KRAFT && e.b == 1) ? PRESS_KRAFT_ANFAHRT : PRESS_ABBRUCH;
    presseAbschliessen(st, e.grund, e.a);
    if (e.b == 0) meld.println(F("#   Der Antrieb steht an der Abbruchstelle - mit 'mot auf <mm>' entlasten."));
  }
}
