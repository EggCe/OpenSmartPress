/*
 * ADS1256.cpp - Treiber für den ADS1256, siehe ADS1256.h
 */
#include "ADS1256.h"
#include <hardware/gpio.h>
#include <hardware/irq.h>
#include <hardware/sync.h>
#include <hardware/structs/iobank0.h>
#include <hardware/timer.h>

// Das Objekt mit aktivem DRDY-Interrupt (es gibt nur einen ADS1256)
static ADS1256 *isrInstanz = nullptr;
static bool     isrHandlerDa = false;   // GPIO-Handler auf Kern 1 eingetragen

// Warten ohne Funktionsaufruf in den Flash (aus dem Interrupt): mindestens 'us' Mikrosekunden
static inline __attribute__((always_inline)) void warteUs(uint32_t us) {
  const uint32_t t0 = time_us_32();
  while (time_us_32() - t0 <= us) { }
}

// 3 Bytes lesen (0x00 senden) direkt über die SPI-Register: Takt, Modus und 8 Bit hat die
// letzte beginTransaction() schon eingestellt. Bytes ohne Pause hintereinander (FIFO 8 tief).
static inline __attribute__((always_inline)) void spiLesen3(spi_inst_t *spi, uint8_t *rx) {
  spi_hw_t *hw = spi_get_hw(spi);
  while (hw->sr & SPI_SSPSR_RNE_BITS) (void)hw->dr;          // alte Empfangsdaten verwerfen
  for (int i = 0; i < 3; i++) {
    while (!(hw->sr & SPI_SSPSR_TNF_BITS)) { }
    hw->dr = 0x00;
  }
  for (int i = 0; i < 3; i++) {
    while (!(hw->sr & SPI_SSPSR_RNE_BITS)) { }
    rx[i] = (uint8_t)hw->dr;
  }
}

// Datenraten für 7,68-MHz-Quarz
const ADS1256::RateEntry ADS1256::RATES[] = {
  {2.5f, DR_2_5}, {5.0f, DR_5}, {10.0f, DR_10}, {15.0f, DR_15},
  {25.0f, DR_25}, {30.0f, DR_30}, {50.0f, DR_50}, {60.0f, DR_60},
  {100.0f, DR_100}, {500.0f, DR_500}, {1000.0f, DR_1000}, {2000.0f, DR_2000},
  {3750.0f, DR_3750}, {7500.0f, DR_7500}, {15000.0f, DR_15000}, {30000.0f, DR_30000}
};

// Sendedaten beim Lesen: 0x00 ist im RDATAC-Modus harmlos (weder SDATAC noch RESET)
static const uint8_t NULLEN[3] = {0x00, 0x00, 0x00};

ADS1256::ADS1256(SPIClass &spi, uint8_t pinCS, uint8_t pinDRDY, int8_t pinPDWN, float vref, uint32_t spiTakt)
  : _spi(spi),
    _spiSettings(spiTakt, MSBFIRST, SPI_MODE1),   // CPOL 0, CPHA 1
    _spiTakt(spiTakt),
    _pinCS(pinCS), _pinDRDY(pinDRDY), _pinPDWN(pinPDWN), _vref(vref) {}

// ---------------------------------------------------------------------------
// Low-Level-SPI (ohne Pause-Schutz)
// ---------------------------------------------------------------------------
void ADS1256::select() {
  _spi.beginTransaction(_spiSettings);
  gpio_put(_pinCS, 0);
}

void ADS1256::deselect() {
  delayMicroseconds(T10_US);       // t10: CS erst nach 8 tCLKIN anheben
  gpio_put(_pinCS, 1);
  _spi.endTransaction();
  delayMicroseconds(T11_US);       // t11: Mindestabstand zum nächsten Befehl
}

void ADS1256::sendCommandRoh(uint8_t cmd) {
  select();
  _spi.transfer(cmd);
  deselect();
}

