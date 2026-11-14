#include "motor.h"

#include <math.h>
#include "konfig.h"
#include "meldung.h"
#include "einstellungen.h"
#include "adc.h"
#include "auftraege.h"

double motorMaxKraftG() {
  return (mot.maxKraftG < NENNLAST_G) ? mot.maxKraftG : NENNLAST_G;   // nie über Nennlast
}

// Parameter für Kern 1; Kraftgrenzen aus aktueller Tara und Faktor in Counts umgerechnet
static antrieb::Param antriebParam() {
  antrieb::Param p;
  p.schritteProMm = mot.schritteProMm;
  p.dirAb         = mot.dirAb;
  p.invertiert    = mot.invertiert;
  p.enaModus      = mot.enaModus;
  p.almModus      = mot.almModus;
  p.vSuchMmS      = mot.vSuchMmS;
  p.vFahrtMmS     = mot.vFahrtMmS;
  p.accMmS2       = mot.accMmS2;
  p.hubMm         = mot.hubMm;
  p.ueberSchritte = mot.ueberSchritte;
  const double fa = fabs((double)cfg.faktor);
  p.tara          = (int32_t)lround(cfg.tara);
  p.vorzeichen    = (cfg.faktor < 0.0f) ? -1 : 1;
  long k = lround(mot.kontaktG * fa);
  p.kontaktCounts = (int32_t)(k < 1 ? 1 : k);
  p.maxCounts     = (int32_t)lround(motorMaxKraftG() * fa);
  return p;
}

bool antriebSenden(antrieb::BefehlTyp t, int32_t schritte) {
  const uint32_t start = millis();
  while (!antrieb::sende(t, schritte, antriebParam())) {
    if (millis() - start > 100) {
      meld.println(F("# FEHLER: Antrieb (Kern 1) nimmt keinen Befehl an."));
      return false;
    }
  }
  return true;
}

// Voraussetzungen für jede Bewegung (die Kraftüberwachung muss zuverlässig sein)
static bool antriebPruefen() {
  if (mot.schritteProMm <= 0.0f) {
    meld.println(F("# Antrieb nicht eingerichtet: zuerst 'mp spmm <Schritte pro mm>' setzen."));
    return false;
  }
  if (!adsOk || !ads.isContinuous()) {
    meld.println(F("# ADC misst nicht - ohne Kraftueberwachung keine Bewegung ('init')."));
    return false;
  }
  if (!cfg.kalibriert) {
    meld.println(F("# Waage nicht kalibriert - ohne gueltigen Faktor sind die Kraftgrenzen unsicher ('k <g>' oder 'f')."));
    return false;
  }
  if (ads.sampleRate() < 500.0f) {
    meld.println(F("# Datenrate unter 500 SPS - fuer schnelle Kraftueberwachung zuerst 'r 2000' einstellen."));
    return false;
  }
  if (auftragLaeuft()) {
    meld.println(F("# Es laeuft ein Auftrag (Tara, Kalibrierung, Rauschtest) - erst abwarten oder 'x'."));
    return false;
  }
  if (antrieb::beschaeftigt()) {
    meld.println(F("# Antrieb faehrt noch - Stopp mit 'x'."));
    return false;
  }
  if (antrieb::status().alarm) {
    meld.println(F("# Treiber meldet Alarm (ALM) - erst Ursache beheben."));
    return false;
  }
  return true;
}

static void antriebKalibrieren() {
  if (!antriebPruefen()) return;
  meld.printf("# Endpunkt-Kalibrierung: langsam ab (%.2f mm/s, max. %.1f mm) bis Lastaenderung > %.0f g,\n",
              mot.vSuchMmS, mot.hubMm, mot.kontaktG);
  meld.printf("#   dann bis Kontakt + %ld Schritte oder %.1f kg, danach zurueck. Abbruch: 'x' oder STOPP-Taster.\n",
              (long)mot.ueberSchritte, motorMaxKraftG() / 1000.0);
  antriebSenden(antrieb::BefehlTyp::KALIBRIEREN, 0);
}

