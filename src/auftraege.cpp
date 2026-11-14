#include "auftraege.h"

#include <math.h>
#include "konfig.h"
#include "meldung.h"
#include "einstellungen.h"
#include "adc.h"
#include "filter.h"
#include "usb_ausgabe.h"
#include "verarbeitung.h"
#include "web/wlan.h"

static constexpr uint32_t N_MAX = 2000000UL;

const float VERGLEICH_STANDARD[8] = {100, 500, 1000, 2000, 3750, 7500, 15000, 30000};

// Auftrag (Tara, Kalibrierung, Rauschanalyse)
static Auftrag   auftrag = AUFTRAG_KEIN;
static uint32_t  auftragN = 0;
static uint32_t  auftragVerwerfen = 0;    // Einschwingwerte am Anfang überspringen
static float     auftragGewichtG = 0.0f;
static bool      auftragClip = false;
static Statistik stat;

// Rauschvergleich über mehrere Datenraten
static bool           vergleichAktiv = false;
static bool           vergleichWeiter = false;   // nächster Schritt in loop() fällig
static float          vergleichRaten[VERGLEICH_MAX];
static RauschErgebnis vergleichErg[VERGLEICH_MAX];
static uint8_t        vergleichAnzahl = 0;
static uint8_t        vergleichIndex = 0;
static float          vergleichSekunden = 3.0f;
static uint8_t        vergleichStartCode = ADS1256::DR_100;

static void druckeRauschen(const RauschErgebnis &e);
static void druckeRauschZeile(const RauschErgebnis &e);
static void druckeVergleichstabelle();
static void vergleichBeenden(bool abgebrochen);

// ===========================================================================
// Aufträge
// ===========================================================================
bool auftragLaeuft() {
  return auftrag != AUFTRAG_KEIN || vergleichAktiv;
}

void starteAuftrag(Auftrag a, uint32_t n, uint32_t verwerfen) {
  if (n < 2) n = 2;
  if (n > N_MAX) n = N_MAX;
  float sps = ads.sampleRate();
  stat.reset(blockLaenge10());
  auftrag = a;
  auftragN = n;
  auftragVerwerfen = verwerfen;
  auftragClip = false;
  ads.flush();                     // nur Werte ab jetzt auswerten
  messTimeoutNeustart();
  if (a == AUFTRAG_VERGLEICH) {
    meld.printf("# Rauschvergleich %u/%u: %.1f SPS, %lu Messungen (ca. %.1f s) ...\n",
                vergleichIndex + 1, vergleichAnzahl, sps, (unsigned long)n, (double)n / sps);
    return;
  }
  const char *name = (a == AUFTRAG_TARA) ? "Tara" : (a == AUFTRAG_KALIBRIEREN) ? "Kalibrierung" : "Rauschanalyse";
  meld.printf("# %s: nehme %lu Messungen auf (ca. %.1f s), Abbruch mit 'x' ...\n",
              name, (unsigned long)n, (double)n / sps);
}

void starteKalibrierung(float gewichtG, uint32_t n) {
  auftragGewichtG = gewichtG;
  starteAuftrag(AUFTRAG_KALIBRIEREN, n);
}

void auftragAbbrechen(const char *grund) {
  if (!auftragLaeuft()) return;
  auftrag = AUFTRAG_KEIN;
  meld.printf("# Auftrag abgebrochen: %s\n", grund);
  if (vergleichAktiv) vergleichBeenden(true);
}

void auftragUebersteuert() {
  auftragClip = true;
}

RauschErgebnis rauschErgebnis(const Statistik &s, bool clip) {
  RauschErgebnis e;
  e.sps      = ads.sampleRate();
  e.n        = s.roh.n;
  e.verloren = s.verloren();
  e.rateIst  = s.rateIst();
  e.mittel   = s.roh.mittel;
  e.sigma    = s.roh.stdAbw();
  e.minimum  = s.minimum;
  e.maximum  = s.maximum;
  e.bloecke  = s.block.n;
  e.sigma10  = (s.block.n >= 5) ? s.block.stdAbw() : 0.0;
  e.clip     = clip;
  return e;
}

