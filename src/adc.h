/*
 * adc.h - ADS1256 und Postfach zwischen den Kernen
 *
 * Kern 1 macht alle SPI-Zugriffe (Init, Datenrate, Selbstkalibrierung, Register)
 * und bedient den DRDY-Interrupt. Kern 0 gibt Aufträge über ein Postfach mit
 * einem Platz ab und wartet auf die Erledigung. Die Messwerte laufen über den
 * Ringpuffer des Treibers (ads.popSample) von Kern 1 zu Kern 0.
 */
#pragma once

#include <Arduino.h>
#include "treiber/ADS1256.h"

extern ADS1256 ads;
extern bool    adsOk;          // ADS1256 initialisiert und im Dauerbetrieb

// ----- Kern 0 -----
// SPI-Pins setzen, DRDY-Callback eintragen (läuft im Interrupt auf Kern 1) und initADS().
void adcStarten(ADS1256::DrdyCallback cb);
bool initADS();                                    // (neu) initialisieren und Ergebnis melden
bool setzeRate(uint8_t code, bool melden);         // Datenrate setzen, Filter und Web-Daten zurücksetzen
bool adcSelbstkalibrierung();                      // false: Timeout oder Dauerbetrieb nicht wieder gestartet
// Register lesen. Rückgabe false: Kern 1 hat nicht geantwortet.
// dauerbetriebOk = false: RDATAC lief danach nicht wieder an.
bool adcRegisterLesen(uint8_t regs[ADS1256::REG_COUNT], bool &dauerbetriebOk);
uint32_t blockLaenge10();                          // Rohwerte je 0,1 s (0 unter 20 SPS)
uint32_t standardN(float sekunden, uint32_t minimum);   // Messanzahl für eine Dauer

// ----- Kern 1 -----
void adcKern1Schritt();                            // aus loop1(): offenen Auftrag erledigen
