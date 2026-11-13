/*
 * Waegezelle_Test.ino
 * Testsoftware für eine 50-kg-Wägezelle am ADS1256 mit Raspberry Pi Pico W / WH
 *
 * Umgebung: Arduino IDE, Core "Raspberry Pi Pico/RP2040/RP2350" (Earle Philhower),
 *           Board "Raspberry Pi Pico W" (RP2040)
 *
 * Verdrahtung:
 *   Pico VBUS -> ADS1256 5V        Pico GND  -> ADS1256 GND
 *   GP18 (SPI0 SCK) -> SCLK        GP19 (SPI0 TX) -> DIN
 *   GP16 (SPI0 RX)  -> DOUT        GP17 (GPIO)    -> CS
 *   GP20 -> DRDY                   GP21 -> PDWN (optional)
 *   Wägezelle: Rot -> 5,00 V (LT3042), Schwarz -> GND, Grün -> AIN0, Weiß -> AIN1
 *
 * Aufteilung auf die beiden Kerne:
 *   Kern 1: ADS1256 komplett (DRDY-Interrupt, SPI, Init, Datenrate, Kalibrierung).
 *           Kern 0 schickt Aufträge über ein Postfach (adcAusfuehren) und wartet.
 *   Kern 0: Verarbeitung (Filter, Überwachung, Aufträge), USB, WLAN und Weboberfläche.
 *           (Laut arduino-pico darf WLAN-Code ohne FreeRTOS nur auf Kern 0 laufen.)
 *   Die Messwerte laufen über den Ringpuffer des Treibers von Kern 1 zu Kern 0.
 *
 * Bedienung:
 *   - Serieller Monitor, Zeilenende "Neue Zeile". Befehl "h" zeigt die Hilfe.
 *   - Weboberfläche: http://waegezelle.local oder die IP aus dem seriellen Monitor.
 *     WLAN-Zugangsdaten in wlan_zugang.h eintragen.
 *   - Live-Anzeige über USB: waegezelle_plot.py (schaltet Binärmodus ein).
 *
 * Ausgabeformate (USB):
 *   - Alle Meldungen beginnen mit "#" und erscheinen auch im Web-Log.
 *   - "#CFG ..." ist eine maschinenlesbare Zeile mit allen Umrechnungsgrößen.
 *   - Modus 2 (CSV):    "seq,t_us,roh" - nur Ganzzahlen.
 *   - Modus 4 (Binär):  8-Byte-Rahmen je Messwert:
 *        Byte 0..1  0xA5 0x5A          Synchronwort
 *        Byte 2..3  seq   (uint16, LE) laufende Nummer, Lücke = verlorene Werte
 *        Byte 4..6  roh   (int24,  LE) ADC-Counts, Zweierkomplement
 *        Byte 7     CRC-8 (Polynom 0x07, Start 0x00) über Byte 2..6
 *
 * Weboberfläche (HTTP):
 *   GET  /        Seite (webui.h)
 *   GET  /daten   JSON: Konfiguration, Graphpunkte (Mittel/Min/Max, 50 pro s),
 *                 Live-Statistik, neue Log-Zeilen. Parameter p/l = ab Punkt/Log-Position.
 *   POST /cmd     Parameter c = Befehl (wie im seriellen Monitor)
 */
#include <Arduino.h>
#include <SPI.h>
#include <EEPROM.h>
#include <WiFi.h>
#include <WebServer.h>
#include <SimpleMDNS.h>
#include <math.h>
#include <stdarg.h>
#include <hardware/gpio.h>
#include <hardware/sync.h>
#include "ADS1256.h"
#include "wlan_zugang.h"
#include "webstil.h"
#include "webui.h"
#include "webinfo.h"

// ===========================================================================
// Hardware-Konstanten
// ===========================================================================
const uint8_t PIN_SPI_SCK  = 18;
const uint8_t PIN_SPI_MOSI = 19;   // Pico TX -> ADS1256 DIN
const uint8_t PIN_SPI_MISO = 16;   // Pico RX <- ADS1256 DOUT
const uint8_t PIN_ADS_CS   = 17;   // manuell als GPIO
const uint8_t PIN_ADS_DRDY = 20;
const int8_t  PIN_ADS_PDWN = 21;   // -1, falls nicht verwendet

const float    VREF_V      = 2.50f;      // ADR03
const uint32_t SPI_TAKT_HZ = 1900000;    // max. fCLKIN/4 = 1,92 MHz
const float ERREGUNG_V      = 5.00f;     // Brückenspeisung (LT3042)
const float NENNLAST_G      = 50000.0f;  // 50 kg
const float WARNGRENZE_G    = 50000.0f;  // 100 % Nennlast
const float ALARMGRENZE_G   = 60000.0f;  // 120 % (sichere Überlast)
const float WARN_HYSTERESE  = 0.98f;     // Rückfall erst unter 98 % der Grenze

// Startwert für den Kalibrierfaktor, solange nicht kalibriert wurde:
// 1,5 mV/V * 5 V = 7,5 mV bei 50 kg; 1 Count = 9,313 nV  ->  ca. 16,1 Counts/g
const float STANDARD_FAKTOR = 16.1f;

// Kern 1 bekommt einen eigenen 8-KB-Stack (statt 4 KB geteilt mit Kern 0)
bool core1_separate_stack = true;

// ===========================================================================
// Typen (stehen vor allen Funktionen)
// ===========================================================================
enum Ausgabemodus : uint8_t {
  MODUS_AUS = 0, MODUS_LESBAR = 1, MODUS_CSV = 2, MODUS_PLOTTER = 3, MODUS_BINAER = 4
};
enum Auftrag : uint8_t {
  AUFTRAG_KEIN = 0, AUFTRAG_TARA, AUFTRAG_KALIBRIEREN, AUFTRAG_RAUSCHEN, AUFTRAG_VERGLEICH
};
// Aufträge von Kern 0 an Kern 1 (alles, was SPI braucht)
enum AdcOp : uint8_t { OP_KEIN = 0, OP_INIT, OP_RATE, OP_SELFCAL, OP_REG };

// Im Flash gespeicherte Einstellungen (32 Byte, ohne Füllbytes)
struct Einstellungen {
  uint32_t magic;
  uint32_t version;
  double   tara;          // Nullpunkt in Counts
  float    faktor;        // Counts pro Gramm (vorzeichenbehaftet)
  uint16_t filterMs;      // Glättung: gleitender Mittelwert über filterMs Millisekunden (0 = aus)
  uint8_t  kalibriert;    // 1 = Faktor stammt aus Kalibrierung/Eingabe
  uint8_t  drateCode;     // DRATE-Register
  uint8_t  modus;         // Ausgabemodus
  uint8_t  reserviert[3];
  uint32_t crc;
};
static_assert(sizeof(Einstellungen) == 32, "Einstellungen muessen 32 Byte gross sein");

// Mittelwert und Varianz nach Welford (numerisch stabil)
struct Welford {
  uint32_t n;
  double   mittel;
  double   m2;
  void reset() { n = 0; mittel = 0.0; m2 = 0.0; }
  void add(double x) {
    n++;
    double d = x - mittel;
    mittel += d / (double)n;
    m2 += d * (x - mittel);
  }
  double stdAbw() const { return (n > 1) ? sqrt(m2 / (double)(n - 1)) : 0.0; }
};

// Statistik über Rohwerte: Welford, Blockmittel (10-SPS-Äquivalent), Lücken
struct Statistik {
  Welford  roh;
  Welford  block;           // Mittelwerte über je blockLaenge Rohwerte
  int32_t  minimum;
  int32_t  maximum;
  uint32_t blockLaenge;     // 0 = keine Blockmittelung
  uint32_t blockZaehler;
  int64_t  blockSumme;
  uint32_t seqErst, seqLetzt;
  uint32_t tErst, tLetzt;

  void reset(uint32_t blockLen) {
    roh.reset(); block.reset();
    minimum = INT32_MAX; maximum = INT32_MIN;
    blockLaenge = blockLen; blockZaehler = 0; blockSumme = 0;
    seqErst = seqLetzt = tErst = tLetzt = 0;
  }
  void add(const ADS1256::Messwert &m) {
    if (roh.n == 0) { seqErst = m.seq; tErst = m.tMikros; }
    seqLetzt = m.seq; tLetzt = m.tMikros;
    roh.add((double)m.counts);
    if (m.counts < minimum) minimum = m.counts;
    if (m.counts > maximum) maximum = m.counts;
    if (blockLaenge > 0) {
      blockSumme += m.counts;
      if (++blockZaehler >= blockLaenge) {
        block.add((double)blockSumme / (double)blockLaenge);
        blockSumme = 0;
        blockZaehler = 0;
      }
    }
  }
  uint32_t erwartet() const { return roh.n ? (seqLetzt - seqErst + 1) : 0; }
  uint32_t verloren() const { return erwartet() - roh.n; }
  // tatsächliche Datenrate aus Zeitstempeln und laufender Nummer
  double rateIst() const {
    uint32_t dt = tLetzt - tErst;
    return (dt > 0 && seqLetzt != seqErst) ? (double)(seqLetzt - seqErst) * 1e6 / (double)dt : 0.0;
  }
};

