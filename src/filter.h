/*
 * filter.h - Glättung: gleitender Mittelwert über cfg.filterMs, unabhängig von der Datenrate
 *
 * Je filterBlockLaenge() Rohwerte werden zu einer Blocksumme zusammengefasst;
 * gemittelt wird über die letzten filterBloecke() Blöcke (max. 256). Bei kurzen
 * Zeiten ist ein Block = ein Messwert, dann ist es ein exakter gleitender Mittelwert.
 */
#pragma once

#include <Arduino.h>

// Muss nach jeder Änderung von Datenrate oder cfg.filterMs aufgerufen werden.
void     filterReset();
void     filterUpdate(int32_t x);
double   filterMittel();           // geglätteter Wert in Counts
double   filterSekunden();         // tatsächliche Fensterlänge nach Rundung
uint16_t filterBloecke();          // Anzahl Blöcke im Fenster
uint32_t filterBlockLaenge();      // Rohwerte pro Block

double   inGramm(double counts);   // (counts - Tara) / Faktor
