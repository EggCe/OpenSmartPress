/*
 * motortest.ino - Eigenständiges Testprogramm NUR für den Antrieb (HB808C + Closed-Loop-Motor)
 *
 * Kein ADS1256, kein WLAN, KEINE KRAFTÜBERWACHUNG: Der Motor fährt blind. Nur ohne Werkstück
 * und mit freiem Weg benutzen (Motor am besten von der Spindel abgekuppelt).
 * Stopp: 'x' (bzw. jede Eingabe während der Fahrt), STOPP-Taster an GP7, Alarm (ALM) mit 'alm 1|2'.
 *
 * Board wie das Hauptprogramm ("Raspberry Pi Pico W", Core 6.3.2), eigener Sketch-Ordner.
 * Bedienung: Serieller Monitor oder Eingabezeile des seriellen Plotters (Arduino IDE 2),
 * Zeilenende "Neue Zeile". Der Plotter zeigt Soll (Schritte), Enc (Encoder-Zählschritte),
 * Fehler (Encoder) und ALM (Pegel GP5).
 *
 * Pins wie src/konfig.h (hier kopiert, weil ein eigener Sketch nicht aus src/ einbinden kann).
 */
#include <Arduino.h>
#include <hardware/gpio.h>

// ----- Pins (wie src/konfig.h) -----
static const uint8_t PIN_PUL   = 2;
static const uint8_t PIN_DIR   = 3;
static const uint8_t PIN_ENA   = 4;
static const uint8_t PIN_ALM   = 5;
static const uint8_t PIN_STOPP = 7;    // Taster gegen GND
static const uint8_t PIN_ENC_A = 10;   // Encoder EA+ über Teiler 10k/20k (optional)
static const uint8_t PIN_ENC_B = 11;   // Encoder EB+ über Teiler 10k/20k (optional)

// ----- Grenzen -----
static const uint32_t PULS_US      = 5;        // Impulsbreite (HB808C: >= 1,5 µs)
static const uint32_t DIR_VORLAUF  = 20;       // DIR so lange vor dem ersten Impuls (>= 1 µs)
static const float    F_MIN        = 10.0f;    // Schritte/s
static const float    F_MAX        = 20000.0f;
static const int32_t  SCHRITTE_MAX = 400000;   // je Befehl
static const uint32_t PLOT_FAHRT_MS = 20;
static const uint32_t PLOT_RUHE_MS  = 100;

// ----- Einstellungen (nur RAM) -----
static uint8_t inv     = 1;        // 1: aktiv = LOW (gemeinsame Anode, MOSFET-Pegelwandler); 0: aktiv = HIGH (direkt)
static uint8_t enaModus = 0;       // 0: ENA nicht benutzt (Pin hochohmig), 1: Opto an = Motor aus, 2: Opto an = Motor ein
static uint8_t almModus = 0;       // 0: ALM ignorieren, 1: Alarm bei LOW, 2: Alarm bei HIGH
static uint8_t dirAb    = 0;       // logischer DIR-Pegel für positive Schritte
static bool    enaEin   = false;
static float   vHz      = 400.0f;  // Schritte/s
static float   accHz    = 2000.0f; // Schritte/s²
static bool    plotAn   = true;

static int32_t pos = 0;            // Soll-Position in Schritten

// ----- Encoder (Interrupt) -----
static volatile int32_t  encZaehler = 0;
static volatile uint32_t encFehler  = 0;
static volatile uint8_t  encAlt     = 0;

static uint8_t encPegel() {
  const uint32_t alle = gpio_get_all();
  return (uint8_t)(((alle >> PIN_ENC_A) & 1u) | (((alle >> PIN_ENC_B) & 1u) << 1));
}

static void encIsr() {
  // Zustand = B << 1 | A, Index = alt * 4 + neu; vorwärts 00 -> 01 -> 11 -> 10 -> 00
  static const int8_t TABELLE[16] = { 0, +1, -1, 0,  -1, 0, 0, +1,  +1, 0, 0, -1,  0, -1, +1, 0 };
  const uint8_t s = encPegel();
  const uint8_t a = encAlt;
  if (s == a) return;
  if ((a ^ s) == 3u) encFehler = encFehler + 1;
  else encZaehler = encZaehler + TABELLE[(a << 2) | s];
  encAlt = s;
}

// ----- Ausgänge -----
static inline void ausgang(uint8_t pin, bool logisch) { gpio_put(pin, logisch != (inv != 0)); }

static void enaAnwenden() {
  if (enaModus == 0) { gpio_set_dir(PIN_ENA, GPIO_IN); return; }
  gpio_set_dir(PIN_ENA, GPIO_OUT);
  ausgang(PIN_ENA, enaModus == 1 ? !enaEin : enaEin);
}

static void ruhepegel() {
  ausgang(PIN_PUL, false);
  ausgang(PIN_DIR, dirAb);
  enaAnwenden();
}

