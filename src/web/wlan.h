/*
 * wlan.h - WLAN-Verbindung und mDNS (nur Kern 0; arduino-pico ohne FreeRTOS)
 *
 * Zugangsdaten in wlan_zugang.h im Sketch-Ordner (Vorlage: wlan_zugang.beispiel.h).
 */
#pragma once

#include <Arduino.h>

extern bool wlanAn;            // false nach 'wlan aus'

void wlanStarten();            // nicht blockierend verbinden
void wlanSchritt();            // aus loop(): fälliges 'wlan aus', alle 500 ms Verbindung prüfen
void wlanAusschalten();        // erst im nächsten wlanSchritt() (Befehl kann aus HTTP kommen)
void wlanEinschalten();
void wlanInfo();
long wlanRssi();               // 0, wenn nicht verbunden
