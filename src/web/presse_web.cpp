/*
 * presse_web.cpp - HTTP-Routen der Pressseite (nur Kern 0)
 *
 *   GET  /presse                    Pressseite (seiten/presse.h)
 *   GET  /presse/status?ab=&id=&l=  JSON: Bereitschaft, Antrieb, Sitzung, Live-Kurve ab Punkt ab, neue Log-Zeilen ab l
 *   GET  /presse/roh?id=&ab=        binär: Rohdaten der laufenden/letzten Pressung ab Probe ab (Kopf 48 Byte + RohProbe je 8 Byte)
 *   GET  /sitzungen                 JSON: alle Sitzungen, Speicherbelegung
 *   GET  /sitzung/index?id=         binär: PressKopf je Pressung (128 Byte)
 *   GET  /sitzung/presse?id=&nr=    binär: PressKopf + Punkte
 *   GET  /sitzung/export?id=&f=&e=  CSV (Excel, ';' und Dezimalkomma, Kraft in e = N|kg|lbs) bzw. JSON (immer N)
 *   POST /sitzung  a=neu|wahl|zu|aendern|loeschen, id, name, notiz
 *   POST /zeit     t = Unix-Zeit, tz = Minuten östlich von UTC
 */
#include <WebServer.h>
#include <LittleFS.h>
#include <math.h>
#include "webserver.h"
#include "../konfig.h"
#include "../meldung.h"
#include "../einstellungen.h"
#include "../adc.h"
#include "../filter.h"
#include "../auftraege.h"
#include "../motor.h"
#include "../presse.h"
#include "../sitzungen.h"
#include "../verarbeitung.h"
#include "../watchdog.h"
#include "seiten/presse.h"

static WebServer *srv = nullptr;

// Zahl als JSON (NaN -> null)
static void jsonZahl(const char *name, float v, uint8_t stellen, bool komma = true) {
  if (isfinite(v)) jsonf("\"%s\":%.*f%s", name, stellen, (double)v, komma ? "," : "");
  else             jsonf("\"%s\":null%s", name, komma ? "," : "");
}

static uint16_t argId(const char *name) {
  return srv->hasArg(name) ? (uint16_t)strtoul(srv->arg(name).c_str(), nullptr, 10) : 0;
}

// Bei langen Antworten: Watchdog bedienen und Messwerte abholen (ADC-Ringpuffer reicht ca. 2 s)
static void hintergrund() {
  watchdogFuettern();
  messwerteAbholen();
}

// ===========================================================================
// Seite und Status
// ===========================================================================
static void webPresseSeite() {
  srv->send_P(200, "text/html; charset=utf-8", PRESSE_HTML);
}