void ADS1256::readRegistersRoh(uint8_t start, uint8_t *puffer, uint8_t anzahl) {
  if (anzahl == 0) return;
  select();
  _spi.transfer(CMD_RREG | (start & 0x0F));
  _spi.transfer((uint8_t)(anzahl - 1));
  delayMicroseconds(T6_US);        // t6: 50 tCLKIN warten, bevor Daten getaktet werden
  for (uint8_t i = 0; i < anzahl; i++) puffer[i] = _spi.transfer(0xFF);
  deselect();
}

void ADS1256::writeRegistersRoh(uint8_t start, const uint8_t *daten, uint8_t anzahl) {
  if (anzahl == 0) return;
  select();
  _spi.transfer(CMD_WREG | (start & 0x0F));
  _spi.transfer((uint8_t)(anzahl - 1));
  for (uint8_t i = 0; i < anzahl; i++) _spi.transfer(daten[i]);
  deselect();
}

// ---------------------------------------------------------------------------
// Öffentlicher Registerzugriff: unterbricht ggf. den Dauerbetrieb
// ---------------------------------------------------------------------------
void ADS1256::sendCommand(uint8_t cmd) {
  Pause p(*this);
  sendCommandRoh(cmd);
}

void ADS1256::readRegisters(uint8_t start, uint8_t *puffer, uint8_t anzahl) {
  Pause p(*this);
  readRegistersRoh(start, puffer, anzahl);
}

uint8_t ADS1256::readRegister(uint8_t reg) {
  uint8_t wert = 0;
  readRegisters(reg, &wert, 1);
  return wert;
}

void ADS1256::writeRegisters(uint8_t start, const uint8_t *daten, uint8_t anzahl) {
  Pause p(*this);
  writeRegistersRoh(start, daten, anzahl);
}

void ADS1256::writeRegister(uint8_t reg, uint8_t wert) {
  writeRegisters(reg, &wert, 1);
}

// ---------------------------------------------------------------------------
// Warten auf DRDY
// ---------------------------------------------------------------------------
bool ADS1256::waitDRDY(uint32_t timeoutMs) const {
  uint32_t start = millis();
  while (gpio_get(_pinDRDY)) {
    if (millis() - start >= timeoutMs) return false;
  }
  return true;
}

// Während RESET/SELFCAL/Auto-Kalibrierung liegt DRDY auf HIGH und fällt, sobald
// die Kalibrierung fertig ist und gültige Daten vorliegen.
bool ADS1256::waitCalibrationDone(uint32_t timeoutMs) {
  // Kurz abwarten, bis der Baustein DRDY angehoben hat (max. 3 ms). Bleibt
  // DRDY LOW (z. B. weil keine Kalibrierung ausgelöst wurde), ist das kein Fehler.
  uint32_t start = micros();
  while (!gpio_get(_pinDRDY)) {
    if (micros() - start > 3000) break;
  }
  return waitDRDY(timeoutMs);
}

