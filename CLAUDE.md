# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

Firmware for a Raspberry Pi Pico W (RP2040) that reads a 50 kg load cell through an ADS1256 24-bit ADC, drives a
Nema-23 closed-loop stepper (HB808C driver), and runs a production press process: every press is recorded as a
force-over-path curve with statistics, stored in sessions on the Pico's flash, and shown/exported via a web UI over WLAN.
`README.md` is the authoritative user documentation (wiring, commands, data formats, file layout); keep it in sync when
commands, pins, HTTP endpoints, the USB protocol or the file structure change. New commands also go into
`zeigeHilfe()` in `src/befehle.cpp` and the command table in `help.md` (user help, linked from the web UI's
"Hilfe" button via GitHub; there is no help page on the Pico anymore).

**Bilingual docs and UI:** `README.md`, `help.md` and `docs/hardware.md` are German; `README.en.md`, `help.en.md` and
`docs/hardware.en.md` are their English translations — every change to one must be made to the other (same structure;
the press page links `help.en.md#pressing`, `README.en.md` links `help.en.md#curve-resolution`). The web UI is written
in German; `src/web/seiten/sprache.h` (served as `/sprache.js`) translates it in the browser (DE/EN switch,
localStorage `wz_sprache`): static text/`title`/`placeholder` via the `WOERTER` dictionary (key = German text with
collapsed whitespace), strings built in JS via `T("… {0} …", v)`, firmware strings (`zustandText`, `stoppText`,
`presseBereit` reasons) via `TF()`; numbers/dates via `LOC()`, never a hard-coded `"de-DE"`. New UI text needs a
`WOERTER` entry (must stay valid JSON); `python tools/sprache_pruefen.py` (run in CI) reports missing ones. New
firmware texts shown in the UI go into `WOERTER` (and `DE_TEXT` for umlauts) by hand. Don't name a local variable
`T`, `TF`, `LOC` or `SPR` in page scripts. Serial/log messages, commands and `/sitzung/export` stay German.

**Hardware data:** datasheet values of the parts (load cell accuracy, overload limits, excitation range, …) and what
they mean for this setup are in `docs/hardware.md`. Read it before changing limits in `src/konfig.h`, force
thresholds or accuracy claims; add new parts' datasheets there.

