/*
 * pruefsumme.h - CRC-32 (Einstellungen im Flash) und CRC-8 (USB-Binärrahmen)
 */
#pragma once

#include <stddef.h>
#include <stdint.h>   // ohne Arduino-Abhängigkeit: auch in tests/host übersetzbar

uint32_t crc32(const uint8_t *daten, size_t laenge);

// CRC-8, Polynom 0x07, Startwert 0x00, nicht reflektiert (CRC-8/SMBUS).
// crc8Init() einmal in setup() aufrufen, bevor crc8() benutzt wird.
void    crc8Init();
uint8_t crc8(const uint8_t *daten, size_t laenge);
