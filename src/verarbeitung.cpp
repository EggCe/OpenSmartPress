#include "verarbeitung.h"

#include <math.h>
#include "konfig.h"
#include "meldung.h"
#include "einstellungen.h"
#include "adc.h"
#include "filter.h"
#include "auftraege.h"
#include "usb_ausgabe.h"
#include "web/livedaten.h"

// Überwachung
static uint8_t  warnStufe = 0;            // 0 = ok, 1 = >= 100 %, 2 = >= 120 %
static bool     adcClip = false;
static uint32_t clipZuletztMs = 0;
static uint32_t letzteWarnungMs = 0;

// DRDY-Timeout
static uint32_t letzteMessungMs = 0;
static bool     timeoutAktiv = false;
static uint32_t letzteTimeoutMeldungMs = 0;

void messTimeoutNeustart(bool meldungZuruecksetzen) {
  letzteMessungMs = millis();
  if (meldungZuruecksetzen) timeoutAktiv = false;
}

// ===========================================================================
// Überwachung: Überlast der Zelle und Übersteuerung des ADC
// ===========================================================================
static void ueberwachung(int32_t roh) {
  uint32_t jetzt = millis();
  bool sofort = false;

  // --- ADC-Übersteuerung ---
  if (ADS1256::isClipped(roh)) {
    clipZuletztMs = jetzt;
    auftragUebersteuert();
    webUebersteuert();
    if (!adcClip) { adcClip = true; sofort = true; }
  } else if (adcClip && jetzt - clipZuletztMs > 500) {
    adcClip = false;
    meld.println(F("# ADC nicht mehr uebersteuert."));
  }

  // --- Last (ungefilterter Einzelwert, damit die Warnung sofort kommt) ---
  double last = fabs(inGramm((double)roh));
  uint8_t neu;
  if (last >= ALARMGRENZE_G)                                        neu = 2;
  else if (warnStufe == 2 && last >= ALARMGRENZE_G * WARN_HYSTERESE) neu = 2;
  else if (last >= WARNGRENZE_G)                                    neu = 1;
  else if (warnStufe >= 1 && last >= WARNGRENZE_G * WARN_HYSTERESE)  neu = 1;
  else                                                              neu = 0;

  if (neu > warnStufe) sofort = true;
  if (neu == 0 && warnStufe > 0) meld.println(F("# Last wieder unter 100 % Nennlast."));
  if (neu != warnStufe) {
    // Pico W: LED hängt am WLAN-Chip, Schalten ist langsam -> nur bei Zustandswechsel
    digitalWrite(LED_BUILTIN, neu > 0 ? HIGH : LOW);
    warnStufe = neu;
  }

  // --- Meldungen: sofort bei Verschlechterung, sonst höchstens 1x pro Sekunde ---
  if (!(adcClip || warnStufe > 0)) return;
  if (!sofort && jetzt - letzteWarnungMs < 1000) return;
  letzteWarnungMs = jetzt;

  if (adcClip) {
    meld.printf("# !!! ADC UEBERSTEUERT (Rohwert %ld) - Bruecke/Verdrahtung pruefen (Aderbruch, Kurzschluss)!\n", (long)roh);
    return;   // Lastwert ist dann ohnehin ungültig
  }
  const char *hinweis = cfg.kalibriert ? "" : " [Faktor unkalibriert]";
  if (warnStufe == 2) {
    meld.printf("# !!!!!! UEBERLAST: %.2f kg >= 120 %% Nennlast - SOFORT ENTLASTEN !!!!!!%s\n", last / 1000.0, hinweis);
  } else {
    meld.printf("# ! Warnung: %.2f kg >= 100 %% Nennlast%s\n", last / 1000.0, hinweis);
  }
}

// ===========================================================================
// Verarbeitung eines neuen Messwerts
// ===========================================================================
static void verarbeiteMesswert(const ADS1256::Messwert &m) {
  filterUpdate(m.counts);
  ueberwachung(m.counts);
  webAufnehmen(m);

  // Während eines Auftrags nur die Datenströme (CSV/Binär) weiterlaufen lassen
  if (auftragMesswert(m) && cfg.modus != MODUS_CSV && cfg.modus != MODUS_BINAER) return;
  ausgabe(m);
}

// DRDY-Timeout: kommt länger als 3 Messperioden + 100 ms kein Messwert?
static void pruefeTimeout() {
  uint32_t grenze = (uint32_t)(3000.0f / ads.sampleRate()) + 100;
  uint32_t jetzt = millis();
  if (jetzt - letzteMessungMs <= grenze) return;
  if (timeoutAktiv && jetzt - letzteTimeoutMeldungMs < 2000) return;
  timeoutAktiv = true;
  letzteTimeoutMeldungMs = jetzt;
  meld.printf("# FEHLER: DRDY-Timeout - seit %lu ms kein Messwert (DRDY-Leitung/Versorgung pruefen, ggf. 'init').\n",
              (unsigned long)(jetzt - letzteMessungMs));
  auftragAbbrechen("DRDY-Timeout");
}

void messwerteAbholen() {
  if (!adsOk) return;

  // Alle angefallenen Messwerte verarbeiten (begrenzt, damit Befehle zügig drankommen)
  ADS1256::Messwert m;
  uint16_t anzahl = 0;
  while (anzahl < 1024 && !vergleichFaellig() && ads.popSample(m)) {
    verarbeiteMesswert(m);
    anzahl++;
  }
  pufferSenden();

  if (anzahl > 0) {
    letzteMessungMs = millis();
    if (timeoutAktiv) {
      timeoutAktiv = false;
      meld.println(F("# Messwerte kommen wieder."));
    }
  } else if (!vergleichFaellig()) {
    pruefeTimeout();
  }
}