static void webPresseStatus() {
  const antrieb::Status s = antrieb::status();
  char grund[80] = "";
  const bool bereit = presseBereit(grund, sizeof(grund));
  const double spmm = (mot.schritteProMm > 0.0f) ? mot.schritteProMm : 1.0;
  const double fa = fabs((double)cfg.faktor);

  jsonStart();
  jsonf("{\"t\":%lu,\"bereit\":%u,\"grund\":\"%s\",\"zust\":\"%s\",\"bes\":%u,\"alarm\":%u,",
        (unsigned long)millis(), bereit ? 1u : 0u, grund, antrieb::zustandText(s.zustand),
        antrieb::beschaeftigt() ? 1u : 0u, s.alarm ? 1u : 0u);
  jsonf("\"ok\":%u,\"rate\":%g,\"kal\":%u,\"auftrag\":%u,\"ref\":%u,\"sim\":%u,",
        adsOk ? 1u : 0u, (double)ads.sampleRate(), (unsigned)cfg.kalibriert, auftragLaeuft() ? 1u : 0u,
        s.referenziert ? 1u : 0u, motorSimulation() ? 1u : 0u);
  jsonZahl("pos", mot.schritteProMm > 0.0f ? (float)(s.position / spmm) : NAN, 3);
  jsonZahl("kraft", adsOk ? (float)(inGramm(filterMittel()) * N_PRO_GRAMM) : NAN, 2);
  jsonZahl("last", (float)(s.lastCounts / fa * N_PRO_GRAMM), 2);
  jsonf("\"end\":{\"akt\":%u,", motorEndpunktAktuell() ? 1u : 0u);
  jsonZahl("tiefe", mot.kalGueltig ? (float)(mot.kalTiefe / spmm) : NAN, 3);
  jsonZahl("ende", mot.kalGueltig ? (float)(mot.kalEnde / spmm) : NAN, 4);
  jsonZahl("kraft", mot.kalGueltig ? mot.kalKraftG * N_PRO_GRAMM : NAN, 1, false);
  jsonf("},\"par\":{");
  jsonZahl("anfahrt", pres.anfahrtMm, 2);
  jsonZahl("vpress", pres.vPressMmS, 3);
  jsonf("\"halt\":%lu,", (unsigned long)pres.haltMs);
  jsonZahl("fmax", (float)(motorMaxKraftG() * N_PRO_GRAMM), 1);
  jsonZahl("kontakt", mot.kontaktG * N_PRO_GRAMM, 2);
  jsonZahl("vfahrt", mot.vFahrtMmS, 2);
  jsonZahl("spmm", mot.schritteProMm, 3, false);
  jsonf("},");
  {
    const SimEinst &se = motorSimEinst();
    jsonf("\"simp\":{\"werkzeug\":%.2f,\"weg\":%.2f,\"fmin\":%.0f,\"fmax\":%.0f,\"rauschen\":%.0f,\"riss\":%.1f,"
          "\"spitze\":%.1f,\"wegmax\":%.1f},",
          (double)se.werkzeugMm, (double)se.wegMm, (double)se.fminG, (double)se.fmaxG, (double)se.rauschenG,
          (double)se.rissProzent, (double)se.spitzeProzent, (double)PRESSWEG_MAX_MM);
  }

  if (sitzungAktiv()) {
    const SitzungMeta &m = sitzungAktivMeta();
    jsonf("\"sit\":{\"id\":%u,\"name\":\"%s\",\"n\":%lu},", m.id, m.name, (unsigned long)sitzungAktivAnzahl());
  } else {
    jsonf("\"sit\":null,");
  }
  uint32_t b, g;
  speicherBelegung(b, g);
  jsonf("\"sp\":{\"b\":%lu,\"g\":%lu},", (unsigned long)b, (unsigned long)g);

  // Live-Kurve: neue Punkte ab 'ab'; bei neuer Pressung (id) von vorn
  const uint32_t id = presseLiveId();
  uint32_t ab = srv->hasArg("ab") ? strtoul(srv->arg("ab").c_str(), nullptr, 10) : 0;
  if (!srv->hasArg("id") || strtoul(srv->arg("id").c_str(), nullptr, 10) != id) ab = 0;
  const uint16_t n = presseLiveAnzahl();
  if (ab > n) ab = 0;
  const RohInfo ri = presseRohInfo();
  jsonf("\"roh\":{\"id\":%lu,\"n\":%lu},", (unsigned long)ri.id, (unsigned long)ri.anzahl);
  jsonf("\"pr\":{\"lauf\":%u,\"id\":%lu,", presseLaeuft() ? 1u : 0u, (unsigned long)id);
  jsonZahl("s", presseLiveStartMm(), 5);
  jsonZahl("d", presseLivePunktMm(), 6);
  jsonf("\"ab\":%lu,\"p\":[", (unsigned long)ab);
  const PressPunkt *p = presseLivePunkte();
  for (uint32_t i = ab; i < n; i++) {
    if (p[i].kraft == PUNKT_LEER) jsonf("%snull", i == ab ? "" : ",");
    else jsonf("%s%d", i == ab ? "" : ",", (int)p[i].kraft);
  }
  jsonf("]},");

  // neue Log-Zeilen (wie /daten)
  const uint32_t lz = webLogPos;
  uint32_t lab = srv->hasArg("l") ? strtoul(srv->arg("l").c_str(), nullptr, 10) : UINT32_MAX;
  if (lab > lz) lab = (lz > 1500) ? lz - 1500 : 0;
  if (lz - lab > WEBLOG_GROESSE) lab = lz - WEBLOG_GROESSE;
  uint32_t lbis = lz;
  if (lbis - lab > 3000) lbis = lab + 3000;
  jsonf("\"ln\":%lu,\"log\":\"", (unsigned long)lbis);
  for (uint32_t i = lab; i < lbis; i++) jsonZeichen(webLog[i & (WEBLOG_GROESSE - 1)]);
  jsonf("\"}");
  jsonEnde();
}

