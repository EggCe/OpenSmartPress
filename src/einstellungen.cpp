#include "einstellungen.h"

#include <EEPROM.h>
#include <LittleFS.h>
#include <math.h>
#include "konfig.h"
#include "meldung.h"
#include "pruefsumme.h"
#include "sitzungen.h"
#include "usb_ausgabe.h"
#include "verarbeitung.h"
#include "watchdog.h"
#include "treiber/ADS1256.h"
#include "treiber/Antrieb.h"

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

// Datei im LittleFS: die drei Blöcke hintereinander (wie im EEPROM, ohne Lücken)
static const char *const EINST_DATEI     = "/einst.bin";
static const char *const EINST_DATEI_NEU = "/einst.neu";   // wird geschrieben und dann umbenannt
struct EinstDatei {
  Einstellungen e;
  MotorEinst    m;
  PresseEinst   p;
};
static_assert(sizeof(EinstDatei) == 128, "EinstDatei muss 128 Byte gross sein");

Einstellungen cfg;
MotorEinst    mot;
PresseEinst   pres;

// ===========================================================================
// Standardwerte und Prüfung
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

static bool gueltig(const Einstellungen &e) {
  return e.magic == EINST_MAGIC && e.version == EINST_VERSION &&
         e.crc == crc32((const uint8_t *)&e, sizeof(e) - sizeof(e.crc)) &&
         isfinite(e.tara) && isfinite(e.faktor) && e.faktor != 0.0f &&
         e.filterMs <= FILTER_MAX_MS &&
         ADS1256::rateFromCode(e.drateCode) > 0.0f && e.modus <= MODUS_BINAER;
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

static bool gueltig(const MotorEinst &e) {
  return e.magic == MOTOR_MAGIC && e.version == MOTOR_VERSION &&
         e.crc == crc32((const uint8_t *)&e, sizeof(e) - sizeof(e.crc)) &&
         isfinite(e.schritteProMm) && e.schritteProMm >= 0.0f &&
         isfinite(e.vSuchMmS) && isfinite(e.vFahrtMmS) && isfinite(e.accMmS2) &&
         e.hubMm > 0.0f && e.hubMm <= 45.0f && e.maxKraftG > 0.0f && e.maxKraftG <= NENNLAST_G &&
         e.dirAb <= 1 && e.invertiert <= 1 && e.enaModus <= 2 && e.almModus <= 2 &&
         e.kalRueck100 <= (uint16_t)lroundf(KAL_RUECK_MAX_MM * 100.0f);
}

void presseStandard() {
  memset(&pres, 0, sizeof(pres));
  pres.magic     = PRESSE_MAGIC;
  pres.version   = PRESSE_VERSION;
  pres.anfahrtMm = PRESSWEG_STANDARD_MM;   // 5 mm pressen, darüber Eilgang (einstellbar bis PRESSWEG_MAX_MM)
  pres.vPressMmS = 1.0f;
  pres.haltMs    = 0;
  pres.vAnfahrtMmS = PRESSE_V_STANDARD_MMS;
  pres.vRueckMmS   = PRESSE_V_STANDARD_MMS;
}

// 0 = Block aus älterer Firmware (Reservebytes), sonst im erlaubten Bereich
static bool vGueltig(float v) {
  return v == 0.0f || (isfinite(v) && v >= PRESSE_V_MIN_MMS && v <= PRESSE_V_MAX_MMS);
}

static bool gueltig(const PresseEinst &e) {
  return e.magic == PRESSE_MAGIC && e.version == PRESSE_VERSION &&
         e.crc == crc32((const uint8_t *)&e, sizeof(e) - sizeof(e.crc)) &&
         isfinite(e.anfahrtMm) && e.anfahrtMm >= 0.0f && e.anfahrtMm <= 45.0f &&
         isfinite(e.vPressMmS) && e.vPressMmS >= 0.01f && e.vPressMmS <= 20.0f &&
         e.haltMs <= 60000 && vGueltig(e.vAnfahrtMmS) && vGueltig(e.vRueckMmS);
}

// ===========================================================================
// Laden
// ===========================================================================
static bool dateiLesen(const char *pfad, EinstDatei &d) {
  File f = LittleFS.open(pfad, "r");
  if (!f) return false;
  const bool ok = f.read((uint8_t *)&d, sizeof(d)) == sizeof(d);
  f.close();
  return ok;
}

// Block aus der Datei (falls gültig), sonst aus dem alten EEPROM-Bereich, sonst false
template <typename T>
static bool blockLaden(T &ziel, bool dateiDa, const T &ausDatei, int eepromAdr, bool &ausEeprom) {
  if (dateiDa && gueltig(ausDatei)) { ziel = ausDatei; return true; }
  T e;
  EEPROM.get(eepromAdr, e);
  if (!gueltig(e)) return false;
  ziel = e;
  ausEeprom = true;
  return true;
}

void einstellungenStarten() {
  // Ältere Firmware speicherte im EEPROM-Sektor; er bleibt als Rückfallebene lesbar.
  EEPROM.begin(EEPROM_GROESSE);
  EinstDatei d;
  memset(&d, 0, sizeof(d));
  // Normalfall /einst.bin. /einst.neu nur, falls der Strom beim allerersten Speichern
  // zwischen Schreiben und Umbenennen ausfiel (unvollständig -> CRC passt nicht).
  const bool dateiDa = dateisystemOk() && (dateiLesen(EINST_DATEI, d) || dateiLesen(EINST_DATEI_NEU, d));
  bool ausEeprom = false;

  if (blockLaden(cfg, dateiDa, d.e, EINST_EEPROM_ADR, ausEeprom)) {
    meld.printf("# Einstellungen geladen: Tara %.1f Counts, Faktor %.5f Counts/g%s\n",
                cfg.tara, cfg.faktor, cfg.kalibriert ? "" : " (unkalibriert)");
  } else {
    standardEinstellungen();
    meld.println(F("# Keine gueltigen Einstellungen im Flash - Standardwerte aktiv (unkalibriert)."));
  }
  // Binärmodus nicht automatisch starten (unlesbar im seriellen Monitor);
  // das PC-Programm schaltet ihn selbst ein.
  if (cfg.modus == MODUS_BINAER) cfg.modus = MODUS_LESBAR;

  if (blockLaden(mot, dateiDa, d.m, MOTOR_EEPROM_ADR, ausEeprom)) {
    meld.printf("# Antrieb-Einstellungen geladen: %.3f Schritte/mm%s\n", mot.schritteProMm,
                mot.schritteProMm > 0.0f ? "" : " (nicht eingerichtet: 'mp spmm <wert>')");
  } else {
    motorStandard();
    meld.println(F("# Antrieb: Standardwerte, nicht eingerichtet ('mp' zeigt die Parameter)."));
  }

  if (blockLaden(pres, dateiDa, d.p, PRESSE_EEPROM_ADR, ausEeprom)) {
    if (pres.anfahrtMm > PRESSWEG_MAX_MM) pres.anfahrtMm = PRESSWEG_MAX_MM;   // alte Werte (bis 45 mm) begrenzen
    if (pres.vAnfahrtMmS == 0.0f) pres.vAnfahrtMmS = PRESSE_V_STANDARD_MMS;  // ältere Blöcke: Reservebytes
    if (pres.vRueckMmS == 0.0f)   pres.vRueckMmS   = PRESSE_V_STANDARD_MMS;
  } else {
    presseStandard();
    meld.println(F("# Pressvorgang: Standardwerte (Pressen ab 5 mm, 1 mm/s, An-/Rueckfahrt 3 mm/s, keine Haltezeit)."));
  }

  if (ausEeprom && dateisystemOk()) {
    meld.println(F("# Einstellungen stammen aus dem alten EEPROM-Bereich - mit 's' stromausfallsicher in /einst.bin speichern."));
  }
}

// ===========================================================================
// Speichern
// ===========================================================================
// Mit Dateisystem: /einst.neu vollständig schreiben, zurücklesen und dann auf /einst.bin
// umbenennen. LittleFS ersetzt das Ziel dabei in einem Schritt - ein Stromausfall hinterlässt
// immer die alte oder die neue, aber nie eine halbe Fassung. (Der EEPROM-Sektor dagegen wird
// gelöscht und neu beschrieben; fällt dazwischen der Strom aus, sind alle Blöcke weg.)
static bool dateiSchreiben() {
  const EinstDatei d = {cfg, mot, pres};
  File f = LittleFS.open(EINST_DATEI_NEU, "w");
  if (!f) return false;
  const bool geschrieben = f.write((const uint8_t *)&d, sizeof(d)) == sizeof(d);
  f.close();
  EinstDatei probe;
  if (!geschrieben || !dateiLesen(EINST_DATEI_NEU, probe) || memcmp(&probe, &d, sizeof(d)) != 0) return false;
  return LittleFS.rename(EINST_DATEI_NEU, EINST_DATEI);
}

bool speichereEinstellungen() {
  // Flash schreiben hält Kern 1 an (ADC-Interrupt, Schrittzählung): nie während einer Fahrt
  if (antrieb::beschaeftigt()) return false;
  cfg.crc  = crc32((const uint8_t *)&cfg, sizeof(cfg) - sizeof(cfg.crc));
  mot.crc  = crc32((const uint8_t *)&mot, sizeof(mot) - sizeof(mot.crc));
  pres.crc = crc32((const uint8_t *)&pres, sizeof(pres) - sizeof(pres.crc));
  // Flash-Schreiben sperrt die Interrupts und hält Kern 1 kurz an. Die dabei
  // verpassten Wandlungen erkennt der Treiber am Zeitstempel (Lücke in seq).
  bool ok;
  if (dateisystemOk()) {
    ok = dateiSchreiben();
  } else {                         // ohne Dateisystem (falsche Flash-Aufteilung): EEPROM-Sektor
    EEPROM.put(EINST_EEPROM_ADR, cfg);
    EEPROM.put(MOTOR_EEPROM_ADR, mot);
    EEPROM.put(PRESSE_EEPROM_ADR, pres);
    ok = EEPROM.commit();
  }
  watchdogWarten();                // Kern 1 war während des Schreibens angehalten
  messTimeoutNeustart();
  return ok;
}
