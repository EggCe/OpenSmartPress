#include "wlan.h"

#include <WiFi.h>
#include <SimpleMDNS.h>
#include "../meldung.h"
#include "../../wlan_zugang.h"

bool            wlanAn = true;
static bool     wlanVerbunden = false;
static bool     mdnsAktiv = false;
static bool     wlanAusFaellig = false;        // 'wlan aus' nach dem HTTP-Aufruf ausführen
static uint32_t wlanLetzterVersuch = 0;
static uint32_t wlanLetztePruefung = 0;

void wlanStarten() {
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(WLAN_HOSTNAME);
  WiFi.beginNoBlock(WLAN_SSID, WLAN_PASSWORT);   // blockiert nicht, Verbindung läuft im Hintergrund
  wlanLetzterVersuch = millis();
  meld.printf("# WLAN: verbinde mit '%s' ...\n", WLAN_SSID);
}

// Verbindung melden, bei Abbruch neu verbinden
static void wlanPruefen() {
  if (!wlanAn) return;
  const bool verbunden = (WiFi.status() == WL_CONNECTED);
  if (verbunden && !wlanVerbunden) {
    wlanVerbunden = true;
    if (!mdnsAktiv) {
      mdnsAktiv = MDNS.begin(WLAN_HOSTNAME);
      if (mdnsAktiv) MDNS.addService("http", "tcp", 80);
    }
    meld.printf("# WLAN verbunden: http://%s  bzw. http://%s.local  (RSSI %ld dBm)\n",
                WiFi.localIP().toString().c_str(), WLAN_HOSTNAME, (long)WiFi.RSSI());
  } else if (!verbunden && wlanVerbunden) {
    wlanVerbunden = false;
    meld.println(F("# WLAN getrennt - verbinde neu ..."));
    wlanLetzterVersuch = millis();
  }
  if (!verbunden && millis() - wlanLetzterVersuch > 20000) {
    WiFi.disconnect();
    wlanStarten();
  }
}

void wlanSchritt() {
  if (wlanAusFaellig) {
    wlanAusFaellig = false;
    wlanAn = false;
    wlanVerbunden = false;
    if (mdnsAktiv) { MDNS.end(); mdnsAktiv = false; }
    WiFi.end();
    meld.println(F("# WLAN aus. Wieder einschalten mit 'wlan an' (nur ueber USB moeglich)."));
  }
  if (millis() - wlanLetztePruefung >= 500) {
    wlanLetztePruefung = millis();
    wlanPruefen();
  }
}

void wlanAusschalten() {
  if (wlanAn) wlanAusFaellig = true;
}

void wlanEinschalten() {
  if (!wlanAn) { wlanAn = true; wlanStarten(); }
}

void wlanInfo() {
  meld.printf("# WLAN: %s, SSID '%s'", wlanAn ? "an" : "AUS", WLAN_SSID);
  if (wlanAn && WiFi.status() == WL_CONNECTED) {
    meld.printf(", verbunden: http://%s (http://%s.local), RSSI %ld dBm\n",
                WiFi.localIP().toString().c_str(), WLAN_HOSTNAME, (long)WiFi.RSSI());
  } else {
    meld.println(wlanAn ? F(", nicht verbunden") : F(""));
  }
}

long wlanRssi() {
  return (wlanAn && WiFi.status() == WL_CONNECTED) ? (long)WiFi.RSSI() : 0L;
}
