#include "usb_ausgabe.h"

#include "konfig.h"
#include "meldung.h"
#include "rahmen.h"
#include "einstellungen.h"
#include "adc.h"
#include "filter.h"

// Ausgabepuffer für CSV- und Binärdaten (wird in Blöcken über USB gesendet)
static uint8_t  ausPuffer[1024];
static uint16_t ausLen = 0;
static uint32_t letzteAusgabeMs = 0;

void pufferSenden() {
  if (ausLen == 0) return;
  Serial.write(ausPuffer, ausLen);
  ausLen = 0;
}

static void puffern(const void *daten, uint16_t n) {
  if (ausLen + n > sizeof(ausPuffer)) pufferSenden();
  memcpy(ausPuffer + ausLen, daten, n);
  ausLen = (uint16_t)(ausLen + n);
}

// Maschinenlesbare Konfigurationszeile: alles, was der PC zur Umrechnung braucht.
// Gramm = (roh - tara) / faktor,  uV = roh * uv_count
void sendeCfg() {
  pufferSenden();                  // Reihenfolge wahren
  meld.printf("#CFG rate=%g pga=%u vref=%.4f uv_count=%.9g tara=%.3f faktor=%.6f kalibriert=%u "
              "modus=%u filter_s=%.3f nennlast_g=%.0f spi_hz=%lu\n",
              ads.sampleRate(), ads.gain(), VREF_V, ads.microvoltsPerCount(), cfg.tara, cfg.faktor,
              cfg.kalibriert, cfg.modus, cfg.filterMs / 1000.0, NENNLAST_G, (unsigned long)ads.spiTakt());
}

// Lesbar und Plotter: Gewicht aus dem Filter. CSV und Binär: nur Rohwerte.
// Diese Datenströme gehen nur an USB, nicht ins Web-Log.
void ausgabe(const ADS1256::Messwert &m) {
  switch (cfg.modus) {
    case MODUS_LESBAR: {
      uint32_t jetzt = millis();
      if (jetzt - letzteAusgabeMs < 250) return;       // 4 Zeilen pro Sekunde
      letzteAusgabeMs = jetzt;
      double gefiltert = filterMittel();
      Serial.printf("Roh: %9ld | %11.3f uV | %9.4f kg%s\n", (long)m.counts,
                    ads.countsToMicrovolts(gefiltert), inGramm(gefiltert) / 1000.0,
                    cfg.kalibriert ? "" : "  (unkalibriert)");
      break;
    }

    case MODUS_CSV: {                                  // jeder Messwert, nur Ganzzahlen
      char z[40];
      int n = snprintf(z, sizeof(z), "%lu,%lu,%ld\n", (unsigned long)m.seq,
                       (unsigned long)m.tMikros, (long)m.counts);
      if (n > 0) puffern(z, (uint16_t)n);
      break;
    }

    case MODUS_PLOTTER: {
      uint32_t jetzt = millis();
      if (jetzt - letzteAusgabeMs < 10) return;        // höchstens 100 Punkte pro Sekunde
      letzteAusgabeMs = jetzt;
      Serial.printf("Gramm_roh:%.2f,Gramm_gefiltert:%.2f\n", inGramm((double)m.counts), inGramm(filterMittel()));
      break;
    }

    case MODUS_BINAER: {
      uint8_t f[RAHMEN_LAENGE];
      binaerRahmen(f, m.seq, m.counts);
      puffern(f, sizeof(f));
      break;
    }

    default:
      break;
  }
}

void setzeModus(uint8_t m) {
  pufferSenden();
  cfg.modus = m;
  switch (m) {
    case MODUS_AUS:     meld.println(F("# Ausgabe aus.")); break;
    case MODUS_LESBAR:  meld.println(F("# Ausgabe: lesbar (4x pro Sekunde).")); break;
    case MODUS_CSV:     meld.println(F("# Ausgabe: CSV, jeder Messwert als Rohwert (Umrechnung siehe #CFG)."));
                        meld.println(F("# seq = laufende Nummer, t_us = Zeitstempel (laeuft nach ca. 71 min ueber)"));
                        Serial.println(F("seq,t_us,roh")); break;
    case MODUS_PLOTTER: meld.println(F("# Ausgabe: Serieller Plotter (max. 100 Punkte/s).")); break;
    case MODUS_BINAER:  meld.println(F("# Ausgabe: binaer (8-Byte-Rahmen, fuer waegezelle_plot.py).")); break;
  }
  sendeCfg();
}