static void auftragFertig() {
  Auftrag a = auftrag;
  auftrag = AUFTRAG_KEIN;

  const double uvProCount = ads.microvoltsPerCount();
  const double sigma = stat.roh.stdAbw();
  uint32_t verloren = stat.verloren();

  if (auftragClip) {
    meld.println(F("# WARNUNG: Waehrend der Aufnahme war der ADC uebersteuert - Ergebnis unbrauchbar!"));
  }
  if (verloren > 0 && a != AUFTRAG_VERGLEICH) {
    meld.printf("# Hinweis: %lu Messwerte gingen waehrend der Aufnahme verloren.\n", (unsigned long)verloren);
  }

  if (a == AUFTRAG_TARA) {
    if (auftragClip) { meld.println(F("# Tara NICHT uebernommen.")); return; }
    cfg.tara = stat.roh.mittel;
    meld.printf("# Tara gesetzt: %.1f Counts (%.3f uV Brueckenoffset), Streuung %.2f Counts\n",
                cfg.tara, cfg.tara * uvProCount, sigma);
    meld.println(F("# Mit 's' dauerhaft speichern."));
    sendeCfg();
    return;
  }

  if (a == AUFTRAG_KALIBRIEREN) {
    double delta = stat.roh.mittel - cfg.tara;
    // Unsicherheit des MITTELWERTS (nicht des Einzelwerts!). Aus den 0,1-s-Blockmitteln
    // geschätzt, damit langsames Rauschen und Drift mitzählen; sonst aus der Rohstreuung.
    const double sem = (stat.block.n >= 5) ? stat.block.stdAbw() / sqrt((double)stat.block.n)
                                           : sigma / sqrt((double)stat.roh.n);
    const double semDiff = sem * M_SQRT2;          // Tara und Kalibrierung je mit eigener Unsicherheit
    double mindest = 10.0 * semDiff;
    if (mindest < 20.0) mindest = 20.0;            // absolute Untergrenze (ca. 2 g bei 10 Counts/g)
    if (auftragClip || fabs(delta) < mindest) {
      meld.printf("# FEHLER: Signalaenderung zu klein oder ungueltig (%.1f Counts, noetig mind. %.1f Counts; "
                  "Unsicherheit des Mittelwerts %.2f Counts).\n", delta, mindest, sem);
      meld.println(F("# Liegt das Gewicht auf? Wurde vorher ohne Last tariert? Faktor NICHT geaendert."));
      return;
    }
    cfg.faktor = (float)(delta / (double)auftragGewichtG);   // Vorzeichen bleibt erhalten
    cfg.kalibriert = 1;
    // Plausibilität: daraus berechnete Empfindlichkeit der Zelle in mV/V
    double empf = fabs((double)cfg.faktor) * NENNLAST_G * uvProCount / 1000.0 / ERREGUNG_V;
    meld.printf("# Kalibriert mit %.2f g: Aenderung %.1f Counts (%.3f uV)\n",
                auftragGewichtG, delta, delta * uvProCount);
    meld.printf("# Kalibrierfaktor: %.5f Counts/g%s\n", cfg.faktor,
                cfg.faktor < 0 ? "  (negativ: Signal ist verpolt, wird automatisch beruecksichtigt)" : "");
    const double relUnsich = 100.0 * semDiff / fabs(delta);
    meld.printf("# Statistische Unsicherheit des Faktors: +-%.3f %% (= +-%.1f g bei %.0f kg)\n",
                relUnsich, relUnsich / 100.0 * NENNLAST_G, NENNLAST_G / 1000.0);
    if (auftragGewichtG < 0.05f * NENNLAST_G) {
      meld.printf("# Hinweis: Kalibriergewicht ist klein (%.0f g < 5 %% der Nennlast). Fehler im Faktor wachsen\n"
                  "#          bis zur Nennlast mit. Fuer beste Genauigkeit mind. %.0f kg (10 %%) verwenden.\n",
                  auftragGewichtG, NENNLAST_G * 0.1 / 1000.0);
    }
    meld.printf("# Entspricht einer Empfindlichkeit von %.3f mV/V bei %.0f kg", empf, NENNLAST_G / 1000.0);
    meld.println((empf < 0.8 || empf > 2.4) ? F("  -> AUSSERHALB des erwarteten Bereichs 1,0-2,0 mV/V, bitte pruefen!")
                                            : F("  -> plausibel"));
    meld.println(F("# Mit 's' dauerhaft speichern."));
    sendeCfg();
    return;
  }

  RauschErgebnis e = rauschErgebnis(stat, auftragClip);
  if (a == AUFTRAG_RAUSCHEN) {
    druckeRauschen(e);
    druckeRauschZeile(e);
    return;
  }

  // ----- ein Schritt des Rauschvergleichs -----
  if (vergleichIndex < VERGLEICH_MAX) vergleichErg[vergleichIndex] = e;
  druckeRauschZeile(e);
  vergleichIndex++;
  vergleichWeiter = true;          // Ratenwechsel erst in loop(), nicht mitten in der Verarbeitung
}

