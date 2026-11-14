#include "watchdog.h"

#include <hardware/watchdog.h>
#include "meldung.h"
#include "treiber/Antrieb.h"

// Marke im Scratch-Register 0 (übersteht den Watchdog-Neustart, nicht das Ausschalten).
// Die Register 4..7 benutzt das Boot-ROM, daher Register 0.
static constexpr uint32_t MARKE_KERN1 = 0x4B31484EUL;   // "NH1K": Neustart, weil Kern 1 hing

static volatile uint32_t herzschlag1 = 0;      // von Kern 1 hochgezählt
static volatile bool     kern1Aufhaengen = false;   // nur für 'wd test 1'

static bool        aktiv = false;
static uint32_t    letzterHerzschlag = 0;
static uint32_t    letzteAenderungMs = 0;
static const char *neustartGrund = "unbekannt";

// ===========================================================================
// Kern 1
// ===========================================================================
void watchdogHerzschlag() {
  herzschlag1 = herzschlag1 + 1;
  if (kern1Aufhaengen) {
    while (true) { tight_loop_contents(); }   // Test: Kern 1 hängt (Interrupts laufen weiter)
  }
}

// ===========================================================================
// Kern 0
// ===========================================================================
void watchdogStarten() {
  const bool kern1 = (watchdog_hw->scratch[0] == MARKE_KERN1);
  watchdog_hw->scratch[0] = 0;
  const RP2040::resetReason_t r = rp2040.getResetReason();

  if (kern1) {
    neustartGrund = "Kern 1 reagierte nicht (Watchdog)";
    meld.println(F("# !!! WARNUNG: Neustart, weil Kern 1 nicht mehr reagierte. Der Antrieb wurde vorher gesperrt."));
  } else if (r == RP2040::WDT_RESET) {
    neustartGrund = "Kern 0 reagierte nicht (Watchdog)";
    meld.printf("# !!! WARNUNG: Neustart durch den Watchdog - Kern 0 hat laenger als %lu s nicht reagiert.\n",
                (unsigned long)(WATCHDOG_TIMEOUT_MS / 1000));
  } else if (r == RP2040::PWRON_RESET) {
    neustartGrund = "Einschalten";
  } else if (r == RP2040::RUN_PIN_RESET) {
    neustartGrund = "RUN-Pin";
  } else if (r == RP2040::SOFT_RESET) {
    neustartGrund = "Software (z. B. Upload)";
  }

  letzterHerzschlag = herzschlag1;
  letzteAenderungMs = millis();
  watchdog_enable(WATCHDOG_TIMEOUT_MS, true);   // beim Debuggen angehalten
  aktiv = true;
}

// Kern 1 hängt: Antrieb per Hardware sperren, Grund merken, sofort neu starten
static void kern1Haengt(uint32_t seitMs) {
  antrieb::notHalt();
  watchdog_hw->scratch[0] = MARKE_KERN1;
  meld.printf("# !!! Kern 1 reagiert seit %lu ms nicht - Antrieb gesperrt, Neustart.\n", (unsigned long)seitMs);
  Serial.flush();
  delay(50);                                    // Meldung über USB hinausgehen lassen
  watchdog_reboot(0, 0, 10);
  while (true) { tight_loop_contents(); }
}

void watchdogFuettern() {
  if (!aktiv) return;
  const uint32_t h = herzschlag1;
  const uint32_t jetzt = millis();
  if (h != letzterHerzschlag) {
    letzterHerzschlag = h;
    letzteAenderungMs = jetzt;
  } else if (jetzt - letzteAenderungMs > KERN1_TIMEOUT_MS) {
    kern1Haengt(jetzt - letzteAenderungMs);
  }
  watchdog_update();
}

void watchdogWarten() {
  if (!aktiv) return;
  letzteAenderungMs = millis();                 // Kern 1 ist absichtlich beschäftigt
  watchdog_update();
}

void watchdogInfo() {
  meld.printf("# Watchdog:       %s, Kern 0 max. %lu ms, Kern 1 max. %lu ms ohne Herzschlag\n",
              aktiv ? "aktiv" : "AUS", (unsigned long)WATCHDOG_TIMEOUT_MS, (unsigned long)KERN1_TIMEOUT_MS);
  meld.printf("# Kern 1:         letzter Herzschlag vor %lu ms\n", (unsigned long)(millis() - letzteAenderungMs));
  meld.printf("# Letzter Neustart: %s\n", neustartGrund);
}

void watchdogTest(uint8_t kern) {
  if (!aktiv) { meld.println(F("# Watchdog ist nicht aktiv.")); return; }
  if (kern == 1) {
    meld.printf("# Test: Kern 1 haengt ab jetzt - Neustart nach ca. %lu ms erwartet.\n", (unsigned long)KERN1_TIMEOUT_MS);
    kern1Aufhaengen = true;
    return;
  }
  meld.printf("# Test: Kern 0 haengt ab jetzt - Neustart nach ca. %lu ms erwartet.\n", (unsigned long)WATCHDOG_TIMEOUT_MS);
  Serial.flush();
  while (true) { tight_loop_contents(); }
}
