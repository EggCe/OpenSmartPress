#include "befehle.h"

#include <math.h>
#include "konfig.h"
#include "meldung.h"
#include "einstellungen.h"
#include "adc.h"
#include "filter.h"
#include "auftraege.h"
#include "usb_ausgabe.h"
#include "motor.h"
#include "watchdog.h"
#include "web/livedaten.h"
#include "web/wlan.h"

// Serielle Eingabe
static char    zeile[BEFEHL_MAX_LAENGE];
static uint8_t zeileLen = 0;

static bool ist(const char *cmd, const char *a, const char *b) {
  return strcmp(cmd, a) == 0 || strcmp(cmd, b) == 0;
}

static void zeigeHilfe() {
  meld.println(F("# ---------------- Befehle (Kurzform / Langform) ----------------"));
  meld.println(F("#  h | hilfe              diese Hilfe"));
  meld.println(F("#  i | info               aktuelle Einstellungen und Status"));
  meld.println(F("#      cfg                #CFG-Zeile (Umrechnungsgroessen) senden"));
  meld.println(F("#  t | tara [n]           Nullpunkt aus n Messungen (Standard: ca. 2 s)"));
  meld.println(F("#  k | kal <g> [n]        Kalibrieren mit bekanntem Gewicht in Gramm"));
  meld.println(F("#  f | faktor <c/g>       Kalibrierfaktor direkt setzen (Counts pro Gramm)"));
  meld.println(F("#  r | rate <sps>         Datenrate: 2.5 5 10 15 25 30 50 60 100 500 1000 2000 3750 7500 15000 30000"));
  meld.println(F("# fl | filter <s>         Glaettung: gleitender Mittelwert ueber s Sekunden (0..60, 0 = aus)"));
  meld.println(F("#  m | modus <0..4>       0 aus, 1 lesbar, 2 CSV (Rohwerte), 3 Plotter, 4 binaer"));
  meld.println(F("#  n | rausch [n]         Rauschanalyse ueber n Messungen (Standard: ca. 5 s)"));
  meld.println(F("# nv | rauschvergleich [s] [sps ...]"));
  meld.println(F("#                         Rauschanalyse fuer mehrere Datenraten, je s Sekunden"));
  meld.println(F("#                         (Standard: 3 s; 100 500 1000 2000 3750 7500 15000 30000)"));
  meld.println(F("#  d | reg                Registerdump des ADS1256"));
  meld.println(F("#  c | selfcal            Selbstkalibrierung des ADC (Offset + Gain)"));
  meld.println(F("#  s | speichern          Tara, Faktor und Einstellungen im Flash ablegen"));
  meld.println(F("#  w | werk ja            Werkseinstellungen laden und speichern"));
  meld.println(F("#  x | stop               laufenden Auftrag abbrechen bzw. Ausgabe aus"));
  meld.println(F("#      init               ADS1256 neu initialisieren"));
  meld.println(F("#      ws <s>             Dauer der Live-Statistik im Web (0.5..60 s)"));
  meld.println(F("#      mot                Antrieb: Status"));
  meld.println(F("#      mot kal            Endpunkt-Kalibrierung (wie START-Taster)"));
  meld.println(F("#      mot ab|auf <mm>    Tippbetrieb; 'mot s <n>' = n Schritte (+ ab)"));
  meld.println(F("#      mot null|ein|aus   Position 0 setzen / Motorstrom ein / aus"));
  meld.println(F("#      mot stop           anhalten (auch 'x' und STOPP-Taster)"));
  meld.println(F("#      mp [name wert]     Antrieb-Parameter anzeigen bzw. setzen"));
  meld.println(F("#      wlan [an|aus]      WLAN-Status bzw. ein-/ausschalten"));
  meld.println(F("#                         ('wlan aus' fuer Rauschmessungen ohne Funk; danach nur per USB wieder an)"));
  meld.println(F("#      wd                 Watchdog-Status und Grund des letzten Neustarts"));
  meld.println(F("#      wd test 0|1        Kern 0 bzw. 1 absichtlich aufhaengen (Watchdog pruefen, loest Neustart aus)"));
  meld.println(F("# Ablauf: Zelle entlasten -> 't' -> Gewicht auflegen -> 'k 5000' -> 's'"));
  meld.println(F("# ---------------------------------------------------------------"));
}

