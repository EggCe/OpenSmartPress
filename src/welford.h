/*
 * welford.h - Mittelwert und Varianz nach Welford (numerisch stabil)
 *
 * Ohne Arduino-Abhängigkeit, damit tests/host es auf dem PC prüfen kann.
 */
#pragma once

#include <math.h>
#include <stdint.h>

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
