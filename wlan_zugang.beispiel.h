/*
 * wlan_zugang.beispiel.h - Vorlage für die WLAN-Zugangsdaten
 *
 * Diese Datei nach "wlan_zugang.h" kopieren und dort SSID und Passwort eintragen.
 * wlan_zugang.h steht in .gitignore und wird nicht ins Repository übernommen.
 * Der Pico W unterstützt nur 2,4-GHz-Netze.
 */
#pragma once

static const char WLAN_SSID[]     = "MEIN_WLAN";
static const char WLAN_PASSWORT[] = "MEIN_PASSWORT";

// Gerätename im Netz: Weboberfläche dann unter http://waegezelle.local
// (falls .local nicht funktioniert: IP-Adresse aus dem seriellen Monitor)
static const char WLAN_HOSTNAME[] = "waegezelle";