bool auftragMesswert(const ADS1256::Messwert &m) {
  if (auftrag == AUFTRAG_KEIN) return false;
  if (auftragVerwerfen > 0) {
    auftragVerwerfen--;            // Einschwingen nach Ratenwechsel
  } else {
    stat.add(m);
    if (stat.roh.n >= auftragN) auftragFertig();
  }
  return true;
}

// ===========================================================================
// Rauschanalyse: Ausgabe
// ===========================================================================
static void druckeRauschen(const RauschErgebnis &e) {
  const double uv = ads.microvoltsPerCount();
  const double fa = fabs((double)cfg.faktor);
  const double ss = (double)e.maximum - (double)e.minimum;
  const char *unkal = cfg.kalibriert ? "" : "  (g unkalibriert!)";

  meld.println(F("# ================= Rauschanalyse ================="));
  meld.printf("# Messungen:          %lu bei %.1f SPS (gemessen %.2f SPS), PGA %u, ungefiltert\n",
              (unsigned long)e.n, e.sps, e.rateIst, ads.gain());
  meld.printf("# Verlorene Werte:    %lu\n", (unsigned long)e.verloren);
  meld.printf("# Mittelwert:         %.2f Counts = %.3f uV = %.2f g\n",
              e.mittel, e.mittel * uv, inGramm(e.mittel));
  meld.printf("# Standardabweichung: %.3f Counts = %.4f uV = %.4f g%s\n",
              e.sigma, e.sigma * uv, e.sigma / fa, unkal);
  meld.printf("# Minimum:            %ld Counts = %.3f uV\n", (long)e.minimum, e.minimum * uv);
  meld.printf("# Maximum:            %ld Counts = %.3f uV\n", (long)e.maximum, e.maximum * uv);
  meld.printf("# Spitze-Spitze:      %.0f Counts = %.4f uV = %.4f g\n", ss, ss * uv, ss / fa);
  if (e.sigma > 0.0) {
    // bezogen auf den vollen ADC-Bereich von 2^24 Counts (+-2*Vref/PGA)
    meld.printf("# Effektive Aufloesung:   %.2f Bit (RMS, bezogen auf ADC-Vollbereich)\n",
                log2(16777216.0 / e.sigma));
    meld.printf("# Rauschfreie Aufloesung: %.2f Bit (6,6 Sigma)\n", log2(16777216.0 / (6.6 * e.sigma)));
    double teile = (double)NENNLAST_G * fa / e.sigma;
    meld.printf("# Bezogen auf %.0f kg:     %.0f Teile (1 Sigma) = %.2f Bit\n",
                NENNLAST_G / 1000.0, teile, log2(teile));
  } else {
    meld.println(F("# Effektive Aufloesung:   24 Bit (kein Rauschen messbar - Signal pruefen!)"));
  }
  if (e.sigma10 > 0.0) {
    // Gemessene Streuung der Mittelwerte über je 0,1 s: zeigt, was hohe
    // Datenrate + Mittelung im Vergleich zu einer niedrigen Datenrate bringt.
    meld.printf("# Nach Mittelung auf 10 SPS (%lu Bloecke): %.4f Counts = %.5f uV = %.5f g = %.2f Bit\n",
                (unsigned long)e.bloecke, e.sigma10, e.sigma10 * uv, e.sigma10 / fa,
                log2(16777216.0 / e.sigma10));
  } else if (e.sps >= 20.0f) {
    meld.println(F("# Nach Mittelung auf 10 SPS: zu wenige Bloecke (mind. 5, also >= 0,5 s messen)"));
  }
  meld.println(F("# ================================================="));
}

// Maschinenlesbare Zeile für das PC-Programm
static void druckeRauschZeile(const RauschErgebnis &e) {
  const double uv = ads.microvoltsPerCount();
  const double fa = fabs((double)cfg.faktor);
  double enob = e.sigma > 0.0 ? log2(16777216.0 / e.sigma) : 24.0;
  double nfb  = e.sigma > 0.0 ? log2(16777216.0 / (6.6 * e.sigma)) : 24.0;
  meld.printf("#RAUSCH rate=%g rate_ist=%.3f n=%lu verloren=%lu mittel=%.3f sigma=%.5f "
              "sigma_uv=%.6f sigma_g=%.6f enob=%.3f nfbit=%.3f sigma10=%.5f sigma10_g=%.6f pp=%ld clip=%u\n",
              e.sps, e.rateIst, (unsigned long)e.n, (unsigned long)e.verloren, e.mittel, e.sigma,
              e.sigma * uv, e.sigma / fa, enob, nfb, e.sigma10, e.sigma10 / fa,
              (long)(e.maximum - e.minimum), e.clip ? 1 : 0);
}

