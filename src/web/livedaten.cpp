#include "livedaten.h"

#include <math.h>
#include "../adc.h"
#include "../filter.h"

static constexpr float GRAPH_PUNKTE_PRO_S = 50.0f;

// --- Diagrammpunkte ---
Punkt           punkte[PUNKTE_GROESSE];
uint32_t        punktZaehler = 0;
uint32_t        graphLaenge = 1;
static uint32_t graphN = 0;
static int64_t  graphSumme = 0;
static int32_t  graphMin = 0, graphMax = 0;
static uint64_t tErweitert = 0;                // 64-Bit-Zeit in µs (ohne Überlauf)
static uint32_t tLetzteRoh = 0;
static bool     zeitStart = true;

// --- Live-Statistik ---
static Statistik webStat;
RauschErgebnis   webErg;
uint32_t         webErgId = 0;
float            webStatSek = 2.0f;
static uint32_t  webStatN = 200;
static bool      webClip = false;
static Welford   webGlatt;                     // Streuung des geglätteten Signals (je Graphpunkt)
double           webErgGlatt = 0.0;

void graphReset() {
  float sps = ads.sampleRate();
  uint32_t n = (uint32_t)lroundf(sps / GRAPH_PUNKTE_PRO_S);
  graphLaenge = (n < 1) ? 1 : n;
  graphN = 0;
}

void webStatReset() {
  webStat.reset(blockLaenge10());
  uint32_t n = (uint32_t)lroundf(ads.sampleRate() * webStatSek);
  webStatN = (n < 10) ? 10 : n;
  webClip = false;
  webGlatt.reset();
}

void liveZeitNeustart() {
  zeitStart = true;
}

void webUebersteuert() {
  webClip = true;
}

void webAufnehmen(const ADS1256::Messwert &m) {
  // 64-Bit-Zeit aus den 32-Bit-µs-Zeitstempeln (läuft sonst nach 71 min über)
  if (zeitStart) {
    zeitStart = false;
    tErweitert = m.tMikros;
  } else {
    tErweitert += (uint32_t)(m.tMikros - tLetzteRoh);
  }
  tLetzteRoh = m.tMikros;

  // Diagrammpunkt: Mittel, Minimum und Maximum über graphLaenge Werte
  if (graphN == 0) {
    graphSumme = 0;
    graphMin = graphMax = m.counts;
  }
  graphSumme += m.counts;
  if (m.counts < graphMin) graphMin = m.counts;
  if (m.counts > graphMax) graphMax = m.counts;
  if (++graphN >= graphLaenge) {
    Punkt &p = punkte[punktZaehler & (PUNKTE_GROESSE - 1)];
    p.tMs     = (uint32_t)(tErweitert / 1000u);
    p.mittel  = (float)((double)graphSumme / (double)graphN);
    p.minimum = graphMin;
    p.maximum = graphMax;
    const double g = filterMittel();             // geglätteter Wert, ca. 50x pro Sekunde
    p.geglaettet = (float)g;
    webGlatt.add(g);
    punktZaehler++;
    graphN = 0;
  }

  // Live-Statistik über die volle Datenrate
  webStat.add(m);
  if (webStat.roh.n >= webStatN) {
    webErg = rauschErgebnis(webStat, webClip);
    webErgGlatt = (webGlatt.n >= 5) ? webGlatt.stdAbw() : 0.0;
    webErgId++;
    webStatReset();
  }
}
