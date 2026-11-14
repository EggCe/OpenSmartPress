# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

Firmware for a Raspberry Pi Pico W (RP2040) that reads a 50 kg load cell through an ADS1256 24-bit ADC, drives a
Nema-23 closed-loop stepper (HB808C driver), and runs a production press process: every press is recorded as a
force-over-path curve with statistics, stored in sessions on the Pico's flash, and shown/exported via a web UI over WLAN.
`README.md` is the authoritative user documentation (wiring, commands, data formats, file layout); keep it in sync when
commands, pins, HTTP endpoints, the USB protocol or the file structure change. New commands also go into
`zeigeHilfe()` in `src/befehle.cpp` and the command table in `src/web/seiten/webinfo.h`.

**Language:** All identifiers, comments, serial/web messages and docs are German. Write new code, comments and UI text
in German in the same style (e.g. `Einstellungen`, `verarbeiteBefehl`, `Auftrag`).

## Build / run

- Arduino sketch; the folder must be named `Waegezelle_Test`. All code except `setup/loop/setup1/loop1` lives in `src/`,
  which Arduino compiles recursively. The `.ino` includes headers as `"src/..."`; files inside `src/` include each other
  with relative paths (`"../adc.h"` from `src/web/`).
- Core: "Raspberry Pi Pico/RP2040/RP2350" by Earle Philhower **6.3.2**, board "Raspberry Pi Pico W", Flash Size
  **"2MB (Sketch: 1MB, FS: 1MB)"** (LittleFS holds the sessions; the EEPROM sector is the same in every partition option).
  No extra libraries.
- `wlan_zugang.h` (git-ignored, sketch root) must exist; copy from `wlan_zugang.beispiel.h`. Only `src/web/wlan.cpp`
  includes it.
- Compile check (arduino-cli is installed at `~/bin/arduino-cli`, core already installed):
  ```
  ~/bin/arduino-cli compile --fqbn rp2040:rp2040:rpipicow:flash=2097152_1048576 --warnings all .
  ```
  Must build without warnings. Reference size: ~553 KB of 1 MB flash, ~192 KB static RAM (~70 KB left for heap/stack —
  watch this when adding buffers). Upload: same FQBN with `upload -p COMx` (or the Arduino IDE).
- No automated tests. Functional verification is on hardware via the serial monitor (newline line ending; `h` = help,
  `i` = status) or the web UI at `http://waegezelle.local/`.
- PC tool: `python tools/waegezelle_plot.py [--port COM5] [--rate 2000] [--fenster 10]` (pyserial, numpy, pyqtgraph,
  PyQt5). It switches the firmware into binary mode `m 4` and parses `#CFG` lines.

## Architecture

### Dual-core split (the central design constraint)
- **Core 1** (`setup1`/`loop1` in the `.ino`) owns all ADS1256 access and the motor: `loop1()` calls
  `antrieb::schritt()` and `adcKern1Schritt()`. The DRDY ISR must be attached from core 1, which is why
  `startContinuous()` runs inside the core-1 mailbox handler.
- **Core 0** (`setup`/`loop`) does processing, USB, WLAN and the web server. WLAN code may only run on core 0
  (arduino-pico without FreeRTOS).
- **Core 0 → core 1 ADC jobs** (`src/adc.cpp`): single-slot mailbox (`volatile adcOp` + `__dmb()` barriers); core 0
  busy-waits in the private `adcAusfuehren()`. Public wrappers: `initADS`, `setzeRate`, `adcSelbstkalibrierung`,
  `adcRegisterLesen`. Never call `ads.*` configuration methods from core 0; add a new `OP_*` and wrapper in `adc.cpp`.
- **Core 1 → core 0 samples** flow through the driver's ring buffer (4096 entries, sequence number + timestamp; gaps
  are counted as lost samples). `messwerteAbholen()` (`src/verarbeitung.cpp`) drains it and runs the per-sample
  pipeline: filter → overload/clip monitoring → web live data → running job → USB output.
- **Motor**: `src/treiber/Antrieb.*` runs on core 1; core 0 only uses `sende()`, `stopp()`, `status()`,
  `ereignisHolen()`, `beschaeftigt()`. `src/motor.cpp` converts force limits from grams to counts (tare/factor) and
  sends them in `antrieb::Param` with every command. `antrieb::messwertIsr()` is the ADS1256 DRDY callback, so a
  downward move stops within one step on contact/max force — keep that path short and allocation-free.

### Press process and sessions
- **Core 1** (`Antrieb.cpp`, `BefehlTyp::PRESSEN`): states `PR_ANFAHRT` (rapid to `presseAnfahrt`) → `PR_DRUECKEN`
  (`vPressMmS` to `presseEnde` = `mot.kalEnde`, max force ends early) → `PR_HALTEN` → `PR_ZURUECK`; events
  `PRESSE_FERTIG`/`PRESSE_FEHLER`. During pressing/holding `messwertIsr()` pushes `{t, pos, load counts, phase}` into an
  SPSC ring (`aufzeichnungHolen()`), so force and position come from the same ISR.
