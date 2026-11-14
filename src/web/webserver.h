/*
 * webserver.h - HTTP-Server der Weboberfläche (nur Kern 0)
 *
 *   GET  /          Messseite (seiten/webui.h)
 *   GET  /info      Hilfeseite (seiten/webinfo.h)
 *   GET  /stil.css  gemeinsames Stylesheet (seiten/webstil.h)
 *   GET  /daten     JSON: Konfiguration, Graphpunkte, Statistik, Antrieb, neue Log-Zeilen
 *                   Parameter p/l = ab Punkt/Log-Position
 *   POST /cmd       Parameter c = Befehl (wie im seriellen Monitor)
 */
#pragma once

void webserverStarten();       // Routen eintragen und Server starten
void webserverBearbeiten();    // aus loop(), solange WLAN an ist