void antriebEreignisse() {
  antrieb::Ereignis e;
  while (antrieb::ereignisHolen(e)) {
    const double spmm = (mot.schritteProMm > 0.0f) ? mot.schritteProMm : 1.0;
    const double fa = fabs((double)cfg.faktor);
    switch (e.typ) {
      case antrieb::EreignisTyp::FAHRT_ENDE:
        meld.printf("# Antrieb: Fahrt beendet (%s), Position %.3f mm\n", antrieb::stoppText(e.grund), e.a / spmm);
        break;

      case antrieb::EreignisTyp::KAL_FERTIG: {
        mot.kalGueltig = 1;
        mot.kalGrund   = (e.grund == antrieb::Stopp::KRAFT) ? 2 : 1;
        mot.kalTiefe   = e.a;
        mot.kalEnde    = e.b;
        mot.kalKraftG  = (float)(e.c / fa);
        meld.println(F("# ============== Endpunkt-Kalibrierung fertig =============="));
        meld.printf("# Werkzeug-Endpunkt (Kontakt): %.3f mm unter der Startposition -> neuer Nullpunkt\n", e.a / spmm);
        meld.printf("# Tatsaechlicher Endpunkt:     +%ld Schritte (%.4f mm) nach dem Kontakt, %.2f kg\n",
                    (long)e.b, e.b / spmm, mot.kalKraftG / 1000.0);
        meld.printf("# Beendet durch:               %s\n",
                    mot.kalGrund == 2 ? "Maximalkraft erreicht" : "Ueberlauf (Schritte) erreicht");
        if (e.b > 0) {
          meld.printf("# Steifigkeit:                 %.2f kg/mm\n", (mot.kalKraftG / 1000.0) / (e.b / spmm));
        }
        meld.println(F("# Antrieb steht wieder auf der Startposition. Mit 's' speichern."));
        meld.println(F("# =========================================================="));
        break;
      }

      case antrieb::EreignisTyp::KAL_FEHLER:
        meld.printf("# Endpunkt-Kalibrierung abgebrochen: %s\n", antrieb::stoppText(e.grund));
        if (e.grund == antrieb::Stopp::KEIN_KONTAKT) {
          meld.printf("#   Innerhalb von %.1f mm keine Lastaenderung ueber %.0f g. Werkzeug, Weg und 'mp kontakt' pruefen.\n",
                      mot.hubMm, mot.kontaktG);
        } else if (e.grund == antrieb::Stopp::KRAFT) {
          meld.println(F("#   Maximalkraft schon vor dem Kontakt - Tara pruefen. Antrieb steht unten: mit 'mot auf <mm>' entlasten."));
        }
        meld.println(F("#   Der Antrieb steht an der Abbruchstelle."));
        break;

      case antrieb::EreignisTyp::START_TASTE:
        meld.println(F("# START-Taster gedrueckt."));
        if (!antrieb::beschaeftigt()) antriebKalibrieren();
        break;

      case antrieb::EreignisTyp::ALARM:
        meld.println(F("# !!! Alarm vom Schrittmotortreiber (ALM) - Fahrt gestoppt. Ursache pruefen (Schleppfehler, Ueberstrom)."));
        break;

      default:
        break;
    }
  }
}

static void motorStatus() {
  const antrieb::Status s = antrieb::status();
  const double spmm = (mot.schritteProMm > 0.0f) ? mot.schritteProMm : 1.0;
  const double fa = fabs((double)cfg.faktor);
  meld.println(F("# ---------------- Antrieb ----------------"));
  meld.printf("# Zustand:        %s%s\n", antrieb::zustandText(s.zustand), s.alarm ? ", ALARM vom Treiber" : "");
  if (mot.schritteProMm > 0.0f) {
    meld.printf("# Position:       %.4f mm (%ld Schritte) %s\n", s.position / spmm, (long)s.position,
                s.referenziert ? "ab Werkzeug-Endpunkt" : "relativ, noch nicht kalibriert");
  } else {
    meld.println(F("# Position:       - (nicht eingerichtet: 'mp spmm')"));
  }
  meld.printf("# Last:           %.2f kg (Mittel ueber 4 Werte)\n", s.lastCounts / fa / 1000.0);
  meld.printf("# Letzter Stopp:  %s\n", antrieb::stoppText(s.letzterStopp));
  if (mot.enaModus) meld.printf("# Motorstrom:     %s\n", s.enaEin ? "ein" : "aus ('mot ein')");
  if (mot.kalGueltig) {
    meld.printf("# Kalibrierung:   Kontakt %.3f mm unter Start, Endpunkt +%ld Schritte bei %.2f kg (%s)\n",
                mot.kalTiefe / spmm, (long)mot.kalEnde, mot.kalKraftG / 1000.0,
                mot.kalGrund == 2 ? "Maximalkraft" : "Schritte");
  } else {
    meld.println(F("# Kalibrierung:   noch keine ('mot kal' oder START-Taster)"));
  }
  meld.println(F("# Parameter mit 'mp', Befehle mit 'h'."));
}

