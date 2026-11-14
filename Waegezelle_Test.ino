/*
 * Waegezelle_Test.ino
 * Testsoftware für eine 50-kg-Wägezelle am ADS1256 mit Raspberry Pi Pico W / WH
 * und kraftüberwachtem Schrittmotor-Antrieb (HB808C)
 *
 * Umgebung: Arduino IDE, Core "Raspberry Pi Pico/RP2040/RP2350" (Earle Philhower),
 *           Board "Raspberry Pi Pico W" (RP2040). Pins und Grenzwerte: src/konfig.h
 *
 * Aufteilung auf die beiden Kerne:
 *   Kern 1: ADS1256 komplett (DRDY-Interrupt, SPI, Init, Datenrate, Kalibrierung) und
 *           der Antrieb (PWM-Schritte, Rampen, Kraftüberwachung im ADC-Interrupt).
 *           Kern 0 schickt ADC-Aufträge über ein Postfach (src/adc.cpp) und wartet.
 *   Kern 0: Verarbeitung (Filter, Überwachung, Aufträge), USB, WLAN und Weboberfläche.
 *           (Laut arduino-pico darf WLAN-Code ohne FreeRTOS nur auf Kern 0 laufen.)
 *   Die Messwerte laufen über den Ringpuffer des Treibers von Kern 1 zu Kern 0.
 *
 * Module (src/):
 *   konfig.h          Pins, Referenz, Nennlast, Grenzwerte
 *   adc               ADS1256-Objekt, Postfach Kern 0 -> Kern 1, Init, Datenrate
 *   verarbeitung      Messwerte abholen, Überwachung, DRDY-Timeout
 *   filter            Glättung (gleitender Mittelwert über eine Zeit)
 *   auftraege         Tara, Kalibrierung, Rauschanalyse, Rauschvergleich
 *   usb_ausgabe       Ausgabemodi (lesbar, CSV, Plotter, binär), #CFG-Zeile
 *   befehle           Befehle aus seriellem Monitor und Weboberfläche
 *   motor             Antrieb auf Kern 0: Parameter, Prüfungen, Meldungen
 *   einstellungen     Flash-Speicher (Tara, Faktor, Antrieb)
 *   meldung           Meldungen an USB und Web-Log
 *   watchdog          Hardware-Watchdog für Kern 0, Herzschlag von Kern 1
 *   web/              WLAN, Webserver, Live-Daten, Seiten (HTML/CSS)
 *   treiber/          ADS1256 und Antrieb (HB808C)
 *
 * Bedienung:
 *   - Serieller Monitor, Zeilenende "Neue Zeile". Befehl "h" zeigt die Hilfe.
 *   - Weboberfläche: http://waegezelle.local oder die IP aus dem seriellen Monitor.
 *     WLAN-Zugangsdaten in wlan_zugang.h eintragen (Vorlage: wlan_zugang.beispiel.h).
 *   - Live-Anzeige über USB: tools/waegezelle_plot.py (schaltet Binärmodus ein).
 *   Datenformate (USB und HTTP): siehe README.md.
 */
#include <Arduino.h>
#include "src/konfig.h"
#include "src/meldung.h"
#include "src/pruefsumme.h"
#include "src/einstellungen.h"
#include "src/adc.h"
#include "src/verarbeitung.h"
#include "src/auftraege.h"
#include "src/usb_ausgabe.h"
#include "src/befehle.h"
#include "src/motor.h"
#include "src/web/wlan.h"
#include "src/web/webserver.h"
#include "src/watchdog.h"
#include "src/treiber/Antrieb.h"

// Kern 1 bekommt einen eigenen 8-KB-Stack (statt 4 KB geteilt mit Kern 0)
bool core1_separate_stack = true;

// ===========================================================================
// Kern 0
// ===========================================================================
void setup() {
  Serial.begin(115200);            // USB-CDC: Baudrate ohne Bedeutung
  uint32_t start = millis();
  while (!Serial && millis() - start < 3000) { }   // kurz auf den seriellen Monitor warten

  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);
  crc8Init();

  meld.println();
  meld.println(F("# ================================================="));
  meld.println(F("#  Waegezellen-Test: ADS1256 + Raspberry Pi Pico W"));
  meld.println(F("# ================================================="));

  einstellungenStarten();          // Einstellungen und Antrieb-Parameter aus dem Flash

  // Jeder Messwert geht im Interrupt auch an die Kraftüberwachung des Antriebs
  adcStarten(antrieb::messwertIsr);
  antriebSenden(antrieb::BefehlTyp::PARAM, 0);

  webserverStarten();
  wlanStarten();

  watchdogStarten();               // ab hier: Neustart, wenn ein Kern hängt

  meld.println(F("# Befehl 'h' zeigt die Hilfe."));
  if (cfg.modus == MODUS_CSV) Serial.println(F("seq,t_us,roh"));
}

void loop() {
  watchdogFuettern();              // nur solange auch Kern 1 lebt
  if (vergleichFaellig()) vergleichSchritt();   // vor den Befehlen, damit kein Befehl dazwischenfunkt
  leseSeriell();

  if (wlanAn) webserverBearbeiten();
  wlanSchritt();

  antriebEreignisse();             // Meldungen von Kern 1 (Fahrt beendet, Kalibrierung, Taster, Alarm)
  messwerteAbholen();
}

// ===========================================================================
// Kern 1
// ===========================================================================
void setup1() {
  // Antrieb auf Kern 1 (PWM-Interrupt hier); ohne 'mp spmm' bewegt er sich nicht.
  // Den ADS1256 initialisiert Kern 0 per Auftrag, sobald er bereit ist.
  antrieb::init(PIN_MOT_PUL, PIN_MOT_DIR, PIN_MOT_ENA, PIN_MOT_ALM, PIN_TASTE_START, PIN_TASTE_STOPP);
}

void loop1() {
  watchdogHerzschlag();            // Lebenszeichen für Kern 0
  antrieb::schritt();              // Rampen, Ablauf, Taster, Alarm (kehrt sofort zurück)
  adcKern1Schritt();               // offenen ADC-Auftrag von Kern 0 erledigen
}