// ---------------------------------------------------------------------------
// Initialisierung
// ---------------------------------------------------------------------------
ADS1256::Status ADS1256::begin(const Config &cfg) {
  // Läuft noch der Dauerbetrieb: Interrupt aus. RESET wird auch im
  // RDATAC-Modus erkannt, ein SDATAC vorher ist nicht nötig.
  disableDrdyInterrupt();
  _continuous = false;

  if (cfg.pgaCode > 6 || cfg.ainP > AINCOM || cfg.ainN > AINCOM ||
      rateFromCode(cfg.drateCode) == 0.0f) {
    return Status::UNGUELTIGE_KONFIG;
  }
  _cfg = cfg;
  _chipId = 0xFF;
  _hwSpi = (&_spi == &SPI1) ? spi1 : spi0;
  neuePeriode();

  pinMode(_pinCS, OUTPUT);
  gpio_put(_pinCS, 1);
  // DRDY wird vom ADS1256 aktiv (Push-Pull) getrieben -> reiner Eingang
  pinMode(_pinDRDY, INPUT);
  if (_pinPDWN >= 0) {
    pinMode(_pinPDWN, OUTPUT);
    digitalWrite(_pinPDWN, HIGH);  // HIGH = Baustein aktiv
  }

  _spi.begin();
  delay(50);                       // Versorgung und Quarzoszillator einschwingen lassen

  // 1) RESET; der Baustein kalibriert sich danach selbst (30 kSPS, < 1 ms)
  sendCommandRoh(CMD_RESET);
  if (!waitCalibrationDone(1000)) return Status::TIMEOUT_RESET;

  // 2) Kontinuierliches Lesen sicher beenden
  sendCommandRoh(CMD_SDATAC);
  delayMicroseconds(50);

  // 3) Chip-ID prüfen (obere 4 Bit des STATUS-Registers)
  uint8_t st = 0;
  readRegistersRoh(REG_STATUS, &st, 1);
  _chipId = st >> 4;
  if (_chipId != EXPECTED_ID) return Status::FALSCHE_CHIP_ID;

  // 4) Register STATUS, MUX, ADCON, DRATE in einem Zug schreiben
  uint8_t soll[4];
  soll[0] = (uint8_t)((cfg.autoCal ? STATUS_ACAL : 0) | (cfg.buffer ? STATUS_BUFEN : 0)); // MSB zuerst
  soll[1] = (uint8_t)((cfg.ainP << 4) | (cfg.ainN & 0x0F));
  soll[2] = (uint8_t)(cfg.pgaCode & 0x07);   // CLKOUT aus, Sensorerkennung aus, PGA
  soll[3] = cfg.drateCode;
  writeRegistersRoh(REG_STATUS, soll, 4);
  // Mit ACAL startet hier bereits eine Kalibrierung -> erst abwarten
  if (!waitCalibrationDone(CAL_TIMEOUT_MS)) return Status::TIMEOUT_KALIBRIERUNG;

  // 5) Explizite Selbstkalibrierung (Offset + Gain) mit den endgültigen Einstellungen
  sendCommandRoh(CMD_SELFCAL);
  if (!waitCalibrationDone(CAL_TIMEOUT_MS)) return Status::TIMEOUT_KALIBRIERUNG;

  // 6) Register zurücklesen und vergleichen (im STATUS nur ORDER/ACAL/BUFEN)
  uint8_t ist[4];
  readRegistersRoh(REG_STATUS, ist, 4);
  if ((ist[0] & 0x0E) != soll[0] || ist[1] != soll[1] || ist[2] != soll[2] || ist[3] != soll[3]) {
    return Status::REGISTER_FEHLER;
  }

  // Ringpuffer leeren (ISR ist aus, daher gefahrlos), neue Zeitbasis
  _ringEnde = _ringKopf;
  _ringMax = 0;
  _zeitbasisNeu = true;
  clearOverruns();
  return Status::ERFOLG;
}

const char *ADS1256::statusText(Status s) {
  switch (s) {
    case Status::ERFOLG:               return "OK";
    case Status::TIMEOUT_RESET:        return "DRDY-Timeout nach RESET (Versorgung, DRDY-, SCLK-, DIN-, CS-Leitung pruefen)";
    case Status::FALSCHE_CHIP_ID:      return "Falsche Chip-ID (DOUT-Leitung, SPI-Modus, Baustein pruefen)";
    case Status::TIMEOUT_KALIBRIERUNG: return "DRDY-Timeout waehrend der Selbstkalibrierung";
    case Status::REGISTER_FEHLER:      return "Zurueckgelesene Register stimmen nicht mit den Sollwerten ueberein";
    case Status::UNGUELTIGE_KONFIG:    return "Ungueltige Konfiguration (PGA, Eingang oder Datenrate)";
  }
  return "Unbekannter Fehler";
}

// ---------------------------------------------------------------------------
// Dauerbetrieb (RDATAC)
// ---------------------------------------------------------------------------

