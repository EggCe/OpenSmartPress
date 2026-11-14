#include "meldung.h"

char     webLog[WEBLOG_GROESSE];
uint32_t webLogPos = 0;
Meldung  meld;

static void webLogSchreiben(const uint8_t *s, size_t n) {
  for (size_t i = 0; i < n; i++) webLog[webLogPos++ & (WEBLOG_GROESSE - 1)] = (char)s[i];
}

size_t Meldung::write(const uint8_t *buf, size_t n) {
  webLogSchreiben(buf, n);
  Serial.write(buf, n);
  return n;
}