// Rohdaten: jede Probe der laufenden bzw. letzten Pressung. Kopf (48 Byte, little-endian):
//   0 magic "ROH1" | 4 id | 8 ab | 12 n | 16 gesamt | 20 t0 [µs] | 24 startSchritt (i32) | 28 spmm (f32)
//   32 |Faktor| (f32) | 36 Sitzung (u16) | 38 läuft (u8) | 39 frei | 40 Pressnummer (0 = nicht gespeichert)
//   44 SPS (f32), danach n x RohProbe. ab > angefragt heißt: Proben dazwischen sind überschrieben.
static constexpr uint32_t ROH_MAGIC = 0x31484F52UL;   // "ROH1"
static constexpr uint32_t ROH_MAX_ANTWORT = 2048;     // 16 KB je Antwort

static void webPresseRoh() {
  const RohInfo r = presseRohInfo();
  uint32_t ab = srv->hasArg("ab") ? strtoul(srv->arg("ab").c_str(), nullptr, 10) : 0;
  if (!srv->hasArg("id") || strtoul(srv->arg("id").c_str(), nullptr, 10) != r.id) ab = 0;
  if (ab > r.anzahl) ab = 0;
  if (r.anzahl - ab > ROH_N) ab = r.anzahl - ROH_N;
  uint32_t n = r.anzahl - ab;
  if (n > ROH_MAX_ANTWORT) n = ROH_MAX_ANTWORT;

  uint8_t k[48];
  memset(k, 0, sizeof(k));
  auto u32 = [&](size_t o, uint32_t v) { memcpy(k + o, &v, 4); };
  auto f32 = [&](size_t o, float v) { memcpy(k + o, &v, 4); };
  u32(0, ROH_MAGIC); u32(4, r.id); u32(8, ab); u32(12, n); u32(16, r.anzahl); u32(20, r.t0);
  u32(24, (uint32_t)r.startSchritt); f32(28, r.spmm); f32(32, r.fa);
  memcpy(k + 36, &r.sit, 2); k[38] = r.lauf ? 1 : 0; u32(40, r.nr); f32(44, r.sps);

  srv->sendHeader("Cache-Control", "no-store");
  srv->setContentLength(sizeof(k) + n * sizeof(RohProbe));
  srv->send(200, "application/octet-stream", "");
  srv->sendContent((const char *)k, sizeof(k));
  const RohProbe *p = presseRohPuffer();
  const uint32_t i = ab & (ROH_N - 1);
  const uint32_t erst = min(n, ROH_N - i);                 // bis zum Pufferende, Rest von vorn
  if (erst) srv->sendContent((const char *)&p[i], erst * sizeof(RohProbe));
  if (n > erst) srv->sendContent((const char *)p, (n - erst) * sizeof(RohProbe));
}

// ===========================================================================
// Sitzungen
// ===========================================================================
struct ListeCtx { bool erste; };

static void listeEintrag(const SitzungMeta &m, uint32_t anzahl, void *ctx) {
  ListeCtx *c = (ListeCtx *)ctx;
  char t[20];
  zeitText(m.erstellt, t, sizeof(t));
  jsonf("%s{\"id\":%u,\"name\":\"%s\",\"n\":%lu,\"erst\":\"%s\",\"notiz\":\"", c->erste ? "" : ",", m.id, m.name,
        (unsigned long)anzahl, t);
  for (const char *q = m.notiz; *q; q++) jsonZeichen(*q);
  jsonf("\"}");
  c->erste = false;
}

static void webSitzungen() {
  uint32_t b, g;
  speicherBelegung(b, g);
  jsonStart();
  jsonf("{\"aktiv\":%u,\"sp\":{\"b\":%lu,\"g\":%lu},\"liste\":[", sitzungAktiv(), (unsigned long)b, (unsigned long)g);
  ListeCtx c{true};
  sitzungenAuflisten(listeEintrag, &c);
  jsonf("]}");
  jsonEnde();
}

static void dateiSenden(const char *pfad) {
  File f = LittleFS.open(pfad, "r");
  srv->sendHeader("Cache-Control", "no-store");
  if (!f) { srv->send(200, "application/octet-stream", ""); return; }   // leer = noch keine Pressungen
  srv->streamFile(f, "application/octet-stream");
  f.close();
}

static void webSitzungIndex() {
  char p[40];
  sitzungPfad(p, sizeof(p), argId("id"), "index.bin");
  dateiSenden(p);
}

