# Hardware – datasheets and specifications

**English** · [Deutsch](hardware.md)

Contents: load cell, drive (HB808C driver), linear module with ball screw.

Technical data of the installed parts for reference (also as context for AI assistants). Wiring, pins and
operation are in `README.en.md`, the limits in the code in `src/konfig.h`.

## Load cell

350 Ω full bridge, alloy steel. Values according to the dealer's datasheet (product series; "measuring range 0–2 t"
there applies to the whole series; the installed one is the **50 kg variant**, `NENNLAST_G = 50000` in `src/konfig.h`).

| Parameter | Value |
|---|---|
| Measuring range (series) | 0–2 t, installed: 50 kg |
| Output sensitivity (rated output) | 1.0–2.0 ± 0.05 mV/V |
| Zero balance (output without load) | ± 1 % F.S. |
| Non-linearity | 0.5 % F.S. |
| Hysteresis | 0.3 % F.S. |
| Repeatability | 0.3 % F.S. |
| Creep (30 min) | 0.1 % F.S. |
| Temperature effect on output (span) | 0.1 % F.S. / 10 °C |
| Temperature effect on zero | 0.1 % F.S. / 10 °C |
| Response frequency | 10 kHz |
| Material | alloy steel |
| Impedance | 350 Ω |
| Insulation resistance | ≥ 5000 MΩ / 100 V DC |
| Excitation voltage | 5–10 V (mislabelled as "operating temperature range" in the datasheet) |
| Operating temperature | −20 … 80 °C |
| Safe overload | 120 % F.S. |
| Maximum (ultimate) overload | 150 % F.S. |
| Cable | Ø 3 mm, 2 m |
| Cable tensile strength | 10 kg |

Wire colours: red = +excitation, black = GND, green = signal + (AIN0), white = signal − (AIN1).

### What this means for this setup (50 kg, 5.00 V excitation)

| Quantity | Value at 50 kg F.S. |
|---|---|
| Signal at rated load | 5–10 mV (1.0–2.0 mV/V × 5 V), typically ~7.5 mV → start factor `STANDARD_FAKTOR` ≈ 16.1 counts/g |
| Zero point without load | up to ± 500 g or ± 50–100 µV → removed by tare, well within ± 78 mV (PGA 64) |
| Non-linearity | ± 250 g – cannot be corrected with a single factor (one-point calibration); choose a calibration weight close to the working range |
| Hysteresis / repeatability | ± 150 g each – lower limit for the significance of individual presses, independent of the ADC noise |
| Creep | ± 50 g in 30 min under constant load |
| Temperature | ± 50 g / 10 °C on zero and span – tare again after a temperature change |
| Safe overload | 60 kg = `ALARMGRENZE_G` (120 %) |
| Ultimate load | 75 kg (150 %) – mechanical limit, the motor force must stay safely below it |

Notes:

- The datasheet accuracy (0.3–0.5 % F.S.) is much coarser than the ADC resolution (≈ 0.06 g/count).
  Noise values of a few grams are achievable, but absolute force values are only accurate to about ± 0.5 % F.S.
  – judge force limits and statistics accordingly.
- 5 V excitation is at the lower end of the permitted range (5–10 V); a higher excitation would give more signal, but
  is fixed by the LT3042 design and the ADS1256 input range (5 V supply, buffer on).
- The response frequency (10 kHz) does not limit the measurement dynamics: the ADS1256 data rate at the rates used
  is in the same range or below, and the mechanical arrangement (tool, spindle) is slower.
- Do not use the cable as strain relief (max. 10 kg).

## Drive: HB808C driver (Nema 23, closed loop)

Values according to the dealer's description.

| Parameter | Value |
|---|---|
| Type | DSP stepper motor driver with closed control loop (encoder feedback) |
| Max. peak current | 13 A |
| Pulse response frequency | 200 kHz (max. step frequency at PUL) |
| Microstepping | 200–25 600 pulses/revolution (ppr) |
| Input voltage | 24–80 V DC |
| Protection functions | overvoltage, overcurrent, tracking error (position deviation), overload → alarm output ALM |
| Signal input | PUL/DIR or CW/CCW (switchable), differential |
| Signal level | 4.5–28 V, no series resistor needed – 3.3 V from the Pico is **not** enough, hence a level shifter (see `README.en.md`) |

Consequences for the firmware:

- With the MOSFET level shifter the current of the optocoupler LED (approx. 7–16 mA, at 5 V rather at the low end)
  flows through the MOSFET into the Pico pin. The firmware therefore sets GP2/GP3/GP4 to 12 mA drive strength in
  `antrieb::init()` (RP2040 default: 4 mA).