static void druckeVergleichstabelle() {
  const double uv = ads.microvoltsPerCount();
  const double fa = fabs((double)cfg.faktor);
  meld.println(F("# ============================== Rauschvergleich =============================="));
  meld.printf("# PGA %u, 1 Count = %.4f nV, Faktor %.4f Counts/g%s, WLAN %s\n", ads.gain(), uv * 1000.0,
              cfg.faktor, cfg.kalibriert ? "" : " (unkalibriert)", wlanAn ? "an" : "aus");
  meld.println(F("#     SPS |       n | verl. | sigma Cnt | sigma uV | sigma g  | ENOB  | NF-Bit | sigma g @10SPS"));
  for (uint8_t i = 0; i < vergleichIndex && i < VERGLEICH_MAX; i++) {
    const RauschErgebnis &e = vergleichErg[i];
    double enob = e.sigma > 0.0 ? log2(16777216.0 / e.sigma) : 24.0;
    double nfb  = e.sigma > 0.0 ? log2(16777216.0 / (6.6 * e.sigma)) : 24.0;
    char s10[16];
    if (e.sigma10 > 0.0) snprintf(s10, sizeof(s10), "%.5f", e.sigma10 / fa);
    else                 snprintf(s10, sizeof(s10), "-");
    meld.printf("# %7.1f | %7lu | %5lu | %9.3f | %8.4f | %8.4f | %5.2f | %6.2f | %s%s\n",
                e.sps, (unsigned long)e.n, (unsigned long)e.verloren, e.sigma, e.sigma * uv,
                e.sigma / fa, enob, nfb, s10, e.clip ? "  UEBERSTEUERT" : "");
  }
  meld.println(F("# ENOB = log2(2^24 / sigma), NF-Bit = rauschfrei (6,6 sigma)."));
  meld.println(F("# sigma @10SPS = gemessene Streuung der 0,1-s-Mittelwerte (vergleichbar ueber alle Raten)."));
  meld.println(F("# ============================================================================="));
}

// ===========================================================================
// Rauschvergleich über mehrere Datenraten
// ===========================================================================
void starteVergleich(float sekunden, const float *raten, uint8_t anzahl) {
  vergleichSekunden  = sekunden;
  vergleichAnzahl    = anzahl;
  vergleichIndex     = 0;
  vergleichStartCode = cfg.drateCode;
  for (uint8_t i = 0; i < anzahl; i++) vergleichRaten[i] = raten[i];
  vergleichAktiv  = true;
  vergleichWeiter = true;          // erster Schritt in loop()
  meld.printf("# Rauschvergleich: %u Datenraten, je %.1f s. Zelle unbelastet und ruhig lassen! Abbruch mit 'x'.\n",
              anzahl, sekunden);
}

bool vergleichFaellig() {
  return vergleichWeiter;
}

void vergleichSchritt() {
  vergleichWeiter = false;
  if (!vergleichAktiv) return;
  if (vergleichIndex >= vergleichAnzahl) { vergleichBeenden(false); return; }

  uint8_t code = 0;
  ADS1256::codeFromRate(vergleichRaten[vergleichIndex], code);
  if (!setzeRate(code, false)) {
    auftragAbbrechen("Datenrate konnte nicht gesetzt werden");
    return;
  }
  float sps = ads.sampleRate();
  uint32_t n = (uint32_t)lroundf(sps * vergleichSekunden);
  if (n < 50) n = 50;
  // Einschwingen nach dem Ratenwechsel: ca. 50 ms bzw. mindestens 3 Werte verwerfen
  uint32_t verwerfen = (uint32_t)lroundf(sps * 0.05f);
  if (verwerfen < 3) verwerfen = 3;
  starteAuftrag(AUFTRAG_VERGLEICH, n, verwerfen);
}

static void vergleichBeenden(bool abgebrochen) {
  vergleichAktiv = false;
  vergleichWeiter = false;
  if (vergleichIndex > 0) druckeVergleichstabelle();
  if (abgebrochen) meld.println(F("# Rauschvergleich abgebrochen."));
  // Ursprüngliche Datenrate wiederherstellen
  if (cfg.drateCode != vergleichStartCode) {
    setzeRate(vergleichStartCode, true);
  }
}