static bool alarm() {
  if (almModus == 1) return !gpio_get(PIN_ALM);
  if (almModus == 2) return gpio_get(PIN_ALM);
  return false;
}

// ----- Ausgabe -----
static void plot() {
  if (!plotAn) return;
  Serial.printf("Soll:%ld Enc:%ld Fehler:%lu ALM:%u\n", (long)pos, (long)encZaehler,
                (unsigned long)encFehler, gpio_get(PIN_ALM) ? 1u : 0u);
}

static void hilfe() {
  Serial.println(F("# ---- Motortest (OHNE Kraftueberwachung!) ----"));
  Serial.println(F("#  s <n>        n Schritte fahren (+/-), max. 400000"));
  Serial.println(F("#  v <Hz>       Geschwindigkeit in Schritten/s (10..20000)"));
  Serial.println(F("#  a <Hz/s>     Beschleunigung in Schritten/s^2 (0 = ohne Rampe)"));
  Serial.println(F("#  inv 0|1      0 = aktiv HIGH (Pico direkt an PUL+/DIR+), 1 = aktiv LOW (Pegelwandler, gemeinsame Anode)"));
  Serial.println(F("#  dir 0|1      Drehrichtung fuer positive Schritte umkehren"));
  Serial.println(F("#  ena 0|1|2    0 = ENA offen, 1 = Opto an schaltet Motor aus, 2 = Opto an schaltet Motor ein"));
  Serial.println(F("#  ein | aus    Motorstrom ein/aus (nur mit ena 1|2)"));
  Serial.println(F("#  alm 0|1|2    Alarmeingang: 0 ignorieren, 1 Alarm bei LOW, 2 Alarm bei HIGH"));
  Serial.println(F("#  null         Soll und Encoder auf 0"));
  Serial.println(F("#  p 0|1        Plotter-Ausgabe aus/an"));
  Serial.println(F("#  i            Status"));
  Serial.println(F("#  x            Stopp (waehrend der Fahrt stoppt jede Eingabe)"));
}

static void status() {
  Serial.printf("# inv=%u dir=%u ena=%u (%s) alm=%u v=%.0f a=%.0f pos=%ld enc=%ld fehler=%lu\n",
                inv, dirAb, enaModus, enaEin ? "ein" : "aus", almModus, vHz, accHz, (long)pos,
                (long)encZaehler, (unsigned long)encFehler);
  Serial.printf("# Pegel: ALM=%u STOPP=%u EncA=%u EncB=%u\n", gpio_get(PIN_ALM) ? 1u : 0u,
                gpio_get(PIN_STOPP) ? 1u : 0u, encPegel() & 1u, (encPegel() >> 1) & 1u);
}

// ----- Fahrt (blockierend, prüft bei jedem Schritt die Abbruchbedingungen) -----
static void fahre(int32_t n) {
  if (n == 0) return;
  if (n > SCHRITTE_MAX || n < -SCHRITTE_MAX) { Serial.println(F("# zu viele Schritte")); return; }
  if (enaModus != 0 && !enaEin) { Serial.println(F("# Motorstrom aus - erst 'ein'")); return; }
  if (!gpio_get(PIN_STOPP)) { Serial.println(F("# STOPP-Taster gedrueckt")); return; }
  if (alarm()) { Serial.println(F("# Treiber meldet Alarm - Treiber aus- und wieder einschalten")); return; }

  const int32_t richtung = n > 0 ? 1 : -1;
  const int32_t gesamt = n > 0 ? n : -n;
  ausgang(PIN_DIR, richtung > 0 ? dirAb : !dirAb);
  delayMicroseconds(DIR_VORLAUF);

  const float vMax = constrain(vHz, F_MIN, F_MAX);
  const float vStart = accHz > 0.0f ? constrain(sqrtf(accHz), F_MIN, vMax) : vMax;
  const char *grund = "Ziel erreicht";
  uint32_t tNaechster = micros();
  uint32_t tPlot = millis();
  const int32_t enc0 = encZaehler;
  int32_t gemacht = 0;

  for (int32_t i = 0; i < gesamt; i++) {
    if (Serial.available()) { while (Serial.available()) Serial.read(); grund = "Stopp (Eingabe)"; break; }
    if (!gpio_get(PIN_STOPP)) { grund = "STOPP-Taster"; break; }
    if (alarm()) { grund = "Alarm vom Treiber"; break; }

    // Trapezrampe: v^2 = vStart^2 + 2 a s, beim Abbremsen mit den restlichen Schritten
    float v = vMax;
    if (accHz > 0.0f) {
      const int32_t rest = gesamt - i;
      const int32_t s = i < rest ? i : rest;
      v = sqrtf(vStart * vStart + 2.0f * accHz * (float)s);
      if (v > vMax) v = vMax;
    }
    const uint32_t periode = (uint32_t)(1.0e6f / v);

    while ((int32_t)(micros() - tNaechster) < 0) { }
    tNaechster += periode;
    if ((int32_t)(micros() - tNaechster) > (int32_t)periode) tNaechster = micros() + periode;   // nach Verzögerung nicht nachholen

    ausgang(PIN_PUL, true);
    delayMicroseconds(PULS_US);
    ausgang(PIN_PUL, false);
    pos += richtung;
    gemacht++;

    if (millis() - tPlot >= PLOT_FAHRT_MS) { tPlot = millis(); plot(); }
  }
  plot();
  const int32_t dEnc = encZaehler - enc0;
  Serial.printf("# Fahrt beendet (%s): %ld von %ld Schritten, pos=%ld, Encoder %+ld Zaehlschritte",
                grund, (long)gemacht, (long)gesamt, (long)pos, (long)dEnc);
  if (gemacht != 0) Serial.printf(" (%.3f je Schritt)", (double)dEnc / (double)(gemacht * richtung));
  Serial.println();
}

