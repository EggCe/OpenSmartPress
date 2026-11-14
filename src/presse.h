/*
 * presse.h - Pressvorgang auf Kern 0: Startprüfung, Aufzeichnung Kraft über Weg,
 *            Kennwerte, Speichern in der aktiven Sitzung
 *
 * Die Bewegung läuft auf Kern 1 (antrieb, BefehlTyp::PRESSEN). Kern 0 leert in
 * presseSchritt() den Probenpuffer, fasst die Proben zu höchstens PRESSE_PUNKTE_MAX
 * Wegabschnitten zusammen und berechnet die Kennwerte exakt aus allen Proben.
 * Nach der Rückfahrt (Antrieb steht) wird die Pressung im Flash gespeichert.
 */
#pragma once

#include <Arduino.h>
#include "sitzungen.h"
#include "treiber/Antrieb.h"

constexpr float N_PRO_GRAMM = 0.00980665f;

bool presseStarten(const char *quelle);          // false: Voraussetzung fehlt (Meldung ausgegeben)
bool presseBereit(char *grund, size_t n);        // Voraussetzungen erfüllt? sonst kurzer Grund
void presseSchritt();                            // aus loop()
void presseEreignis(const antrieb::Ereignis &e); // PRESSE_FERTIG / PRESSE_FEHLER
bool presseLaeuft();

// Live-Daten der laufenden bzw. letzten Pressung (für /presse/status)
uint16_t          presseLiveAnzahl();
const PressPunkt *presseLivePunkte();
float             presseLiveStartMm();
float             presseLivePunktMm();
uint32_t          presseLiveId();                // ändert sich mit jeder neuen Pressung
