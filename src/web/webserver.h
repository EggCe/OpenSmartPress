/*
 * webserver.h - HTTP-Server der Weboberfläche (nur Kern 0)
 *
 *   GET  /          Messseite (seiten/webui.h)
 *   GET  /stil.css  gemeinsames Stylesheet (seiten/webstil.h)
 *   GET  /daten     JSON: Konfiguration, Graphpunkte, Statistik, Antrieb, neue Log-Zeilen
 *                   Parameter p/l = ab Punkt/Log-Position
 *   POST /cmd       Parameter c = Befehl (wie im seriellen Monitor)
 *   Pressseite und Sitzungen: siehe presse_web.cpp
 *
 * Schreibende Anfragen (POST /cmd, /sitzung, /zeit) brauchen die Kopfzeile
 * "X-Waegezelle: 1" (webSchreibenErlaubt). Eine fremde Webseite kann sie im Browser
 * nicht ohne CORS-Vorabfrage setzen, die der Pico nie beantwortet - so löst kein
 * Link und kein Formular einer anderen Seite eine Fahrt aus (CSRF). Das ist KEIN
 * Passwortschutz: jedes Programm im selben Netz kann die Kopfzeile senden.
 */
#pragma once

#include <WebServer.h>

void webserverStarten();       // Routen eintragen und Server starten
void webserverBearbeiten();    // aus loop(), solange WLAN an ist

// ----- intern für die Routen-Module unter src/web/ -----
WebServer &webServer();
void jsonStart(const char *typ = "application/json");   // chunked-Antwort beginnen
void jsonf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));   // gepuffert senden (max. 255 Zeichen je Aufruf)
void jsonZeichen(char c);                                // ein Zeichen in einem JSON-String (mit Escape)
void jsonFlush();
void jsonEnde();
void presseWebRouten();                                  // presse_web.cpp
bool webSchreibenErlaubt();                              // false: 403 schon gesendet
bool webSeiteGesperrt();                                 // true: Antrieb läuft, kurze Warteseite (503) schon gesendet