// Ergebnis einer Rauschmessung
struct RauschErgebnis {
  float    sps;
  uint32_t n;
  uint32_t verloren;
  double   rateIst;
  double   mittel;      // Counts
  double   sigma;       // Counts
  int32_t  minimum, maximum;
  double   sigma10;     // Counts, Streuung der 10-SPS-Blockmittel (0 = nicht verfügbar)
  uint32_t bloecke;
  bool     clip;
};

// Ein Punkt für das Web-Diagramm (Zusammenfassung von graphLaenge Rohwerten)
struct Punkt {
  uint32_t tMs;         // Zeitstempel des letzten Werts (ms seit Start)
  float    mittel;      // Counts
  int32_t  minimum;
  int32_t  maximum;
  float    geglaettet;  // Counts, Ausgang des Glättungsfilters
};

// ===========================================================================
// Web-Log: Ringpuffer für alle Meldungen (nur Kern 0)
// ===========================================================================
const uint32_t WEBLOG_GROESSE = 4096;          // Zweierpotenz
char     webLog[WEBLOG_GROESSE];
uint32_t webLogPos = 0;                         // insgesamt geschriebene Zeichen

void webLogSchreiben(const uint8_t *s, size_t n) {
  for (size_t i = 0; i < n; i++) webLog[webLogPos++ & (WEBLOG_GROESSE - 1)] = (char)s[i];
}

// Meldungen gehen an USB UND in das Web-Log
class Meldung : public Print {
public:
  size_t write(uint8_t c) override { return write(&c, 1); }
  size_t write(const uint8_t *buf, size_t n) override {
    webLogSchreiben(buf, n);
    Serial.write(buf, n);
    return n;
  }
};
Meldung meld;

// ===========================================================================
// Globale Objekte und Zustände
// ===========================================================================
const uint32_t EINST_MAGIC   = 0x315A4C57UL;   // "WLZ1"
const uint32_t EINST_VERSION = 1;
const uint16_t FILTER_BLOECKE = 256;           // max. Anzahl Blöcke im Glättungsfilter
const uint16_t FILTER_MAX_MS  = 60000;         // längste Glättung: 60 s
const uint16_t FILTER_STANDARD_MS = 200;       // Standard: 0,2 s
const uint32_t N_MAX         = 2000000UL;
const uint8_t  VERGLEICH_MAX = 16;

ADS1256 ads(SPI, PIN_ADS_CS, PIN_ADS_DRDY, PIN_ADS_PDWN, VREF_V, SPI_TAKT_HZ);
Einstellungen cfg;
bool adsOk = false;

// Postfach Kern 0 -> Kern 1
volatile uint8_t adcOp = OP_KEIN;     // von Kern 0 gesetzt, von Kern 1 nach Erledigung gelöscht
uint8_t          adcParam = 0;
ADS1256::Config  adcKonfig;
ADS1256::Status  adcStatus = ADS1256::Status::ERFOLG;
bool             adcErgebnis = false;
uint8_t          adcRegs[ADS1256::REG_COUNT];

// Glättungsfilter: gleitender Mittelwert über cfg.filterMs, unabhängig von der Datenrate.
// Je filterBlockLen Rohwerte werden zu einer Blocksumme zusammengefasst; gemittelt
// wird über die letzten filterK Blöcke (max. 256). Bei kurzen Zeiten ist ein Block
// = ein Messwert, dann ist es ein exakter gleitender Mittelwert.
int64_t  filterBlock[FILTER_BLOECKE];
uint16_t filterK = 1;            // Anzahl Blöcke im Fenster
uint32_t filterBlockLen = 1;     // Rohwerte pro Block
uint16_t filterIndex = 0;
uint16_t filterAnzahl = 0;       // gefüllte Blöcke (<= filterK)
int64_t  filterSumme = 0;        // Summe der gefüllten Blöcke
int64_t  filterBlockSumme = 0;   // laufender, noch unvollständiger Block
uint32_t filterBlockN = 0;
int32_t  filterLetzter = 0;      // letzter Rohwert (Ausgabe, solange noch kein Block voll ist)

// Auftrag (Tara, Kalibrierung, Rauschanalyse)
Auftrag   auftrag = AUFTRAG_KEIN;
uint32_t  auftragN = 0;
uint32_t  auftragVerwerfen = 0;    // Einschwingwerte am Anfang überspringen
float     auftragGewichtG = 0.0f;
bool      auftragClip = false;
Statistik stat;

// Rauschvergleich über mehrere Datenraten
bool           vergleichAktiv = false;
bool           vergleichWeiter = false;   // nächster Schritt in loop() fällig
float          vergleichRaten[VERGLEICH_MAX];
RauschErgebnis vergleichErg[VERGLEICH_MAX];
uint8_t        vergleichAnzahl = 0;
uint8_t        vergleichIndex = 0;
float          vergleichSekunden = 3.0f;
uint8_t        vergleichStartCode = ADS1256::DR_100;
const float    VERGLEICH_STANDARD[] = {100, 500, 1000, 2000, 3750, 7500, 15000, 30000};

// Überwachung
uint8_t  warnStufe = 0;            // 0 = ok, 1 = >= 100 %, 2 = >= 120 %
bool     adcClip = false;
uint32_t clipZuletztMs = 0;
uint32_t letzteWarnungMs = 0;

// Zeitsteuerung
uint32_t letzteMessungMs = 0;
uint32_t letzteAusgabeMs = 0;
bool     timeoutAktiv = false;
uint32_t letzteTimeoutMeldungMs = 0;

// Ausgabepuffer für CSV- und Binärdaten (wird in Blöcken über USB gesendet)
uint8_t  ausPuffer[1024];
uint16_t ausLen = 0;
uint8_t  crcTabelle[256];

// Serielle Eingabe
char    zeile[96];
uint8_t zeileLen = 0;

// --- Web: Diagrammpunkte ---
const float    GRAPH_PUNKTE_PRO_S = 50.0f;
const uint32_t PUNKTE_GROESSE = 1024;          // Zweierpotenz, ca. 20 s Vorrat
Punkt    punkte[PUNKTE_GROESSE];
uint32_t punktZaehler = 0;                      // insgesamt erzeugte Punkte
uint32_t graphLaenge = 1;                       // Rohwerte pro Punkt
uint32_t graphN = 0;
int64_t  graphSumme = 0;
int32_t  graphMin = 0, graphMax = 0;
uint64_t tErweitert = 0;                        // 64-Bit-Zeit in µs (ohne Überlauf)
uint32_t tLetzteRoh = 0;
bool     zeitStart = true;

// --- Web: Live-Statistik ---
Statistik      webStat;
RauschErgebnis webErg;
uint32_t       webErgId = 0;                    // 0 = noch kein Ergebnis
float          webStatSek = 2.0f;
uint32_t       webStatN = 200;
bool           webClip = false;
Welford        webGlatt;                        // Streuung des geglätteten Signals (je Graphpunkt)
double         webErgGlatt = 0.0;               // Ergebnis: sigma geglättet in Counts (0 = zu wenige Werte)

// --- WLAN / Webserver ---
WebServer server(80);
bool     wlanAn = true;
bool     wlanVerbunden = false;
bool     mdnsAktiv = false;
bool     wlanAusFaellig = false;                // 'wlan aus' nach dem HTTP-Aufruf ausführen
uint32_t wlanLetzterVersuch = 0;
uint32_t wlanLetztePruefung = 0;
char     jsonPuf[1400];
uint16_t jsonLen = 0;

// ===========================================================================
// Prototypen
// ===========================================================================
uint32_t crc32(const uint8_t *daten, size_t laenge);
void     crc8Init();
uint8_t  crc8(const uint8_t *daten, size_t laenge);
void     standardEinstellungen();
bool     ladeEinstellungen();
bool     speichereEinstellungen();
bool     adcAusfuehren(uint8_t op, uint8_t param, uint32_t timeoutMs = 8000);
bool     initADS();
void     filterReset();
void     filterUpdate(int32_t x);
double   filterMittel();
double   filterSekunden();
double   inGramm(double counts);
uint32_t standardN(float sekunden, uint32_t minimum);
void     starteAuftrag(Auftrag a, uint32_t n, uint32_t verwerfen = 0);
void     auftragFertig();
void     auftragAbbrechen(const char *grund);
RauschErgebnis rauschErgebnis(const Statistik &s, bool clip);
void     druckeRauschen(const RauschErgebnis &e);
void     druckeRauschZeile(const RauschErgebnis &e);
void     druckeVergleichstabelle();
void     starteVergleich(float sekunden, const float *raten, uint8_t anzahl);
void     vergleichSchritt();
void     vergleichBeenden(bool abgebrochen);
bool     setzeRate(uint8_t code, bool melden);
void     ueberwachung(int32_t roh);
void     pufferSenden();
void     puffern(const void *daten, uint16_t n);
void     sendeCfg();
void     ausgabe(const ADS1256::Messwert &m);
void     verarbeiteMesswert(const ADS1256::Messwert &m);
void     pruefeTimeout();
void     leseSeriell();
void     verarbeiteBefehl(char *text);
void     zeigeHilfe();
void     zeigeInfo();
void     zeigeRaten();
void     registerDump();
void     setzeModus(uint8_t m);
bool     ist(const char *cmd, const char *a, const char *b);
bool     auftragLaeuft();
void     graphReset();
void     webStatReset();
void     webAufnehmen(const ADS1256::Messwert &m);
void     wlanStarten();
void     wlanPruefen();
void     wlanInfo();
void     webSeite();
void     webInfo();
void     webStil();
void     webDaten();
void     webBefehl();
void     jsonf(const char *fmt, ...);
void     jsonZeichen(char c);
void     jsonFlush();

