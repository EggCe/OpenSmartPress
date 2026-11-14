#include "watchdog.h"

#include <hardware/watchdog.h>
#include "meldung.h"
#include "treiber/Antrieb.h"

// Marke im Scratch-Register 0 (übersteht den Watchdog-Neustart, nicht das Ausschalten).
// Die Register 4..7 benutzt das Boot-ROM, daher Register 0.
static constexpr uint32_t MARKE_KERN1 = 0x4B31484EUL;   // "NH1K": Neustart, weil Kern 1 hing

static volatile uint32_t herzschlag1 = 0;      // von Kern 1 hochgezählt
static volatile bool     kern1Aufhaengen = false;   // nur für 'wd test 1'

// Stapel (nullptr = nicht markiert)
static constexpr uint32_t STAPEL_MUSTER = 0x5354504BUL;   // "KPTS"
static uint32_t *stapelUnten[2] = {nullptr, nullptr};
static uint32_t *stapelOben[2]  = {nullptr, nullptr};

extern "C" {
  extern uint32_t __scratch_x_end__[];           // Linker: Ende der Daten in Scratch X
  extern uint32_t __StackTop[];                  // Linker: oberes Ende des Stapels von Kern 0
  extern uint32_t *core1_separate_stack_address; // arduino-pico: 8-KB-Stapel von Kern 1
}

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

// ===========================================================================
// Stapel-Hochwassermarke (beide Kerne)
// ===========================================================================
void stapelMarkieren() {
  const uint32_t kern = get_core_num();
  uint32_t *unten, *oben;
  if (kern == 0) {
    // Kern 0 beginnt oben in Scratch Y und darf bis an die Daten in Scratch X wachsen
    // (Kern 1 hat einen eigenen Stapel, Scratch X ist sonst frei)
    unten = __scratch_x_end__;
    oben  = __StackTop;
  } else {
    if (!core1_separate_stack_address) return;   // ohne eigenen Stapel nicht messbar
    unten = core1_separate_stack_address;
    oben  = core1_separate_stack_address + 0x2000 / sizeof(uint32_t);
  }
  uint32_t sp;
  asm volatile("mov %0, sp" : "=r"(sp));
  uint32_t *bis = (uint32_t *)(sp - 256);        // Reserve für diesen Aufruf und Interrupts
  if (bis <= unten || bis > oben) return;
  for (uint32_t *p = unten; p < bis; p++) *p = STAPEL_MUSTER;
  stapelUnten[kern] = unten;
  stapelOben[kern]  = oben;
}

// Höchste Belegung in Byte (0 = nicht markiert)
static uint32_t stapelMaximum(uint8_t kern, uint32_t &groesse) {
  const uint32_t *unten = stapelUnten[kern], *oben = stapelOben[kern];
  groesse = 0;
  if (!unten) return 0;
  groesse = (uint32_t)(oben - unten) * sizeof(uint32_t);
  const uint32_t *p = unten;
  while (p < oben && *p == STAPEL_MUSTER) p++;
  return (uint32_t)(oben - p) * sizeof(uint32_t);
}

void watchdogInfo() {
  meld.printf("# Watchdog:       %s, Kern 0 max. %lu ms, Kern 1 max. %lu ms ohne Herzschlag\n",
              aktiv ? "aktiv" : "AUS", (unsigned long)WATCHDOG_TIMEOUT_MS, (unsigned long)KERN1_TIMEOUT_MS);
  meld.printf("# Kern 1:         letzter Herzschlag vor %lu ms\n", (unsigned long)(millis() - letzteAenderungMs));
  meld.printf("# Letzter Neustart: %s\n", neustartGrund);
  for (uint8_t k = 0; k < 2; k++) {
    uint32_t groesse = 0;
    const uint32_t max = stapelMaximum(k, groesse);
    if (groesse) meld.printf("# Stapel Kern %u:  max. %lu von %lu Byte belegt%s\n", k, (unsigned long)max,
                             (unsigned long)groesse, max * 4 > groesse * 3 ? "  !!! knapp" : "");
  }
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
