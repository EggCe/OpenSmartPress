/*
 * Antrieb.h - Ansteuerung des Closed-Loop-Schrittmotortreibers HB808C (PUL/DIR/ENA)
 *             mit kraftüberwachter Endpunkt-Kalibrierung und Pressvorgang
 *
 * Läuft komplett auf Kern 1 (gleicher Kern wie der ADS1256-Interrupt):
 *   - Schritterzeugung in Hardware mit einem PWM-Kanal des RP2040. Jeder Schritt
 *     löst einen kurzen Interrupt aus, der die Position zählt und genau am Ziel stoppt.
 *   - messwertIsr() wird aus dem ADS1256-Interrupt für JEDEN Messwert aufgerufen und
 *     stoppt eine Abwärtsfahrt sofort bei Kontakt bzw. bei Maximalkraft.
 *   - schritt() wird aus loop1() aufgerufen: Rampen, Ablauf von Kalibrierung und
 *     Pressvorgang, Taster, Alarmeingang, Überwachung der Messwerte.
 *
 * Pressvorgang (BefehlTyp::PRESSEN): Eilgang bis presseAnfahrt, mit vPressMmS bis
 * presseEnde (kalibrierter Endpunkt; Maximalkraft beendet früher), Haltezeit, zurück
 * zur Startposition. Während Drücken und Halten schreibt messwertIsr() jede Probe
 * (Position + Last) in einen Ringpuffer, den Kern 0 mit aufzeichnungHolen() leert.
 *
 * Simulation (Param::simulation): ohne Motor laufen die Schrittimpulse ins Leere, die
 * Position zählt trotzdem. messwertIsr() ersetzt die gemessene Last durch ein Modell:
 * virtuelles Werkzeug (steif) und je Pressung ein virtuelles Werkstück mit zufälliger
 * Spitzenkraft. Der ADC liefert weiter den Takt der Proben.
 *
 * Kern 0 benutzt nur: sende(), stopp(), status(), ereignisHolen(), aufzeichnungHolen().
 *
 * Positionsangaben in Schritten, positiv = nach unten. Nach einer Kalibrierung ist
 * Position 0 der Werkzeug-Endpunkt (erster Kontakt).
 */
#pragma once

#include <Arduino.h>

