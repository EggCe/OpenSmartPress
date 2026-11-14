/*
 * test_host.cpp - Tests der hardwareunabhängigen Logik auf dem PC (g++, ohne Arduino)
 *
 * Aufruf: sh tests/host/run.sh   (läuft auch in .github/workflows/firmware.yml)
 *
 * Geprüft: CRC-8/CRC-32, USB-Binärrahmen, Welford-Statistik, SensorWaechter.
 * Der Ordner tests/ wird von der Arduino IDE nicht übersetzt (nur das Sketch-
 * Verzeichnis und src/).
 */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "pruefsumme.h"
#include "rahmen.h"
#include "welford.h"
#include "treiber/SensorWaechter.h"

static int fehler = 0, pruefungen = 0;

#define PRUEFE(bed)                                                        \
  do {                                                                     \
    pruefungen++;                                                          \
    if (!(bed)) {                                                          \
      fehler++;                                                            \
      printf("FEHLER %s:%d: %s\n", __FILE__, __LINE__, #bed);              \
    }                                                                      \
  } while (0)

#define PRUEFE_NAH(a, b, tol) PRUEFE(fabs((double)(a) - (double)(b)) <= (tol))

// ---------------------------------------------------------------------------
static void testCrc() {
  const uint8_t text[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
  crc8Init();
  PRUEFE(crc8(text, sizeof(text)) == 0xF4);                // Prüfwert CRC-8/SMBUS
  PRUEFE(crc32(text, sizeof(text)) == 0xCBF43926UL);       // Prüfwert CRC-32 (IEEE)
  PRUEFE(crc8(text, 0) == 0x00);
}

// Gegenstück zu tools/waegezelle_plot.py: Rahmen prüfen und zerlegen
static bool rahmenLesen(const uint8_t f[8], uint16_t &seq, int32_t &counts) {
  if (f[0] != 0xA5 || f[1] != 0x5A || crc8(&f[2], 5) != f[7]) return false;
  seq = (uint16_t)(f[2] | (f[3] << 8));
  uint32_t c = (uint32_t)f[4] | ((uint32_t)f[5] << 8) | ((uint32_t)f[6] << 16);
  if (c & 0x800000u) c |= 0xFF000000u;
  counts = (int32_t)c;
  return true;
}

static void testRahmen() {
  crc8Init();
  uint8_t f[RAHMEN_LAENGE];
  binaerRahmen(f, 0x00051234UL, -2);
  PRUEFE(f[0] == 0xA5 && f[1] == 0x5A);
  PRUEFE(f[2] == 0x34 && f[3] == 0x12);                    // seq: nur die unteren 16 Bit
  PRUEFE(f[4] == 0xFE && f[5] == 0xFF && f[6] == 0xFF);    // -2 als int24 LE
  PRUEFE(f[7] == crc8(&f[2], 5));

  const int32_t werte[] = {0, 1, -1, 0x7FFFFF, -0x800000, 123456, -654321};
  for (int32_t w : werte) {
    binaerRahmen(f, 777, w);
    uint16_t s = 0;
    int32_t c = 0;
    PRUEFE(rahmenLesen(f, s, c));
    PRUEFE(s == 777 && c == w);
  }
  binaerRahmen(f, 1, 1000);
  f[5] ^= 0x01;                                            // ein gekipptes Bit
  uint16_t s;
  int32_t c;
  PRUEFE(!rahmenLesen(f, s, c));
}

// ---------------------------------------------------------------------------
static void testWelford() {
  Welford w;
  w.reset();
  PRUEFE(w.n == 0 && w.stdAbw() == 0.0);
  const double x[] = {2, 4, 4, 4, 5, 5, 7, 9};             // Mittel 5, Stichproben-sigma sqrt(32/7)
  for (double v : x) w.add(v);
  PRUEFE(w.n == 8);
  PRUEFE_NAH(w.mittel, 5.0, 1e-12);
  PRUEFE_NAH(w.stdAbw(), sqrt(32.0 / 7.0), 1e-12);

  // numerisch stabil bei großem Gleichanteil (typisch: Rohwerte um 8 Mio. Counts)
  Welford g;
  g.reset();
  for (int i = 0; i < 100000; i++) g.add(8000000.0 + ((i & 1) ? 1.0 : -1.0));
  PRUEFE_NAH(g.mittel, 8000000.0, 1e-6);
  PRUEFE_NAH(g.stdAbw(), 1.0, 1e-4);
}

// ---------------------------------------------------------------------------
static void testSensorWaechter() {
  SensorWaechter s;
  s.reset();

  // Rauschen um einen normalen Messwert: nie ein Fehler
  uint32_t z = 12345;
  for (int i = 0; i < 100000; i++) {
    z = z * 1664525u + 1013904223u;
    PRUEFE(s.pruefe(250000 + (int32_t)(z >> 28) - 8) == SensorWaechter::OK);
  }

  // Übersteuerung in beide Richtungen, sofort beim ersten Wert
  s.reset();
  PRUEFE(s.pruefe(SensorWaechter::CLIP_GRENZE - 1) == SensorWaechter::OK);
  PRUEFE(s.pruefe(SensorWaechter::CLIP_GRENZE) == SensorWaechter::UEBERSTEUERT);
  PRUEFE(s.pruefe(0x7FFFFF) == SensorWaechter::UEBERSTEUERT);
  PRUEFE(s.pruefe(-0x800000) == SensorWaechter::UEBERSTEUERT);   // z. B. Aderbruch
  PRUEFE(s.pruefe(-SensorWaechter::CLIP_GRENZE) == SensorWaechter::UEBERSTEUERT);
  PRUEFE(s.pruefe(1000) == SensorWaechter::OK);                  // wieder im Bereich

  // eingefrorener Wert: GLEICH_MAX Wiederholungen nach dem ersten Wert
  s.reset();
  PRUEFE(s.pruefe(4711) == SensorWaechter::OK);
  for (int i = 1; i < SensorWaechter::GLEICH_MAX; i++) PRUEFE(s.pruefe(4711) == SensorWaechter::OK);
  PRUEFE(s.pruefe(4711) == SensorWaechter::EINGEFROREN);
  PRUEFE(s.pruefe(4711) == SensorWaechter::EINGEFROREN);         // bleibt, Zähler läuft nicht über
  PRUEFE(s.pruefe(4712) == SensorWaechter::OK);                  // ein anderer Wert hebt auf

  // gleicher Wert am Anschlag: Übersteuerung hat Vorrang
  s.reset();
  for (int i = 0; i < 200; i++) s.pruefe(0x7FFFFF);
  PRUEFE(s.art == SensorWaechter::UEBERSTEUERT);
}

int main() {
  testCrc();
  testRahmen();
  testWelford();
  testSensorWaechter();
  printf("%d Pruefungen, %d Fehler\n", pruefungen, fehler);
  return fehler ? 1 : 0;
}