static void zeigeInfo() {
  meld.println(F("# ---------------- Status ----------------"));
  meld.printf("# ADS1256:        %s\n", adsOk ? "bereit" : "NICHT initialisiert");
  if (adsOk) {
    meld.printf("# Datenrate:      %.1f SPS, PGA %u, Bereich +-%.3f mV\n",
                ads.sampleRate(), ads.gain(), ads.fullScaleMicrovolts() / 1000.0);
    meld.printf("# Aufloesung:     1 Count = %.4f nV\n", ads.microvoltsPerCount() * 1000.0);
    meld.printf("# Betrieb:        %s, SPI %.2f MHz\n",
                ads.isContinuous() ? "Dauerbetrieb (RDATAC, Kern 1)" : "ANGEHALTEN", ads.spiTakt() / 1e6);
    meld.printf("# Ringpuffer:     max. %lu von %lu belegt\n",
                (unsigned long)ads.ringMaximum(), (unsigned long)ADS1256::RING_GROESSE);
    meld.printf("# Verlorene Werte: %lu (DRDY verpasst %lu, Puffer voll %lu, Interrupt blockiert %lu)\n",
                (unsigned long)ads.overruns(), (unsigned long)ads.verlorenDrdy(),
                (unsigned long)ads.verlorenVoll(), (unsigned long)ads.verlorenLuecke());
  }
  if (cfg.filterMs == 0) {
    meld.println(F("# Glaettung:      aus"));
  } else {
    meld.printf("# Glaettung:      %.3f s (%u Bloecke zu je %lu Werten)\n",
                filterSekunden(), filterBloecke(), (unsigned long)filterBlockLaenge());
  }
  meld.printf("# Ausgabemodus:   %u\n", cfg.modus);
  meld.printf("# Tara:           %.1f Counts\n", cfg.tara);
  meld.printf("# Faktor:         %.5f Counts/g (%s)\n", cfg.faktor,
              cfg.kalibriert ? "kalibriert" : "Schaetzwert, NICHT kalibriert");
  wlanInfo();
  watchdogInfo();
  {
    const antrieb::Status s = antrieb::status();
    meld.printf("# Antrieb:        %s, %s\n", antrieb::zustandText(s.zustand),
                mot.schritteProMm > 0.0f ? "eingerichtet ('mot' fuer Details)" : "nicht eingerichtet ('mp spmm')");
  }
  meld.println(F("# -----------------------------------------"));
}

static void registerDump() {
  static const char *const NAMEN[ADS1256::REG_COUNT] = {
    "STATUS", "MUX", "ADCON", "DRATE", "IO", "OFC0", "OFC1", "OFC2", "FSC0", "FSC1", "FSC2"
  };
  uint8_t r[ADS1256::REG_COUNT];
  bool dauerbetriebOk = false;
  if (!adcRegisterLesen(r, dauerbetriebOk)) return;  // Kern 1 hält RDATAC kurz an
  if (!dauerbetriebOk) meld.println(F("# FEHLER: Dauerbetrieb nach dem Lesen nicht wieder gestartet ('init')."));

  meld.println(F("# ---------------- Register ADS1256 ----------------"));
  for (uint8_t i = 0; i < ADS1256::REG_COUNT; i++) {
    meld.printf("#  0x%02X %-6s = 0x%02X\n", i, NAMEN[i], r[i]);
  }
  meld.printf("# STATUS: ID=%u, ORDER=%s, ACAL=%u, BUFEN=%u\n", r[0] >> 4,
              (r[0] & ADS1256::STATUS_ORDER) ? "LSB" : "MSB",
              (r[0] & ADS1256::STATUS_ACAL) ? 1 : 0, (r[0] & ADS1256::STATUS_BUFEN) ? 1 : 0);
  meld.printf("# MUX:    AINP=%u, AINN=%u (8 = AINCOM)\n", r[1] >> 4, r[1] & 0x0F);
  meld.printf("# ADCON:  CLKOUT=%u, SDCS=%u, PGA=%u\n", (r[2] >> 5) & 0x03, (r[2] >> 3) & 0x03, 1u << (r[2] & 0x07));
  meld.printf("# DRATE:  %.1f SPS\n", ADS1256::rateFromCode(r[3]));
  uint32_t ofc = ((uint32_t)r[7] << 16) | ((uint32_t)r[6] << 8) | r[5];
  uint32_t fsc = ((uint32_t)r[10] << 16) | ((uint32_t)r[9] << 8) | r[8];
  meld.printf("# OFC:    %ld (Offsetkalibrierung)\n", (long)ADS1256::signExtend24(ofc));
  meld.printf("# FSC:    %lu (Verstaerkungskalibrierung)\n", (unsigned long)fsc);
  meld.println(F("# ---------------------------------------------------"));
}