// ===========================================================================
// Prüfsummen
// ===========================================================================
uint32_t crc32(const uint8_t *daten, size_t laenge) {
  uint32_t c = 0xFFFFFFFFUL;
  while (laenge--) {
    c ^= *daten++;
    for (uint8_t i = 0; i < 8; i++) c = (c & 1) ? ((c >> 1) ^ 0xEDB88320UL) : (c >> 1);
  }
  return ~c;
}

// CRC-8, Polynom 0x07, Startwert 0x00, nicht reflektiert (CRC-8/SMBUS)
void crc8Init() {
  for (uint16_t i = 0; i < 256; i++) {
    uint8_t c = (uint8_t)i;
    for (uint8_t b = 0; b < 8; b++) c = (c & 0x80) ? (uint8_t)((c << 1) ^ 0x07) : (uint8_t)(c << 1);
    crcTabelle[i] = c;
  }
}

uint8_t crc8(const uint8_t *daten, size_t laenge) {
  uint8_t c = 0;
  while (laenge--) c = crcTabelle[c ^ *daten++];
  return c;
}

// ===========================================================================
// Einstellungen / Flash
// ===========================================================================
void standardEinstellungen() {
  memset(&cfg, 0, sizeof(cfg));
  cfg.magic        = EINST_MAGIC;
  cfg.version      = EINST_VERSION;
  cfg.tara         = 0.0;
  cfg.faktor       = STANDARD_FAKTOR;
  cfg.filterMs     = FILTER_STANDARD_MS;
  cfg.kalibriert   = 0;
  cfg.drateCode    = ADS1256::DR_100;
  cfg.modus        = MODUS_LESBAR;
}

bool ladeEinstellungen() {
  Einstellungen e;
  EEPROM.get(0, e);
  bool ok = e.magic == EINST_MAGIC && e.version == EINST_VERSION &&
            e.crc == crc32((const uint8_t *)&e, sizeof(e) - sizeof(e.crc)) &&
            isfinite(e.tara) && isfinite(e.faktor) && e.faktor != 0.0f &&
            e.filterMs <= FILTER_MAX_MS &&
            ADS1256::rateFromCode(e.drateCode) > 0.0f && e.modus <= MODUS_BINAER;
  if (ok) memcpy(&cfg, &e, sizeof(cfg));
  return ok;
}

bool speichereEinstellungen() {
  cfg.crc = crc32((const uint8_t *)&cfg, sizeof(cfg) - sizeof(cfg.crc));
  EEPROM.put(0, cfg);
  // Flash-Schreiben sperrt die Interrupts und hält Kern 1 kurz an. Die dabei
  // verpassten Wandlungen erkennt der Treiber am Zeitstempel (Lücke in seq).
  bool ok = EEPROM.commit();
  letzteMessungMs = millis();
  return ok;
}

// ===========================================================================
// Kern 1: ADS1256 (alle SPI-Zugriffe und der DRDY-Interrupt laufen hier)
// ===========================================================================
void setup1() {
  // nichts zu tun: Kern 0 schickt den Init-Auftrag, sobald er bereit ist
}

void loop1() {
  const uint8_t op = adcOp;
  if (op == OP_KEIN) return;
  __dmb();                         // Parameter erst nach dem Auftrag lesen

  switch (op) {
    case OP_INIT:
      adcStatus = ads.begin(adcKonfig);
      // DOUT ist bei CS = HIGH hochohmig; der Pin bleibt ohne Pull-Widerstand.
      gpio_disable_pulls(PIN_SPI_MISO);
      // startContinuous() hier aufrufen, damit der Interrupt an Kern 1 gebunden wird
      adcErgebnis = (adcStatus == ADS1256::Status::ERFOLG) && ads.startContinuous();
      break;

    case OP_RATE:
      adcErgebnis = ads.setDataRate(adcParam);
      if (!ads.isContinuous() && !ads.startContinuous()) adcErgebnis = false;
      break;

    case OP_SELFCAL:
      adcErgebnis = ads.selfCalibrate();
      if (!ads.isContinuous() && !ads.startContinuous()) adcErgebnis = false;
      break;

    case OP_REG:
      ads.readRegisters(ADS1256::REG_STATUS, adcRegs, ADS1256::REG_COUNT);
      adcErgebnis = ads.isContinuous() || ads.startContinuous();
      break;

    default:
      adcErgebnis = false;
      break;
  }

  __dmb();                         // Ergebnisse vor der Fertigmeldung sichtbar machen
  adcOp = OP_KEIN;
}

// Kern 0: Auftrag an Kern 1 geben und auf Erledigung warten. Kern 0 entnimmt
// währenddessen keine Messwerte, daher darf Kern 1 den Ringpuffer leeren.
bool adcAusfuehren(uint8_t op, uint8_t param, uint32_t timeoutMs) {
  pufferSenden();
  adcParam = param;
  __dmb();
  adcOp = op;
  const uint32_t start = millis();
  while (adcOp != OP_KEIN) {
    if (millis() - start > timeoutMs) {
      meld.println(F("# FEHLER: Kern 1 hat den ADC-Auftrag nicht erledigt (Timeout)."));
      return false;
    }
  }
  __dmb();
  letzteMessungMs = millis();
  return true;
}

// ===========================================================================
// ADS1256 initialisieren (auf Kern 1) und Ergebnis melden
// ===========================================================================
bool initADS() {
  ADS1256::Config k;
  k.ainP      = 0;                 // AIN0 = Signal + (Grün)
  k.ainN      = 1;                 // AIN1 = Signal - (Weiß)
  k.pgaCode   = 6;                 // PGA 64 -> +-78,125 mV
  k.drateCode = cfg.drateCode;
  k.buffer    = true;
  k.autoCal   = true;
  adcKonfig = k;

  adsOk = false;
  if (!adcAusfuehren(OP_INIT, 0)) return false;
  const ADS1256::Status s = adcStatus;

  if (s == ADS1256::Status::TIMEOUT_RESET) {
    meld.println(F("# FEHLER: ADS1256 antwortet nicht."));
  } else {
    meld.printf("# Chip-ID: %u (Soll: %u) %s\n", ads.chipId(), ADS1256::EXPECTED_ID,
                ads.chipId() == ADS1256::EXPECTED_ID ? "-> OK" : "-> FALSCH");
  }
  if (s != ADS1256::Status::ERFOLG) {
    meld.printf("# FEHLER bei der Initialisierung: %s\n", ADS1256::statusText(s));
    meld.println(F("# Verdrahtung pruefen, danach Befehl 'init' eingeben."));
    return false;
  }
  if (!adcErgebnis) {
    meld.println(F("# FEHLER: Dauerbetrieb (RDATAC) konnte nicht gestartet werden (DRDY-Timeout)."));
    return false;
  }

  adsOk = true;
  filterReset();
  graphReset();
  webStatReset();
  zeitStart = true;
  letzteMessungMs = millis();
  timeoutAktiv = false;

  meld.printf("# ADS1256 bereit: PGA %u, %.1f SPS, Messbereich +-%.3f mV, 1 Count = %.4f nV\n",
              ads.gain(), ads.sampleRate(), ads.fullScaleMicrovolts() / 1000.0,
              ads.microvoltsPerCount() * 1000.0);
  meld.printf("# Register geprueft: OK. Dauerbetrieb (RDATAC + DRDY-Interrupt auf Kern 1), SPI %.2f MHz\n",
              ads.spiTakt() / 1e6);
  sendeCfg();
  return true;
}

// ===========================================================================
// Filter und Umrechnung
// ===========================================================================
// Filterparameter aus Glättungszeit und Datenrate berechnen, Filter leeren.
// Muss nach jeder Änderung von Datenrate oder cfg.filterMs aufgerufen werden.
void filterReset() {
  uint32_t n = (uint32_t)lroundf(ads.sampleRate() * (float)cfg.filterMs / 1000.0f);  // Werte im Fenster
  if (n < 1) n = 1;
  if (n <= FILTER_BLOECKE) {
    filterBlockLen = 1;
    filterK = (uint16_t)n;
  } else {
    filterBlockLen = (n + FILTER_BLOECKE - 1) / FILTER_BLOECKE;   // aufrunden
    uint32_t k = (n + filterBlockLen / 2) / filterBlockLen;
    filterK = (uint16_t)((k < 1) ? 1 : (k > FILTER_BLOECKE ? FILTER_BLOECKE : k));
  }
  filterIndex = 0;
  filterAnzahl = 0;
  filterSumme = 0;
  filterBlockSumme = 0;
  filterBlockN = 0;
}

