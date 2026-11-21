#include "sitzungen.h"

#include <LittleFS.h>
#include <time.h>
#include "meldung.h"
#include "verarbeitung.h"
#include "watchdog.h"
#include "treiber/Antrieb.h"

static constexpr uint32_t META_MAGIC   = 0x31544953UL;   // "SIT1"
static constexpr uint16_t META_VERSION = 1;
static constexpr uint32_t RESERVE_BYTES = 16384;         // so viel muss für eine weitere Pressung frei bleiben

static bool        fsOk = false;
static uint16_t    aktivId = 0;
static SitzungMeta aktivMeta;
static uint32_t    aktivAnzahl = 0;

static uint32_t belegtCache = 0, gesamtCache = 0;   // LittleFS.info() durchläuft das ganze Dateisystem

static uint32_t zeitBasis = 0;      // Unix-Zeit bei millis() == 0 (0 = unbekannt)
static int32_t  zeitZone = 0;       // Minuten östlich von UTC

// ===========================================================================
// Hilfsfunktionen
// ===========================================================================
bool dateisystemOk() { return fsOk; }

void sitzungPfad(char *puf, size_t n, uint16_t id, const char *datei) {
  snprintf(puf, n, "/s/%04u/%s", id, datei);
}

void pressePfad(char *puf, size_t n, uint16_t id, uint32_t nr) {
  snprintf(puf, n, "/s/%04u/p%04lu.bin", id, (unsigned long)nr);
}

// Flash schreiben hält Kern 1 an: nur im Stillstand
static bool schreibenErlaubt() {
  if (!fsOk) { meld.println(F("# FEHLER: Dateisystem nicht verfuegbar (Flash-Aufteilung 'FS: 1MB' gewaehlt?).")); return false; }
  if (antrieb::beschaeftigt()) { meld.println(F("# Antrieb faehrt - Speichern erst im Stillstand.")); return false; }
  return true;
}

static void belegungAktualisieren() {
  belegtCache = gesamtCache = 0;
  if (!fsOk) return;
  FSInfo i;
  if (LittleFS.info(i)) { belegtCache = (uint32_t)i.usedBytes; gesamtCache = (uint32_t)i.totalBytes; }
}

// nach jedem Schreibzugriff: Kern 1 war angehalten; Belegung neu bestimmen
static void nachSchreiben() {
  watchdogWarten();
  belegungAktualisieren();
  watchdogWarten();
  messTimeoutNeustart();
}

static void textKopieren(char *ziel, size_t n, const char *quelle) {
  size_t j = 0;
  for (size_t i = 0; quelle && quelle[i] && j + 1 < n; i++) {
    const char c = quelle[i];
    if ((uint8_t)c >= 0x20 && c != '"' && c != '\\' && c != ';') ziel[j++] = c;   // CSV/JSON-sicher
  }
  ziel[j] = '\0';
}

static uint32_t anzahlPressungen(uint16_t id) {
  char p[32];
  sitzungPfad(p, sizeof(p), id, "index.bin");
  File f = LittleFS.open(p, "r");
  if (!f) return 0;
  const uint32_t n = f.size() / sizeof(PressKopf);
  f.close();
  return n;
}

bool sitzungMetaLesen(uint16_t id, SitzungMeta &m, uint32_t &anzahl) {
  if (!fsOk || id == 0) return false;
  char p[32];
  sitzungPfad(p, sizeof(p), id, "meta.bin");
  File f = LittleFS.open(p, "r");
  if (!f) return false;
  const bool ok = f.read((uint8_t *)&m, sizeof(m)) == sizeof(m) && m.magic == META_MAGIC && m.id == id;
  f.close();
  m.name[sizeof(m.name) - 1] = '\0';
  m.notiz[sizeof(m.notiz) - 1] = '\0';
  anzahl = ok ? anzahlPressungen(id) : 0;
  return ok;
}

uint32_t sitzungAnzahlGueltig(uint16_t id) {
  if (!fsOk || id == 0) return 0;
  char p[32];
  sitzungPfad(p, sizeof(p), id, "index.bin");
  File f = LittleFS.open(p, "r");
  if (!f) return 0;
  const uint32_t n = f.size() / sizeof(PressKopf);
  uint32_t g = 0;
  for (uint32_t i = 0; i < n; i++) {                   // nur das Statusbyte jedes Eintrags lesen
    uint8_t s;
    if (!f.seek(i * sizeof(PressKopf) + offsetof(PressKopf, status)) || f.read(&s, 1) != 1) break;
    if (s != PRESS_GELOESCHT) g++;
  }
  f.close();
  return g;
}

