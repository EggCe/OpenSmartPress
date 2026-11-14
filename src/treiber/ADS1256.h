/*
 * ADS1256.h - Treiber für den 24-Bit-Delta-Sigma-ADC ADS1256 (Texas Instruments)
 *
 * Zielplattform: Raspberry Pi Pico W / WH (RP2040) mit dem Arduino-Core von
 * Earle Philhower (arduino-pico). Der Treiber enthält keine Anwendungslogik
 * (keine Tara, keine Kalibrierung, keine seriellen Ausgaben).
 *
 * Zwei Betriebsarten:
 *   a) Einzelabfrage (SDATAC + RDATA): dataReady() / readData(). Nur für
 *      niedrige Datenraten oder Diagnose gedacht.
 *   b) Dauerbetrieb (RDATAC + DRDY-Interrupt): startContinuous().
 *      Die Interrupt-Routine liest jeden Messwert direkt nach der fallenden
 *      DRDY-Flanke (24 Bit, ca. 14 µs bei 1,9 MHz) und legt ihn mit
 *      laufender Nummer und Zeitstempel in einen Ringpuffer. Das Hauptprogramm
 *      holt die Werte mit popSample() ab. So gehen auch dann keine Werte
 *      verloren, wenn loop() oder die USB-Ausgabe kurz hängen.
 *
 * Laufende Nummer (seq):
 *   Zählt jede Wandlung des ADC, auch verlorene. Wurde der Interrupt länger
 *   blockiert (z. B. Flash-Schreiben), wird die Lücke aus dem Zeitstempel
 *   berechnet und seq entsprechend weitergezählt. Lücken in seq zeigen also
 *   zuverlässig verlorene Messwerte an.
 *
 * Regeln:
 *   - Alle SPI-Zugriffe kommen aus EINEM Kern (hier Kern 0). Funktionen mit
 *     Registerzugriff (setDataRate, selfCalibrate, read/writeRegister...)
 *     unterbrechen den Dauerbetrieb automatisch und starten ihn danach neu.
 *   - Ringpuffer: genau ein Erzeuger (ISR) und ein Verbraucher (loop) ->
 *     ohne Sperren, nur mit Speicherbarrieren. Funktioniert auch, wenn die
 *     ISR später auf Kern 1 verlegt wird (enableDrdyInterrupt in setup1()).
 *   - setDrdyCallback(): Funktion, die IM Interrupt nach dem Lesen mit Zeitstempel
 *     und Messwert aufgerufen wird (z. B. Kraftüberwachung des Antriebs). Kurz halten!
 */
#pragma once

#include <Arduino.h>
#include <SPI.h>

class ADS1256 {
public:
  // ----- Register-Adressen -----
  enum : uint8_t {
    REG_STATUS = 0x00, REG_MUX = 0x01, REG_ADCON = 0x02, REG_DRATE = 0x03,
    REG_IO = 0x04, REG_OFC0 = 0x05, REG_OFC1 = 0x06, REG_OFC2 = 0x07,
    REG_FSC0 = 0x08, REG_FSC1 = 0x09, REG_FSC2 = 0x0A,
    REG_COUNT = 11
  };

  // ----- Befehle -----
  enum : uint8_t {
    CMD_WAKEUP = 0x00, CMD_RDATA = 0x01, CMD_RDATAC = 0x03, CMD_SDATAC = 0x0F,
    CMD_RREG = 0x10, CMD_WREG = 0x50, CMD_SELFCAL = 0xF0, CMD_SELFOCAL = 0xF1,
    CMD_SELFGCAL = 0xF2, CMD_SYSOCAL = 0xF3, CMD_SYSGCAL = 0xF4,
    CMD_SYNC = 0xFC, CMD_STANDBY = 0xFD, CMD_RESET = 0xFE
  };

  // ----- DRATE-Codes (gültig für 7,68-MHz-Quarz) -----
  enum : uint8_t {
    DR_30000 = 0xF0, DR_15000 = 0xE0, DR_7500 = 0xD0, DR_3750 = 0xC0,
    DR_2000 = 0xB0, DR_1000 = 0xA1, DR_500 = 0x92, DR_100 = 0x82,
    DR_60 = 0x72, DR_50 = 0x63, DR_30 = 0x53, DR_25 = 0x43,
    DR_15 = 0x33, DR_10 = 0x23, DR_5 = 0x13, DR_2_5 = 0x03
  };

  // ----- STATUS-Bits -----
  enum : uint8_t {
    STATUS_ORDER = 0x08,   // 0 = MSB zuerst
    STATUS_ACAL  = 0x04,   // Auto-Kalibrierung
    STATUS_BUFEN = 0x02,   // Eingangspuffer
    STATUS_NDRDY = 0x01    // Kopie von /DRDY (nur lesbar)
  };