void filterUpdate(int32_t x) {
  filterLetzter = x;
  filterBlockSumme += x;
  if (++filterBlockN < filterBlockLen) return;   // Block noch nicht voll

  // Block fertig -> in das gleitende Fenster übernehmen
  if (filterAnzahl >= filterK) {
    filterSumme -= filterBlock[filterIndex];      // ältesten Block entfernen
  } else {
    filterAnzahl++;
  }
  filterBlock[filterIndex] = filterBlockSumme;
  filterSumme += filterBlockSumme;
  filterIndex = (uint16_t)((filterIndex + 1) % filterK);
  filterBlockSumme = 0;
  filterBlockN = 0;
}

// Geglätteter Wert in Counts. Division nur bei Bedarf (kein FPU!)
double filterMittel() {
  if (filterAnzahl == 0) return (double)filterLetzter;
  return (double)filterSumme / ((double)filterAnzahl * (double)filterBlockLen);
}

// tatsächliche Fensterlänge in Sekunden (nach Rundung auf ganze Werte/Blöcke)
double filterSekunden() {
  return (double)filterK * (double)filterBlockLen / (double)ads.sampleRate();
}

double inGramm(double counts) {
  return (counts - cfg.tara) / (double)cfg.faktor;
}

// Messanzahl für eine gewünschte Dauer bei der aktuellen Datenrate
uint32_t standardN(float sekunden, uint32_t minimum) {
  uint32_t n = (uint32_t)lroundf(ads.sampleRate() * sekunden);
  return (n < minimum) ? minimum : n;
}

// ===========================================================================
// Datenrate setzen (auf Kern 1; Dauerbetrieb wird angehalten und neu gestartet)
// ===========================================================================
bool setzeRate(uint8_t code, bool melden) {
  bool ok = adcAusfuehren(OP_RATE, code) && adcErgebnis;
  if (ok) cfg.drateCode = code;
  filterReset();
  graphReset();
  webStatReset();
  letzteMessungMs = millis();
  if (!ads.isContinuous()) {
    meld.println(F("# FEHLER: Dauerbetrieb laeuft nicht ('init' versuchen)."));
    ok = false;
  }
  if (melden) {
    if (ok) meld.printf("# Datenrate: %.1f SPS (ADC neu kalibriert)\n", ads.sampleRate());
    else    meld.println(F("# FEHLER: Datenrate konnte nicht gesetzt werden (Timeout oder Ruecklesefehler)."));
  }
  sendeCfg();
  return ok;
}

// ===========================================================================
// Aufträge: Tara, Kalibrierung, Rauschanalyse (nicht blockierend)
// ===========================================================================
bool auftragLaeuft() {
  return auftrag != AUFTRAG_KEIN || vergleichAktiv;
}

// Blocklänge für die 10-SPS-Mittelung (erst ab 20 SPS sinnvoll)
uint32_t blockLaenge10() {
  float sps = ads.sampleRate();
  return (sps >= 20.0f) ? (uint32_t)lroundf(sps / 10.0f) : 0;
}

void starteAuftrag(Auftrag a, uint32_t n, uint32_t verwerfen) {
  if (n < 2) n = 2;
  if (n > N_MAX) n = N_MAX;
  float sps = ads.sampleRate();
  stat.reset(blockLaenge10());
  auftrag = a;
  auftragN = n;
  auftragVerwerfen = verwerfen;
  auftragClip = false;
  ads.flush();                     // nur Werte ab jetzt auswerten
  letzteMessungMs = millis();
  if (a == AUFTRAG_VERGLEICH) {
    meld.printf("# Rauschvergleich %u/%u: %.1f SPS, %lu Messungen (ca. %.1f s) ...\n",
                vergleichIndex + 1, vergleichAnzahl, sps, (unsigned long)n, (double)n / sps);
    return;
  }
  const char *name = (a == AUFTRAG_TARA) ? "Tara" : (a == AUFTRAG_KALIBRIEREN) ? "Kalibrierung" : "Rauschanalyse";
  meld.printf("# %s: nehme %lu Messungen auf (ca. %.1f s), Abbruch mit 'x' ...\n",
              name, (unsigned long)n, (double)n / sps);
}

void auftragAbbrechen(const char *grund) {
  if (!auftragLaeuft()) return;
  auftrag = AUFTRAG_KEIN;
  meld.printf("# Auftrag abgebrochen: %s\n", grund);
  if (vergleichAktiv) vergleichBeenden(true);
}

RauschErgebnis rauschErgebnis(const Statistik &s, bool clip) {
  RauschErgebnis e;
  e.sps      = ads.sampleRate();
  e.n        = s.roh.n;
  e.verloren = s.verloren();
  e.rateIst  = s.rateIst();
  e.mittel   = s.roh.mittel;
  e.sigma    = s.roh.stdAbw();
  e.minimum  = s.minimum;
  e.maximum  = s.maximum;
  e.bloecke  = s.block.n;
  e.sigma10  = (s.block.n >= 5) ? s.block.stdAbw() : 0.0;
  e.clip     = clip;
  return e;
}

void auftragFertig() {
  Auftrag a = auftrag;
  auftrag = AUFTRAG_KEIN;

  const double uvProCount = ads.microvoltsPerCount();
  const double sigma = stat.roh.stdAbw();
  uint32_t verloren = stat.verloren();

  if (auftragClip) {
    meld.println(F("# WARNUNG: Waehrend der Aufnahme war der ADC uebersteuert - Ergebnis unbrauchbar!"));
  }
  if (verloren > 0 && a != AUFTRAG_VERGLEICH) {
    meld.printf("# Hinweis: %lu Messwerte gingen waehrend der Aufnahme verloren.\n", (unsigned long)verloren);
  }

  if (a == AUFTRAG_TARA) {
    if (auftragClip) { meld.println(F("# Tara NICHT uebernommen.")); return; }
    cfg.tara = stat.roh.mittel;
    meld.printf("# Tara gesetzt: %.1f Counts (%.3f uV Brueckenoffset), Streuung %.2f Counts\n",
                cfg.tara, cfg.tara * uvProCount, sigma);
    meld.println(F("# Mit 's' dauerhaft speichern."));
    sendeCfg();
    return;
  }

  if (a == AUFTRAG_KALIBRIEREN) {
    double delta = stat.roh.mittel - cfg.tara;
    // Unsicherheit des MITTELWERTS (nicht des Einzelwerts!). Aus den 0,1-s-Blockmitteln
    // geschätzt, damit langsames Rauschen und Drift mitzählen; sonst aus der Rohstreuung.
    const double sem = (stat.block.n >= 5) ? stat.block.stdAbw() / sqrt((double)stat.block.n)
                                           : sigma / sqrt((double)stat.roh.n);
    const double semDiff = sem * M_SQRT2;          // Tara und Kalibrierung je mit eigener Unsicherheit
    double mindest = 10.0 * semDiff;
    if (mindest < 20.0) mindest = 20.0;            // absolute Untergrenze (ca. 2 g bei 10 Counts/g)
    if (auftragClip || fabs(delta) < mindest) {
      meld.printf("# FEHLER: Signalaenderung zu klein oder ungueltig (%.1f Counts, noetig mind. %.1f Counts; "
                  "Unsicherheit des Mittelwerts %.2f Counts).\n", delta, mindest, sem);
      meld.println(F("# Liegt das Gewicht auf? Wurde vorher ohne Last tariert? Faktor NICHT geaendert."));
      return;
    }
    cfg.faktor = (float)(delta / (double)auftragGewichtG);   // Vorzeichen bleibt erhalten
    cfg.kalibriert = 1;
    // Plausibilität: daraus berechnete Empfindlichkeit der Zelle in mV/V
    double empf = fabs((double)cfg.faktor) * NENNLAST_G * uvProCount / 1000.0 / ERREGUNG_V;
    meld.printf("# Kalibriert mit %.2f g: Aenderung %.1f Counts (%.3f uV)\n",
                auftragGewichtG, delta, delta * uvProCount);
    meld.printf("# Kalibrierfaktor: %.5f Counts/g%s\n", cfg.faktor,
                cfg.faktor < 0 ? "  (negativ: Signal ist verpolt, wird automatisch beruecksichtigt)" : "");
    const double relUnsich = 100.0 * semDiff / fabs(delta);
    meld.printf("# Statistische Unsicherheit des Faktors: +-%.3f %% (= +-%.1f g bei %.0f kg)\n",
                relUnsich, relUnsich / 100.0 * NENNLAST_G, NENNLAST_G / 1000.0);
    if (auftragGewichtG < 0.05f * NENNLAST_G) {
      meld.printf("# Hinweis: Kalibriergewicht ist klein (%.0f g < 5 %% der Nennlast). Fehler im Faktor wachsen\n"
                  "#          bis zur Nennlast mit. Fuer beste Genauigkeit mind. %.0f kg (10 %%) verwenden.\n",
                  auftragGewichtG, NENNLAST_G * 0.1 / 1000.0);
    }
    meld.printf("# Entspricht einer Empfindlichkeit von %.3f mV/V bei %.0f kg", empf, NENNLAST_G / 1000.0);
    meld.println((empf < 0.8 || empf > 2.4) ? F("  -> AUSSERHALB des erwarteten Bereichs 1,0-2,0 mV/V, bitte pruefen!")
                                            : F("  -> plausibel"));
    meld.println(F("# Mit 's' dauerhaft speichern."));
    sendeCfg();
    return;
  }

  RauschErgebnis e = rauschErgebnis(stat, auftragClip);
  if (a == AUFTRAG_RAUSCHEN) {
    druckeRauschen(e);
    druckeRauschZeile(e);
    return;
  }

  // ----- ein Schritt des Rauschvergleichs -----
  if (vergleichIndex < VERGLEICH_MAX) vergleichErg[vergleichIndex] = e;
  druckeRauschZeile(e);
  vergleichIndex++;
  vergleichWeiter = true;          // Ratenwechsel erst in loop(), nicht mitten in der Verarbeitung
}