static void webEinzelpresse() {
  char p[40];
  pressePfad(p, sizeof(p), argId("id"), srv->hasArg("nr") ? strtoul(srv->arg("nr").c_str(), nullptr, 10) : 0);
  if (!LittleFS.exists(p)) { srv->send(404, "text/plain", "Pressung nicht gefunden"); return; }
  dateiSenden(p);
}

static void webSitzungAktion() {
  const String a = srv->arg("a");
  const uint16_t id = argId("id");
  bool ok = false;
  if (presseLaeuft() || antrieb::beschaeftigt()) {
    meld.println(F("# Antrieb faehrt - Sitzungen erst im Stillstand aendern."));
  } else if (a == "neu") {
    ok = sitzungNeu(srv->arg("name").c_str(), srv->arg("notiz").c_str()) != 0;
  } else if (a == "wahl") {
    ok = sitzungWaehlen(id);
  } else if (a == "zu") {
    sitzungSchliessen();
    ok = true;
  } else if (a == "aendern") {
    ok = sitzungAendern(id, srv->arg("name").c_str(), srv->hasArg("notiz") ? srv->arg("notiz").c_str() : nullptr);
  } else if (a == "loeschen") {
    ok = sitzungLoeschen(id);
  }
  srv->send(200, "application/json", ok ? "{\"ok\":1}" : "{\"ok\":0}");
}

static void webZeit() {
  zeitSetzen(strtoul(srv->arg("t").c_str(), nullptr, 10), (int32_t)strtol(srv->arg("tz").c_str(), nullptr, 10));
  srv->send(200, "text/plain", "ok");
}

// ===========================================================================
// Export
// ===========================================================================
// Zahl mit Dezimalkomma für Excel (NaN -> leer)
static const char *dz(char *puf, size_t n, float v, uint8_t stellen) {
  if (!isfinite(v)) { puf[0] = '\0'; return puf; }
  snprintf(puf, n, "%.*f", stellen, (double)v);
  for (char *c = puf; *c; c++) if (*c == '.') *c = ',';
  return puf;
}

static const char *statusText(uint8_t s) {
  return s == PRESS_OK ? "OK" : s == PRESS_KRAFT_ANFAHRT ? "Kraft im Eilgang" : "Abbruch";
}

// Alle Pressungen einer Sitzung nacheinander lesen
static bool kopfLesen(File &ix, PressKopf &k) {
  return ix.read((uint8_t *)&k, sizeof(k)) == sizeof(k) && k.magic == PRESS_MAGIC;
}

// Punkte einer Pressung lesen und je Punkt cb aufrufen
static void punkteLesen(uint16_t id, const PressKopf &k, void (*cb)(const PressKopf &k, uint16_t i, const PressPunkt &p)) {
  char pf[40];
  pressePfad(pf, sizeof(pf), id, k.nr);
  File f = LittleFS.open(pf, "r");
  if (!f) return;
  f.seek(sizeof(PressKopf));
  PressPunkt puf[64];
  uint16_t i = 0;
  while (i < k.punkte) {
    const uint16_t m = (uint16_t)min<uint32_t>(64u, (uint32_t)(k.punkte - i));
    const size_t bytes = m * sizeof(PressPunkt);
    if ((size_t)f.read((uint8_t *)puf, bytes) != bytes) break;
    for (uint16_t j = 0; j < m; j++, i++) cb(k, i, puf[j]);
    if ((i & 255) == 0) hintergrund();
  }
  f.close();
}

static bool exportJsonErster = true;

// Krafteinheit des CSV-Exports (gespeichert wird immer in N)
static float       csvFaktor  = 1.0f;     // Einheiten pro N
static const char *csvEinheit = "N";
static uint8_t     csvStellen = 2;

static void csvEinheitSetzen(const String &e) {
  if (e == "kg")       { csvFaktor = 1.0f / 9.80665f;   csvEinheit = "kg";  csvStellen = 3; }
  else if (e == "lbs") { csvFaktor = 1.0f / 4.4482216f; csvEinheit = "lbs"; csvStellen = 3; }
  else                 { csvFaktor = 1.0f;              csvEinheit = "N";   csvStellen = 2; }
}

// Kraft (in N gespeichert) in der Exporteinheit
static const char *dzk(char *puf, size_t n, float vN, uint8_t mehr = 0) {
  return dz(puf, n, vN * csvFaktor, (uint8_t)(csvStellen + mehr));
}

