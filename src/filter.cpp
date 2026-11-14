#include "filter.h"

#include <math.h>
#include "adc.h"
#include "einstellungen.h"

static constexpr uint16_t FILTER_BLOECKE = 256;   // max. Anzahl Blöcke im Glättungsfilter

static int64_t  filterBlock[FILTER_BLOECKE];
static uint16_t filterK = 1;            // Anzahl Blöcke im Fenster
static uint32_t filterBlockLen = 1;     // Rohwerte pro Block
static uint16_t filterIndex = 0;
static uint16_t filterAnzahl = 0;       // gefüllte Blöcke (<= filterK)
static int64_t  filterSumme = 0;        // Summe der gefüllten Blöcke
static int64_t  filterBlockSumme = 0;   // laufender, noch unvollständiger Block
static uint32_t filterBlockN = 0;
static int32_t  filterLetzter = 0;      // letzter Rohwert (Ausgabe, solange noch kein Block voll ist)

// Filterparameter aus Glättungszeit und Datenrate berechnen, Filter leeren.
void filterReset() {
  uint32_t n = (uint32_t)lroundf(ads.sampleRate() * (float)cfg.filterMs / 1000.0f);  // Werte im Fenster
  if (n < 1) n = 1;
  if (n <= FILTER_BLOECKE) {
    filterBlockLen = 1;
    filterK = (uint16_t)n;
  } else {
    filterBlockLen = (n + FILTER_BLOECKE - 1) / FILTER_BLOECKE;   // aufrunden
    uint32_t k = (n + filterBlockLen / 2) / filterBlockLen;
    filterK = (uint16_t)((k < 1) ? 1 : (k > FILTER_BLOECKE ? FILTER_BLOECKE : k));
  }
  filterIndex = 0;
  filterAnzahl = 0;
  filterSumme = 0;
  filterBlockSumme = 0;
  filterBlockN = 0;
}

void filterUpdate(int32_t x) {
  filterLetzter = x;
  filterBlockSumme += x;
  if (++filterBlockN < filterBlockLen) return;   // Block noch nicht voll

  // Block fertig -> in das gleitende Fenster übernehmen
  if (filterAnzahl >= filterK) {
    filterSumme -= filterBlock[filterIndex];      // ältesten Block entfernen
  } else {
    filterAnzahl++;
  }
  filterBlock[filterIndex] = filterBlockSumme;
  filterSumme += filterBlockSumme;
  filterIndex = (uint16_t)((filterIndex + 1) % filterK);
  filterBlockSumme = 0;
  filterBlockN = 0;
}

// Geglätteter Wert in Counts. Division nur bei Bedarf (kein FPU!)
double filterMittel() {
  if (filterAnzahl == 0) return (double)filterLetzter;
  return (double)filterSumme / ((double)filterAnzahl * (double)filterBlockLen);
}

// tatsächliche Fensterlänge in Sekunden (nach Rundung auf ganze Werte/Blöcke)
double filterSekunden() {
  return (double)filterK * (double)filterBlockLen / (double)ads.sampleRate();
}

uint16_t filterBloecke()     { return filterK; }
uint32_t filterBlockLaenge() { return filterBlockLen; }

double inGramm(double counts) {
  return (counts - cfg.tara) / (double)cfg.faktor;
}
