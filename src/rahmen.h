/*
 * rahmen.h - USB-Binärrahmen (Modus 'm 4'), 8 Byte je Messwert
 *
 *   A5 5A | seq u16 LE | counts i24 LE | CRC-8 (Polynom 0x07, Start 0) über Byte 2..6
 *
 * tools/waegezelle_plot.py liest dieses Format - beide zusammen ändern.
 * Ohne Arduino-Abhängigkeit, damit tests/host es auf dem PC prüfen kann.
 */
#pragma once

#include <stdint.h>
#include "pruefsumme.h"

constexpr uint8_t RAHMEN_LAENGE = 8;

// crc8Init() muss vorher gelaufen sein
inline void binaerRahmen(uint8_t f[RAHMEN_LAENGE], uint32_t seq, int32_t counts) {
  const uint32_t c = (uint32_t)counts;
  f[0] = 0xA5;
  f[1] = 0x5A;
  f[2] = (uint8_t)(seq);
  f[3] = (uint8_t)(seq >> 8);
  f[4] = (uint8_t)(c);
  f[5] = (uint8_t)(c >> 8);
  f[6] = (uint8_t)(c >> 16);
  f[7] = crc8(&f[2], 5);
}