  static const uint8_t AINCOM = 8;        // Multiplexer-Code für AINCOM
  static const uint8_t EXPECTED_ID = 3;   // Chip-ID des ADS1256
  static const int32_t FULLSCALE_POS = 0x7FFFFF;
  static const int32_t FULLSCALE_NEG = -0x800000;

  // SCLK: max. fCLKIN/4 = 1,92 MHz. Der RP2040 rundet immer nach unten.
  static const uint32_t SPI_TAKT_STANDARD = 1900000;

  // Ringpuffer für den Dauerbetrieb (Zweierpotenz!). 4096 Werte = 48 KB,
  // reicht bei 30 kSPS für ca. 136 ms, bei 2 kSPS für ca. 2 s.
  static const uint32_t RING_GROESSE = 4096;

  // Ergebnis der Initialisierung
  enum class Status : uint8_t {
    ERFOLG = 0,
    TIMEOUT_RESET,      // DRDY nach RESET nicht aktiv geworden
    FALSCHE_CHIP_ID,    // ID-Bits im STATUS-Register != 3
    TIMEOUT_KALIBRIERUNG,
    REGISTER_FEHLER,    // zurückgelesene Register stimmen nicht
    UNGUELTIGE_KONFIG
  };

  // Konfiguration für begin()
  struct Config {
    uint8_t ainP      = 0;        // positiver Eingang (0..7, 8 = AINCOM)
    uint8_t ainN      = 1;        // negativer Eingang
    uint8_t pgaCode   = 6;        // Verstärkung = 2^pgaCode (6 -> 64)
    uint8_t drateCode = DR_100;   // Datenrate
    bool    buffer    = true;     // Eingangspuffer (Eingänge dann 0 .. AVDD-2 V)
    bool    autoCal   = true;     // Auto-Kalibrierung bei PGA/DRATE/BUFEN-Änderung
  };

  // Ein Messwert aus dem Dauerbetrieb
  struct Messwert {
    uint32_t seq;       // laufende Nummer der Wandlung (Lücken = verlorene Werte)
    uint32_t tMikros;   // Zeitstempel der DRDY-Flanke in µs
    int32_t  counts;    // vorzeichenrichtiger 24-Bit-Wert
  };

  // Callback, der im DRDY-Interrupt für jeden gültigen Messwert aufgerufen wird (kurz halten!)
  typedef void (*DrdyCallback)(uint32_t zeitstempelMikros, int32_t counts);

  // pinPDWN = -1, wenn der Pin nicht angeschlossen ist
  ADS1256(SPIClass &spi, uint8_t pinCS, uint8_t pinDRDY, int8_t pinPDWN = -1,
          float vref = 2.5f, uint32_t spiTakt = SPI_TAKT_STANDARD);

  // Komplette Initialisierung: RESET, SDATAC, ID prüfen, Register schreiben,
  // SELFCAL, Register zurücklesen. Blockiert (je nach Datenrate bis ca. 1 s).
  // Danach ist der Baustein im Einzelabfrage-Modus (SDATAC).
  Status begin(const Config &cfg);
  static const char *statusText(Status s);

  // ----- Dauerbetrieb (RDATAC + Interrupt) -----
  bool startContinuous();                 // false bei DRDY-Timeout
  void stopContinuous();
  bool isContinuous() const { return _continuous; }
  bool popSample(Messwert &m);            // nächsten Wert aus dem Ringpuffer holen
  uint32_t available() const;             // Anzahl Werte im Ringpuffer
  void     flush();                       // Ringpuffer leeren (nur aus loop)
  uint32_t overruns() const { return _verlorenIsr + _verlorenVoll + _verlorenLuecke; }
  uint32_t verlorenDrdy() const { return _verlorenIsr; }     // ISR zu spät (DRDY schon HIGH)
  uint32_t verlorenVoll() const { return _verlorenVoll; }    // Ringpuffer war voll
  uint32_t verlorenLuecke() const { return _verlorenLuecke; } // Interrupt war blockiert
  uint32_t ringMaximum() const { return _ringMax; }          // höchster Füllstand
  void     clearOverruns();
  void     setDrdyCallback(DrdyCallback cb) { _callback = cb; }

  // ----- Einzelabfrage (nur außerhalb des Dauerbetriebs) -----
  bool    dataReady() const;                        // DRDY = LOW?
  int32_t readData(uint32_t *zeitMikros = nullptr); // RDATA
  bool    waitDRDY(uint32_t timeoutMs) const;       // blockierend auf DRDY = LOW warten

  // ----- Konfiguration zur Laufzeit -----
  bool setDataRate(uint8_t drateCode);   // inkl. Kalibrierung und Rücklesen
  bool selfCalibrate();                  // SELFCAL (Offset + Gain)