static bool metaSchreiben(const SitzungMeta &m) {
  char p[32];
  sitzungPfad(p, sizeof(p), m.id, "meta.bin");
  File f = LittleFS.open(p, "w");
  if (!f) return false;
  const bool ok = f.write((const uint8_t *)&m, sizeof(m)) == sizeof(m);
  f.close();
  return ok;
}

static void aktivSpeichern() {
  File f = LittleFS.open("/aktiv", "w");
  if (f) { f.printf("%u", aktivId); f.close(); }
}

// ===========================================================================
// Start, Uhr
// ===========================================================================
bool sitzungenStarten() {
  fsOk = LittleFS.begin();         // formatiert beim ersten Start selbst
  nachSchreiben();
  if (!fsOk) {
    meld.println(F("# FEHLER: Dateisystem (LittleFS) nicht verfuegbar - Flash-Aufteilung '2MB (Sketch: 1MB, FS: 1MB)' waehlen."));
    return false;
  }
  if (!LittleFS.exists("/s")) LittleFS.mkdir("/s");
  nachSchreiben();                 // auch: Belegung bestimmen

  File f = LittleFS.open("/aktiv", "r");
  if (f) {
    const uint16_t id = (uint16_t)f.readString().toInt();
    f.close();
    uint32_t n = 0;
    if (id && sitzungMetaLesen(id, aktivMeta, n)) { aktivId = id; aktivAnzahl = n; }
  }
  uint32_t belegt = 0, gesamt = 0;
  speicherBelegung(belegt, gesamt);
  if (aktivId) {
    meld.printf("# Sitzung %u '%s' aktiv (%lu Pressungen). Speicher: %lu von %lu KB belegt.\n", aktivId, aktivMeta.name,
                (unsigned long)aktivAnzahl, (unsigned long)(belegt / 1024), (unsigned long)(gesamt / 1024));
  } else {
    meld.printf("# Keine aktive Sitzung. Speicher: %lu von %lu KB belegt.\n",
                (unsigned long)(belegt / 1024), (unsigned long)(gesamt / 1024));
  }
  return true;
}

void zeitSetzen(uint32_t unix, int32_t tzMinuten) {
  if (unix < 1600000000UL) return;                       // offensichtlich ungültig
  zeitBasis = unix - millis() / 1000;
  if (tzMinuten >= -14 * 60 && tzMinuten <= 14 * 60) zeitZone = tzMinuten;
}

uint32_t zeitJetzt() {
  return zeitBasis ? zeitBasis + millis() / 1000 : 0;
}

int32_t zeitZoneMinuten() { return zeitZone; }

void zeitText(uint32_t unix, char *puf, size_t n) {
  if (!unix) { snprintf(puf, n, "-"); return; }
  const time_t t = (time_t)unix + (time_t)zeitZone * 60;
  struct tm z;
  gmtime_r(&t, &z);
  snprintf(puf, n, "%04d-%02d-%02d %02d:%02d", z.tm_year + 1900, z.tm_mon + 1, z.tm_mday, z.tm_hour, z.tm_min);
}

// ===========================================================================
// Speicher
// ===========================================================================
void speicherBelegung(uint32_t &belegt, uint32_t &gesamt) {
  belegt = belegtCache;
  gesamt = gesamtCache;
}

bool speicherReichtFuerPresse() {
  uint32_t b, g;
  speicherBelegung(b, g);
  return fsOk && g > b && g - b >= RESERVE_BYTES;
}

// ===========================================================================
// Aktive Sitzung
// ===========================================================================
uint16_t           sitzungAktiv()       { return aktivId; }
const SitzungMeta &sitzungAktivMeta()   { return aktivMeta; }
uint32_t           sitzungAktivAnzahl() { return aktivAnzahl; }

bool sitzungPresseSpeichern(PressKopf &k, const PressPunkt *punkte, uint16_t n) {
  if (!aktivId) { meld.println(F("# FEHLER: keine aktive Sitzung - Pressung nicht gespeichert.")); return false; }
  if (!schreibenErlaubt()) return false;
  if (n > PRESSE_PUNKTE_MAX) n = PRESSE_PUNKTE_MAX;
  k.magic   = PRESS_MAGIC;
  k.version = PRESS_VERSION;
  k.punkte  = n;
  k.nr      = aktivAnzahl + 1;

  char p[40];
  pressePfad(p, sizeof(p), aktivId, k.nr);
  File f = LittleFS.open(p, "w");
  bool ok = f && f.write((const uint8_t *)&k, sizeof(k)) == sizeof(k) &&
            f.write((const uint8_t *)punkte, n * sizeof(PressPunkt)) == n * sizeof(PressPunkt);
  if (f) f.close();
  if (ok) {
    sitzungPfad(p, sizeof(p), aktivId, "index.bin");
    File ix = LittleFS.open(p, "a");
    ok = ix && ix.write((const uint8_t *)&k, sizeof(k)) == sizeof(k);
    if (ix) ix.close();
  }
  nachSchreiben();
  if (!ok) {
    meld.println(F("# FEHLER: Pressung konnte nicht gespeichert werden (Speicher voll?)."));
    return false;
  }
  aktivAnzahl = k.nr;
  if (!speicherReichtFuerPresse()) {
    meld.println(F("# WARNUNG: Speicher fast voll - Sitzungen exportieren und alte loeschen."));
  }
  return true;
}

