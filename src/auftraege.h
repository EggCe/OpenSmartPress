/*
 * auftraege.h - Nicht blockierende Aufträge über n Messwerte (Kern 0):
 *               Tara, Kalibrierung, Rauschanalyse und Rauschvergleich mehrerer Datenraten
 */
#pragma once

#include <Arduino.h>
#include "statistik.h"

enum Auftrag : uint8_t {
  AUFTRAG_KEIN = 0, AUFTRAG_TARA, AUFTRAG_KALIBRIEREN, AUFTRAG_RAUSCHEN, AUFTRAG_VERGLEICH
};

constexpr uint8_t VERGLEICH_MAX = 16;
extern const float VERGLEICH_STANDARD[8];

bool auftragLaeuft();                                  // Auftrag oder Rauschvergleich aktiv
void starteAuftrag(Auftrag a, uint32_t n, uint32_t verwerfen = 0);
void starteKalibrierung(float gewichtG, uint32_t n);
void auftragAbbrechen(const char *grund);
void auftragUebersteuert();                            // ADC war während der Aufnahme übersteuert
// Messwert an einen laufenden Auftrag geben. Rückgabe true, wenn ein Auftrag lief.
bool auftragMesswert(const ADS1256::Messwert &m);

void starteVergleich(float sekunden, const float *raten, uint8_t anzahl);
bool vergleichFaellig();                               // nächster Ratenwechsel steht an
void vergleichSchritt();                               // aus loop(), nicht während der Verarbeitung