// Periodendauer und Lückenschwelle (1,5 Perioden) zur aktuellen Datenrate
void ADS1256::neuePeriode() {
  float sps = rateFromCode(_cfg.drateCode);
  if (sps <= 0.0f) sps = 100.0f;
  _periodeNs = (uint32_t)(1.0e9f / sps + 0.5f);
  _lueckeUs  = (uint32_t)((float)_periodeNs * 1.5f / 1000.0f);
  // Verspätung, ab der das Lesen in die nächste Aktualisierung des Datenregisters fallen kann
  const uint32_t periodeUs = _periodeNs / 1000u;
  const uint32_t reserve = (periodeUs / 4u > 20u) ? periodeUs / 4u : 20u;
  _spaetUs = periodeUs + reserve;
}

bool ADS1256::startContinuous() {
  if (_continuous) return true;
  // Timeout: zwei Wandlungsperioden + Reserve (bei 2,5 SPS also ca. 0,85 s)
  const uint32_t timeoutMs = 2u * (_periodeNs / 1000000u) + 50u;

  // RDATAC soll kurz NACH einer fallenden DRDY-Flanke kommen. Dazu erst den
  // anstehenden Wert per RDATA abholen (DRDY geht dabei HIGH) und dann auf
  // die nächste Flanke warten.
  if (!waitDRDY(timeoutMs)) return false;
  select();
  _spi.transfer(CMD_RDATA);
  delayMicroseconds(T6_US);
  uint8_t verwerfen[3];
  _spi.transfer(NULLEN, verwerfen, 3);
  deselect();
  if (!waitDRDY(timeoutMs)) return false;

  // RDATAC senden; der erste Wert folgt nach t6 und wird verworfen
  select();
  _spi.transfer(CMD_RDATAC);
  delayMicroseconds(T6_US);
  _spi.transfer(NULLEN, verwerfen, 3);
  deselect();

  _continuous = true;
  enableDrdyInterrupt();           // löscht dabei alte, gespeicherte Flanken
  return true;
}

void ADS1256::stopContinuous() {
  if (!_continuous) return;
  disableDrdyInterrupt();
  _continuous = false;
  // SDATAC kurz nach einer DRDY-Flanke senden. Bei Timeout trotzdem senden.
  waitDRDY(2u * (_periodeNs / 1000000u) + 50u);
  sendCommandRoh(CMD_SDATAC);
}

bool ADS1256::popSample(Messwert &m) {
  const uint32_t ende = _ringEnde;
  if (_ringKopf == ende) return false;
  __dmb();                          // erst Kopf lesen, dann Daten
  m = _ring[ende & (RING_GROESSE - 1)];
  __dmb();                          // erst Daten kopieren, dann Platz freigeben
  _ringEnde = ende + 1;
  return true;
}

uint32_t ADS1256::available() const {
  return _ringKopf - _ringEnde;
}

void ADS1256::flush() {
  _ringEnde = _ringKopf;           // nur vom Verbraucher (loop) aufrufen
}

void ADS1256::clearOverruns() {
  noInterrupts();
  _verlorenIsr = 0;
  _verlorenVoll = 0;
  _verlorenLuecke = 0;
  interrupts();
}

// ---------------------------------------------------------------------------
// DRDY-Interrupt
// ---------------------------------------------------------------------------
// Geteilter GPIO-Interrupt dieses Kerns: nur auf die eigene fallende DRDY-Flanke reagieren
void __not_in_flash_func(ADS1256::isrRoh)() {
  ADS1256 *a = isrInstanz;
  if (!a) return;
  const uint32_t pin = a->_pinDRDY;
  const uint32_t maske = (uint32_t)GPIO_IRQ_EDGE_FALL << (4u * (pin % 8u));
  io_bank0_irq_ctrl_hw_t *ctrl = get_core_num() ? &io_bank0_hw->proc1_irq_ctrl : &io_bank0_hw->proc0_irq_ctrl;
  if (!(ctrl->ints[pin / 8u] & maske)) return;
  io_bank0_hw->intr[pin / 8u] = maske;                       // Flanke quittieren (wie gpio_acknowledge_irq)
  a->isrLesen();
}

