/*
 * befehle.h - Textbefehle aus dem seriellen Monitor und von POST /cmd
 *
 * Neue Befehle in verarbeiteBefehl() eintragen und in zeigeHilfe(), README.md und
 * help.md dokumentieren.
 */
#pragma once

#include <Arduino.h>

constexpr size_t BEFEHL_MAX_LAENGE = 96;   // inkl. Nullbyte

void verarbeiteBefehl(char *text);         // text wird verändert (Kleinbuchstaben, strtok)
void leseSeriell();                        // aus loop(): Zeichen sammeln, bei Zeilenende ausführen
