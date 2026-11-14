/*
 * SensorWaechter.h - Erkennt einen Sensorfehler am Rohwert des ADS1256
 *
 * Aderbruch, fehlende Brückenspeisung oder eine gestörte SPI-Leitung zeigen sich als
 *   a) Übersteuerung: der Wert steht am positiven oder negativen Anschlag, oder
 *   b) eingefrorener Wert: GLEICH_MAX gleiche Rohwerte hintereinander. Bei 24 Bit und
 *      dem Eigenrauschen der Messkette kommt das im Betrieb praktisch nicht vor.
 * In beiden Fällen ist die Last unbekannt - eine Abwärtsfahrt darf nicht weiterlaufen,
 * weil die Maximalkraft sonst nie erkannt würde (z. B. Anschlag negativ bei Aderbruch).
 *
 * Läuft im ADC-Interrupt (Kern 1): nur Ganzzahlen, alles inline. Ohne Arduino-
 * Abhängigkeit, damit tests/ es auf dem PC prüfen kann.
 */
#pragma once

#include <stdint.h>

struct SensorWaechter {
  enum Art : uint8_t { OK = 0, UEBERSTEUERT = 1, EINGEFROREN = 2 };

  // Wie ADS1256::isClipped(): ca. 0,1 % Abstand zum Vollausschlag (adc.cpp prüft die Gleichheit)
  static constexpr int32_t  CLIP_GRENZE = 0x7FFFFF - 8388;
  static constexpr uint16_t GLEICH_MAX  = 64;   // bei 2000 SPS 32 ms, bei 30000 SPS ca. 2 ms

  int32_t  letzter = 0;
  uint16_t gleich  = 0;      // so viele Werte gleich dem vorherigen
  uint8_t  art     = OK;

  // Neuen Rohwert prüfen. Rückgabe: aktueller Fehlerzustand (Art), OK = Sensor plausibel.
  inline __attribute__((always_inline)) uint8_t pruefe(int32_t roh) {
    if (roh == letzter) {
      if (gleich < GLEICH_MAX) gleich++;
    } else {
      gleich = 0;
      letzter = roh;
    }
    if (roh >= CLIP_GRENZE || roh <= -CLIP_GRENZE) art = UEBERSTEUERT;
    else if (gleich >= GLEICH_MAX)                 art = EINGEFROREN;
    else                                           art = OK;
    return art;
  }

  void reset() { letzter = 0; gleich = 0; art = OK; }
};