- `schritteProMm` (`mp spmm`) = ppr set on the driver / spindle pitch, e.g. 1600 ppr / 5 mm = 320 steps/mm
  (set: 4000 ppr → 800 steps/mm, see below).
- Step frequency = speed × `schritteProMm`; must stay below 200 kHz (at 320 steps/mm and
  100 mm/s: 32 kHz).
- On a tracking error/overload (e.g. tool blocked, force too high) the driver goes into alarm and the motor
  is de-energised → evaluate ALM on GP5 (`mp alm`); afterwards the position is no longer reliable (recalibrate).
- The datasheet gives no motor torque; the press force is therefore only limited via the load cell.

### Connection and settings (verified on the setup)

**Motor encoder cable** on the HB808C's 6-pin encoder terminal. The colours differ from the usual tables (where e.g.
orange or white = GND) – on this motor **black = GND**:

| Terminal | Signal | Wire |
|---|---|---|
| 1 | EB+ | yellow |
| 2 | EB− | orange |
| 3 | EA+ | green |
| 4 | EA− | blue |
| 5 | VCC (+5 V from the driver) | red |
| 6 | EGND | black |

Symptom of a wrong GND/swapped pairs: the motor holds at standstill and springs back, but hums loudly as soon as it
should move, and the driver trips on a tracking error (red LED). The assignment can be checked without power using
the diode test (red probe on GND → every signal wire approx. 0.6 V, reverse OL) and with power on the levels (LOW
< 0.5 V, HIGH approx. 4–5 V, the two wires of a pair always opposite). The test command `enc` and the sketch
`tools/motortest/` additionally count the encoder on the Pico.

**DIP switches** (manual: SW1–4 microstepping, SW5 direction, SW6 control mode, SW7/8 motor type; the dealer's table
wrongly labels the microstep columns SW5–SW8): SW1–4 = ON OFF ON OFF → **4000 pulses/rev**, i.e. `mp spmm 800` at
5 mm pitch; SW5 OFF (CCW), SW6 OFF (FOC), SW7/8 = OFF/ON (57 mm closed-loop motor).

**Fault codes** of the red LED (blinks in a 5 s cycle; reset only by power cycling): 1× overcurrent,
2× overvoltage (> 90 V), 5× tracking error out of tolerance. Undervoltage shutdown below 15 V.

**Control inputs:** common anode (PUL+/DIR+/ENA+ at 5 V, the level shifter pulls PUL−/DIR−/ENA− to GND) with
`mp inv 1`; ENA open = enabled. Pulse ≥ 1.5 µs, DIR ≥ 1 µs before the edge (firmware: 5 µs and 20 µs).
Input current 6–16 mA. Supply 24–80 V, recommended 36/48 V.

The same offer includes an open-loop variant with a **DM556** driver (20–50 V DC, 400–25 600 steps/rev) – it is
**not** installed.

## Linear module with ball screw

Values according to the dealer's description (model GGP5080, series with Nema 17/23 mount).

| Parameter | Value |
|---|---|
| Spindle | SFU1605 (Ø 16 mm, pitch 5 mm); the series also comes with SFU1204 / SFU1610 – **check the installed variant** (assumption: SFU1605) |
| Accuracy class | C7 |
| Repeatability | 0.02–0.05 mm |
| Effective stroke | 50–300 mm selectable (firmware: `hubMm` ≤ 45 mm) |
| Total length | effective stroke + 120 mm |
| Carriage | 50 × 80 mm |
| Guide | 2 × round rod Ø 12 mm |
| Max. load horizontal | 40 kg |
| Max. load vertical | 20 kg |
| Max. speed | 100 mm/s |
| Material | aluminium ("aircraft aluminium") |

Consequences for this setup:

- **Force limit:** Vertically the module is only specified for a 20 kg payload, the load cell and the firmware
  allow up to 50 kg (`NENNLAST_G`, `maxKraftG`). The specification refers to the moving load (guide/bearings); the
  spindle itself (SFU1605, dynamic load rating in the kN range) holds considerably more, but guide, mount and
  motor coupling are the weak points. For presses above ~20 kg watch for deformation and play; do not set `mp fmax`
  unnecessarily high.
- **Path resolution:** At 5 mm pitch and 1600 ppr one step is 3.1 µm, much finer than the
  repeatability (20–50 µm) – path values in the press curves are accurate to about ± 0.05 mm, plus the
  elastic deformation of frame and load cell under load.
- **Speed:** rapid traverse and `vpress` at most 100 mm/s.
- Ball screws are not self-locking: without motor current (ENA off, emergency stop, driver alarm) the
  carriage can run down under its own weight or residual force when mounted vertically.
