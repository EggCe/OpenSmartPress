/*
 * statistik.h - Laufende Statistik über Rohwerte (Welford) und Ergebnis einer Rauschmessung
 */
#pragma once

#include <Arduino.h>
#include <math.h>
#include "treiber/ADS1256.h"

// Mittelwert und Varianz nach Welford (numerisch stabil)
struct Welford {
  uint32_t n;
  double   mittel;
  double   m2;
  void reset() { n = 0; mittel = 0.0; m2 = 0.0; }
  void add(double x) {
    n++;
    double d = x - mittel;
    mittel += d / (double)n;
    m2 += d * (x - mittel);
  }
  double stdAbw() const { return (n > 1) ? sqrt(m2 / (double)(n - 1)) : 0.0; }
};

// Statistik über Rohwerte: Welford, Blockmittel (10-SPS-Äquivalent), Lücken
struct Statistik {
  Welford  roh;
  Welford  block;           // Mittelwerte über je blockLaenge Rohwerte
  int32_t  minimum;
  int32_t  maximum;
  uint32_t blockLaenge;     // 0 = keine Blockmittelung
  uint32_t blockZaehler;
  int64_t  blockSumme;
  uint32_t seqErst, seqLetzt;
  uint32_t tErst, tLetzt;

  void reset(uint32_t blockLen) {
    roh.reset(); block.reset();
    minimum = INT32_MAX; maximum = INT32_MIN;
    blockLaenge = blockLen; blockZaehler = 0; blockSumme = 0;
    seqErst = seqLetzt = tErst = tLetzt = 0;
  }
  void add(const ADS1256::Messwert &m) {
    if (roh.n == 0) { seqErst = m.seq; tErst = m.tMikros; }
    seqLetzt = m.seq; tLetzt = m.tMikros;
    roh.add((double)m.counts);
    if (m.counts < minimum) minimum = m.counts;
    if (m.counts > maximum) maximum = m.counts;
    if (blockLaenge > 0) {
      blockSumme += m.counts;
      if (++blockZaehler >= blockLaenge) {
        block.add((double)blockSumme / (double)blockLaenge);
        blockSumme = 0;
        blockZaehler = 0;
      }
    }
  }
  uint32_t erwartet() const { return roh.n ? (seqLetzt - seqErst + 1) : 0; }
  uint32_t verloren() const { return erwartet() - roh.n; }
  // tatsächliche Datenrate aus Zeitstempeln und laufender Nummer
  double rateIst() const {
    uint32_t dt = tLetzt - tErst;
    return (dt > 0 && seqLetzt != seqErst) ? (double)(seqLetzt - seqErst) * 1e6 / (double)dt : 0.0;
  }
};

// Ergebnis einer Rauschmessung
struct RauschErgebnis {
  float    sps;
  uint32_t n;
  uint32_t verloren;
  double   rateIst;
  double   mittel;      // Counts
  double   sigma;       // Counts
  int32_t  minimum, maximum;
  double   sigma10;     // Counts, Streuung der 10-SPS-Blockmittel (0 = nicht verfügbar)
  uint32_t bloecke;
  bool     clip;
};

// Ergebnis aus einer Statistik bei der aktuellen Datenrate
RauschErgebnis rauschErgebnis(const Statistik &s, bool clip);
