/*
 * geber.h - Prüfbefehl 'enc': Encoder (Drehgeber) des Closed-Loop-Motors mitzählen
 *
 * Nur zur Fehlersuche: EA+/EB+ des Motor-Encoders über Spannungsteiler (5 V -> 3,3 V) an
 * PIN_GEBER_A/B, GND an EGND. Kern 1 fragt die Pins in loop1() ab und wertet die
 * Quadratur aus (4 Zählschritte pro Strich), Kern 0 meldet den Stand.
 * Eine Umdrehung von Hand ergibt 4 x Strichzahl (z. B. 4000 bei 1000 Strichen).
 */
#pragma once

#include <Arduino.h>

void geberBefehl(const char *a1);   // Befehl 'enc' (Kern 0): enc | enc 0 | enc aus
void geberSchritt();                // aus loop(): Zählerstand melden, wenn er sich ändert
void geberKern1Schritt();           // aus loop1(): Pins abfragen und zählen
