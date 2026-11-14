/*
 * meldung.h - Textmeldungen an USB und an das Web-Log (nur Kern 0)
 *
 * Alle Meldungen beginnen mit "#", damit sie zwischen CSV- und Binärrahmen stehen
 * können. Die Weboberfläche holt das Log über /daten ab.
 */
#pragma once

#include <Arduino.h>

constexpr uint32_t WEBLOG_GROESSE = 4096;      // Zweierpotenz

extern char     webLog[WEBLOG_GROESSE];
extern uint32_t webLogPos;                     // insgesamt geschriebene Zeichen

// Meldungen gehen an USB UND in das Web-Log
class Meldung : public Print {
public:
  size_t write(uint8_t c) override { return write(&c, 1); }
  size_t write(const uint8_t *buf, size_t n) override;
};

extern Meldung meld;
