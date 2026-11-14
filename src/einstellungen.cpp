#include "einstellungen.h"

#include <EEPROM.h>
#include <math.h>
#include "konfig.h"
#include "meldung.h"
#include "pruefsumme.h"
#include "usb_ausgabe.h"
#include "verarbeitung.h"
#include "watchdog.h"
#include "treiber/ADS1256.h"

static constexpr uint32_t EINST_MAGIC      = 0x315A4C57UL;   // "WLZ1"
static constexpr uint32_t EINST_VERSION    = 1;
static constexpr int      EINST_EEPROM_ADR = 0;
static constexpr uint32_t MOTOR_MAGIC      = 0x31544F4DUL;   // "MOT1"
static constexpr uint32_t MOTOR_VERSION    = 1;
static constexpr int      MOTOR_EEPROM_ADR = 64;
static constexpr uint32_t PRESSE_MAGIC     = 0x31535250UL;   // "PRS1"
static constexpr uint32_t PRESSE_VERSION   = 1;
static constexpr int      PRESSE_EEPROM_ADR = 128;
static constexpr size_t   EEPROM_GROESSE   = 256;

Einstellungen cfg;
MotorEinst    mot;
PresseEinst   pres;

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

static bool ladeEinstellungen() {
  Einstellungen e;
  EEPROM.get(EINST_EEPROM_ADR, e);
  bool ok = e.magic == EINST_MAGIC && e.version == EINST_VERSION &&
            e.crc == crc32((const uint8_t *)&e, sizeof(e) - sizeof(e.crc)) &&
            isfinite(e.tara) && isfinite(e.faktor) && e.faktor != 0.0f &&
            e.filterMs <= FILTER_MAX_MS &&
            ADS1256::rateFromCode(e.drateCode) > 0.0f && e.modus <= MODUS_BINAER;
  if (ok) memcpy(&cfg, &e, sizeof(cfg));
  return ok;
}

void motorStandard() {
  memset(&mot, 0, sizeof(mot));
  mot.magic         = MOTOR_MAGIC;
  mot.version       = MOTOR_VERSION;
  mot.schritteProMm = 0.0f;          // muss eingestellt werden ('mp spmm')
  mot.vSuchMmS      = 1.0f;
  mot.vFahrtMmS     = 5.0f;
  mot.accMmS2       = 20.0f;
  mot.hubMm         = 45.0f;
  mot.kontaktG      = 100.0f;
  mot.maxKraftG     = 50000.0f;
  mot.ueberSchritte = 10;
  mot.dirAb         = 1;
  mot.invertiert    = 0;
  mot.enaModus      = 0;
  mot.almModus      = 0;
}

static bool ladeMotor() {
  MotorEinst e;
  EEPROM.get(MOTOR_EEPROM_ADR, e);
  bool ok = e.magic == MOTOR_MAGIC && e.version == MOTOR_VERSION &&
            e.crc == crc32((const uint8_t *)&e, sizeof(e) - sizeof(e.crc)) &&
            isfinite(e.schritteProMm) && e.schritteProMm >= 0.0f &&
            isfinite(e.vSuchMmS) && isfinite(e.vFahrtMmS) && isfinite(e.accMmS2) &&
            e.hubMm > 0.0f && e.hubMm <= 45.0f && e.maxKraftG > 0.0f && e.maxKraftG <= NENNLAST_G &&
            e.dirAb <= 1 && e.invertiert <= 1 && e.enaModus <= 2 && e.almModus <= 2;
  if (ok) memcpy(&mot, &e, sizeof(mot));
  return ok;
}

void presseStandard() {
  memset(&pres, 0, sizeof(pres));
  pres.magic     = PRESSE_MAGIC;
  pres.version   = PRESSE_VERSION;
  pres.anfahrtMm = 45.0f;            // ganzer Hub: kein Eilgang, alles mit Pressgeschwindigkeit
  pres.vPressMmS = 1.0f;
  pres.haltMs    = 0;
}

static bool ladePresse() {
  PresseEinst e;
  EEPROM.get(PRESSE_EEPROM_ADR, e);
  bool ok = e.magic == PRESSE_MAGIC && e.version == PRESSE_VERSION &&
            e.crc == crc32((const uint8_t *)&e, sizeof(e) - sizeof(e.crc)) &&
            isfinite(e.anfahrtMm) && e.anfahrtMm >= 0.0f && e.anfahrtMm <= 45.0f &&
            isfinite(e.vPressMmS) && e.vPressMmS >= 0.01f && e.vPressMmS <= 20.0f &&
            e.haltMs <= 60000;
  if (ok) memcpy(&pres, &e, sizeof(pres));
  return ok;
}

void einstellungenStarten() {
  EEPROM.begin(EEPROM_GROESSE);
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

  if (ladeMotor()) {
    meld.printf("# Antrieb-Einstellungen geladen: %.3f Schritte/mm%s\n", mot.schritteProMm,
                mot.schritteProMm > 0.0f ? "" : " (nicht eingerichtet: 'mp spmm <wert>')");
  } else {
    motorStandard();
    meld.println(F("# Antrieb: Standardwerte, nicht eingerichtet ('mp' zeigt die Parameter)."));
  }

  if (!ladePresse()) {
    presseStandard();
    meld.println(F("# Pressvorgang: Standardwerte (kein Eilgang, 1 mm/s, keine Haltezeit)."));
  }
}

bool speichereEinstellungen() {
  cfg.crc = crc32((const uint8_t *)&cfg, sizeof(cfg) - sizeof(cfg.crc));
  EEPROM.put(EINST_EEPROM_ADR, cfg);
  mot.crc = crc32((const uint8_t *)&mot, sizeof(mot) - sizeof(mot.crc));
  EEPROM.put(MOTOR_EEPROM_ADR, mot);
  pres.crc = crc32((const uint8_t *)&pres, sizeof(pres) - sizeof(pres.crc));
  EEPROM.put(PRESSE_EEPROM_ADR, pres);
  // Flash-Schreiben sperrt die Interrupts und hält Kern 1 kurz an. Die dabei
  // verpassten Wandlungen erkennt der Treiber am Zeitstempel (Lücke in seq).
  bool ok = EEPROM.commit();
  watchdogWarten();                // Kern 1 war während des Schreibens angehalten
  messTimeoutNeustart();
  return ok;
}
