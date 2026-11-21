# Help & tips – EcklC OpenSmartPress

**English** · [Deutsch](help.md)

This page explains every control and every figure of the web interface and gives recommendations for typical tasks.
All values refer to this setup: 50 kg load cell with 1.0–2.0 mV/V, 5.00 V excitation from the LT3042,
ADS1256 with PGA 64. Wiring, data formats and file layout are in the [README](README.en.md).

The web interface can be switched between German and English (**DE / EN**, measurement page top right, press page
in the left menu). The log and the Pico's messages stay German; commands are German words (e.g. `an` = on,
`aus` = off, `ja` = yes) and are explained in the [command table](#commands).

**Contents:** [Quick start](#quick-start) · [Signal path](#signal-path) · [Controls](#controls) ·
[Figures](#figures) · [Data rate](#data-rate) · [Tips](#tips) · [Calibration](#calibration) ·
[Drive](#drive) · [Pressing](#pressing) · [Commands](#commands)

---

## Quick start

### Recommended basic settings for weighing

| Setting | Value | Why |
|---|---|---|
| Data rate | 2000 SPS | In the noise comparison, 2000 SPS with averaging was the quietest (σ @10 SPS 0.59 g versus 1.46 g at 100 SPS). |
| Smoothing | 0.5 s | Steady reading that still reacts to load changes after about 0.25 s. |
| Statistics | 2–5 s | Enough values for a stable σ, but short enough to see changes. |
| Window | 10 s | Shows settling and drift without details disappearing. |
| Min/max band | on | Shows the noise width around the line. |

### Procedure after power-up

1. Let the device **warm up for 15–30 minutes**. Cell, reference and ADC drift most in the first minutes.
2. Set data rate and smoothing as above.
3. Unload the cell, wait briefly until the line is steady, then press **Tare**.
4. If not done yet: [calibrate](#calibration) once.
5. Save with `s`. Tare, factor, smoothing, data rate and output mode are saved.

---

## Signal path

### From weight to number

**Load cell** (350 Ω full bridge, 5–10 mV at 50 kg = 1.0–2.0 mV/V × 5 V)
→ **ADS1256** (24 bit, PGA 64, range ±78.125 mV, 1 count = 9.313 nV)
→ **Pico, core 1** (reads every value immediately after the DRDY signal and buffers it)
→ **Pico, core 0** (smoothing, statistics, overload warning, WLAN and USB)
→ **Browser / USB** (measurement page 50 points per second, press page every single value, over USB every single value)

At 50 kg the signal uses only about one eighth of the ADC range. The rest is headroom so that an overload
can still be measured.

Conversion: `grams = (counts − tare) / factor` and `µV = counts × 0.0093132`.
Tare is the zero point in counts, the factor is counts per gram (most recently about 10.06).

---

## Controls

### Operate and Setup

At the top right (measurement page) or in the left menu (press page), **Operate / Setup** selects how much the pages
show. The browser remembers the choice for both pages; a new browser starts with **Operate**.

- **Operate** – only what production needs: tare, calibrate stroke, select session, START/STOP,
  chart, presses, trend and statistics.
- **Setup** – additionally data rate, smoothing, statistics, noise tests, calibrate scale, save,
  motor jog mode, simulation, press parameters, analysis settings, renaming/deleting sessions,
  log and command line.

This is a view selection, not access protection.

On both pages:

- **STOP bar** – While the drive is moving, a bar with state, position, force and **STOP** appears at the bottom
  centre, even when scrolled further down. The **Esc** key also stops.
- **Feedback** – After pressing a button, the Pico's answer appears at the bottom right for a few seconds
  (yellow = note, red = error). Error messages from the Pico always appear there. Everything also stays in the log.
  These messages come from the firmware and are German.
- **No connection** – If the Pico does not answer for more than about 1 s, a red bar appears at the top and all
  command buttons except STOP are locked until the connection is back.
- **No reload while moving** – While the drive is moving, the browser asks before reloading or leaving the page;
  the Pico then only delivers a short wait page, which reloads by itself when the move has finished.
  (Sending the whole page would hold up the Pico so long that samples of the press would be lost.)
- **Language** – **DE / EN** switches the interface language immediately, without a reload (so also during a
  press). Numbers, dates and the browser's CSV exports follow the language.
- Hovering over underlined terms (e.g. indicators) shows a short explanation.

### Controls on the measurement page

Controls marked (S) only appear in Setup mode.

| Control | What it does | Tip |
|---|---|---|
| Data rate (S) | How often the ADC measures per second (2.5 to 30000 SPS). After every change the ADC briefly self-calibrates. | See [Data rate](#data-rate). 2000 SPS for weighing. |
| Display | Unit for the weight display and chart: grams, µV (bridge voltage) or counts (raw value). | µV and counts help with troubleshooting because they do not depend on tare and factor. |
| Window | How many seconds the chart shows (1–600 s). | Short (2–5 s) for fast events, long (60 s and more) for drift. |
| Smoothing (S) | Moving average over the selected time. Applies to the weight display and the green line, not to the statistics. | The display lags by about half the smoothing time. |
| Raw signal (S) | Shows or hides the pale, unsmoothed line. | Off if only the smoothed history matters. |
| Statistics (S) | Over how many seconds the live statistics are calculated. Then a new round begins. | 2–5 s for everyday use, 30–60 s for accurate noise values. |
| Min/max band (S) | Green area between the smallest and largest raw value per chart point (every 20 ms). | If the band suddenly gets wider, a disturbance has been added. |
| Pause | Freezes the displayed history. Measuring continues. | In the frozen history you can still zoom and pan. |
| Y scale | **Auto** adjusts the axis continuously. **Auto with zero** keeps the zero line in view. **Fixed** uses the "from" and "to" values in the selected unit. | In the chart: mouse wheel zooms around the pointer, dragging pans, double-click switches to Auto. With a fixed scale "▲ values above" or "▼ values below" shows that something is cut off. Changing the unit switches the scale to Auto. |
| Tare | Averages for about 2 s and sets this value as the zero point. | Only press with an unloaded, steady cell. |
| Calibrate scale … (S) | Opens the calibration dialog: unload and tare, place a known weight and enter its weight in grams, save the result. | See [Calibration](#calibration). |
| Save (S) | Saves tare, factor, smoothing, data rate and output mode permanently (like `s`). | Without saving, changes only last until restart. |
| Noise test (S) | Measures about 5 s unsmoothed and writes a detailed noise analysis to the log. | Do not touch the cell meanwhile. |
| Noise comparison (S) | Measures at several data rates in turn and outputs a comparison table. | Takes about 25 s. Afterwards the previous data rate is restored. |
| Info (S) | Writes the current device status to the log. | Also shows lost values and the WLAN connection. |
| Cancel job | Cancels tare, calibration or noise test (like STOP, so it also stops the motor). | If nothing is running, it switches the USB output off. |
| Save CSV | Downloads the chart data: time, mean, min, max and smoothed value, 50 rows per second. | For every single sample use recording over USB with `waegezelle_plot.py`. |

---

## Figures

### Live statistics

The statistics use **every single, unsmoothed sample** of the last statistics round,
not the 50 chart points per second. So they show what the ADC really delivers.

- **Mean** – average of all values of the round, in counts, µV and grams.
- **Std. dev. σ** – standard deviation, i.e. the typical noise **of a single sample**. About two thirds
  of all values lie within ±1 σ of the mean. Rises with the data rate because the ADC averages less internally at
  high rates. Therefore not a good measure of reading accuracy on its own.
- **Peak-to-peak** – distance between the smallest and largest value of the round. About 6–7 σ for pure noise;
  much more points to outliers, knocks or interference.
- **σ @10 SPS** – scatter of the means over 0.1 s each. This is **the most important figure for comparing
  data rates**, because it measures the same thing at every rate: how accurate a 0.1 s mean is. It is measured, not
  estimated, so it also contains slow noise and drift. 50 Hz hum drops out because 0.1 s is exactly
  5 mains periods.
- **σ smoothed** – scatter of the smoothed value, i.e. **how steady the weight display is with the selected
  smoothing**. Only appears with smoothing switched on. For purely random noise it falls with the square root of the
  smoothing time: smoothing four times as long halves the noise. Smoothing does not remove slow drift.
- **Effective resolution** – `ENOB = log2(2²⁴ / σ)`, in bits, relative to the full ADC range ±78.125 mV.
  Each additional bit halves the noise.
- **Noise-free** – `log2(2²⁴ / (6.6 × σ))`. 99.9 % of all values lie within ±3.3 σ; that many bits can be read
  practically without flicker. Always about 2.7 bits less than the effective resolution.
- **Relative to rated load** – `50 000 g / σ in grams`: into how many distinguishable steps the range 0–50 kg can be
  divided. More meaningful than the effective resolution, because the cell only uses one eighth of the ADC range.
  The line "After averaging to 10 SPS" calculates the same with σ @10 SPS.
- **Measured rate** – actual data rate from the time stamps. Small deviations from the nominal value (below 0.01 %)
  come from the crystal and are normal.
- **lost** – conversions that were not read. Short gaps occur when saving (`s`), changing the rate
  and reading registers. If the counter rises during normal operation, the data rate is too high for processing.
- **CLIPPED** – the ADC was at its limit. With a load cell this almost always means: broken wire, short circuit
  or missing excitation. Do not use values from this round.

### Status display top right

- **measuring** (green dot): the ADC delivers values. Red means stopped, not initialised or no connection to the Pico.
- **WLAN … dBm**: signal strength. Better than −67 dBm is good, worse than −80 dBm becomes unreliable.
- **lost**: total of all lost values since start-up.
- **job running**: tare, calibration or noise test is active. Other commands are rejected meanwhile.

---

## Data rate

### More values, more noise per value

At a low rate the ADS1256 averages internally over many samples, each value is steady. At a high rate
there are more, but noisier values. What matters is how accurate the result is **after averaging**.
That is what σ @10 SPS shows.

| SPS | σ single value | σ @10 SPS | Suitable for |
|---|--:|--:|---|
| 10–60 | – | – | Long-term measurement with little data. 50 SPS suppresses 50 Hz mains hum particularly well. |
| 100 | 25.9 counts · 2.6 g | 1.46 g | Simple display, little data volume. |
| 1000 | 59.6 counts · 5.9 g | 1.03 g | Slow force profiles. |
| 2000 | 76.2 counts · 7.6 g | 0.59 g | **Weighing and calibrating**, best value in the comparison. |
| 3750–7500 | 138.6 counts · 13.8 g (7500) | – | Fast force profiles, pressing. |
| 15000–30000 | – | – | Only for very short events (impacts). High load for Pico and WLAN. |

Values from our own measurements: noise comparison at 100/1000/2000 SPS and a calibration run at 7500 SPS, factor
10.06 counts/g. A single 3 s run is a rough estimate; for reliable figures run `nv 10 2000 100 2000 100`.

---

## Tips

**Static weighing**
- 2000 SPS, smoothing 0.5–1 s.
- Place the weight in the centre. Flat-diaphragm cells react to off-centre loads.
- After placing, wait 2–5 s until the green line is flat.
- For the last digit: smoothing 2 s and watch "σ smoothed".

**Force profiles and pressing**
- 2000–7500 SPS, smoothing off or 0.02–0.05 s, otherwise peaks are flattened.
- Min/max band on: shows peaks even between two chart points.
- Window 2–10 s, use Pause to look at a section.
- Presses: keep the press page open, then the browser gets every single sample (see [Raw data](#raw-data-in-the-browser)).

**Choosing the smoothing**
- Delay ≈ half the smoothing time: at 2 s the display reacts about 1 s later.
- Four times longer smoothing halves random noise.
- Smoothing does not help against drift and creep, only tare and warm-up do.
- Statistics and jobs (tare, calibration) always use raw values.

**Stable readings**
- Let it warm up for 15–30 minutes, then tare.
- Tare again before every measurement series.
- After a large load the cell creeps for a few minutes. Wait briefly before taring.
- Avoid draughts, vibration and temperature changes at the cell.

**Measuring noise**
- Cell unloaded, touch nothing, do not bump the table.
- Measure data rates alternately, e.g. `nv 10 2000 100 2000 100`, so that drift does not penalise one rate.
- Check the WLAN influence: over USB `wlan aus`, `nv 5`, then `wlan an`, `nv 5`.

**Setup**
- Supply the ADS1256 from the LT3042 instead of USB 5 V.
- Star ground at the ADS1256's AGND, separate wires for Pico and cell.
- Twist green/white (AIN0/AIN1) and keep them away from the Pico.
- "CLIPPED" with an unloaded cell: check the wiring.

> **Overload:** From 50 kg (100 %) a warning appears, from 60 kg (120 %) an alarm, and the LED on the Pico lights up.
> Unload immediately. The limits use the factor; without calibration they are only estimated.

---

## Calibration

### Determining the factor

The easiest way is **"Calibrate scale …"** on the measurement page (Setup mode): the dialog guides you through tare,
calibration with the known weight and saving, and shows the new factor. If the press page says "Scale not
calibrated", the link there opens this dialog. Manually or over USB:

1. Data rate 2000 SPS, device warmed up.
2. Unload the cell, wait until steady, **Tare**.
3. Place a known weight in the centre, **preferably 5 kg or more** (10 % of the rated load). Wait about 10 s.
4. Enter the command `k 5000 10000`: weight in grams, then number of values (10000 = 5 s at 2000 SPS).
5. Check the result in the log: a sensitivity of 1.0–2.0 mV/V is plausible, the uncertainty of the factor
   should be below 0.05 %.
6. Save with `s`.

Small weights work, but errors grow up to 50 kg: with 100 g every error is magnified about 500 times.
A factor of 10.06 counts/g corresponds to 0.94 mV/V, just below the cell's specification. Recalibrating with a large,
precisely known weight is worthwhile.

If the change in counts is already known, it can also be set directly: `f <counts / gram>`, e.g. `f 10.06`.

---

## Drive

### Closed-loop stepper motor with HB808C

The Pico outputs step and direction pulses; encoder and control loop are inside the driver. During every
downward move the Pico checks **every single sample** of the load cell and stops the motor within one step
as soon as contact or the maximum force is reached. The motor may always move upwards.

| Pico | Driver / push-button | Note |
|---|---|---|
| GP2 | PUL | The HB808C needs 4.5–28 V at its inputs (PUL, DIR, ENA). Connect via a 5 V level shifter (e.g. 74AHCT125) or common anode at 5 V with a transistor to GND; then `mp inv 1`. |
| GP3 | DIR | like PUL |
| GP4 | ENA (recommended) | like PUL. Route through the emergency stop circuit so that the emergency stop de-energises the motor even without the Pico. |
| GP5 | ALM (optional) | alarm output of the driver, e.g. on a following error. Activate with `mp alm 1` or `2`. |
| GP6 | START push-button to GND | starts a press (when everything is ready). |
| GP7 | STOP push-button to GND | stops immediately (software). Does not replace an emergency stop. |
| GP10 / GP11 | encoder EA+ / EB+ (troubleshooting only) | each via a voltage divider, 10 kΩ in series, 20 kΩ to GND (the encoder outputs 5 V, the Pico only tolerates 3.3 V); Pico GND to EGND. For the `enc` command, disconnect afterwards. |

### Setting up

1. `mp spmm <value>`: steps per mm = microsteps per revolution × gear ratio / spindle pitch.
   Example: 1600 steps, direct, 5 mm pitch → `mp spmm 320`. The motor does not move before that.
2. Calibrate the scale and set the data rate to 2000 SPS. Without a calibrated factor and below 500 SPS the drive
   does not move.
3. Check the direction: `mot ab 1` must move down (clockwise). Otherwise `mp dir 0` or `mp dir 1`.
4. Check the distance: measure `mot ab 10` with a caliper, adjust `spmm` if it deviates.
5. Test the stop: move down slowly and press on the cell by hand. The motor must stop immediately.
6. Save with `s`.

### Calibrating the stroke (end point)

Starts with the **"Calibrate stroke"** button on the measurement or press page or `mot kal`. The tool rests on the
cell, without workpiece. Required after every power-up, because the motor position is unknown then.

1. Slowly down (`vsuch`, default 1 mm/s), at most over the 45 mm stroke.
2. If the load rises by more than `kontakt` (default 100 g) compared with the value before the move, the
   **tool end point** is reached. It becomes the new zero point.
3. On to the **actual end point**: contact + `ueber` steps (default 10), or earlier at `fmax`
   (default and upper limit 50 kg).
4. Back to the start position. The log shows contact depth, end point, force at the end point and the stiffness in kg/mm.

Without a load rise over the whole stroke, the move aborts with "no contact". On every abort the motor stays at the
abort position; move it up again with `mot auf <mm>`.

**Sensor fault:** If the reading is at the ADC's limit (e.g. broken wire, missing bridge excitation) or does not
change at all for 64 values (ADC or SPI hangs), the force is unknown. Then every downward move stops immediately
("sensor fault"), and no new one starts. Moving up with `mot auf <mm>` remains possible. The log shows
"SENSORFEHLER" (sensor fault) and "Sensor wieder plausibel" (sensor plausible again) as soon as the readings have been
fine for 0.5 s. Not checked in simulation.

> **Force reserve:** A Nema 23 motor on a spindle can push many times the 50 kg rated load.
> The force limit in the Pico is fast, but it is software. Provide a hard-wired emergency stop that disconnects the
> motor supply or ENA, and a mechanical end stop. Make the first moves with a small limit
> (`mp fmax 2000`).

### Parameters

| Name | Meaning | Default |
|---|---|---|
| `spmm` | steps per mm, 0 = locked | 0 |
| `dir` | DIR level for "down" | 1 |
| `inv` | outputs inverted (common anode with transistor) | 0 |
| `ena` | 0 unused, 1 opto on = motor off, 2 opto on = motor on | 0 |
| `alm` | 0 off, 1 alarm on LOW, 2 alarm on HIGH | 0 |
| `vsuch` | speed of contact search and overtravel (mm/s) | 1 |
| `vfahrt` | speed of jog mode and return (mm/s) | 5 |
| `acc` | acceleration (mm/s²) | 20 |
| `hub` | maximum search path (mm, at most 45) | 45 |
| `ueber` | steps after contact (overtravel) | 10 |
| `kontakt` | load change that counts as contact (g) | 100 |
| `fmax` | maximum force, downward move stops immediately (g, at most 50 000) | 50 000 |

The step frequency is between 16 Hz and 20 kHz. Microstepping of 1600–3200 per revolution is sufficient with the
closed-loop driver.

---

## Pressing

### Procedure on the press page (`/presse`)

At the top, the procedure (with live force, START/STOP) and the chart fill the screen; indicators, presses, trend,
analysis, statistics, sessions, parameters and log follow below. The left menu (status, jump links)
can be collapsed with « to make the chart wider.
The three colour dots at the bottom of the menu (top right on the measurement page) select the colour scheme:
green/black, red/black or blue/light. The browser remembers the choice for both pages; the same applies to the
language (DE / EN).

1. **Place the tool and tare.** The weight of the tool is subtracted.
2. **Calibrate stroke** (without workpiece): the motor moves onto the tool, stores the tool end point (path 0)
   and the actual end point, and returns.
3. **Select or create a session** ("Select session …": create a new session with the suggested name "Batch
   YYYY-MM-DD" or open one of the last ones). Every press is saved immediately in the active session.
4. **Insert the workpiece and START** (button or push-button): rapid traverse to the approach point, then at press
   speed to the calibrated end point (or to the maximum force), hold time, back to the start.

### Chart and indicators

Each press appears as a **force over path** curve; all presses of the session are overlaid. The path counts
from the tool end point: the workpiece is therefore touched at a negative path. Clicking a curve or a
table row shows its indicators and the deviation from the session mean.

Status **completed** means: the press ran to the end without an abort. This is **not** a good/bad statement
about the part – check indicators, suspected crack and trend for that.

- **Peak force** – largest force of the press and the path at which it occurred.
- **Final force** – mean of the last approx. 10 ms at the end point; with a hold time also the force at the end of
  the hold time.
- **Contact, press depth** – contact: first sample above the contact threshold (`mp kontakt`). Press depth: path from
  contact to the end point.
- **σ, range** – standard deviation and max − min of all force values from contact to the end point. The
  session statistics additionally show mean, σ and range of every indicator over all valid presses – the
  measure of uniformity.
- **Press work** – area under the curve from contact (force × path) in joules.

### Curve resolution

The Pico always calculates the indicators above exactly from **all** samples. There are two sources for the curve:

| Source | Resolution | Where stored |
|---|---|---|
| Pico curve | at most 960 path segments over the press path (approach point to end point, at most 10 mm, default 5 mm), force averaged per segment, 0.02 N steps. Example: 5 mm press path at 320 steps/mm → 2 steps = 6.25 µm per point. | Pico flash (always) |
| Raw data | **every single sample** with time (4 µs), position (1 motor step) and force (1 count ≈ 0.001 N), including the hold time. The curve has one point per motor step (e.g. 1/320 mm = 3.1 µm). | in the browser (IndexedDB) |

Finer than one motor step is not possible, because the Pico only knows the steps it outputs (the encoder stays in the
driver). The force is limited by noise (σ ≈ 0.075 N per single value at 2000 SPS), not by the
24 bits of the ADC.

#### Raw data in the browser

As long as the press page is open, the browser fetches the new samples from the Pico every 100 ms during the press
(buffer in the Pico: 4096 values, i.e. about 2 s at 2000 SPS). After saving it stores them under session and
press number in the IndexedDB. The Pico does no extra calculation for this; the curve from each motor step, the
analysis and the export run in the browser.

- Presses with raw data are marked **R** in the table. The details show "Raw data CSV": time, path,
  force and phase of every sample.
- Raw data only exists in the browser that was open during the press. If it is missing (page closed, WLAN gap longer
  than the buffer), the page automatically uses the Pico curve.
- The press page smooths both sources for display and analysis with a moving average over **0.05 mm** of path.
  That is exactly one period of the drive's force ripple (40 steps at 800 steps/mm = 1/100 revolution of the 5 mm
  screw), which averages out this way. Session statistics and trend stay comparable, and the positions (e.g. path
  of the joining force) become more precise with raw data. The Pico's indicators (Fmax, Fmean, …) and the raw data
  CSV stay unsmoothed.
- Clearing browser data also removes the raw data. Save important presses as "Raw data CSV".

### Analysis ("Analysis" card)

Calculated in the browser from the stored curves; windows and thresholds are adjustable and apply to all
presses. **Joining depth** = path from contact to the end point minus "Exclude end stop".

- **Path from contact** – curves aligned at the contact point: differences in tube length and insertion height
  disappear, you compare the material.
- **Joining force** – largest force up to the end stop – the force of the joint, not of the tool.
- **Insertion peak** – first local maximum in the first 40 % of the joining depth (chamfer, expansion begins).
  Indicates chamfer, burr and diameter.
- **Plateau line** – linear regression in the plateau window. **Slope** (force per mm) ≈ coefficient of friction ×
  surface pressure × circumference; **intercept** = base force of the expansion; **smoothness** = scatter around the
  line (stick-slip, beginning cold welding). Plus the work in the plateau window.
- **Force drop, suspected crack** – largest force decrease within the set distance in the joining range. From the
  threshold (default 5 % within 0.05 mm) the press is marked as suspected crack – typical for a thin-walled tube
  bursting open.
- **Path windows** – force at fixed paths from contact (default 1, 2, 3, 4 mm), basis for later OK/NOK windows.
- **Trend** – an indicator over the press number with mean and ±3σ. A slow rise of the plateau force or the
  smoothness value points to wear or decreasing lubrication.

"Analysis as CSV" exports all indicators and the statistics in the selected force unit.
The force unit (N, kg or lbs) can be selected above the chart; it also applies to the CSV export.
Data is always stored in N.

### Settings

The workpiece path is limited mechanically by a stop screw on the press head (approx. 5 mm). Pressing and
recording therefore happen at most **10 mm** above the calibrated tool end point (5 mm path plus reserve,
`PRESSWEG_MAX_MM` in `src/konfig.h`); above that the drive moves at rapid traverse.

`mp anfahrt <mm>` (0–10, default 5): from this many mm above the tool end point, pressing happens at press speed;
choose slightly higher than the top edge of the workpiece. Older stored values above 10 mm are limited to 10 mm at
start-up. The 960 points of the Pico curve are at most 5.2 µm apart at 5 mm, at most 10.4 µm at 10 mm – set it
as tightly as possible.
`mp vpress <mm/s>`: press speed. `mp halt <ms>`: hold time at the end point. Then `s`.

On the press page (Setup mode) these values are under **Press parameters**, together with **Max. force** (`mp fmax`)
and **Contact at** (`mp kontakt`) in the selected force unit (the Pico calculates in grams). Changed fields are
marked yellow until they are sent with "Apply"; only changed values are sent. Permanent only with
"Save to flash".

### Simulation without motor

`sim an` or the switch on the press page (Setup mode): the force is calculated from the position – virtual tool
below the current position, per press a workpiece with a random peak force. ADC and cell must be
connected, `mp spmm` set. Then calibrate the stroke and press. Simulated presses are marked;
after a restart the simulation is off. While it runs, a yellow-striped bar with "End simulation" appears at the top
of the press page – in both modes.

The **"Parameters …"** button next to the switch opens the simulation window (or `sim par` / `sim <name> <value>`):

| Field | Command | Default | Meaning |
|---|---|---|---|
| Press speed | `mp vpress <mm/s>` | 1 | as in the press parameters |
| Tool distance | `sim werkzeug <mm>` | 15 | virtual tool this far below the position when switching on (= stroke to the tool); changing it resets the tool, then calibrate the stroke |
| Joining path | `sim weg <mm>` | 3 | height of the workpiece above the tool (at most 10 mm, at most 98 % of the approach point) |
| Peak force from / to | `sim fmin <g>` / `sim fmax <g>` | 25 / 35 kg | random peak force per press (in kg in the window) |
| Noise | `sim rauschen <g>` | 30 | noise ± in grams |
| Crack rate | `sim riss <%>` | 8.3 | share of presses with a simulated crack (force drop 15 %) |
| Insertion peak | `sim spitze <%>` | 12 | bump at the chamfer in % of the peak force, 0 = none |

The Pico only keeps the values until restart. The browser remembers the last applied values and sends them again
when the simulation is switched on. The window also shows how long a press takes and how many
samples it delivers.

### Sessions

The Pico's flash has room for about 200 presses. Sessions can be downloaded as CSV (for Excel: semicolon,
decimal comma, German headers) or JSON. Then delete old sessions.

> The force limit stops the motor quickly, but does not replace a protective device (emergency stop, guard,
> two-hand control).

---

## Commands

Enter in the field below the log or in the serial monitor (line ending "New Line"). A decimal comma also works.
Commands and the firmware's answers are German; the English meaning of the German words is given in brackets.

| Command | Function |
|---|---|
| `t [n]` | tare from n values (default about 2 s) |
| `k <g> [n]` | calibrate with a known weight in grams |
| `f <c/g>` | set the factor directly (counts per gram) |
| `r <sps>` | data rate: 2.5 5 10 15 25 30 50 60 100 500 1000 2000 3750 7500 15000 30000 |
| `fl <s>` | smoothing in seconds (0–60, `fl 0` or `fl aus` = none) |
| `ws <s>` | duration of the live statistics (0.5–60 s) |
| `n [n]` | noise analysis over n values (default about 5 s) |
| `nv [s] [sps …]` | noise comparison of several data rates, s seconds each |
| `s` | save settings permanently |
| `i` | show status |
| `m <0..4>` | USB output: 0 off, 1 readable, 2 CSV, 3 plotter, 4 binary |
| `mot` | drive: status, position, last calibration |
| `mot kal` | calibrate stroke (end point calibration, after every power-up) |
| `mot ab\|auf <mm>` | jog mode (down / up); `mot s <n>` = n steps (+ down) |
| `mot null\|ein\|aus` | set position 0 (zero), motor current on / off |
| `mot stop` | stop (also `x`, Esc in the browser, STOP push-button) |
| `mp [name value]` | show or set drive and press parameters (e.g. `anfahrt`, `vpress`, `halt`) |
| `pr` / `pr x` | start a press (like the START push-button) / abort |
| `sim [an\|aus]` | simulation without motor (on / off; force calculated from the position) |
| `sim par` / `sim <name> <value>` | show / set simulation parameters: `werkzeug weg fmin fmax rauschen riss spitze` (tool, path, min/max force, noise, crack, peak) |
| `sit` | show sessions; `sit neu <name>` (new), `sit wahl <no>` (select), `sit zu` (close), `sit loeschen <no> ja` (delete) |
| `enc` | encoder test: counts EA/EB on GP10/GP11. Turn the shaft by hand exactly 1 revolution: counts = 4 × line count. During `mot s <n>`, "Zaehlschritte/Motorschritt" (counts per motor step) shows whether encoder and driver setting match. `enc 0` = zero the counter, `enc aus` = stop |
| `wlan [an\|aus]` | WLAN status or switch on/off (on again only over USB) |
| `wd` | watchdog status, reason for the last restart and highest stack usage of both cores (`wd test 0\|1` hangs a core for a test and triggers a restart) |
| `c` | ADC self-calibration (offset and gain) |
| `d` | show ADS1256 registers |
| `init` | restart the ADS1256 |
| `x` | stop the drive or cancel the running job |
| `w ja` | factory settings (deletes tare and calibration) |
| `h` | help in the log |
