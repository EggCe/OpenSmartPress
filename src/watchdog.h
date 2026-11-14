/*
 * watchdog.h - Überwachung beider Kerne mit dem Hardware-Watchdog des RP2040
 *
 * Kern 0: füttert den Hardware-Watchdog in loop(). Hängt Kern 0 länger als
 *         WATCHDOG_TIMEOUT_MS, startet der Chip neu.
 * Kern 1: zählt in loop1() einen Herzschlag hoch. Kern 0 füttert den Watchdog nur,
 *         solange sich der Herzschlag ändert. Steht er länger als KERN1_TIMEOUT_MS,
 *         sperrt Kern 0 die Schrittimpulse per Hardware (antrieb::notHalt) und
 *         startet sofort neu - der Antrieb fährt nie ohne Kraftüberwachung weiter.
 * Nach einem solchen Neustart meldet watchdogStarten() den Grund.
 *
 * Wartet Kern 0 absichtlich auf Kern 1 (ADC-Auftrag, Flash schreiben), dort
 * watchdogWarten() aufrufen: füttert ohne Herzschlagprüfung.
 */
#pragma once

#include <Arduino.h>

constexpr uint32_t WATCHDOG_TIMEOUT_MS = 8000;   // Kern 0 (RP2040: max. 8388 ms); länger als jede Warteschleife
constexpr uint32_t KERN1_TIMEOUT_MS    = 1000;   // Kern 1 ohne Herzschlag (Flash schreiben dauert < 0,5 s)

// ----- Kern 0 -----
void watchdogStarten();              // Ende von setup(): Neustartgrund melden, Watchdog einschalten
void watchdogFuettern();             // aus loop()
void watchdogWarten();               // in Warteschleifen auf Kern 1 (die einen eigenen Timeout haben)
void watchdogInfo();                 // Befehl 'wd'
void watchdogTest(uint8_t kern);     // Befehl 'wd test 0|1': Kern absichtlich aufhängen

// ----- Kern 1 -----
void watchdogHerzschlag();           // aus loop1()
