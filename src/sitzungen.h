/*
 * sitzungen.h - Sitzungen und Pressungen im Flash (LittleFS, nur Kern 0)
 *
 * Aufbau im Dateisystem:
 *   /aktiv              Nummer der aktiven Sitzung (Text)
 *   /s/<id>/meta.bin    SitzungMeta
 *   /s/<id>/index.bin   PressKopf je Pressung, hintereinander (schnelle Übersicht)
 *   /s/<id>/pNNNN.bin   PressKopf + Punkte einer Pressung (<= 4 KB = ein Flash-Block)
 *
 * Flash schreiben hält Kern 1 an (ADC-Interrupt und Schrittzählung!). Alle
 * schreibenden Funktionen verweigern daher, solange der Antrieb fährt.
 *
 * Binärformat (little-endian) wird von der Pressseite (web/seiten/presse.h) per
 * DataView gelesen - Änderungen dort nachziehen und PRESS_VERSION erhöhen.
 */
#pragma once

#include <Arduino.h>

constexpr uint16_t PRESSE_PUNKTE_MAX = 960;      // Kopf + Punkte passen in einen 4-KB-Block
constexpr float    KRAFT_N_PRO_STUFE = 0.02f;    // Auflösung der gespeicherten Kurve
constexpr int16_t  PUNKT_LEER        = INT16_MIN; // Wegabschnitt ohne Messwert
constexpr uint32_t PRESS_MAGIC       = 0x314B5250UL;   // "PRK1"
constexpr uint16_t PRESS_VERSION     = 1;

enum PressStatus : uint8_t {
  PRESS_OK = 0,           // bis zum Endpunkt bzw. zur Maximalkraft gepresst und zurückgefahren
  PRESS_ABBRUCH = 1,      // abgebrochen (Stopp, Taster, Alarm, Messwerte)
  PRESS_KRAFT_ANFAHRT = 2 // Maximalkraft schon im Eilgang (Anfahrpunkt zu tief?)
};

// Kopf einer Pressung (128 Byte). Wege in mm relativ zum Werkzeug-Endpunkt (+ = tiefer).
struct PressKopf {
  uint32_t magic;
  uint16_t version;
  uint16_t punkte;          // Anzahl Punkte in der Datei
  uint32_t nr;              // laufende Nummer in der Sitzung (ab 1)
  uint32_t zeit;            // Unix-Zeit (0 = unbekannt)
  uint8_t  endGrund;        // antrieb::Stopp beim Ende des Pressens
  uint8_t  status;          // PressStatus
  uint8_t  kontakt;         // 1 = Kontakt erkannt
  uint8_t  simuliert;       // 1 = Simulation ohne Motor (Kraft berechnet)
  float    startMm;         // Weg von Punkt 0 (Mitte des Abschnitts)
  float    punktMm;         // Weg je Punkt
  float    kontaktMm, endMm, tiefeMm;
  float    fmaxN, fmaxMm, fendN, fhaltN;
  float    fmittelN, fsigmaN, fminN, fspanneN;   // ab Kontakt bis Endpunkt
  float    arbeitJ, presszeitS, zykluszeitS;
  uint32_t proben, verloren;
  // Einstellungen beim Pressen
  float    tara, faktor, schritteProMm, sps;
  int32_t  kalTiefe, kalEnde;
  float    vPressMmS, anfahrtMm;
  uint32_t haltMs;
};
static_assert(sizeof(PressKopf) == 128, "PressKopf muss 128 Byte gross sein");
// Diese Lagen liest die Pressseite per DataView (kopf() in web/seiten/presse.h)
static_assert(offsetof(PressKopf, punkte) == 6 && offsetof(PressKopf, nr) == 8 &&
              offsetof(PressKopf, zeit) == 12 && offsetof(PressKopf, endGrund) == 16 &&
              offsetof(PressKopf, status) == 17 && offsetof(PressKopf, kontakt) == 18 &&
              offsetof(PressKopf, simuliert) == 19 && offsetof(PressKopf, startMm) == 20 &&
              offsetof(PressKopf, punktMm) == 24 && offsetof(PressKopf, kontaktMm) == 28 &&
              offsetof(PressKopf, endMm) == 32 && offsetof(PressKopf, tiefeMm) == 36 &&
              offsetof(PressKopf, fmaxN) == 40 && offsetof(PressKopf, fmaxMm) == 44 &&
              offsetof(PressKopf, fendN) == 48 && offsetof(PressKopf, fhaltN) == 52 &&
              offsetof(PressKopf, fmittelN) == 56 && offsetof(PressKopf, fsigmaN) == 60 &&
              offsetof(PressKopf, fminN) == 64 && offsetof(PressKopf, fspanneN) == 68 &&
              offsetof(PressKopf, arbeitJ) == 72 && offsetof(PressKopf, presszeitS) == 76 &&
              offsetof(PressKopf, zykluszeitS) == 80 && offsetof(PressKopf, proben) == 84 &&
              offsetof(PressKopf, verloren) == 88 && offsetof(PressKopf, sps) == 104 &&
              offsetof(PressKopf, vPressMmS) == 116 && offsetof(PressKopf, anfahrtMm) == 120 &&
              offsetof(PressKopf, haltMs) == 124,
              "PressKopf-Lagen passen nicht mehr zu kopf() in web/seiten/presse.h");