// ===========================================================================
// Rauschanalyse: Ausgabe
// ===========================================================================
void druckeRauschen(const RauschErgebnis &e) {
  const double uv = ads.microvoltsPerCount();
  const double fa = fabs((double)cfg.faktor);
  const double ss = (double)e.maximum - (double)e.minimum;
  const char *unkal = cfg.kalibriert ? "" : "  (g unkalibriert!)";

  meld.println(F("# ================= Rauschanalyse ================="));
  meld.printf("# Messungen:          %lu bei %.1f SPS (gemessen %.2f SPS), PGA %u, ungefiltert\n",
              (unsigned long)e.n, e.sps, e.rateIst, ads.gain());
  meld.printf("# Verlorene Werte:    %lu\n", (unsigned long)e.verloren);
  meld.printf("# Mittelwert:         %.2f Counts = %.3f uV = %.2f g\n",
              e.mittel, e.mittel * uv, inGramm(e.mittel));
  meld.printf("# Standardabweichung: %.3f Counts = %.4f uV = %.4f g%s\n",
              e.sigma, e.sigma * uv, e.sigma / fa, unkal);
  meld.printf("# Minimum:            %ld Counts = %.3f uV\n", (long)e.minimum, e.minimum * uv);
  meld.printf("# Maximum:            %ld Counts = %.3f uV\n", (long)e.maximum, e.maximum * uv);
  meld.printf("# Spitze-Spitze:      %.0f Counts = %.4f uV = %.4f g\n", ss, ss * uv, ss / fa);
  if (e.sigma > 0.0) {
    // bezogen auf den vollen ADC-Bereich von 2^24 Counts (+-2*Vref/PGA)
    meld.printf("# Effektive Aufloesung:   %.2f Bit (RMS, bezogen auf ADC-Vollbereich)\n",
                log2(16777216.0 / e.sigma));
    meld.printf("# Rauschfreie Aufloesung: %.2f Bit (6,6 Sigma)\n", log2(16777216.0 / (6.6 * e.sigma)));
    double teile = (double)NENNLAST_G * fa / e.sigma;
    meld.printf("# Bezogen auf %.0f kg:     %.0f Teile (1 Sigma) = %.2f Bit\n",
                NENNLAST_G / 1000.0, teile, log2(teile));
  } else {
    meld.println(F("# Effektive Aufloesung:   24 Bit (kein Rauschen messbar - Signal pruefen!)"));
  }
  if (e.sigma10 > 0.0) {
    // Gemessene Streuung der Mittelwerte über je 0,1 s: zeigt, was hohe
    // Datenrate + Mittelung im Vergleich zu einer niedrigen Datenrate bringt.
    meld.printf("# Nach Mittelung auf 10 SPS (%lu Bloecke): %.4f Counts = %.5f uV = %.5f g = %.2f Bit\n",
                (unsigned long)e.bloecke, e.sigma10, e.sigma10 * uv, e.sigma10 / fa,
                log2(16777216.0 / e.sigma10));
  } else if (e.sps >= 20.0f) {
    meld.println(F("# Nach Mittelung auf 10 SPS: zu wenige Bloecke (mind. 5, also >= 0,5 s messen)"));
  }
  meld.println(F("# ================================================="));
}

// Maschinenlesbare Zeile für das PC-Programm
void druckeRauschZeile(const RauschErgebnis &e) {
  const double uv = ads.microvoltsPerCount();
  const double fa = fabs((double)cfg.faktor);
  double enob = e.sigma > 0.0 ? log2(16777216.0 / e.sigma) : 24.0;
  double nfb  = e.sigma > 0.0 ? log2(16777216.0 / (6.6 * e.sigma)) : 24.0;
  meld.printf("#RAUSCH rate=%g rate_ist=%.3f n=%lu verloren=%lu mittel=%.3f sigma=%.5f "
              "sigma_uv=%.6f sigma_g=%.6f enob=%.3f nfbit=%.3f sigma10=%.5f sigma10_g=%.6f pp=%ld clip=%u\n",
              e.sps, e.rateIst, (unsigned long)e.n, (unsigned long)e.verloren, e.mittel, e.sigma,
              e.sigma * uv, e.sigma / fa, enob, nfb, e.sigma10, e.sigma10 / fa,
              (long)(e.maximum - e.minimum), e.clip ? 1 : 0);
}

void druckeVergleichstabelle() {
  const double uv = ads.microvoltsPerCount();
  const double fa = fabs((double)cfg.faktor);
  meld.println(F("# ============================== Rauschvergleich =============================="));
  meld.printf("# PGA %u, 1 Count = %.4f nV, Faktor %.4f Counts/g%s, WLAN %s\n", ads.gain(), uv * 1000.0,
              cfg.faktor, cfg.kalibriert ? "" : " (unkalibriert)", wlanAn ? "an" : "aus");
  meld.println(F("#     SPS |       n | verl. | sigma Cnt | sigma uV | sigma g  | ENOB  | NF-Bit | sigma g @10SPS"));
  for (uint8_t i = 0; i < vergleichIndex && i < VERGLEICH_MAX; i++) {
    const RauschErgebnis &e = vergleichErg[i];
    double enob = e.sigma > 0.0 ? log2(16777216.0 / e.sigma) : 24.0;
    double nfb  = e.sigma > 0.0 ? log2(16777216.0 / (6.6 * e.sigma)) : 24.0;
    char s10[16];
    if (e.sigma10 > 0.0) snprintf(s10, sizeof(s10), "%.5f", e.sigma10 / fa);
    else                 snprintf(s10, sizeof(s10), "-");
    meld.printf("# %7.1f | %7lu | %5lu | %9.3f | %8.4f | %8.4f | %5.2f | %6.2f | %s%s\n",
                e.sps, (unsigned long)e.n, (unsigned long)e.verloren, e.sigma, e.sigma * uv,
                e.sigma / fa, enob, nfb, s10, e.clip ? "  UEBERSTEUERT" : "");
  }
  meld.println(F("# ENOB = log2(2^24 / sigma), NF-Bit = rauschfrei (6,6 sigma)."));
  meld.println(F("# sigma @10SPS = gemessene Streuung der 0,1-s-Mittelwerte (vergleichbar ueber alle Raten)."));
  meld.println(F("# ============================================================================="));
}

// ===========================================================================
// Rauschvergleich über mehrere Datenraten
// ===========================================================================
void starteVergleich(float sekunden, const float *raten, uint8_t anzahl) {
  vergleichSekunden  = sekunden;
  vergleichAnzahl    = anzahl;
  vergleichIndex     = 0;
  vergleichStartCode = cfg.drateCode;
  for (uint8_t i = 0; i < anzahl; i++) vergleichRaten[i] = raten[i];
  vergleichAktiv  = true;
  vergleichWeiter = true;          // erster Schritt in loop()
  meld.printf("# Rauschvergleich: %u Datenraten, je %.1f s. Zelle unbelastet und ruhig lassen! Abbruch mit 'x'.\n",
              anzahl, sekunden);
}

void vergleichSchritt() {
  vergleichWeiter = false;
  if (!vergleichAktiv) return;
  if (vergleichIndex >= vergleichAnzahl) { vergleichBeenden(false); return; }

  uint8_t code = 0;
  ADS1256::codeFromRate(vergleichRaten[vergleichIndex], code);
  if (!setzeRate(code, false)) {
    auftragAbbrechen("Datenrate konnte nicht gesetzt werden");
    return;
  }
  float sps = ads.sampleRate();
  uint32_t n = (uint32_t)lroundf(sps * vergleichSekunden);
  if (n < 50) n = 50;
  // Einschwingen nach dem Ratenwechsel: ca. 50 ms bzw. mindestens 3 Werte verwerfen
  uint32_t verwerfen = (uint32_t)lroundf(sps * 0.05f);
  if (verwerfen < 3) verwerfen = 3;
  starteAuftrag(AUFTRAG_VERGLEICH, n, verwerfen);
}

