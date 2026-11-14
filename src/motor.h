/*
 * motor.h - Antrieb auf Kern 0: Parameter, Sicherheitsprüfungen, Meldungen, Befehle
 *
 * Die Bewegung selbst und die Kraftüberwachung laufen auf Kern 1 (treiber/Antrieb.cpp).
 * Kraftgrenzen werden hier aus aktueller Tara und Faktor in Counts umgerechnet und
 * mit jedem Befehl an Kern 1 geschickt.
 */
#pragma once

#include <Arduino.h>
#include "treiber/Antrieb.h"

bool antriebSenden(antrieb::BefehlTyp t, int32_t schritte);   // mit aktuellen Parametern
void antriebEreignisse();                                     // aus loop(): Meldungen von Kern 1
void motorBefehl(const char *a1, const char *a2);             // Befehl 'mot'
void motorParameter(const char *name, const char *wert);      // Befehl 'mp'
double motorMaxKraftG();                                      // wirksame Maximalkraft (<= Nennlast)
