/*
 * Antrieb.h - Ansteuerung des Closed-Loop-Schrittmotortreibers HB808C (PUL/DIR/ENA)
 *             mit kraftüberwachter Endpunkt-Kalibrierung
 *
 * Läuft komplett auf Kern 1 (gleicher Kern wie der ADS1256-Interrupt):
 *   - Schritterzeugung in Hardware mit einem PWM-Kanal des RP2040. Jeder Schritt
 *     löst einen kurzen Interrupt aus, der die Position zählt und genau am Ziel stoppt.
 *   - messwertIsr() wird aus dem ADS1256-Interrupt für JEDEN Messwert aufgerufen und
 *     stoppt eine Abwärtsfahrt sofort bei Kontakt bzw. bei Maximalkraft.
 *   - schritt() wird aus loop1() aufgerufen: Rampen, Ablauf der Kalibrierung,
 *     Taster, Alarmeingang, Überwachung der Messwerte.
 *
 * Kern 0 benutzt nur: sende(), stopp(), status(), ereignisHolen().
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
};

enum class Zustand : uint8_t { BEREIT = 0, FAHRT, SUCHE, UEBERLAUF, ZURUECK };

enum class Stopp : uint8_t {
  KEIN = 0, ZIEL, KONTAKT, KRAFT, ALARM, TASTER, BEFEHL, MESSWERTE, KEIN_KONTAKT
};

enum class BefehlTyp : uint8_t { KEIN = 0, FAHRE, KALIBRIEREN, NULLPUNKT, ENA_EIN, ENA_AUS, PARAM };

enum class EreignisTyp : uint8_t { KEIN = 0, FAHRT_ENDE, KAL_FERTIG, KAL_FEHLER, START_TASTE, ALARM };

struct Ereignis {
  EreignisTyp typ   = EreignisTyp::KEIN;
  Stopp       grund = Stopp::KEIN;
  int32_t     a = 0, b = 0, c = 0;   // KAL_FERTIG: a = Kontakttiefe ab Start, b = Überlauf, c = Kraft (Counts über Tara)
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
bool    beschaeftigt();                            // fährt oder Kalibrierung läuft
const char *zustandText(Zustand z);
const char *stoppText(Stopp s);

}  // namespace antrieb