void vergleichBeenden(bool abgebrochen) {
  vergleichAktiv = false;
  vergleichWeiter = false;
  if (vergleichIndex > 0) druckeVergleichstabelle();
  if (abgebrochen) meld.println(F("# Rauschvergleich abgebrochen."));
  // Ursprüngliche Datenrate wiederherstellen
  if (cfg.drateCode != vergleichStartCode) {
    setzeRate(vergleichStartCode, true);
  }
}

// ===========================================================================
// Überwachung: Überlast der Zelle und Übersteuerung des ADC
// ===========================================================================
void ueberwachung(int32_t roh) {
  uint32_t jetzt = millis();
  bool sofort = false;

  // --- ADC-Übersteuerung ---
  if (ADS1256::isClipped(roh)) {
    clipZuletztMs = jetzt;
    auftragClip = true;
    webClip = true;
    if (!adcClip) { adcClip = true; sofort = true; }
  } else if (adcClip && jetzt - clipZuletztMs > 500) {
    adcClip = false;
    meld.println(F("# ADC nicht mehr uebersteuert."));
  }

  // --- Last (ungefilterter Einzelwert, damit die Warnung sofort kommt) ---
  double last = fabs(inGramm((double)roh));
  uint8_t neu;
  if (last >= ALARMGRENZE_G)                                        neu = 2;
  else if (warnStufe == 2 && last >= ALARMGRENZE_G * WARN_HYSTERESE) neu = 2;
  else if (last >= WARNGRENZE_G)                                    neu = 1;
  else if (warnStufe >= 1 && last >= WARNGRENZE_G * WARN_HYSTERESE)  neu = 1;
  else                                                              neu = 0;

  if (neu > warnStufe) sofort = true;
  if (neu == 0 && warnStufe > 0) meld.println(F("# Last wieder unter 100 % Nennlast."));
  if (neu != warnStufe) {
    // Pico W: LED hängt am WLAN-Chip, Schalten ist langsam -> nur bei Zustandswechsel
    digitalWrite(LED_BUILTIN, neu > 0 ? HIGH : LOW);
    warnStufe = neu;
  }

  // --- Meldungen: sofort bei Verschlechterung, sonst höchstens 1x pro Sekunde ---
  if (!(adcClip || warnStufe > 0)) return;
  if (!sofort && jetzt - letzteWarnungMs < 1000) return;
  letzteWarnungMs = jetzt;

  if (adcClip) {
    meld.printf("# !!! ADC UEBERSTEUERT (Rohwert %ld) - Bruecke/Verdrahtung pruefen (Aderbruch, Kurzschluss)!\n", (long)roh);
    return;   // Lastwert ist dann ohnehin ungültig
  }
  const char *hinweis = cfg.kalibriert ? "" : " [Faktor unkalibriert]";
  if (warnStufe == 2) {
    meld.printf("# !!!!!! UEBERLAST: %.2f kg >= 120 %% Nennlast - SOFORT ENTLASTEN !!!!!!%s\n", last / 1000.0, hinweis);
  } else {
    meld.printf("# ! Warnung: %.2f kg >= 100 %% Nennlast%s\n", last / 1000.0, hinweis);
  }
}

// ===========================================================================
// Messwertausgabe (USB)
// ===========================================================================
void pufferSenden() {
  if (ausLen == 0) return;
  Serial.write(ausPuffer, ausLen);
  ausLen = 0;
}

void puffern(const void *daten, uint16_t n) {
  if (ausLen + n > sizeof(ausPuffer)) pufferSenden();
  memcpy(ausPuffer + ausLen, daten, n);
  ausLen = (uint16_t)(ausLen + n);
}

// Maschinenlesbare Konfigurationszeile: alles, was der PC zur Umrechnung braucht.
// Gramm = (roh - tara) / faktor,  uV = roh * uv_count
void sendeCfg() {
  pufferSenden();                  // Reihenfolge wahren
  meld.printf("#CFG rate=%g pga=%u vref=%.4f uv_count=%.9g tara=%.3f faktor=%.6f kalibriert=%u "
              "modus=%u filter_s=%.3f nennlast_g=%.0f spi_hz=%lu\n",
              ads.sampleRate(), ads.gain(), VREF_V, ads.microvoltsPerCount(), cfg.tara, cfg.faktor,
              cfg.kalibriert, cfg.modus, cfg.filterMs / 1000.0, NENNLAST_G, (unsigned long)ads.spiTakt());
}

// Lesbar und Plotter: Gewicht aus dem Filter. CSV und Binär: nur Rohwerte.
// Diese Datenströme gehen nur an USB, nicht ins Web-Log.
void ausgabe(const ADS1256::Messwert &m) {
  switch (cfg.modus) {
    case MODUS_LESBAR: {
      uint32_t jetzt = millis();
      if (jetzt - letzteAusgabeMs < 250) return;       // 4 Zeilen pro Sekunde
      letzteAusgabeMs = jetzt;
      double gefiltert = filterMittel();
      Serial.printf("Roh: %9ld | %11.3f uV | %9.4f kg%s\n", (long)m.counts,
                    ads.countsToMicrovolts(gefiltert), inGramm(gefiltert) / 1000.0,
                    cfg.kalibriert ? "" : "  (unkalibriert)");
      break;
    }

    case MODUS_CSV: {                                  // jeder Messwert, nur Ganzzahlen
      char z[40];
      int n = snprintf(z, sizeof(z), "%lu,%lu,%ld\n", (unsigned long)m.seq,
                       (unsigned long)m.tMikros, (long)m.counts);
      if (n > 0) puffern(z, (uint16_t)n);
      break;
    }

    case MODUS_PLOTTER: {
      uint32_t jetzt = millis();
      if (jetzt - letzteAusgabeMs < 10) return;        // höchstens 100 Punkte pro Sekunde
      letzteAusgabeMs = jetzt;
      Serial.printf("Gramm_roh:%.2f,Gramm_gefiltert:%.2f\n", inGramm((double)m.counts), inGramm(filterMittel()));
      break;
    }

    case MODUS_BINAER: {
      uint8_t f[8];
      const uint32_t c = (uint32_t)m.counts;
      f[0] = 0xA5;
      f[1] = 0x5A;
      f[2] = (uint8_t)(m.seq);
      f[3] = (uint8_t)(m.seq >> 8);
      f[4] = (uint8_t)(c);
      f[5] = (uint8_t)(c >> 8);
      f[6] = (uint8_t)(c >> 16);
      f[7] = crc8(&f[2], 5);
      puffern(f, sizeof(f));
      break;
    }

    default:
      break;
  }
}

void setzeModus(uint8_t m) {
  pufferSenden();
  cfg.modus = m;
  switch (m) {
    case MODUS_AUS:     meld.println(F("# Ausgabe aus.")); break;
    case MODUS_LESBAR:  meld.println(F("# Ausgabe: lesbar (4x pro Sekunde).")); break;
    case MODUS_CSV:     meld.println(F("# Ausgabe: CSV, jeder Messwert als Rohwert (Umrechnung siehe #CFG)."));
                        meld.println(F("# seq = laufende Nummer, t_us = Zeitstempel (laeuft nach ca. 71 min ueber)"));
                        Serial.println(F("seq,t_us,roh")); break;
    case MODUS_PLOTTER: meld.println(F("# Ausgabe: Serieller Plotter (max. 100 Punkte/s).")); break;
    case MODUS_BINAER:  meld.println(F("# Ausgabe: binaer (8-Byte-Rahmen, fuer waegezelle_plot.py).")); break;
  }
  sendeCfg();
}

// ===========================================================================
// Web: Diagrammpunkte und Live-Statistik (Kern 0, für jeden Messwert)
// ===========================================================================
void graphReset() {
  float sps = ads.sampleRate();
  uint32_t n = (uint32_t)lroundf(sps / GRAPH_PUNKTE_PRO_S);
  graphLaenge = (n < 1) ? 1 : n;
  graphN = 0;
}

void webStatReset() {
  webStat.reset(blockLaenge10());
  uint32_t n = (uint32_t)lroundf(ads.sampleRate() * webStatSek);
  webStatN = (n < 10) ? 10 : n;
  webClip = false;
  webGlatt.reset();
}

void webAufnehmen(const ADS1256::Messwert &m) {
  // 64-Bit-Zeit aus den 32-Bit-µs-Zeitstempeln (läuft sonst nach 71 min über)
  if (zeitStart) {
    zeitStart = false;
    tErweitert = m.tMikros;
  } else {
    tErweitert += (uint32_t)(m.tMikros - tLetzteRoh);
  }
  tLetzteRoh = m.tMikros;

  // Diagrammpunkt: Mittel, Minimum und Maximum über graphLaenge Werte
  if (graphN == 0) {
    graphSumme = 0;
    graphMin = graphMax = m.counts;
  }
  graphSumme += m.counts;
  if (m.counts < graphMin) graphMin = m.counts;
  if (m.counts > graphMax) graphMax = m.counts;
  if (++graphN >= graphLaenge) {
    Punkt &p = punkte[punktZaehler & (PUNKTE_GROESSE - 1)];
    p.tMs     = (uint32_t)(tErweitert / 1000u);
    p.mittel  = (float)((double)graphSumme / (double)graphN);
    p.minimum = graphMin;
    p.maximum = graphMax;
    const double g = filterMittel();             // geglätteter Wert, ca. 50x pro Sekunde
    p.geglaettet = (float)g;
    webGlatt.add(g);
    punktZaehler++;
    graphN = 0;
  }

  // Live-Statistik über die volle Datenrate
  webStat.add(m);
  if (webStat.roh.n >= webStatN) {
    webErg = rauschErgebnis(webStat, webClip);
    webErgGlatt = (webGlatt.n >= 5) ? webGlatt.stdAbw() : 0.0;
    webErgId++;
    webStatReset();
  }
}