struct PressPunkt {
  int16_t  kraft;           // in KRAFT_N_PRO_STUFE, PUNKT_LEER = kein Wert
  uint16_t tMs;             // Zeit seit Beginn des Pressens
};
static_assert(sizeof(PressPunkt) == 4, "PressPunkt muss 4 Byte gross sein");

struct SitzungMeta {
  uint32_t magic;
  uint16_t version;
  uint16_t id;
  uint32_t erstellt;        // Unix-Zeit (0 = unbekannt)
  char     name[48];
  char     notiz[96];
};

// ----- Start, Uhr -----
bool     sitzungenStarten();                      // LittleFS einbinden, aktive Sitzung laden
bool     dateisystemOk();                         // LittleFS eingebunden
void     zeitSetzen(uint32_t unix, int32_t tzMinuten);   // vom Browser
uint32_t zeitJetzt();                             // Unix-Zeit, 0 = unbekannt
int32_t  zeitZoneMinuten();
void     zeitText(uint32_t unix, char *puf, size_t n);  // "2026-11-14 09:30" (Ortszeit) bzw. "-"

// ----- aktive Sitzung -----
uint16_t           sitzungAktiv();                // 0 = keine
const SitzungMeta &sitzungAktivMeta();
uint32_t           sitzungAktivAnzahl();          // Pressungen in der aktiven Sitzung
bool sitzungPresseSpeichern(PressKopf &k, const PressPunkt *punkte, uint16_t n);  // setzt k.nr

// ----- Verwaltung (Rückgabe false: Meldung schon ausgegeben) -----
uint16_t sitzungNeu(const char *name, const char *notiz);   // legt an und aktiviert, 0 = Fehler
bool     sitzungWaehlen(uint16_t id);
void     sitzungSchliessen();
bool     sitzungAendern(uint16_t id, const char *name, const char *notiz);
bool     sitzungLoeschen(uint16_t id);
bool     sitzungMetaLesen(uint16_t id, SitzungMeta &m, uint32_t &anzahl);
// Alle Sitzungen (aufsteigend nach id)
void     sitzungenAuflisten(void (*cb)(const SitzungMeta &m, uint32_t anzahl, void *ctx), void *ctx);
void     speicherBelegung(uint32_t &belegt, uint32_t &gesamt);
bool     speicherReichtFuerPresse();
void     sitzungPfad(char *puf, size_t n, uint16_t id, const char *datei);   // "/s/0007/<datei>"
void     pressePfad(char *puf, size_t n, uint16_t id, uint32_t nr);         // "/s/0007/p0012.bin"
void     sitzungenInfo();                         // Befehl 'sit'
