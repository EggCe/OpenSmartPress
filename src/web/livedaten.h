/*
 * livedaten.h - Daten für die Weboberfläche (Kern 0, für jeden Messwert)
 *
 * Diagrammpunkte (Mittel/Min/Max/geglättet, 50 pro Sekunde) in einem Ringpuffer
 * und die Live-Statistik über webStatSek Sekunden bei voller Datenrate.
 */
#pragma once

#include <Arduino.h>
#include "../statistik.h"

// Ein Punkt für das Web-Diagramm (Zusammenfassung von graphLaenge Rohwerten)
struct Punkt {
  uint32_t tMs;         // Zeitstempel des letzten Werts (ms seit Start)
  float    mittel;      // Counts
  int32_t  minimum;
  int32_t  maximum;
  float    geglaettet;  // Counts, Ausgang des Glättungsfilters
};

constexpr uint32_t PUNKTE_GROESSE = 1024;      // Zweierpotenz, ca. 20 s Vorrat

// Lesezugriff für /daten
extern Punkt          punkte[PUNKTE_GROESSE];
extern uint32_t       punktZaehler;            // insgesamt erzeugte Punkte
extern uint32_t       graphLaenge;             // Rohwerte pro Punkt
extern RauschErgebnis webErg;                  // letzte abgeschlossene Live-Statistik
extern uint32_t       webErgId;                // 0 = noch kein Ergebnis
extern double         webErgGlatt;             // sigma geglättet in Counts (0 = zu wenige Werte)
extern float          webStatSek;              // Dauer der Live-Statistik

void graphReset();                             // nach Ratenwechsel
void webStatReset();                           // nach Ratenwechsel oder neuer Dauer
void liveZeitNeustart();                       // Zeitachse neu beginnen (nach 'init')
void webUebersteuert();                        // ADC übersteuert: im laufenden Fenster vermerken
void webAufnehmen(const ADS1256::Messwert &m);
