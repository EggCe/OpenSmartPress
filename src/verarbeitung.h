/*
 * verarbeitung.h - Messwerte auf Kern 0 abholen und verarbeiten
 *
 * Für jeden Messwert: Glättung, Überwachung (Überlast, ADC-Übersteuerung),
 * Web-Diagramm und -Statistik, laufender Auftrag, USB-Ausgabe. Dazu die
 * Erkennung eines DRDY-Timeouts.
 */
#pragma once

#include <Arduino.h>

// Aus loop(): angefallene Messwerte verarbeiten (max. 1024 je Aufruf), sonst Timeout prüfen.
void messwerteAbholen();

// Nach Pausen ohne Messwerte (ADC-Auftrag, Flash schreiben, Ratenwechsel) aufrufen,
// damit kein falscher DRDY-Timeout gemeldet wird. meldungZuruecksetzen: Timeout-Zustand löschen.
void messTimeoutNeustart(bool meldungZuruecksetzen = false);
