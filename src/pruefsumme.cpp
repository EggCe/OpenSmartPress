#include "pruefsumme.h"

static uint8_t crcTabelle[256];

uint32_t crc32(const uint8_t *daten, size_t laenge) {
  uint32_t c = 0xFFFFFFFFUL;
  while (laenge--) {
    c ^= *daten++;
    for (uint8_t i = 0; i < 8; i++) c = (c & 1) ? ((c >> 1) ^ 0xEDB88320UL) : (c >> 1);
  }
  return ~c;
}

void crc8Init() {
  for (uint16_t i = 0; i < 256; i++) {
    uint8_t c = (uint8_t)i;
    for (uint8_t b = 0; b < 8; b++) c = (c & 0x80) ? (uint8_t)((c << 1) ^ 0x07) : (uint8_t)(c << 1);
    crcTabelle[i] = c;
  }
}

uint8_t crc8(const uint8_t *daten, size_t laenge) {
  uint8_t c = 0;
  while (laenge--) c = crcTabelle[c ^ *daten++];
  return c;
}