static void zeigeRaten() {
  meld.print(F("# Gueltige Datenraten (SPS):"));
  for (uint8_t i = 0; i < ADS1256::rateCount(); i++) meld.printf(" %g", ADS1256::rateByIndex(i));
  meld.println();
}

void verarbeiteBefehl(char *text) {
  // Kleinbuchstaben; deutsches Dezimalkomma in Punkt umwandeln
  for (char *p = text; *p; p++) {
    if (*p == ',') *p = '.';
    else *p = (char)tolower((unsigned char)*p);
  }
  const uint8_t ARG_MAX = 18;
  char *arg[ARG_MAX];
  uint8_t argc = 0;
  char *cmd = strtok(text, " \t=");
  if (!cmd) return;
  while (argc < ARG_MAX && (arg[argc] = strtok(nullptr, " \t=")) != nullptr) argc++;
  char *a1 = argc > 0 ? arg[0] : nullptr;
  char *a2 = argc > 1 ? arg[1] : nullptr;

  // ----- immer erlaubt -----
  if (ist(cmd, "h", "hilfe") || strcmp(cmd, "?") == 0) { zeigeHilfe(); return; }
  if (ist(cmd, "i", "info")) { zeigeInfo(); return; }
  if (strcmp(cmd, "cfg") == 0) { sendeCfg(); return; }
  if (ist(cmd, "x", "stop")) {
    if (antrieb::beschaeftigt()) {           // Motor hat Vorrang
      antrieb::stopp();
      meld.println(F("# Antrieb: STOPP."));
      return;
    }
    if (auftragLaeuft()) auftragAbbrechen("durch Benutzer");
    else setzeModus(MODUS_AUS);
    return;
  }
  if (strcmp(cmd, "mot") == 0) { motorBefehl(a1, a2); return; }
  if (strcmp(cmd, "mp") == 0)  { motorParameter(a1, a2); return; }
  if (ist(cmd, "m", "modus")) {    // auch während eines Auftrags erlaubt
    long m = -1;
    if (a1) {
      if (strcmp(a1, "aus") == 0) m = MODUS_AUS;
      else if (strcmp(a1, "lesbar") == 0) m = MODUS_LESBAR;
      else if (strcmp(a1, "csv") == 0) m = MODUS_CSV;
      else if (strncmp(a1, "plot", 4) == 0) m = MODUS_PLOTTER;
      else if (strncmp(a1, "bin", 3) == 0) m = MODUS_BINAER;
      else if (isdigit((unsigned char)a1[0])) m = strtol(a1, nullptr, 10);
    }
    if (m < MODUS_AUS || m > MODUS_BINAER) {
      meld.println(F("# Aufruf: m <0..4>  (0 aus, 1 lesbar, 2 CSV, 3 Plotter, 4 binaer)"));
      return;
    }
    setzeModus((uint8_t)m);
    return;
  }
  if (strcmp(cmd, "wlan") == 0) {
    if (a1 && strcmp(a1, "aus") == 0) {
      wlanAusschalten();           // nicht sofort: der Befehl kann aus einem laufenden HTTP-Aufruf kommen
    } else if (a1 && strcmp(a1, "an") == 0) {
      wlanEinschalten();
    } else {
      wlanInfo();
    }
    return;
  }
  if (strcmp(cmd, "wd") == 0) {
    if (!a1) { watchdogInfo(); return; }
    const bool k0 = a2 && strcmp(a2, "0") == 0, k1 = a2 && strcmp(a2, "1") == 0;
    if (strcmp(a1, "test") != 0 || !(k0 || k1)) { meld.println(F("# Aufruf: wd  bzw.  wd test 0|1")); return; }
    if (antrieb::beschaeftigt()) { meld.println(F("# Antrieb faehrt - Test erst danach ('x').")); return; }
    pufferSenden();
    watchdogTest(k1 ? 1 : 0);
    return;
  }
  if (strcmp(cmd, "ws") == 0) {
    float s = a1 ? strtof(a1, nullptr) : 0.0f;
    if (!a1 || !isfinite(s) || s < 0.5f || s > 60.0f) { meld.println(F("# Aufruf: ws <0.5..60>  (Sekunden)")); return; }
    webStatSek = s;
    webStatReset();
    meld.printf("# Live-Statistik im Web ueber %.1f s.\n", webStatSek);
    return;
  }
  if (ist(cmd, "fl", "filter")) {  // Glättung, auch während eines Auftrags (Aufträge nutzen Rohwerte)
    float s = -1.0f;
    if (a1 && strcmp(a1, "aus") == 0) s = 0.0f;
    else if (a1) s = strtof(a1, nullptr);
    if (!isfinite(s) || s < 0.0f || s > FILTER_MAX_MS / 1000.0f) {
      meld.printf("# Aufruf: fl <Sekunden 0..%u>   z. B. 'fl 0.5', 'fl 0' bzw. 'fl aus' = keine Glaettung\n",
                  FILTER_MAX_MS / 1000u);
      return;
    }
    cfg.filterMs = (uint16_t)lroundf(s * 1000.0f);
    filterReset();
    if (cfg.filterMs == 0) {
      meld.println(F("# Glaettung aus (jeder Rohwert wird direkt angezeigt). Mit 's' speichern."));
    } else {
      meld.printf("# Glaettung: gleitender Mittelwert ueber %.3f s = %lu Werte bei %.1f SPS. Mit 's' speichern.\n",
                  filterSekunden(), (unsigned long)filterBloecke() * filterBlockLaenge(), ads.sampleRate());
    }
    sendeCfg();
    return;
  }
  // Alle folgenden Befehle halten den ADC an, schreiben Flash oder ändern Tara/Faktor:
  // nicht während einer Fahrt (Kraftüberwachung!)
  if (antrieb::beschaeftigt()) {
    meld.println(F("# Antrieb faehrt - Befehl erst danach moeglich (Stopp mit 'x')."));
    return;
  }
  if (auftragLaeuft()) {
    meld.println(F("# Es laeuft noch ein Auftrag - Abbruch mit 'x'."));
    return;
  }
  if (strcmp(cmd, "init") == 0) { initADS(); return; }

  // ----- Befehle ohne ADC -----
  if (ist(cmd, "f", "faktor")) {
    float f = a1 ? strtof(a1, nullptr) : 0.0f;
    if (!a1 || !isfinite(f) || f == 0.0f) { meld.println(F("# Aufruf: f <Counts pro Gramm>, ungleich 0")); return; }
    cfg.faktor = f;
    cfg.kalibriert = 1;
    meld.printf("# Kalibrierfaktor gesetzt: %.5f Counts/g (mit 's' speichern)\n", cfg.faktor);
    sendeCfg();
    return;
  }
  if (ist(cmd, "s", "speichern")) {
    pufferSenden();
    meld.println(speichereEinstellungen() ? F("# Einstellungen im Flash gespeichert.")
                                          : F("# FEHLER: Speichern im Flash fehlgeschlagen!"));
    return;
  }
  if (ist(cmd, "w", "werk")) {
    if (!a1 || strcmp(a1, "ja") != 0) {
      meld.println(F("# Loescht Tara und Kalibrierung! Zur Bestaetigung 'w ja' eingeben."));
      return;
    }
    uint8_t modus = cfg.modus;     // Ausgabemodus beibehalten (z. B. binaer fuer das PC-Programm)
    standardEinstellungen();
    filterReset();
    if (adsOk) setzeRate(cfg.drateCode, false);
    pufferSenden();
    meld.println(speichereEinstellungen() ? F("# Werkseinstellungen geladen und gespeichert.")
                                          : F("# Werkseinstellungen geladen, Speichern FEHLGESCHLAGEN."));
    cfg.modus = modus;
    sendeCfg();
    return;
  }

  // ----- Befehle, die den ADC brauchen -----
  if (!adsOk) {
    meld.println(F("# ADS1256 nicht initialisiert - Befehl nicht moeglich ('init' versuchen) oder unbekannt ('h')."));
    return;
  }

  if (ist(cmd, "t", "tara")) {
    uint32_t n = a1 ? strtoul(a1, nullptr, 10) : standardN(2.0f, 20);
    starteAuftrag(AUFTRAG_TARA, n);
    return;
  }
  if (ist(cmd, "k", "kal")) {
    float g = a1 ? strtof(a1, nullptr) : 0.0f;
    if (!a1 || !isfinite(g) || g <= 0.0f) { meld.println(F("# Aufruf: k <Gewicht in Gramm> [n]   z. B. 'k 5000'")); return; }
    if (g > ALARMGRENZE_G) { meld.println(F("# Gewicht liegt ueber 120 % Nennlast - abgelehnt.")); return; }
    uint32_t n = a2 ? strtoul(a2, nullptr, 10) : standardN(2.0f, 20);
    starteKalibrierung(g, n);
    return;
  }
  if (ist(cmd, "n", "rausch")) {
    uint32_t n = a1 ? strtoul(a1, nullptr, 10) : standardN(5.0f, 100);
    starteAuftrag(AUFTRAG_RAUSCHEN, n);
    return;
  }
  if (ist(cmd, "nv", "rauschvergleich")) {
    float sek = a1 ? strtof(a1, nullptr) : 3.0f;
    if (!isfinite(sek) || sek < 0.5f || sek > 600.0f) {
      meld.println(F("# Aufruf: nv [Sekunden 0.5..600] [sps sps ...]   z. B. 'nv 5 500 1000 2000'"));
      return;
    }
    float raten[VERGLEICH_MAX];
    uint8_t anzahl = 0;
    for (uint8_t i = 1; i < argc && anzahl < VERGLEICH_MAX; i++) {
      uint8_t code;
      float sps = strtof(arg[i], nullptr);
      if (!ADS1256::codeFromRate(sps, code)) {
        meld.printf("# Ungueltige Datenrate '%s'.\n", arg[i]);
        zeigeRaten();
        return;
      }
      raten[anzahl++] = sps;
    }
    if (anzahl == 0) {
      for (float r : VERGLEICH_STANDARD) raten[anzahl++] = r;
    }
    starteVergleich(sek, raten, anzahl);
    return;
  }
  if (ist(cmd, "r", "rate")) {
    uint8_t code = 0;
    float sps = a1 ? strtof(a1, nullptr) : 0.0f;
    if (!a1 || !ADS1256::codeFromRate(sps, code)) { zeigeRaten(); return; }
    // blockiert für die Dauer der Selbstkalibrierung (bei 10 SPS ca. 0,3 s)
    if (setzeRate(code, true) && sps > 2000.0f && cfg.modus != MODUS_BINAER && cfg.modus != MODUS_AUS) {
      meld.println(F("# Hinweis: Ueber 2000 SPS fuer Datenaufzeichnung ueber USB den Binaermodus ('m 4') verwenden."));
    }
    return;
  }
  if (ist(cmd, "d", "reg")) { registerDump(); return; }
  if (ist(cmd, "c", "selfcal")) {
    bool ok = adcSelbstkalibrierung();                     // Kern 1 hält RDATAC kurz an
    filterReset();
    meld.println(ok ? F("# Selbstkalibrierung des ADC abgeschlossen.") : F("# FEHLER: DRDY-Timeout bei der Selbstkalibrierung."));
    return;
  }

  meld.printf("# Unbekannter Befehl '%s' - 'h' zeigt die Hilfe.\n", cmd);
}

// ===========================================================================
// Zeichen einsammeln, bei Zeilenende Befehl ausführen (nicht blockierend)
// ===========================================================================
void leseSeriell() {
  while (Serial.available() > 0) {
    char c = (char)Serial.read();
    if (c == '\r' || c == '\n') {
      if (zeileLen > 0) {
        zeile[zeileLen] = '\0';
        zeileLen = 0;
        verarbeiteBefehl(zeile);
      }
    } else if (zeileLen < sizeof(zeile) - 1) {
      zeile[zeileLen++] = c;
    }
  }
}
