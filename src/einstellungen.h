/*
 * einstellungen.h - Im Flash gespeicherte Einstellungen (EEPROM-Emulation, 256 Byte)
 *
 *   Adresse  0: Einstellungen (32 Byte, Magic "WLZ1")
 *   Adresse 64: MotorEinst    (64 Byte, Magic "MOT1")
 *   Adresse 128: PresseEinst  (32 Byte, Magic "PRS1")
 *
 * Layout ändern: Größe über die reservierten Bytes halten (static_assert) oder die
 * Version erhöhen. Passt Magic, Version oder CRC nicht, gelten die Standardwerte.
 */
#pragma once

#include <Arduino.h>

constexpr uint16_t FILTER_MAX_MS      = 60000;   // längste Glättung: 60 s
constexpr uint16_t FILTER_STANDARD_MS = 200;     // Standard: 0,2 s

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

// Antrieb-Einstellungen im Flash (eigener Block ab Adresse 64, 64 Byte)
struct MotorEinst {
  uint32_t magic;
  uint32_t version;
  float    schritteProMm;   // 0 = nicht eingerichtet -> keine Bewegung
  float    vSuchMmS;        // Kontaktsuche und Überlauf
  float    vFahrtMmS;       // Tippbetrieb und Rückfahrt
  float    accMmS2;
  float    hubMm;           // maximaler Suchweg
  float    kontaktG;        // Laständerung, die als Kontakt gilt
  float    maxKraftG;       // Abwärtsfahrt stoppt sofort ab dieser Kraft (max. Nennlast)
  int32_t  ueberSchritte;   // Überlauf nach dem Kontakt
  uint8_t  dirAb;           // DIR-Pegel für "ab"
  uint8_t  invertiert;      // PUL/DIR/ENA invertiert
  uint8_t  enaModus;        // 0 aus, 1 Opto an = Motor aus, 2 Opto an = Motor ein
  uint8_t  almModus;        // 0 aus, 1 Alarm bei LOW, 2 Alarm bei HIGH
  uint8_t  kalGueltig;      // Ergebnis der letzten Endpunkt-Kalibrierung vorhanden
  uint8_t  kalGrund;        // 1 = Überlauf erreicht, 2 = Maximalkraft erreicht
  uint8_t  reserviert[2];
  int32_t  kalTiefe;        // Schritte vom Start bis zum Kontakt
  int32_t  kalEnde;         // Schritte vom Kontakt bis zum Endpunkt
  float    kalKraftG;       // Kraft am Endpunkt
  uint32_t crc;
};
static_assert(sizeof(MotorEinst) == 64, "MotorEinst muss 64 Byte gross sein");

// Pressvorgang (eigener Block ab Adresse 128, 32 Byte)
struct PresseEinst {
  uint32_t magic;
  uint32_t version;
  float    anfahrtMm;       // Eilgang bis so viele mm über dem Werkzeug-Endpunkt, danach Pressgeschwindigkeit
  float    vPressMmS;       // Pressgeschwindigkeit
  uint32_t haltMs;          // Haltezeit am Endpunkt
  uint8_t  reserviert[8];
  uint32_t crc;
};
static_assert(sizeof(PresseEinst) == 32, "PresseEinst muss 32 Byte gross sein");

extern Einstellungen cfg;
extern MotorEinst    mot;
extern PresseEinst   pres;

void standardEinstellungen();
void motorStandard();
void presseStandard();
void einstellungenStarten();        // EEPROM öffnen, alle Blöcke laden bzw. Standardwerte, melden
bool speichereEinstellungen();      // alle Blöcke mit CRC schreiben
