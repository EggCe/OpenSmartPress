#include "geber.h"

#include <hardware/gpio.h>
#include <hardware/sync.h>
#include "konfig.h"
#include "meldung.h"
#include "treiber/Antrieb.h"

// Kern 0 schreibt aktiv/nullAnfrage, Kern 1 schreibt die Zähler (je ein Schreiber, 32 Bit atomar)
static volatile bool     aktiv = false;
static volatile bool     nullAnfrage = false;
static volatile int32_t  zaehler = 0;        // Quadratur-Zählschritte (+ = A eilt B voraus)
static volatile uint32_t fehler = 0;         // unmögliche Übergänge (beide Spuren zugleich -> Schritt verpasst)

// nur Kern 1
static bool    k1Bereit = false;
static uint8_t k1Alt = 0;

// nur Kern 0
static int32_t  gemeldet = 0;
static uint32_t gemeldetFehler = 0;
static uint32_t letzteMeldungMs = 0;
static int32_t  motorStart = 0;

static const uint32_t MELDUNG_MS = 250;

static uint8_t pegel() {
  const uint32_t alle = gpio_get_all();
  return (uint8_t)(((alle >> PIN_GEBER_A) & 1u) | (((alle >> PIN_GEBER_B) & 1u) << 1));
}

void geberKern1Schritt() {
  if (!aktiv) { k1Bereit = false; return; }
  if (nullAnfrage) {
    zaehler = 0;
    fehler = 0;
    nullAnfrage = false;
  }
  const uint8_t s = pegel();
  if (!k1Bereit) { k1Bereit = true; k1Alt = s; return; }
  if (s == k1Alt) return;
  // Index = alt * 4 + neu, Zustand = B << 1 | A; vorwärts 00 -> 01 -> 11 -> 10 -> 00
  static const int8_t TABELLE[16] = { 0, +1, -1, 0,  -1, 0, 0, +1,  +1, 0, 0, -1,  0, -1, +1, 0 };
  if ((k1Alt ^ s) == 3u) fehler = fehler + 1;
  else zaehler = zaehler + TABELLE[(k1Alt << 2) | s];
  k1Alt = s;
}

static void meldeStand(const char *titel) {
  const int32_t z = zaehler;
  const uint32_t f = fehler;
  const int32_t m = antrieb::status().position - motorStart;
  const uint8_t s = pegel();
  meld.printf("# Geber%s%s: A=%u B=%u, %ld Zaehlschritte (= %.2f Striche), Fehler %lu, Motor %ld Schritte",
              titel[0] ? " " : "", titel, s & 1u, (s >> 1) & 1u, (long)z, z / 4.0, (unsigned long)f, (long)m);
  if (m != 0) meld.printf(", %.3f Zaehlschritte/Motorschritt", (double)z / (double)m);
  meld.println();
  gemeldet = z;
  gemeldetFehler = f;
}

void geberBefehl(const char *a1) {
  if (a1 && strcmp(a1, "aus") == 0) {
    if (!aktiv) { meld.println(F("# Geber: nicht aktiv.")); return; }
    meldeStand("Ende");
    aktiv = false;
    return;
  }
  if (a1 && strcmp(a1, "0") == 0) {
    if (!aktiv) { meld.println(F("# Geber: nicht aktiv ('enc' startet).")); return; }
    nullAnfrage = true;
    motorStart = antrieb::status().position;
    gemeldet = 0;
    gemeldetFehler = 0;
    meld.println(F("# Geber: Zaehler auf 0."));
    return;
  }
  if (a1) { meld.println(F("# Aufruf: enc | enc 0 | enc aus")); return; }
  if (aktiv) { meldeStand("Stand"); return; }

  // Start: Pull-down, damit offene Eingänge 0 lesen (am Teiler 10k/20k bleiben es ca. 2,9 V = HIGH)
  const uint8_t pins[2] = { PIN_GEBER_A, PIN_GEBER_B };
  for (uint8_t p : pins) {
    gpio_init(p);
    gpio_set_dir(p, GPIO_IN);
    gpio_pull_down(p);
  }
  motorStart = antrieb::status().position;
  gemeldet = 0;
  gemeldetFehler = 0;
  nullAnfrage = true;
  __dmb();
  aktiv = true;
  letzteMeldungMs = millis();
  const uint8_t s = pegel();
  meld.printf("# Geber-Test: EA+ -> GP%u, EB+ -> GP%u (je ueber 10k, 20k nach GND), GND -> EGND\n",
              PIN_GEBER_A, PIN_GEBER_B);
  meld.printf("#   Pegel jetzt: A=%u B=%u. Welle langsam genau 1 Umdrehung drehen:\n", s & 1u, (s >> 1) & 1u);
  meld.println(F("#   Zaehlschritte = 4 x Strichzahl. 'enc 0' = Null, 'enc' = Stand, 'enc aus' = Ende."));
  meld.println(F("#   Bei 'mot s <n>' zeigt das Verhaeltnis Zaehlschritte/Motorschritt, ob Encoder und Treiber zusammenpassen."));
}

void geberSchritt() {
  if (!aktiv) return;
  const uint32_t ms = millis();
  if (ms - letzteMeldungMs < MELDUNG_MS) return;
  letzteMeldungMs = ms;
  if (zaehler != gemeldet || fehler != gemeldetFehler) meldeStand("");
}
