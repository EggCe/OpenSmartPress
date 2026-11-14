/*
 * usb_ausgabe.h - Messwertausgabe über USB
 *
 *   Modus 1 lesbar, 3 Plotter: Gewicht aus dem Filter (gedrosselt)
 *   Modus 2 CSV:   "seq,t_us,roh" je Messwert
 *   Modus 4 Binär: 8-Byte-Rahmen  A5 5A | seq u16 LE | counts i24 LE | CRC-8 über Byte 2..6
 *
 * Format von CSV, Binärrahmen und #CFG nur zusammen mit tools/waegezelle_plot.py ändern.
 */
#pragma once

#include <Arduino.h>
#include "treiber/ADS1256.h"

enum Ausgabemodus : uint8_t {
  MODUS_AUS = 0, MODUS_LESBAR = 1, MODUS_CSV = 2, MODUS_PLOTTER = 3, MODUS_BINAER = 4
};

void ausgabe(const ADS1256::Messwert &m);   // je Messwert, je nach cfg.modus
void setzeModus(uint8_t m);
void sendeCfg();                            // "#CFG ..." mit allen Umrechnungsgrößen
void pufferSenden();                        // gepufferte CSV-/Binärdaten jetzt senden