// ===========================================================================
// Verarbeitung eines neuen Messwerts
// ===========================================================================
void verarbeiteMesswert(const ADS1256::Messwert &m) {
  filterUpdate(m.counts);
  ueberwachung(m.counts);
  webAufnehmen(m);

  if (auftrag != AUFTRAG_KEIN) {
    if (auftragVerwerfen > 0) {
      auftragVerwerfen--;          // Einschwingen nach Ratenwechsel
    } else {
      stat.add(m);
      if (stat.roh.n >= auftragN) auftragFertig();
    }
    // Während eines Auftrags nur die Datenströme (CSV/Binär) weiterlaufen lassen
    if (cfg.modus != MODUS_CSV && cfg.modus != MODUS_BINAER) return;
  }
  ausgabe(m);
}

// DRDY-Timeout: kommt länger als 3 Messperioden + 100 ms kein Messwert?
void pruefeTimeout() {
  uint32_t grenze = (uint32_t)(3000.0f / ads.sampleRate()) + 100;
  uint32_t jetzt = millis();
  if (jetzt - letzteMessungMs <= grenze) return;
  if (timeoutAktiv && jetzt - letzteTimeoutMeldungMs < 2000) return;
  timeoutAktiv = true;
  letzteTimeoutMeldungMs = jetzt;
  meld.printf("# FEHLER: DRDY-Timeout - seit %lu ms kein Messwert (DRDY-Leitung/Versorgung pruefen, ggf. 'init').\n",
              (unsigned long)(jetzt - letzteMessungMs));
  auftragAbbrechen("DRDY-Timeout");
}

// ===========================================================================
// WLAN
// ===========================================================================
void wlanStarten() {
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(WLAN_HOSTNAME);
  WiFi.beginNoBlock(WLAN_SSID, WLAN_PASSWORT);   // blockiert nicht, Verbindung läuft im Hintergrund
  wlanLetzterVersuch = millis();
  meld.printf("# WLAN: verbinde mit '%s' ...\n", WLAN_SSID);
}

// Alle 500 ms aus loop(): Verbindung melden, bei Abbruch neu verbinden
void wlanPruefen() {
  if (!wlanAn) return;
  const bool verbunden = (WiFi.status() == WL_CONNECTED);
  if (verbunden && !wlanVerbunden) {
    wlanVerbunden = true;
    if (!mdnsAktiv) {
      mdnsAktiv = MDNS.begin(WLAN_HOSTNAME);
      if (mdnsAktiv) MDNS.addService("http", "tcp", 80);
    }
    meld.printf("# WLAN verbunden: http://%s  bzw. http://%s.local  (RSSI %ld dBm)\n",
                WiFi.localIP().toString().c_str(), WLAN_HOSTNAME, (long)WiFi.RSSI());
  } else if (!verbunden && wlanVerbunden) {
    wlanVerbunden = false;
    meld.println(F("# WLAN getrennt - verbinde neu ..."));
    wlanLetzterVersuch = millis();
  }
  if (!verbunden && millis() - wlanLetzterVersuch > 20000) {
    WiFi.disconnect();
    wlanStarten();
  }
}

void wlanInfo() {
  meld.printf("# WLAN: %s, SSID '%s'", wlanAn ? "an" : "AUS", WLAN_SSID);
  if (wlanAn && WiFi.status() == WL_CONNECTED) {
    meld.printf(", verbunden: http://%s (http://%s.local), RSSI %ld dBm\n",
                WiFi.localIP().toString().c_str(), WLAN_HOSTNAME, (long)WiFi.RSSI());
  } else {
    meld.println(wlanAn ? F(", nicht verbunden") : F(""));
  }
}

// ===========================================================================
// Webserver: Seite, Daten (JSON), Befehle
// ===========================================================================
void jsonFlush() {
  if (jsonLen == 0) return;
  server.sendContent(jsonPuf, jsonLen);
  jsonLen = 0;
}

void jsonf(const char *fmt, ...) {
  char tmp[256];
  va_list ap;
  va_start(ap, fmt);
  int n = vsnprintf(tmp, sizeof(tmp), fmt, ap);
  va_end(ap);
  if (n <= 0) return;
  if (n >= (int)sizeof(tmp)) n = sizeof(tmp) - 1;
  if ((size_t)jsonLen + (size_t)n > sizeof(jsonPuf)) jsonFlush();
  memcpy(jsonPuf + jsonLen, tmp, n);
  jsonLen = (uint16_t)(jsonLen + n);
}

// Ein Zeichen als Teil eines JSON-Strings (mit Escape)
void jsonZeichen(char c) {
  if ((size_t)jsonLen + 2u > sizeof(jsonPuf)) jsonFlush();
  if (c == '"' || c == '\\') { jsonPuf[jsonLen++] = '\\'; jsonPuf[jsonLen++] = c; }
  else if (c == '\n')        { jsonPuf[jsonLen++] = '\\'; jsonPuf[jsonLen++] = 'n'; }
  else if ((uint8_t)c >= 0x20 && (uint8_t)c < 0x7F) jsonPuf[jsonLen++] = c;
  // andere Steuerzeichen (z. B. '\r') weglassen
}

void webSeite() {
  server.send_P(200, "text/html; charset=utf-8", WEBUI_HTML);
}

void webInfo() {
  server.send_P(200, "text/html; charset=utf-8", WEBINFO_HTML);
}

void webStil() {
  // 5 Minuten zwischenspeichern: nach einem Firmware-Update ist neues CSS spätestens dann aktiv
  server.sendHeader("Cache-Control", "max-age=300");
  server.send_P(200, "text/css; charset=utf-8", WEBUI_CSS);
}

void webDaten() {
  // --- welche Punkte? ---
  const uint32_t pz = punktZaehler;
  uint32_t ab = server.hasArg("p") ? strtoul(server.arg("p").c_str(), nullptr, 10) : UINT32_MAX;
  if (ab > pz) ab = (pz > 250) ? pz - 250 : 0;            // erster Abruf oder Pico neu gestartet
  if (pz - ab > PUNKTE_GROESSE) ab = pz - PUNKTE_GROESSE;  // zu alt -> verloren
  uint32_t bis = pz;
  bool mehr = false;
  if (bis - ab > 300) { bis = ab + 300; mehr = true; }

  // --- welcher Log-Text? ---
  const uint32_t lz = webLogPos;
  uint32_t lab = server.hasArg("l") ? strtoul(server.arg("l").c_str(), nullptr, 10) : UINT32_MAX;
  if (lab > lz) lab = (lz > 2000) ? lz - 2000 : 0;
  if (lz - lab > WEBLOG_GROESSE) lab = lz - WEBLOG_GROESSE;
  uint32_t lbis = lz;
  if (lbis - lab > 3000) { lbis = lab + 3000; mehr = true; }

  server.sendHeader("Cache-Control", "no-store");
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);         // chunked senden
  server.send(200, "application/json", "");
  jsonLen = 0;

  const long rssi = (wlanAn && WiFi.status() == WL_CONNECTED) ? (long)WiFi.RSSI() : 0L;
  jsonf("{\"t\":%lu,\"cfg\":{\"rate\":%g,\"uv\":%.9g,\"tara\":%.3f,\"faktor\":%.6f,\"kal\":%u,"
        "\"filter\":%g,\"filter_ist\":%.4f,\"modus\":%u,\"pga\":%u,\"nennlast\":%.0f,\"gl\":%lu},",
        (unsigned long)millis(), ads.sampleRate(), ads.microvoltsPerCount(), cfg.tara, cfg.faktor,
        cfg.kalibriert, cfg.filterMs / 1000.0, cfg.filterMs ? filterSekunden() : 0.0, cfg.modus, ads.gain(),
        NENNLAST_G, (unsigned long)graphLaenge);
  jsonf("\"ok\":%u,\"kont\":%u,\"w\":%.2f,\"auftrag\":%u,\"verl\":%lu,\"ringmax\":%lu,\"rssi\":%ld,\"ws\":%g,",
        adsOk ? 1 : 0, ads.isContinuous() ? 1 : 0, filterMittel(), auftragLaeuft() ? 1 : 0,
        (unsigned long)ads.overruns(), (unsigned long)ads.ringMaximum(), rssi, webStatSek);

  if (webErgId > 0) {
    const RauschErgebnis &e = webErg;
    jsonf("\"st\":{\"id\":%lu,\"n\":%lu,\"mittel\":%.3f,\"sigma\":%.4f,\"pp\":%ld,\"s10\":%.4f,"
          "\"b\":%lu,\"ri\":%.3f,\"verl\":%lu,\"clip\":%u,\"sg\":%.4f},",
          (unsigned long)webErgId, (unsigned long)e.n, e.mittel, e.sigma, (long)(e.maximum - e.minimum),
          e.sigma10, (unsigned long)e.bloecke, e.rateIst, (unsigned long)e.verloren, e.clip ? 1 : 0,
          webErgGlatt);
  } else {
    jsonf("\"st\":null,");
  }

  jsonf("\"p\":[");
  for (uint32_t i = ab; i < bis; i++) {
    const Punkt &p = punkte[i & (PUNKTE_GROESSE - 1)];
    jsonf("%s[%lu,%.2f,%ld,%ld,%.2f]", (i == ab) ? "" : ",", (unsigned long)p.tMs, (double)p.mittel,
          (long)p.minimum, (long)p.maximum, (double)p.geglaettet);
  }
  jsonf("],\"pn\":%lu,\"mehr\":%u,\"ln\":%lu,\"log\":\"", (unsigned long)bis, mehr ? 1 : 0, (unsigned long)lbis);
  for (uint32_t i = lab; i < lbis; i++) jsonZeichen(webLog[i & (WEBLOG_GROESSE - 1)]);
  jsonf("\"}");
  jsonFlush();
  server.sendContent("");                                  // Ende der chunked-Antwort
}