**Language:** All identifiers, comments, serial messages and the primary docs are German. Write new code, comments and
UI text in German in the same style (e.g. `Einstellungen`, `verarbeiteBefehl`, `Auftrag`), then add the English
translation (see "Bilingual docs and UI" above).

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
  Must build without compiler warnings. Reference size: ~627 KB of 1 MB flash, ~219 KB static RAM (~43 KB left for
  heap/stack — the IDE's generic "low memory" notice above 75 % is expected; do not add more big buffers; CI fails
  above 224 000 bytes; `wd` shows both cores' stack high-water marks). Upload: same FQBN with `upload -p COMx` (or the Arduino IDE).
- Host tests: `sh tests/host/run.sh` (g++; on this Windows machine `wsl -e sh tests/host/run.sh`). They cover the
  Arduino-free headers `pruefsumme.*`, `rahmen.h`, `welford.h`, `treiber/SensorWaechter.h` — keep those free of
  `<Arduino.h>` and put new pure logic there with a test. `.github/workflows/firmware.yml` runs the tests and the
  compile check (no warnings, RAM limit). Everything else is verified on hardware via the serial monitor (newline
  line ending; `h` = help, `i` = status) or the web UI at `http://waegezelle.local/`.
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
- **Interrupt code lives in RAM** (`__not_in_flash_func`): `ADS1256::isrRoh/isrLesen` (raw GPIO handler via
  `gpio_add_raw_irq_handler`, SPI read via registers — not `attachInterrupt`/`SPI.transfer`), `antrieb::messwertIsr`,
  `pwmIsr`, `anhalten`. The flash cache is shared with core 0 (WLAN). Anything new called from these paths must be
  inline or RAM-placed too (check with `arm-none-eabi-objdump` for `bl` into flash veneers); float math is only on
  the simulation path, 32/64-bit division only after a lost sample.
- **Sensor fault** (`treiber/SensorWaechter.h`, checked in `messwertIsr()` on the raw counts): clipped (same limit as
  `ADS1256::CLIP_GRENZE`, `static_assert` in `adc.cpp`) or 64 identical raw values → `Stopp::SENSOR` stops every
  downward move and `starten()` refuses new ones; upward moves stay allowed. Not checked in simulation. Core 1
  reports changes as `EreignisTyp::SENSOR` from `loop1` (never call `melde()` from an ISR — one producer context).

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
  A deleted press (`sitzungPresseLoeschen`, `sit pl`, `POST /sitzung a=presse_loeschen`) stays in `index.bin` as a
  tombstone (`status = PRESS_GELOESCHT`, `punkte = 0`) and its `pNNNN.bin` is removed: index entry i stays press
  i+1 and numbers are never reused (new nr = entries + 1). Page, export and session counts skip tombstones.
- **Flash writes pause core 1** (ADC ISR, step counting). All writes go through `sitzungen.cpp`, which refuses while
  `antrieb::beschaeftigt()`, and call `watchdogWarten()` + `messTimeoutNeustart()` afterwards.
- Web: `/presse` page, routes in `src/web/presse_web.cpp` (status, sessions, binary curves, CSV/JSON export). Long
  responses call `hintergrund()` (feed watchdog, drain samples). JSON helpers `jsonStart/jsonf/jsonEnde` live in
  `webserver.cpp`. The START button (GP6) starts a press; calibration only via web/`mot kal`.
- **Simulation** (`sim an`, RAM only): `Param::simulation` makes `messwertIsr()` replace the measured load with a
  model (`simLast()` in `Antrieb.cpp`: virtual tool + per-press workpiece, peak force range, noise, crack rate,
  insertion peak). Model parameters live in `SimEinst` (`motor.cpp`, RAM only, `sim <name> <wert>`), go to core 1 in
  `Param::sim*`, are reported as `simp` in `/presse/status`, and the press page's "Parameter …" dialog remembers them
  in localStorage and re-sends them on `sim an`. Step pulses still
  run, so the whole state machine is exercised without a motor; the ADC still clocks the samples. Saved presses carry
  `PressKopf::simuliert`. New `antrieb::Stopp` values go at the end and into `GRUND` in `presse.h` (stored as
  `PressKopf::endGrund`).
- **Raw press data for the browser**: `probeVerarbeiten()` also packs every sample into `roh[ROH_N]` (`RohProbe`,
  8 B: t in 4 µs, load int24, position 23 bit + hold bit; 32 KB). `GET /presse/roh` serves it (48-byte header, see
  `presse_web.cpp`); `presse.h` (web page) polls it during a press, stores raw samples + a per-step curve in IndexedDB
  and prefers that curve when `n === PressKopf::proben`. Analysis smooths raw curves to the Pico segment width so
  statistics stay comparable. Format changes must be made in `presse.cpp`, `presse_web.cpp` and the page together.
- Settings block `PresseEinst` (third block of `/einst.bin`): `anfahrt`, `vpress`, `halt`, `vanfahrt`, `vrueck` via `mp`.
  `anfahrt` is capped to `PRESSWEG_MAX_MM` (45 mm = largest stroke, default `PRESSWEG_STANDARD_MM` 5 mm, `konfig.h`;
  values are clamped on load); the ≤ 960 Pico curve points spread over this path, so it should be set tightly.
  `mp kalrueck` (`MotorEinst::kalRueck100`) sets where core 1 stops after stroke calibration: that many mm above the
  contact, at most `hubMm` (may be above the calibration start), 0 = back to the start position.

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
  `jsonf` in `src/web/webserver.cpp`) and converts counts → grams/µV itself from `cfg`. No authentication, but every
  state-changing route must be POST and start with `if (!webSchreibenErlaubt()) return;` (requires header
  `X-Waegezelle: 1` — CSRF protection); the page JS sends that header on every POST.

### Persistent settings (`src/einstellungen.*`)
- Stored in LittleFS as `/einst.bin` (128 bytes, the three blocks back to back). Save = write `/einst.neu`, read it
  back, `LittleFS.rename()` onto `/einst.bin` (atomic). Load checks each block: file, else the old EEPROM emulation
  (older firmware; also the save target if LittleFS is unavailable), else defaults. Hence `sitzungenStarten()` runs
  before `einstellungenStarten()` in `setup()`.
- `Einstellungen` (EEPROM address 0, `static_assert` 32 bytes, magic `"WLZ1"`), `MotorEinst` (64, 64 bytes, `"MOT1"`),
  `PresseEinst` (128, 32 bytes, `"PRS1"`), each with CRC-32.
- Changing a layout: keep the asserted size (use reserve bytes) or bump the version — a mismatch silently falls
  back to defaults on load. Saved only on `s` (refused while the motor runs, like all flash writes).

### Data formats
USB binary frame (`m 4`): 8 bytes `A5 5A | seq u16 LE | counts i24 LE | CRC-8 (poly 0x07, init 0) over bytes 2–6`,
built by `binaerRahmen()` in `src/rahmen.h` (tested in `tests/host`). CSV (`m 2`): `seq,t_us,roh`.
`src/rahmen.h`/`src/usb_ausgabe.cpp` and `tools/waegezelle_plot.py` must change together.
`PressKopf` field offsets read by the press page are pinned with `static_assert(offsetof…)` in `src/sitzungen.h`.