  // ----- Registerzugriff (unterbricht ggf. den Dauerbetrieb) -----
  uint8_t readRegister(uint8_t reg);
  void    readRegisters(uint8_t start, uint8_t *puffer, uint8_t anzahl);
  void    writeRegister(uint8_t reg, uint8_t wert);
  void    writeRegisters(uint8_t start, const uint8_t *daten, uint8_t anzahl);
  void    sendCommand(uint8_t cmd);

  // ----- Hilfsfunktionen -----
  uint8_t  chipId() const { return _chipId; }
  uint8_t  gain() const { return (uint8_t)(1u << _cfg.pgaCode); }
  uint8_t  dataRateCode() const { return _cfg.drateCode; }
  float    sampleRate() const { return rateFromCode(_cfg.drateCode); }
  uint32_t spiTakt() const { return _spiTakt; }
  double   microvoltsPerCount() const;               // LSB-Größe in µV
  double   countsToMicrovolts(double counts) const { return counts * microvoltsPerCount(); }
  double   fullScaleMicrovolts() const;              // +Vollausschlag in µV

  static int32_t signExtend24(uint32_t wert24);
  static bool    isClipped(int32_t counts);         // ADC am Anschlag?
  static bool    codeFromRate(float sps, uint8_t &code);
  static float   rateFromCode(uint8_t code);        // 0, wenn Code ungültig
  static uint8_t rateCount();
  static float   rateByIndex(uint8_t index);

private:
  struct RateEntry { float sps; uint8_t code; };
  static const RateEntry RATES[];

  // Zeiten laut Datenblatt bei fCLKIN = 7,68 MHz (tCLKIN = 130 ns), mit Reserve
  static const uint32_t T6_US  = 10;   // >= 50 tCLKIN = 6,5 µs  (Befehl -> Daten)
  static const uint32_t T10_US = 2;    // >= 8 tCLKIN  = 1,0 µs  (letzter SCLK -> CS high)
  static const uint32_t T11_US = 4;    // >= 24 tCLKIN = 3,1 µs  (Abstand zwischen Befehlen)
  static const uint32_t CAL_TIMEOUT_MS = 3000;  // SELFCAL dauert bei 2,5 SPS ca. 1,2 s

  // Hält den Dauerbetrieb für die Lebensdauer des Objekts an (RAII)
  class Pause {
  public:
    explicit Pause(ADS1256 &a) : _a(a), _war(a._continuous) { if (_war) _a.stopContinuous(); }
    ~Pause() { if (_war) _a.startContinuous(); }
  private:
    ADS1256 &_a;
    bool _war;
  };

  void select();
  void deselect();
  bool waitCalibrationDone(uint32_t timeoutMs);
  void enableDrdyInterrupt();
  void disableDrdyInterrupt();
  void neuePeriode();
  static void isrTrampoline(void *param);
  void isrLesen();

  // Rohzugriffe ohne Pause-Schutz (nur intern)
  void sendCommandRoh(uint8_t cmd);
  void readRegistersRoh(uint8_t start, uint8_t *puffer, uint8_t anzahl);
  void writeRegistersRoh(uint8_t start, const uint8_t *daten, uint8_t anzahl);

  SPIClass   &_spi;
  SPISettings _spiSettings;
  uint32_t    _spiTakt;
  uint8_t     _pinCS;
  uint8_t     _pinDRDY;
  int8_t      _pinPDWN;
  float       _vref;
  Config      _cfg;
  uint8_t     _chipId = 0xFF;

  // Dauerbetrieb
  volatile bool     _continuous = false;
  bool              _irqEnabled = false;
  volatile DrdyCallback _callback = nullptr;
  Messwert          _ring[RING_GROESSE];
  volatile uint32_t _ringKopf = 0;     // nur ISR schreibt
  volatile uint32_t _ringEnde = 0;     // nur popSample() schreibt
  volatile uint32_t _ringMax  = 0;
  volatile uint32_t _seq = 0;          // nächste laufende Nummer
  volatile uint32_t _letzteFlanke = 0; // Zeitstempel der letzten DRDY-Flanke
  volatile bool     _zeitbasisNeu = true;
  uint32_t          _periodeNs = 10000000;   // Wandlungsperiode in ns
  uint32_t          _lueckeUs  = 15000;      // ab hier gilt ein Abstand als Lücke
  uint32_t          _spaetUs   = 12500;      // ab hier kam der Interrupt zu spät: Wert nicht lesen
  volatile uint32_t _verlorenIsr = 0;
  volatile uint32_t _verlorenVoll = 0;
  volatile uint32_t _verlorenLuecke = 0;
};