- **Core 0** (`src/presse.cpp`): start checks (`presseBereit()` — includes "stroke calibrated this power cycle",
  `motorEndpunktAktuell()`), bins samples into ≤ `PRESSE_PUNKTE_MAX` path segments, computes per-press statistics
  exactly from all samples, saves after the return stroke. Path = mm relative to the tool endpoint (position 0).
- **Storage** (`src/sitzungen.cpp`, LittleFS): `/s/<id>/meta.bin`, `index.bin` (all `PressKopf`s), `pNNNN.bin`
  (`PressKopf` 128 B + `PressPunkt` 4 B each, ≤ 4 KB = one flash block). The browser parses these with `DataView`
  (`src/web/seiten/presse.h`): changing `PressKopf` means updating the JS offsets and `PRESS_VERSION`.
- **Flash writes pause core 1** (ADC ISR, step counting). All writes go through `sitzungen.cpp`, which refuses while
  `antrieb::beschaeftigt()`, and call `watchdogWarten()` + `messTimeoutNeustart()` afterwards.
- Web: `/presse` page, routes in `src/web/presse_web.cpp` (status, sessions, binary curves, CSV/JSON export). Long
  responses call `hintergrund()` (feed watchdog, drain samples). JSON helpers `jsonStart/jsonf/jsonEnde` live in
  `webserver.cpp`. The START button (GP6) starts a press; calibration only via web/`mot kal`.
- **Simulation** (`sim an`, RAM only): `Param::simulation` makes `messwertIsr()` replace the measured load with a
  model (`simLast()` in `Antrieb.cpp`: virtual tool + per-press workpiece, peak 25–35 kg, noise). Step pulses still
  run, so the whole state machine is exercised without a motor; the ADC still clocks the samples. Saved presses carry
  `PressKopf::simuliert`.
- Settings block `PresseEinst` (EEPROM address 128): `anfahrt`, `vpress`, `halt` via `mp`.

### Module conventions
- Module state is `static` inside its `.cpp`; other modules use functions. Exceptions (shared via `extern`): `ads`,
  `adsOk` (adc), `cfg`, `mot`, `pres` (einstellungen), `meld`/`webLog` (meldung), `wlanAn` (wlan), and the read-only web data
  in `src/web/livedaten.h`.
- Pins and hardware limits live only in `src/konfig.h`.
- All user-facing text goes through `meld` (`Meldung : Print`), which writes to USB **and** the web log ring buffer.
  Messages start with `#` so they can be interleaved with binary/CSV frames. `sendeCfg()` emits the machine-readable
  `#CFG k=v …` line used by the PC tool — keep its keys stable.
- Long operations (tare, calibration, noise analysis, multi-rate comparison `nv`) are non-blocking jobs in
  `src/auftraege.cpp`; the multi-rate comparison changes the data rate only from `loop()` via
  `vergleichFaellig()`/`vergleichSchritt()`, never during sample processing.
- After anything that pauses sample flow (mailbox job, flash write, rate change) call `messTimeoutNeustart()` so no
  false DRDY timeout is reported.
- **Watchdog** (`src/watchdog.*`): hardware watchdog (8 s) fed from `loop()` only while core 1's heartbeat
  (`watchdogHerzschlag()` in `loop1()`) keeps changing; a core-1 stall > 1 s triggers `antrieb::notHalt()` and an
  immediate reboot. Any new code where core 0 intentionally waits on core 1, or where core 1 is paused (flash writes),
  must call `watchdogWarten()`. Nothing on core 0 may block longer than 8 s, nothing in `loop1()` longer than 1 s.
- Web pages are C++ raw string literals in `src/web/seiten/`. The browser polls `GET /daten?p=&l=` (JSON built with
  `jsonf` in `src/web/webserver.cpp`) and converts counts → grams/µV itself from `cfg`. No authentication.

### Persistent settings (`src/einstellungen.*`, EEPROM emulation, 256 bytes)
- `Einstellungen` at address 0 (`static_assert` 32 bytes, magic `"WLZ1"`), `MotorEinst` at 64 (`static_assert`
  64 bytes, magic `"MOT1"`), `PresseEinst` at 128 (32 bytes, magic `"PRS1"`), each with CRC-32.
- Changing either layout: keep the asserted size (use reserve bytes) or bump the version — a mismatch silently falls
  back to defaults on load. Saved only on `s`.

### Data formats
USB binary frame (`m 4`): 8 bytes `A5 5A | seq u16 LE | counts i24 LE | CRC-8 (poly 0x07, init 0) over bytes 2–6`.
CSV (`m 2`): `seq,t_us,roh`. `src/usb_ausgabe.cpp` and `tools/waegezelle_plot.py` must change together.