// ===========================================================================
// Verwaltung
// ===========================================================================
uint16_t sitzungNeu(const char *name, const char *notiz) {
  if (!schreibenErlaubt()) return 0;
  if (!speicherReichtFuerPresse()) { meld.println(F("# Speicher voll - erst alte Sitzungen loeschen.")); return 0; }
  // naechste freie Nummer
  uint16_t id = 0;
  Dir d = LittleFS.openDir("/s");
  while (d.next()) {
    const long n = d.fileName().toInt();
    if (n > id && n < 9999) id = (uint16_t)n;
  }
  id++;

  SitzungMeta m;
  memset(&m, 0, sizeof(m));
  m.magic    = META_MAGIC;
  m.version  = META_VERSION;
  m.id       = id;
  m.erstellt = zeitJetzt();
  textKopieren(m.name, sizeof(m.name), (name && *name) ? name : "Sitzung");
  textKopieren(m.notiz, sizeof(m.notiz), notiz);

  char p[32];
  snprintf(p, sizeof(p), "/s/%04u", id);
  // Elternordner fehlt (z. B. Dateisystem neu formatiert) oder Ordner existiert schon: kein Fehler
  if (!LittleFS.exists("/s")) LittleFS.mkdir("/s");
  const bool ordnerOk = LittleFS.mkdir(p) || LittleFS.exists(p);
  const bool ok = ordnerOk && metaSchreiben(m);
  if (ok) {
    aktivId = id; aktivMeta = m; aktivAnzahl = 0;
    aktivSpeichern();
  }
  nachSchreiben();
  if (!ok) {
    uint32_t b, g;
    speicherBelegung(b, g);
    meld.printf("# FEHLER: Sitzung %u konnte nicht angelegt werden (%s). Speicher %lu/%lu KB, freier Heap %lu Byte.\n",
                id, ordnerOk ? "meta.bin nicht schreibbar" : "Ordner nicht anlegbar", (unsigned long)(b / 1024),
                (unsigned long)(g / 1024), (unsigned long)rp2040.getFreeHeap());
    return 0;
  }
  meld.printf("# Sitzung %u '%s' angelegt und aktiv.\n", id, m.name);
  return id;
}

bool sitzungWaehlen(uint16_t id) {
  if (!schreibenErlaubt()) return false;
  SitzungMeta m;
  uint32_t n;
  if (!sitzungMetaLesen(id, m, n)) { meld.printf("# Sitzung %u gibt es nicht.\n", id); return false; }
  aktivId = id; aktivMeta = m; aktivAnzahl = n;
  aktivSpeichern();
  nachSchreiben();
  meld.printf("# Sitzung %u '%s' aktiv (%lu Pressungen).\n", id, m.name, (unsigned long)sitzungAnzahlGueltig(id));
  return true;
}

void sitzungSchliessen() {
  if (!aktivId || !schreibenErlaubt()) return;
  aktivId = 0;
  aktivAnzahl = 0;
  LittleFS.remove("/aktiv");
  nachSchreiben();
  meld.println(F("# Sitzung geschlossen."));
}

bool sitzungAendern(uint16_t id, const char *name, const char *notiz) {
  if (!schreibenErlaubt()) return false;
  SitzungMeta m;
  uint32_t n;
  if (!sitzungMetaLesen(id, m, n)) { meld.printf("# Sitzung %u gibt es nicht.\n", id); return false; }
  if (name && *name) textKopieren(m.name, sizeof(m.name), name);
  if (notiz) textKopieren(m.notiz, sizeof(m.notiz), notiz);
  const bool ok = metaSchreiben(m);
  nachSchreiben();
  if (ok && id == aktivId) aktivMeta = m;
  meld.println(ok ? F("# Sitzung geaendert.") : F("# FEHLER: Sitzung konnte nicht geaendert werden."));
  return ok;
}