namespace antrieb {

// Parameter, die Kern 0 mit jedem Befehl mitschickt (Kraftwerte schon in Counts)
struct Param {
  float    schritteProMm = 0.0f;   // 0 = nicht eingerichtet -> keine Bewegung
  uint8_t  dirAb         = 1;      // DIR-Pegel (logisch) für "nach unten"
  uint8_t  invertiert    = 0;      // 1: PUL/DIR/ENA invertiert (gemeinsame Anode + Transistor)
  uint8_t  enaModus      = 0;      // 0: ENA nicht angeschlossen, 1: ENA-Ausgang benutzen
  uint8_t  almModus      = 0;      // 0: Alarmeingang aus, 1: Alarm bei LOW, 2: Alarm bei HIGH
  float    vSuchMmS      = 1.0f;   // Kontaktsuche und Überlauf
  float    vFahrtMmS     = 5.0f;   // Tippbetrieb und Rückfahrt
  float    accMmS2       = 20.0f;  // Beschleunigung
  float    hubMm         = 45.0f;  // maximaler Suchweg
  int32_t  ueberSchritte = 10;     // Überlauf nach dem Kontakt
  // Kraft (von Kern 0 aus Tara und Faktor berechnet)
  int32_t  tara          = 0;      // Counts ohne Last
  int32_t  vorzeichen    = 1;      // +1/-1, damit Last positiv ist (Vorzeichen des Faktors)
  int32_t  kontaktCounts = 1000;   // Kontakt: Laständerung gegenüber dem Start
  int32_t  maxCounts     = 500000; // Maximalkraft über Tara: Abwärtsfahrt stoppt sofort
  // Pressvorgang (absolute Positionen, 0 = Werkzeug-Endpunkt)
  int32_t  presseAnfahrt = 0;      // Ende des Eilgangs, ab hier mit vPressMmS
  int32_t  presseEnde    = 0;      // Ende des Pressens (kalibrierter tatsächlicher Endpunkt)
  float    vPressMmS     = 1.0f;   // Pressgeschwindigkeit
  uint32_t haltMs        = 0;      // Haltezeit am Ende
  // Simulation ohne Motor und Last: die Kraft wird im ADC-Interrupt aus der Position berechnet
  uint8_t  simulation     = 0;
  float    countsProGramm = 16.1f; // |Faktor|, rechnet die simulierte Kraft in Counts um
  float    simWerkzeugMm  = 15.0f; // virtuelles Werkzeug so weit unter der Position beim Einschalten
  float    simHoeheMm     = 3.0f;  // virtuelles Werkstück steht so weit über dem Werkzeug
  float    simFminG       = 25000.0f;   // Spitzenkraft je Pressung zufällig zwischen min und max
  float    simFmaxG       = 35000.0f;
};

enum class Zustand : uint8_t {
  BEREIT = 0, FAHRT, SUCHE, UEBERLAUF, ZURUECK,
  PR_ANFAHRT, PR_DRUECKEN, PR_HALTEN, PR_ZURUECK
};

enum class Stopp : uint8_t {
  KEIN = 0, ZIEL, KONTAKT, KRAFT, ALARM, TASTER, BEFEHL, MESSWERTE, KEIN_KONTAKT
};

enum class BefehlTyp : uint8_t { KEIN = 0, FAHRE, KALIBRIEREN, NULLPUNKT, ENA_EIN, ENA_AUS, PARAM, PRESSEN };

enum class EreignisTyp : uint8_t {
  KEIN = 0, FAHRT_ENDE, KAL_FERTIG, KAL_FEHLER, START_TASTE, ALARM, PRESSE_FERTIG, PRESSE_FEHLER
};

struct Ereignis {
  EreignisTyp typ   = EreignisTyp::KEIN;
  Stopp       grund = Stopp::KEIN;
  int32_t     a = 0, b = 0, c = 0;
  // KAL_FERTIG:    a = Kontakttiefe ab Start, b = Überlauf, c = Kraft (Counts über Tara)
  // PRESSE_FERTIG: grund = ZIEL oder KRAFT (Ende des Pressens), a = Position am Ende des Pressens
  // PRESSE_FEHLER: grund = Abbruchursache, a = Position, b = 1: zur Startposition zurückgefahren
};

// Eine Probe des Pressvorgangs (aus dem ADC-Interrupt)
enum : uint8_t { PHASE_DRUECKEN = 2, PHASE_HALTEN = 3 };
struct Probe {
  uint32_t tMikros;
  int32_t  position;      // Schritte
  int32_t  last;          // Counts über Tara, Last positiv
  uint8_t  phase;         // PHASE_DRUECKEN / PHASE_HALTEN
};

struct Status {
  Zustand zustand;
  bool    laeuft;
  bool    referenziert;   // Position bezieht sich auf den Werkzeug-Endpunkt
  bool    alarm;
  bool    enaEin;
  int32_t position;       // Schritte, + = unten
  int32_t lastCounts;     // aktuelle Last über Tara (Counts, Mittel über 4 Werte)
  Stopp   letzterStopp;
};

// ----- Kern 1 -----
void init(uint8_t pinPuls, uint8_t pinDir, uint8_t pinEna, uint8_t pinAlm,
          uint8_t pinStart, uint8_t pinStopp);
void schritt();                                    // aus loop1()
void messwertIsr(uint32_t tMikros, int32_t counts); // aus dem ADS1256-Interrupt

// ----- Kern 0 -----
bool    sende(BefehlTyp typ, int32_t schritte, const Param &p);   // false = Kern 1 noch beschäftigt
void    stopp();                                   // sofort anhalten (Vorrang vor allem)
void    notHalt();                                 // Impulse per Hardware sperren, auch wenn Kern 1 hängt (vor Neustart)
Status  status();
bool    ereignisHolen(Ereignis &e);
bool    beschaeftigt();                            // fährt, Kalibrierung oder Pressvorgang läuft
bool    aufzeichnungHolen(Probe &p);               // nächste Probe des Pressvorgangs
uint32_t aufzeichnungVerloren();                   // Proben verworfen, weil der Ringpuffer voll war
const char *zustandText(Zustand z);
const char *stoppText(Stopp s);

}  // namespace antrieb
