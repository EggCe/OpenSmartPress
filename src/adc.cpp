#include "adc.h"

#include <SPI.h>
#include <math.h>
#include <hardware/gpio.h>
#include <hardware/sync.h>
#include "konfig.h"
#include "meldung.h"
#include "einstellungen.h"
#include "filter.h"
#include "usb_ausgabe.h"
#include "verarbeitung.h"
#include "watchdog.h"
#include "web/livedaten.h"

ADS1256 ads(SPI, PIN_ADS_CS, PIN_ADS_DRDY, PIN_ADS_PDWN, VREF_V, SPI_TAKT_HZ);
bool    adsOk = false;

// ---------------------------------------------------------------------------
// Postfach Kern 0 -> Kern 1 (alles, was SPI braucht)
// ---------------------------------------------------------------------------
enum AdcOp : uint8_t { OP_KEIN = 0, OP_INIT, OP_RATE, OP_SELFCAL, OP_REG };

static volatile uint8_t adcOp = OP_KEIN;   // von Kern 0 gesetzt, von Kern 1 nach Erledigung gelöscht
static uint8_t          adcParam = 0;
static ADS1256::Config  adcKonfig;
static ADS1256::Status  adcStatus = ADS1256::Status::ERFOLG;
static bool             adcErgebnis = false;
static uint8_t          adcRegs[ADS1256::REG_COUNT];

// Kern 1: wird ständig aus loop1() aufgerufen
void adcKern1Schritt() {
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
static bool adcAusfuehren(uint8_t op, uint8_t param, uint32_t timeoutMs = 8000) {
  pufferSenden();
  adcParam = param;
  __dmb();
  adcOp = op;
  const uint32_t start = millis();
  while (adcOp != OP_KEIN) {
    watchdogWarten();              // Kern 1 ist absichtlich beschäftigt; eigener Timeout hier
    if (millis() - start > timeoutMs) {
      meld.println(F("# FEHLER: Kern 1 hat den ADC-Auftrag nicht erledigt (Timeout)."));
      return false;
    }
  }
  __dmb();
  messTimeoutNeustart();
  return true;
}

// ---------------------------------------------------------------------------
// Kern 0: Aufträge
// ---------------------------------------------------------------------------
void adcStarten(ADS1256::DrdyCallback cb) {
  // SPI0 auf die vorgegebenen Pins legen (nur Konfiguration, SPI.begin() läuft auf Kern 1)
  SPI.setSCK(PIN_SPI_SCK);
  SPI.setTX(PIN_SPI_MOSI);
  SPI.setRX(PIN_SPI_MISO);
  ads.setDrdyCallback(cb);
  initADS();                       // läuft auf Kern 1
}

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
  liveZeitNeustart();
  messTimeoutNeustart(true);

  meld.printf("# ADS1256 bereit: PGA %u, %.1f SPS, Messbereich +-%.3f mV, 1 Count = %.4f nV\n",
              ads.gain(), ads.sampleRate(), ads.fullScaleMicrovolts() / 1000.0,
              ads.microvoltsPerCount() * 1000.0);
  meld.printf("# Register geprueft: OK. Dauerbetrieb (RDATAC + DRDY-Interrupt auf Kern 1), SPI %.2f MHz\n",
              ads.spiTakt() / 1e6);
  sendeCfg();
  return true;
}

// Datenrate setzen (auf Kern 1; Dauerbetrieb wird angehalten und neu gestartet)
bool setzeRate(uint8_t code, bool melden) {
  bool ok = adcAusfuehren(OP_RATE, code) && adcErgebnis;
  if (ok) cfg.drateCode = code;
  filterReset();
  graphReset();
  webStatReset();
  messTimeoutNeustart();
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

bool adcSelbstkalibrierung() {
  return adcAusfuehren(OP_SELFCAL, 0) && adcErgebnis;   // Kern 1 hält RDATAC kurz an
}

bool adcRegisterLesen(uint8_t regs[ADS1256::REG_COUNT], bool &dauerbetriebOk) {
  if (!adcAusfuehren(OP_REG, 0)) return false;          // Kern 1 hält RDATAC kurz an
  memcpy(regs, adcRegs, sizeof(adcRegs));
  dauerbetriebOk = adcErgebnis;
  return true;
}

// Blocklänge für die 10-SPS-Mittelung (erst ab 20 SPS sinnvoll)
uint32_t blockLaenge10() {
  float sps = ads.sampleRate();
  return (sps >= 20.0f) ? (uint32_t)lroundf(sps / 10.0f) : 0;
}

// Messanzahl für eine gewünschte Dauer bei der aktuellen Datenrate
uint32_t standardN(float sekunden, uint32_t minimum) {
  uint32_t n = (uint32_t)lroundf(ads.sampleRate() * sekunden);
  return (n < minimum) ? minimum : n;
}