static void csvPunkt(const PressKopf &k, uint16_t i, const PressPunkt &p) {
  if (p.kraft == PUNKT_LEER) return;
  char a[16], b[16];
  jsonf("%lu;%s;%s;%u\r\n", (unsigned long)k.nr, dz(a, sizeof(a), k.startMm + i * k.punktMm, 4),
        dzk(b, sizeof(b), p.kraft * KRAFT_N_PRO_STUFE), p.tMs);
}

static void jsonPunkt(const PressKopf &k, uint16_t i, const PressPunkt &p) {
  if (p.kraft == PUNKT_LEER) return;
  jsonf("%s[%.4f,%.2f,%u]", exportJsonErster ? "" : ",", (double)(k.startMm + i * k.punktMm),
        (double)(p.kraft * KRAFT_N_PRO_STUFE), p.tMs);
  exportJsonErster = false;
}

static void webExport() {
  const uint16_t id = argId("id");
  const bool csv = srv->arg("f") != "json";
  SitzungMeta m;
  uint32_t anzahl;
  if (!sitzungMetaLesen(id, m, anzahl)) { srv->send(404, "text/plain", "Sitzung nicht gefunden"); return; }
  if (presseLaeuft() || antrieb::beschaeftigt()) { srv->send(409, "text/plain", "Antrieb faehrt - Export erst im Stillstand"); return; }

  char ix[40];
  sitzungPfad(ix, sizeof(ix), id, "index.bin");
  char datei[80];
  snprintf(datei, sizeof(datei), "attachment; filename=\"sitzung_%04u.%s\"", id, csv ? "csv" : "json");
  srv->sendHeader("Content-Disposition", datei);
  jsonStart(csv ? "text/csv; charset=utf-8" : "application/json");

  char erst[20], jetzt[20];
  zeitText(m.erstellt, erst, sizeof(erst));
  zeitText(zeitJetzt(), jetzt, sizeof(jetzt));
  PressKopf k;

  if (csv) {
    csvEinheitSetzen(srv->arg("e"));
    const char *E = csvEinheit;
    jsonf("\xEF\xBB\xBF");                               // BOM: Excel erkennt UTF-8
    jsonf("Sitzung;%u;%s\r\n", m.id, m.name);
    jsonf("Notiz;%s\r\n", m.notiz);
    jsonf("Erstellt;%s\r\nExportiert;%s\r\nPressungen;%lu\r\n\r\n", erst, jetzt, (unsigned long)anzahl);
    jsonf("Nr;Zeit;Status;Endgrund;Kontakt [mm];Endpunkt [mm];Presstiefe [mm];Fmax [%s];Weg bei Fmax [mm];", E);
    jsonf("Fend [%s];Fhalt [%s];Fmittel [%s];Sigma [%s];Fmin [%s];Spannweite [%s];", E, E, E, E, E, E);
    jsonf("Arbeit [J];Presszeit [s];Zykluszeit [s];Proben;Verloren;Pressgeschw. [mm/s];Haltezeit [ms];Simulation\r\n");
    File f = LittleFS.open(ix, "r");
    while (f && kopfLesen(f, k)) {
      char t[20], v[12][16];
      zeitText(k.zeit, t, sizeof(t));
      jsonf("%lu;%s;%s;%s;", (unsigned long)k.nr, t, statusText(k.status), antrieb::stoppText((antrieb::Stopp)k.endGrund));
      jsonf("%s;%s;%s;%s;%s;%s;", dz(v[0], 16, k.kontaktMm, 4), dz(v[1], 16, k.endMm, 4), dz(v[2], 16, k.tiefeMm, 4),
            dzk(v[3], 16, k.fmaxN), dz(v[4], 16, k.fmaxMm, 4), dzk(v[5], 16, k.fendN));
      jsonf("%s;%s;%s;%s;%s;%s;", dzk(v[6], 16, k.fhaltN), dzk(v[7], 16, k.fmittelN), dzk(v[8], 16, k.fsigmaN, 1),
            dzk(v[9], 16, k.fminN), dzk(v[10], 16, k.fspanneN), dz(v[11], 16, k.arbeitJ, 4));
      jsonf("%s;%s;%lu;%lu;%s;%lu;%s\r\n", dz(v[0], 16, k.presszeitS, 3), dz(v[1], 16, k.zykluszeitS, 2),
            (unsigned long)k.proben, (unsigned long)k.verloren, dz(v[2], 16, k.vPressMmS, 3), (unsigned long)k.haltMs,
            k.simuliert ? "ja" : "");
    }
    if (f) f.close();
    hintergrund();
    jsonf("\r\nNr;Weg [mm];Kraft [%s];Zeit [ms]\r\n", E);
    f = LittleFS.open(ix, "r");
    while (f && kopfLesen(f, k)) { punkteLesen(id, k, csvPunkt); hintergrund(); }
    if (f) f.close();
  } else {
    // jsonf sendet höchstens 255 Zeichen je Aufruf: Name und Notiz einzeln
    jsonf("{\"format\":\"waegezelle-sitzung\",\"version\":1,\"sitzung\":{\"id\":%u,\"name\":\"%s\",", m.id, m.name);
    jsonf("\"notiz\":\"%s\",", m.notiz);
    jsonf("\"erstellt\":%lu,\"exportiert\":%lu},\"pressen\":[", (unsigned long)m.erstellt, (unsigned long)zeitJetzt());
    File f = LittleFS.open(ix, "r");
    bool erste = true;
    while (f && kopfLesen(f, k)) {
      jsonf("%s{\"nr\":%lu,\"zeit\":%lu,\"status\":\"%s\",\"endgrund\":\"%s\",\"kontakt\":%u,\"simuliert\":%u,",
            erste ? "" : ",", (unsigned long)k.nr, (unsigned long)k.zeit, statusText(k.status),
            antrieb::stoppText((antrieb::Stopp)k.endGrund), k.kontakt, k.simuliert);
      jsonZahl("kontaktMm", k.kontaktMm, 4); jsonZahl("endMm", k.endMm, 4); jsonZahl("tiefeMm", k.tiefeMm, 4);
      jsonZahl("fmaxN", k.fmaxN, 2); jsonZahl("fmaxMm", k.fmaxMm, 4); jsonZahl("fendN", k.fendN, 2);
      jsonZahl("fhaltN", k.fhaltN, 2); jsonZahl("fmittelN", k.fmittelN, 2); jsonZahl("fsigmaN", k.fsigmaN, 3);
      jsonZahl("fminN", k.fminN, 2); jsonZahl("fspanneN", k.fspanneN, 2); jsonZahl("arbeitJ", k.arbeitJ, 4);
      jsonZahl("presszeitS", k.presszeitS, 3); jsonZahl("zykluszeitS", k.zykluszeitS, 2);
      jsonf("\"proben\":%lu,\"verloren\":%lu,", (unsigned long)k.proben, (unsigned long)k.verloren);
      jsonf("\"einstellungen\":{\"tara\":%.1f,\"faktor\":%.6f,\"schritteProMm\":%.3f,\"sps\":%g,\"kalTiefe\":%ld,",
            (double)k.tara, (double)k.faktor, (double)k.schritteProMm, (double)k.sps, (long)k.kalTiefe);
      jsonf("\"kalEnde\":%ld,\"vPressMmS\":%.3f,\"anfahrtMm\":%.2f,\"haltMs\":%lu},\"punkte\":[",
            (long)k.kalEnde, (double)k.vPressMmS, (double)k.anfahrtMm, (unsigned long)k.haltMs);
      exportJsonErster = true;
      punkteLesen(id, k, jsonPunkt);
      jsonf("]}");
      erste = false;
      hintergrund();
    }
    if (f) f.close();
    jsonf("]}");
  }
  jsonEnde();
}

// ===========================================================================
void presseWebRouten() {
  srv = &webServer();
  srv->on("/presse", HTTP_GET, webPresseSeite);
  srv->on("/presse/status", HTTP_GET, webPresseStatus);
  srv->on("/presse/roh", HTTP_GET, webPresseRoh);
  srv->on("/sitzungen", HTTP_GET, webSitzungen);
  srv->on("/sitzung/index", HTTP_GET, webSitzungIndex);
  srv->on("/sitzung/presse", HTTP_GET, webEinzelpresse);
  srv->on("/sitzung/export", HTTP_GET, webExport);
  srv->on("/sitzung", HTTP_POST, webSitzungAktion);
  srv->on("/zeit", HTTP_POST, webZeit);
}
