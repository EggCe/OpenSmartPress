#include "webserver.h"

#include <WebServer.h>
#include <math.h>
#include <stdarg.h>
#include "../konfig.h"
#include "../meldung.h"
#include "../einstellungen.h"
#include "../adc.h"
#include "../filter.h"
#include "../auftraege.h"
#include "../motor.h"
#include "../befehle.h"
#include "livedaten.h"
#include "wlan.h"
#include "seiten/webstil.h"
#include "seiten/webui.h"
#include "seiten/webinfo.h"

static WebServer server(80);
static char      jsonPuf[1400];
static uint16_t  jsonLen = 0;

// ===========================================================================
// JSON gepuffert und chunked senden
// ===========================================================================
WebServer &webServer() { return server; }

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

// Antwort mit unbekannter Länge beginnen (chunked); danach jsonf/jsonZeichen, Ende mit jsonEnde()
void jsonStart(const char *typ) {
  server.sendHeader("Cache-Control", "no-store");
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, typ, "");
  jsonLen = 0;
}

void jsonEnde() {
  jsonFlush();
  server.sendContent("");                                  // Ende der chunked-Antwort
}

// ===========================================================================
// Routen
// ===========================================================================
static void webSeite() {
  server.send_P(200, "text/html; charset=utf-8", WEBUI_HTML);
}

static void webInfo() {
  server.send_P(200, "text/html; charset=utf-8", WEBINFO_HTML);
}

static void webStil() {
  // 5 Minuten zwischenspeichern: nach einem Firmware-Update ist neues CSS spätestens dann aktiv
  server.sendHeader("Cache-Control", "max-age=300");
  server.send_P(200, "text/css; charset=utf-8", WEBUI_CSS);
}

static void webDaten() {
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

  jsonf("{\"t\":%lu,\"cfg\":{\"rate\":%g,\"uv\":%.9g,\"tara\":%.3f,\"faktor\":%.6f,\"kal\":%u,"
        "\"filter\":%g,\"filter_ist\":%.4f,\"modus\":%u,\"pga\":%u,\"nennlast\":%.0f,\"gl\":%lu},",
        (unsigned long)millis(), ads.sampleRate(), ads.microvoltsPerCount(), cfg.tara, cfg.faktor,
        cfg.kalibriert, cfg.filterMs / 1000.0, cfg.filterMs ? filterSekunden() : 0.0, cfg.modus, ads.gain(),
        NENNLAST_G, (unsigned long)graphLaenge);
  jsonf("\"ok\":%u,\"kont\":%u,\"w\":%.2f,\"auftrag\":%u,\"verl\":%lu,\"ringmax\":%lu,\"rssi\":%ld,\"ws\":%g,",
        adsOk ? 1 : 0, ads.isContinuous() ? 1 : 0, filterMittel(), auftragLaeuft() ? 1 : 0,
        (unsigned long)ads.overruns(), (unsigned long)ads.ringMaximum(), wlanRssi(), webStatSek);

  {  // Antrieb
    const antrieb::Status s = antrieb::status();
    const double fa = fabs((double)cfg.faktor);
    jsonf("\"mot\":{\"ok\":%u,\"z\":\"%s\",\"lauf\":%u,\"pos\":%ld,\"spmm\":%g,\"ref\":%u,\"alm\":%u,"
          "\"enam\":%u,\"ena\":%u,\"stopp\":\"%s\",\"last\":%.1f,",
          mot.schritteProMm > 0.0f ? 1 : 0, antrieb::zustandText(s.zustand), antrieb::beschaeftigt() ? 1 : 0,
          (long)s.position, mot.schritteProMm, s.referenziert ? 1 : 0, s.alarm ? 1 : 0,
          mot.enaModus, s.enaEin ? 1 : 0, antrieb::stoppText(s.letzterStopp), s.lastCounts / fa);
    jsonf("\"vs\":%g,\"vf\":%g,\"kg\":%g,\"fmax\":%g,\"ue\":%ld,\"hub\":%g,"
          "\"kal\":%u,\"kt\":%ld,\"ke\":%ld,\"kk\":%.1f,\"kgr\":%u},",
          mot.vSuchMmS, mot.vFahrtMmS, mot.kontaktG, motorMaxKraftG(), (long)mot.ueberSchritte, mot.hubMm,
          mot.kalGueltig, (long)mot.kalTiefe, (long)mot.kalEnde, mot.kalKraftG, mot.kalGrund);
  }

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

static void webBefehl() {
  if (!server.hasArg("c")) {
    server.send(400, "text/plain", "Parameter c fehlt");
    return;
  }
  char puf[BEFEHL_MAX_LAENGE];
  strncpy(puf, server.arg("c").c_str(), sizeof(puf) - 1);
  puf[sizeof(puf) - 1] = '\0';
  server.send(200, "text/plain", "ok");     // erst antworten (z. B. vor 'wlan aus')
  meld.printf("> %s   (Web)\n", puf);
  verarbeiteBefehl(puf);
}

// ===========================================================================
void webserverStarten() {
  server.on("/", HTTP_GET, webSeite);
  server.on("/info", HTTP_GET, webInfo);
  server.on("/stil.css", HTTP_GET, webStil);
  server.on("/daten", HTTP_GET, webDaten);
  server.on("/cmd", webBefehl);    // GET oder POST
  presseWebRouten();               // Pressseite, Sitzungen, Export (presse_web.cpp)
  server.onNotFound([]() { server.send(404, "text/plain", "Nicht gefunden"); });
  server.enableDelay(false);       // handleClient() soll die Messwertverarbeitung nicht bremsen
  server.begin();
}

void webserverBearbeiten() {
  server.handleClient();
}
