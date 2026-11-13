/*
 * wlan_zugang.h - WLAN-Zugangsdaten für Waegezelle_Test
 *
 * Hier SSID und Passwort des eigenen WLANs eintragen.
 * Diese Datei NICHT weitergeben oder veröffentlichen!
 * Der Pico W unterstützt nur 2,4-GHz-Netze.
 */
#pragma once

static const char WLAN_SSID[]     = "";
static const char WLAN_PASSWORT[] = "";

// Gerätename im Netz: Weboberfläche dann unter http://waegezelle.local
// (falls .local nicht funktioniert: IP-Adresse aus dem seriellen Monitor)
static const char WLAN_HOSTNAME[] = "waegezelle";