void motorBefehl(const char *a1, const char *a2) {
  if (!a1 || strcmp(a1, "status") == 0) { motorStatus(); return; }
  if (strcmp(a1, "stop") == 0 || strcmp(a1, "x") == 0) { antrieb::stopp(); meld.println(F("# Antrieb: STOPP.")); return; }
  if (strcmp(a1, "kal") == 0) { antriebKalibrieren(); return; }
  if (strcmp(a1, "ein") == 0 || strcmp(a1, "aus") == 0) {
    if (mot.enaModus == 0) { meld.println(F("# ENA nicht benutzt ('mp ena 1' bzw. 'mp ena 2')."));  return; }
    if (antrieb::beschaeftigt()) { meld.println(F("# Antrieb faehrt - erst 'x'.")); return; }
    const bool ein = strcmp(a1, "ein") == 0;
    if (antriebSenden(ein ? antrieb::BefehlTyp::ENA_EIN : antrieb::BefehlTyp::ENA_AUS, 0))
      meld.println(ein ? F("# Motorstrom ein.") : F("# Motorstrom aus (Motor frei drehbar)."));
    return;
  }
  if (strcmp(a1, "null") == 0) {
    if (antrieb::beschaeftigt()) { meld.println(F("# Antrieb faehrt - erst 'x'.")); return; }
    if (antriebSenden(antrieb::BefehlTyp::NULLPUNKT, 0)) meld.println(F("# Aktuelle Position ist jetzt 0."));
    return;
  }
  const bool ab = strcmp(a1, "ab") == 0, auf = strcmp(a1, "auf") == 0, sch = strcmp(a1, "s") == 0;
  if (ab || auf || sch) {
    const float w = a2 ? strtof(a2, nullptr) : 0.0f;
    int32_t schritte;
    if (sch) {
      schritte = (int32_t)lroundf(w);                 // Schritte, + = ab
    } else {
      if (!(w > 0.0f) || w > mot.hubMm) { meld.printf("# Aufruf: mot %s <mm>  (0 < mm <= %.1f)\n", a1, mot.hubMm); return; }
      schritte = (int32_t)lroundf(w * mot.schritteProMm) * (ab ? 1 : -1);
    }
    if (schritte == 0) { meld.println(F("# Weg ist 0 Schritte.")); return; }
    if (!antriebPruefen()) return;
    meld.printf("# Antrieb: %s %.3f mm (%ld Schritte) mit %.2f mm/s\n", schritte > 0 ? "ab" : "auf",
                fabs(schritte / (double)mot.schritteProMm), (long)labs(schritte), mot.vFahrtMmS);
    antriebSenden(antrieb::BefehlTyp::FAHRE, schritte);
    return;
  }
  meld.println(F("# Aufruf: mot [status|kal|stop|ab <mm>|auf <mm>|s <schritte>|null|ein|aus]"));
}