// ----- Befehle -----
static void befehl(char *z) {
  for (char *p = z; *p; p++) { if (*p == ',') *p = '.'; else *p = (char)tolower((unsigned char)*p); }
  char *cmd = strtok(z, " \t=");
  if (!cmd) return;
  char *a = strtok(nullptr, " \t=");
  const long w = a ? strtol(a, nullptr, 10) : -1;

  if (!strcmp(cmd, "h") || !strcmp(cmd, "?")) hilfe();
  else if (!strcmp(cmd, "i")) status();
  else if (!strcmp(cmd, "x")) Serial.println(F("# steht"));
  else if (!strcmp(cmd, "s") && a) fahre((int32_t)w);
  else if (!strcmp(cmd, "v") && a) { vHz = constrain(atof(a), F_MIN, F_MAX); status(); }
  else if (!strcmp(cmd, "a") && a) { accHz = max(0.0, atof(a)); status(); }
  else if (!strcmp(cmd, "inv") && (w == 0 || w == 1)) { inv = (uint8_t)w; ruhepegel(); status(); }
  else if (!strcmp(cmd, "dir") && (w == 0 || w == 1)) { dirAb = (uint8_t)w; ruhepegel(); status(); }
  else if (!strcmp(cmd, "ena") && w >= 0 && w <= 2) { enaModus = (uint8_t)w; enaAnwenden(); status(); }
  else if (!strcmp(cmd, "ein")) { enaEin = true; enaAnwenden(); status(); }
  else if (!strcmp(cmd, "aus")) { enaEin = false; enaAnwenden(); status(); }
  else if (!strcmp(cmd, "alm") && w >= 0 && w <= 2) { almModus = (uint8_t)w; status(); }
  else if (!strcmp(cmd, "null")) { pos = 0; encZaehler = 0; encFehler = 0; status(); }
  else if (!strcmp(cmd, "p") && (w == 0 || w == 1)) plotAn = (w == 1);
  else Serial.println(F("# unbekannt - 'h' fuer Hilfe"));
}

void setup() {
  Serial.begin(115200);
  const uint32_t t0 = millis();
  while (!Serial && millis() - t0 < 3000) { }

  gpio_init(PIN_PUL); gpio_set_dir(PIN_PUL, GPIO_OUT);
  gpio_init(PIN_DIR); gpio_set_dir(PIN_DIR, GPIO_OUT);
  gpio_init(PIN_ENA); gpio_set_dir(PIN_ENA, GPIO_IN);
  // MOSFET-Pegelwandler bzw. direkter Anschluss: Optokoppler-Strom fließt durch den Pin
  gpio_set_drive_strength(PIN_PUL, GPIO_DRIVE_STRENGTH_12MA);
  gpio_set_drive_strength(PIN_DIR, GPIO_DRIVE_STRENGTH_12MA);
  gpio_set_drive_strength(PIN_ENA, GPIO_DRIVE_STRENGTH_12MA);
  ruhepegel();

  pinMode(PIN_ALM, INPUT_PULLUP);
  pinMode(PIN_STOPP, INPUT_PULLUP);
  pinMode(PIN_ENC_A, INPUT_PULLDOWN);
  pinMode(PIN_ENC_B, INPUT_PULLDOWN);
  encAlt = encPegel();
  attachInterrupt(digitalPinToInterrupt(PIN_ENC_A), encIsr, CHANGE);
  attachInterrupt(digitalPinToInterrupt(PIN_ENC_B), encIsr, CHANGE);

  hilfe();
  status();
}

void loop() {
  static char zeile[48];
  static uint8_t n = 0;
  while (Serial.available()) {
    const char c = (char)Serial.read();
    if (c == '\r') continue;
    if (c == '\n') { zeile[n] = 0; if (n) befehl(zeile); n = 0; continue; }
    if (n < sizeof(zeile) - 1) zeile[n++] = c;
  }
  static uint32_t tPlot = 0;
  if (millis() - tPlot >= PLOT_RUHE_MS) { tPlot = millis(); plot(); }
}