void webBefehl() {
  if (!server.hasArg("c")) {
    server.send(400, "text/plain", "Parameter c fehlt");
    return;
  }
  char puf[sizeof(zeile)];
  strncpy(puf, server.arg("c").c_str(), sizeof(puf) - 1);
  puf[sizeof(puf) - 1] = '\0';
  server.send(200, "text/plain", "ok");     // erst antworten (z. B. vor 'wlan aus')
  meld.printf("> %s   (Web)\n", puf);
  verarbeiteBefehl(puf);
}

// ===========================================================================
// Serielle Befehle
// ===========================================================================
bool ist(const char *cmd, const char *a, const char *b) {
  return strcmp(cmd, a) == 0 || strcmp(cmd, b) == 0;
}

void zeigeHilfe() {
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
  meld.println(F("#      wlan [an|aus]      WLAN-Status bzw. ein-/ausschalten"));
  meld.println(F("#                         ('wlan aus' fuer Rauschmessungen ohne Funk; danach nur per USB wieder an)"));
  meld.println(F("# Ablauf: Zelle entlasten -> 't' -> Gewicht auflegen -> 'k 5000' -> 's'"));
  meld.println(F("# ---------------------------------------------------------------"));
}

void zeigeInfo() {
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
                filterSekunden(), filterK, (unsigned long)filterBlockLen);
  }
  meld.printf("# Ausgabemodus:   %u\n", cfg.modus);
  meld.printf("# Tara:           %.1f Counts\n", cfg.tara);
  meld.printf("# Faktor:         %.5f Counts/g (%s)\n", cfg.faktor,
              cfg.kalibriert ? "kalibriert" : "Schaetzwert, NICHT kalibriert");
  wlanInfo();
  meld.println(F("# -----------------------------------------"));
}

void registerDump() {
  static const char *const NAMEN[ADS1256::REG_COUNT] = {
    "STATUS", "MUX", "ADCON", "DRATE", "IO", "OFC0", "OFC1", "OFC2", "FSC0", "FSC1", "FSC2"
  };
  if (!adcAusfuehren(OP_REG, 0)) return;             // Kern 1 hält RDATAC kurz an
  uint8_t r[ADS1256::REG_COUNT];
  memcpy(r, adcRegs, sizeof(r));
  if (!adcErgebnis) meld.println(F("# FEHLER: Dauerbetrieb nach dem Lesen nicht wieder gestartet ('init')."));

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

void zeigeRaten() {
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
    if (auftragLaeuft()) auftragAbbrechen("durch Benutzer");
    else setzeModus(MODUS_AUS);
    return;
  }
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
      // nicht sofort: der Befehl kann aus einem laufenden HTTP-Aufruf kommen
      if (wlanAn) wlanAusFaellig = true;
    } else if (a1 && strcmp(a1, "an") == 0) {
      if (!wlanAn) { wlanAn = true; wlanStarten(); }
    } else {
      wlanInfo();
    }
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
                  filterSekunden(), (unsigned long)filterK * filterBlockLen, ads.sampleRate());
    }
    sendeCfg();
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
    auftragGewichtG = g;
    uint32_t n = a2 ? strtoul(a2, nullptr, 10) : standardN(2.0f, 20);
    starteAuftrag(AUFTRAG_KALIBRIEREN, n);
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
    bool ok = adcAusfuehren(OP_SELFCAL, 0) && adcErgebnis;   // Kern 1 hält RDATAC kurz an
    filterReset();
    meld.println(ok ? F("# Selbstkalibrierung des ADC abgeschlossen.") : F("# FEHLER: DRDY-Timeout bei der Selbstkalibrierung."));
    return;
  }

  meld.printf("# Unbekannter Befehl '%s' - 'h' zeigt die Hilfe.\n", cmd);
}

// Zeichen einsammeln, bei Zeilenende Befehl ausführen (nicht blockierend)
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

// ===========================================================================
// setup / loop (Kern 0)
// ===========================================================================
void setup() {
  Serial.begin(115200);            // USB-CDC: Baudrate ohne Bedeutung
  uint32_t start = millis();
  while (!Serial && millis() - start < 3000) { }   // kurz auf den seriellen Monitor warten

  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);
  crc8Init();

  meld.println();
  meld.println(F("# ================================================="));
  meld.println(F("#  Waegezellen-Test: ADS1256 + Raspberry Pi Pico W"));
  meld.println(F("# ================================================="));

  // Einstellungen aus dem Flash laden
  EEPROM.begin(256);
  if (ladeEinstellungen()) {
    meld.printf("# Einstellungen geladen: Tara %.1f Counts, Faktor %.5f Counts/g%s\n",
                cfg.tara, cfg.faktor, cfg.kalibriert ? "" : " (unkalibriert)");
  } else {
    standardEinstellungen();
    meld.println(F("# Keine gueltigen Einstellungen im Flash - Standardwerte aktiv (unkalibriert)."));
  }
  // Binärmodus nicht automatisch starten (unlesbar im seriellen Monitor);
  // das PC-Programm schaltet ihn selbst ein.
  if (cfg.modus == MODUS_BINAER) cfg.modus = MODUS_LESBAR;

  // SPI0 auf die vorgegebenen Pins legen (nur Konfiguration, SPI.begin() läuft auf Kern 1)
  SPI.setSCK(PIN_SPI_SCK);
  SPI.setTX(PIN_SPI_MOSI);
  SPI.setRX(PIN_SPI_MISO);

  initADS();                       // läuft auf Kern 1

  // Webserver und WLAN (Kern 0)
  server.on("/", HTTP_GET, webSeite);
  server.on("/info", HTTP_GET, webInfo);
  server.on("/stil.css", HTTP_GET, webStil);
  server.on("/daten", HTTP_GET, webDaten);
  server.on("/cmd", webBefehl);    // GET oder POST
  server.onNotFound([]() { server.send(404, "text/plain", "Nicht gefunden"); });
  server.enableDelay(false);       // handleClient() soll die Messwertverarbeitung nicht bremsen
  server.begin();
  wlanStarten();

  meld.println(F("# Befehl 'h' zeigt die Hilfe."));
  if (cfg.modus == MODUS_CSV) Serial.println(F("seq,t_us,roh"));
}

void loop() {
  if (vergleichWeiter) vergleichSchritt();   // vor den Befehlen, damit kein Befehl dazwischenfunkt
  leseSeriell();

  // Weboberfläche und WLAN-Verbindung
  if (wlanAn) server.handleClient();
  if (wlanAusFaellig) {
    wlanAusFaellig = false;
    wlanAn = false;
    wlanVerbunden = false;
    if (mdnsAktiv) { MDNS.end(); mdnsAktiv = false; }
    WiFi.end();
    meld.println(F("# WLAN aus. Wieder einschalten mit 'wlan an' (nur ueber USB moeglich)."));
  }
  if (millis() - wlanLetztePruefung >= 500) {
    wlanLetztePruefung = millis();
    wlanPruefen();
  }

  if (!adsOk) return;

  // Alle angefallenen Messwerte verarbeiten (begrenzt, damit Befehle zügig drankommen)
  ADS1256::Messwert m;
  uint16_t anzahl = 0;
  while (anzahl < 1024 && !vergleichWeiter && ads.popSample(m)) {
    verarbeiteMesswert(m);
    anzahl++;
  }
  pufferSenden();

  if (anzahl > 0) {
    letzteMessungMs = millis();
    if (timeoutAktiv) {
      timeoutAktiv = false;
      meld.println(F("# Messwerte kommen wieder."));
    }
  } else if (!vergleichWeiter) {
    pruefeTimeout();
  }
}
