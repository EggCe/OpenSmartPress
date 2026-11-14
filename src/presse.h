/*
 * presse.h - Pressvorgang auf Kern 0: Startprüfung, Aufzeichnung Kraft über Weg,
 *            Kennwerte, Speichern in der aktiven Sitzung
 *
 * Die Bewegung läuft auf Kern 1 (antrieb, BefehlTyp::PRESSEN). Kern 0 leert in
 * presseSchritt() den Probenpuffer, fasst die Proben zu höchstens PRESSE_PUNKTE_MAX
 * Wegabschnitten zusammen und berechnet die Kennwerte exakt aus allen Proben.
 * Nach der Rückfahrt (Antrieb steht) wird die Pressung im Flash gespeichert.
 *
 * Zusätzlich landet jede Probe kompakt (8 Byte) in einem Ringpuffer, den der Browser
 * während der Pressung über GET /presse/roh abholt. Der Browser baut daraus die Kurve
 * mit einem Punkt je Motorschritt und speichert die Rohdaten selbst (IndexedDB); der
 * Pico rechnet dafür nichts zusätzlich.
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

// ----- Rohdaten für den Browser (GET /presse/roh) -----
// Eine Probe, little-endian: t4 = Zeit in 4 µs (läuft alle 262 ms über), last = Counts über
// Tara (int24), pos = Bit 0..22 Schritte ab startSchritt (vorzeichenbehaftet), Bit 23 = Haltezeit.
struct RohProbe {
  uint16_t t4;
  uint8_t  last[3];
  uint8_t  pos[3];
};
static_assert(sizeof(RohProbe) == 8, "RohProbe muss 8 Byte gross sein");
constexpr uint32_t ROH_N = 4096;                 // Zweierpotenz: ca. 2 s bei 2000 SPS (32 KB RAM)

struct RohInfo {
  uint32_t id;              // zählt je gestarteter Pressung hoch
  uint32_t anzahl;          // Proben dieser Pressung bisher (= PressKopf::proben am Ende)
  uint32_t t0;              // µs-Zeitstempel der ersten Probe
  int32_t  startSchritt;    // Bezug für pos
  float    spmm, fa, sps;   // Schritte/mm, |Faktor| (Counts/g), Datenrate
  uint32_t nr;              // gespeicherte Pressnummer, 0 = (noch) nicht gespeichert
  uint16_t sit;             // Sitzung, in der gespeichert wurde
  bool     lauf;
};
RohInfo         presseRohInfo();
const RohProbe *presseRohPuffer();               // ROH_N Einträge, Index & (ROH_N - 1)