void motorParameter(const char *name, const char *wert) {
  if (!name) {
    meld.println(F("# ---------------- Antrieb-Parameter ('mp <name> <wert>', danach 's') ----------------"));
    meld.printf("#  spmm    %10.3f  Schritte pro mm (Mikroschritt x Uebersetzung / Spindelsteigung), 0 = gesperrt\n", mot.schritteProMm);
    meld.printf("#  dir     %10u  DIR-Pegel fuer 'ab' (Rechtslauf); falsche Richtung -> umschalten\n", mot.dirAb);
    meld.printf("#  inv     %10u  1 = PUL/DIR/ENA invertiert (gemeinsame Anode mit Transistor)\n", mot.invertiert);
    meld.printf("#  ena     %10u  0 = ENA unbenutzt, 1 = Opto an schaltet Motor aus, 2 = Opto an schaltet Motor ein\n", mot.enaModus);
    meld.printf("#  alm     %10u  0 = Alarmeingang aus, 1 = Alarm bei LOW, 2 = Alarm bei HIGH\n", mot.almModus);
    meld.printf("#  vsuch   %10.3f  mm/s Kontaktsuche und Ueberlauf\n", mot.vSuchMmS);
    meld.printf("#  vfahrt  %10.3f  mm/s Tippbetrieb und Rueckfahrt\n", mot.vFahrtMmS);
    meld.printf("#  acc     %10.1f  mm/s^2 Beschleunigung\n", mot.accMmS2);
    meld.printf("#  hub     %10.1f  mm maximaler Suchweg (nutzbarer Hub)\n", mot.hubMm);
    meld.printf("#  ueber   %10ld  Schritte Ueberlauf nach dem Kontakt\n", (long)mot.ueberSchritte);
    meld.printf("#  kontakt %10.0f  g Lastaenderung, die als Kontakt gilt\n", mot.kontaktG);
    meld.printf("#  fmax    %10.0f  g Maximalkraft, Abwaertsfahrt stoppt sofort (wirksam: %.0f g)\n", mot.maxKraftG, motorMaxKraftG());
    if (mot.schritteProMm > 0.0f) {
      meld.printf("# Schrittfrequenz: Suche %.0f Hz, Fahrt %.0f Hz (erlaubt 16..20000 Hz)\n",
                  mot.vSuchMmS * mot.schritteProMm, mot.vFahrtMmS * mot.schritteProMm);
    }
    return;
  }
  if (antrieb::beschaeftigt()) { meld.println(F("# Antrieb faehrt - Parameter erst danach aendern ('x')."));  return; }
  if (!wert) { meld.println(F("# Aufruf: mp <name> <wert>   ('mp' zeigt alle Parameter)")); return; }
  const float w = strtof(wert, nullptr);
  bool ok = isfinite(w);
  if      (strcmp(name, "spmm") == 0)    { ok = ok && w >= 0.0f && w <= 100000.0f; if (ok) mot.schritteProMm = w; }
  else if (strcmp(name, "dir") == 0)     { ok = ok && (w == 0.0f || w == 1.0f);   if (ok) mot.dirAb = (uint8_t)w; }
  else if (strcmp(name, "inv") == 0)     { ok = ok && (w == 0.0f || w == 1.0f);   if (ok) mot.invertiert = (uint8_t)w; }
  else if (strcmp(name, "ena") == 0)     { ok = ok && w >= 0.0f && w <= 2.0f && w == floorf(w); if (ok) mot.enaModus = (uint8_t)w; }
  else if (strcmp(name, "alm") == 0)     { ok = ok && w >= 0.0f && w <= 2.0f && w == floorf(w); if (ok) mot.almModus = (uint8_t)w; }
  else if (strcmp(name, "vsuch") == 0)   { ok = ok && w >= 0.01f && w <= 20.0f;   if (ok) mot.vSuchMmS = w; }
  else if (strcmp(name, "vfahrt") == 0)  { ok = ok && w >= 0.1f && w <= 50.0f;    if (ok) mot.vFahrtMmS = w; }
  else if (strcmp(name, "acc") == 0)     { ok = ok && w >= 1.0f && w <= 1000.0f;  if (ok) mot.accMmS2 = w; }
  else if (strcmp(name, "hub") == 0)     { ok = ok && w >= 1.0f && w <= 45.0f;    if (ok) mot.hubMm = w; }
  else if (strcmp(name, "ueber") == 0)   { ok = ok && w >= 0.0f && w <= 10000.0f; if (ok) mot.ueberSchritte = (int32_t)lroundf(w); }
  else if (strcmp(name, "kontakt") == 0) { ok = ok && w >= 5.0f && w <= 5000.0f;  if (ok) mot.kontaktG = w; }
  else if (strcmp(name, "fmax") == 0)    { ok = ok && w >= 100.0f && w <= NENNLAST_G; if (ok) mot.maxKraftG = w; }
  else { meld.printf("# Unbekannter Parameter '%s' ('mp' zeigt alle).\n", name); return; }
  if (!ok) { meld.printf("# Ungueltiger Wert fuer '%s' ('mp' zeigt die Bereiche).\n", name); return; }
  if (strcmp(name, "hub") == 0 || strcmp(name, "spmm") == 0) mot.kalGueltig = 0;   // altes Ergebnis passt nicht mehr
  antriebSenden(antrieb::BefehlTyp::PARAM, 0);
  meld.printf("# %s = %s (mit 's' speichern)\n", name, wert);
  if (mot.schritteProMm > 0.0f) {
    const float fs = mot.vSuchMmS * mot.schritteProMm, ff = mot.vFahrtMmS * mot.schritteProMm;
    if (fs < 16.0f) meld.printf("# Hinweis: Suchgeschwindigkeit ergibt nur %.1f Hz, es werden 16 Hz gefahren.\n", fs);
    if (ff > 20000.0f) meld.printf("# Hinweis: Fahrgeschwindigkeit ergibt %.0f Hz, begrenzt auf 20000 Hz.\n", ff);
  }
}