void __not_in_flash_func(ADS1256::isrLesen)() {
  const uint32_t jetzt = time_us_32();                       // gleiche Zeitbasis wie micros()
  if (!_continuous) return;

  // --- Laufende Nummer, Lücken aus dem Zeitabstand erkennen ---
  uint32_t seq = _seq;
  bool spaet = false;
  if (_zeitbasisNeu) {
    _zeitbasisNeu = false;
  } else {
    const uint32_t dt = jetzt - _letzteFlanke;
    spaet = dt > _spaetUs;
    if (dt > _lueckeUs) {
      // Anzahl Perioden seit der letzten Flanke (gerundet), davon ist eine diese. Bis 3,5 s
      // Lücke reicht 32-Bit-Rechnung (Hardware-Teiler); 64 Bit nur für noch längere Lücken.
      const uint32_t perioden = (dt < 3500000u)
          ? (dt * 1000u + _periodeNs / 2u) / _periodeNs
          : (uint32_t)(((uint64_t)dt * 1000u + _periodeNs / 2u) / _periodeNs);
      if (perioden > 1) {
        seq += perioden - 1;
        _verlorenLuecke = _verlorenLuecke + (perioden - 1);
      }
    }
  }
  _letzteFlanke = jetzt;
  _seq = seq + 1;

  // --- Interrupt kam deutlich zu spät (z. B. Kern 1 war beim Flash-Schreiben angehalten):
  //     Der Zeitpunkt im Wandlertakt ist unbekannt, das Lesen könnte in die Aktualisierung
  //     des Datenregisters fallen und einen zerrissenen Wert liefern -> nicht lesen, als
  //     verloren zählen (die Lücke in seq zeigt ihn an) ---
  if (spaet) {
    _verlorenLuecke = _verlorenLuecke + 1;
    return;
  }

  // --- Ist DRDY schon wieder HIGH, steht die nächste Aktualisierung bevor:
  //     Lesen könnte einen zerrissenen Wert liefern -> verwerfen ---
  if (gpio_get(_pinDRDY)) {
    _verlorenIsr = _verlorenIsr + 1;
    return;
  }

  // --- 24 Bit lesen (im RDATAC-Modus ohne Befehl und ohne t6) ---
  uint8_t rx[3];
  gpio_put(_pinCS, 0);
  spiLesen3(_hwSpi, rx);
  warteUs(T10_US);
  gpio_put(_pinCS, 1);
  const int32_t wert = signExtend24(((uint32_t)rx[0] << 16) | ((uint32_t)rx[1] << 8) | rx[2]);

  // --- In den Ringpuffer ---
  const uint32_t kopf = _ringKopf;
  const uint32_t fuellung = kopf - _ringEnde;
  if (fuellung >= RING_GROESSE) {
    _verlorenVoll = _verlorenVoll + 1;
  } else {
    Messwert &m = _ring[kopf & (RING_GROESSE - 1)];
    m.seq = seq;
    m.tMikros = jetzt;
    m.counts = wert;
    __dmb();                        // Daten vor dem Kopf sichtbar machen
    _ringKopf = kopf + 1;
    if (fuellung + 1 > _ringMax) _ringMax = fuellung + 1;
  }

  DrdyCallback cb = _callback;
  if (cb) cb(jetzt, wert);         // z. B. Kraftüberwachung des Antriebs (auch bei vollem Puffer)
}

// Nur auf Kern 1 aufrufen (der Interrupt gehört dem aufrufenden Kern)
void ADS1256::enableDrdyInterrupt() {
  if (_irqEnabled) return;
  isrInstanz = this;
  if (!isrHandlerDa) {
    gpio_add_raw_irq_handler(_pinDRDY, isrRoh);
    isrHandlerDa = true;
  }
  // Höchste Priorität, damit z. B. der PWM-Interrupt des Antriebs das Lesen nicht verzögert
  // (wichtig ab ca. 7500 SPS). Gilt für alle GPIO-Interrupts dieses Kerns.
  irq_set_priority(IO_IRQ_BANK0, PICO_HIGHEST_IRQ_PRIORITY);
  gpio_set_irq_enabled(_pinDRDY, GPIO_IRQ_EDGE_FALL, true);   // löscht dabei alte, gespeicherte Flanken
  irq_set_enabled(IO_IRQ_BANK0, true);
  _irqEnabled = true;
}