bool sitzungLoeschen(uint16_t id) {
  if (!schreibenErlaubt()) return false;
  char p[40];
  snprintf(p, sizeof(p), "/s/%04u", id);
  if (!LittleFS.exists(p)) { meld.printf("# Sitzung %u gibt es nicht.\n", id); return false; }
  // jeweils den ersten Eintrag löschen und neu öffnen (Löschen während des Durchlaufs kann Einträge überspringen)
  for (uint32_t n = 0; n < 2000; n++) {
    Dir d = LittleFS.openDir(p);
    if (!d.next()) break;
    char f[64];
    snprintf(f, sizeof(f), "%s/%s", p, d.fileName().c_str());
    if (!LittleFS.remove(f)) break;
    if ((n & 15) == 15) nachSchreiben();         // viele Dateien: Watchdog zwischendurch bedienen
  }
  LittleFS.rmdir(p);
  if (id == aktivId) { aktivId = 0; aktivAnzahl = 0; LittleFS.remove("/aktiv"); }
  nachSchreiben();
  meld.printf("# Sitzung %u geloescht.\n", id);
  return true;
}

bool sitzungPresseLoeschen(uint16_t id, uint32_t nr) {
  if (!schreibenErlaubt()) return false;
  char p[40];
  sitzungPfad(p, sizeof(p), id, "index.bin");
  File ix = LittleFS.open(p, "r+");
  if (!ix) { meld.printf("# Sitzung %u gibt es nicht oder sie hat keine Pressungen.\n", id); return false; }
  const uint32_t n = ix.size() / sizeof(PressKopf);
  PressKopf k;
  int32_t stelle = -1;
  auto lesen = [&](uint32_t i) {
    return ix.seek(i * sizeof(PressKopf)) && ix.read((uint8_t *)&k, sizeof(k)) == sizeof(k) &&
           k.magic == PRESS_MAGIC && k.nr == nr;
  };
  if (nr >= 1 && nr <= n && lesen(nr - 1)) stelle = (int32_t)(nr - 1);   // Normalfall: Eintrag nr-1
  for (uint32_t i = 0; stelle < 0 && i < n; i++) if (lesen(i)) stelle = (int32_t)i;
  if (stelle < 0 || k.status == PRESS_GELOESCHT) {
    ix.close();
    meld.printf("# Pressung %lu gibt es in Sitzung %u nicht (mehr).\n", (unsigned long)nr, id);
    return false;
  }
  k.status = PRESS_GELOESCHT;                           // Grabstein: Nummer bleibt belegt
  k.punkte = 0;
  const bool ok = ix.seek((uint32_t)stelle * sizeof(PressKopf)) && ix.write((const uint8_t *)&k, sizeof(k)) == sizeof(k);
  ix.close();
  if (ok) {
    pressePfad(p, sizeof(p), id, nr);
    LittleFS.remove(p);                                 // Kurve entfernen, gibt den Flash-Block frei
  }
  nachSchreiben();
  if (ok) meld.printf("# Pressung %lu in Sitzung %u geloescht.\n", (unsigned long)nr, id);
  else    meld.printf("# FEHLER: Pressung %lu konnte nicht geloescht werden.\n", (unsigned long)nr);
  return ok;
}

void sitzungenAuflisten(void (*cb)(const SitzungMeta &m, uint32_t anzahl, void *ctx), void *ctx) {
  if (!fsOk) return;
  // Nummern einsammeln und sortieren (Verzeichnisreihenfolge ist nicht garantiert)
  static uint16_t ids[256];
  uint16_t anzahl = 0;
  Dir d = LittleFS.openDir("/s");
  while (d.next() && anzahl < 256) {
    const long n = d.fileName().toInt();
    if (n > 0 && n < 10000) ids[anzahl++] = (uint16_t)n;
  }
  for (uint16_t i = 1; i < anzahl; i++) {
    const uint16_t v = ids[i];
    uint16_t j = i;
    while (j > 0 && ids[j - 1] > v) { ids[j] = ids[j - 1]; j--; }
    ids[j] = v;
  }
  for (uint16_t i = 0; i < anzahl; i++) {
    SitzungMeta m;
    uint32_t n;
    if (sitzungMetaLesen(ids[i], m, n)) cb(m, sitzungAnzahlGueltig(ids[i]), ctx);
  }
}

static void infoZeile(const SitzungMeta &m, uint32_t anzahl, void *) {
  char t[20];
  zeitText(m.erstellt, t, sizeof(t));
  meld.printf("# %c %4u  %-24s %5lu Pressungen  %s\n", m.id == aktivId ? '*' : ' ', m.id, m.name,
              (unsigned long)anzahl, t);
}

void sitzungenInfo() {
  uint32_t b, g;
  speicherBelegung(b, g);
  meld.printf("# ---------------- Sitzungen (Speicher %lu von %lu KB belegt) ----------------\n",
              (unsigned long)(b / 1024), (unsigned long)(g / 1024));
  sitzungenAuflisten(infoZeile, nullptr);
  if (!aktivId) meld.println(F("# Keine aktive Sitzung ('sit neu <Name>' oder 'sit wahl <Nr>')."));
}