void ADS1256::disableDrdyInterrupt() {
  if (!_irqEnabled) return;
  gpio_set_irq_enabled(_pinDRDY, GPIO_IRQ_EDGE_FALL, false);
  _irqEnabled = false;
}

// ---------------------------------------------------------------------------
// Einzelabfrage (nur außerhalb des Dauerbetriebs)
// ---------------------------------------------------------------------------
bool ADS1256::dataReady() const {
  if (_continuous) return available() > 0;
  return !gpio_get(_pinDRDY);
}

int32_t ADS1256::readData(uint32_t *zeitMikros) {
  if (_continuous) return 0;       // im Dauerbetrieb popSample() benutzen
  if (zeitMikros) *zeitMikros = micros();
  select();
  _spi.transfer(CMD_RDATA);
  delayMicroseconds(T6_US);        // t6 einhalten
  uint8_t rx[3];
  _spi.transfer(NULLEN, rx, 3);
  deselect();
  return signExtend24(((uint32_t)rx[0] << 16) | ((uint32_t)rx[1] << 8) | rx[2]);
}

bool ADS1256::isClipped(int32_t counts) {
  // kleiner Sicherheitsabstand (ca. 0,1 %) zum Vollausschlag
  return counts >= CLIP_GRENZE || counts <= -CLIP_GRENZE;
}

// ---------------------------------------------------------------------------
// Konfiguration zur Laufzeit
// ---------------------------------------------------------------------------
bool ADS1256::setDataRate(uint8_t drateCode) {
  if (rateFromCode(drateCode) == 0.0f) return false;
  bool ok;
  {
    Pause p(*this);                 // Dauerbetrieb anhalten, am Blockende neu starten
    writeRegistersRoh(REG_DRATE, &drateCode, 1);
    ok = waitCalibrationDone(CAL_TIMEOUT_MS);   // Auto-Kalibrierung (falls ACAL)
    if (ok && !_cfg.autoCal) {
      sendCommandRoh(CMD_SELFCAL);
      ok = waitCalibrationDone(CAL_TIMEOUT_MS);
    }
    uint8_t ist = 0;
    readRegistersRoh(REG_DRATE, &ist, 1);
    if (ok) ok = (ist == drateCode);
    if (ok) _cfg.drateCode = drateCode;
    neuePeriode();                  // vor dem Neustart, damit die Lückenerkennung stimmt
    _zeitbasisNeu = true;
  }
  flush();                          // Werte mit alter Rate verwerfen
  return ok;
}

bool ADS1256::selfCalibrate() {
  bool ok;
  {
    Pause p(*this);
    sendCommandRoh(CMD_SELFCAL);
    ok = waitCalibrationDone(CAL_TIMEOUT_MS);
  }
  return ok;
}

// ---------------------------------------------------------------------------
// Umrechnungen
// ---------------------------------------------------------------------------
double ADS1256::fullScaleMicrovolts() const {
  return 2.0 * (double)_vref * 1e6 / (double)gain();   // +-2*Vref/PGA
}

double ADS1256::microvoltsPerCount() const {
  return fullScaleMicrovolts() / 8388607.0;            // 0x7FFFFF = +Vollausschlag
}

bool ADS1256::codeFromRate(float sps, uint8_t &code) {
  for (uint8_t i = 0; i < rateCount(); i++) {
    if (fabsf(RATES[i].sps - sps) < 0.01f) { code = RATES[i].code; return true; }
  }
  return false;
}

float ADS1256::rateFromCode(uint8_t code) {
  for (uint8_t i = 0; i < rateCount(); i++) {
    if (RATES[i].code == code) return RATES[i].sps;
  }
  return 0.0f;
}

uint8_t ADS1256::rateCount() { return (uint8_t)(sizeof(RATES) / sizeof(RATES[0])); }

float ADS1256::rateByIndex(uint8_t index) {
  return (index < rateCount()) ? RATES[index].sps : 0.0f;
}
