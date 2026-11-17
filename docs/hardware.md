# Hardware – Datenblätter und Kennwerte

Inhalt: Wägezelle, Antrieb (Treiber HB808C), Linearmodul mit Kugelumlaufspindel.

Technische Daten der verbauten Teile als Referenz (auch als Kontext für KI-Assistenten). Verdrahtung, Pins und
Bedienung stehen in `README.md`, die Grenzwerte im Code in `src/konfig.h`.

## Wägezelle

Vollbrücke 350 Ω, legierter Stahl. Angaben laut Datenblatt des Händlers (Baureihe, „Messbereich 0–2 T“ steht dort für
die ganze Baureihe; verbaut ist die **50-kg-Variante**, `NENNLAST_G = 50000` in `src/konfig.h`).

| Parameter | Wert |
|---|---|
| Messbereich (Baureihe) | 0–2 t, verbaut: 50 kg |
| Ausgangsempfindlichkeit (Kennwert) | 1,0–2,0 ± 0,05 mV/V |
| Nullpunkt (Ausgang ohne Last) | ± 1 % F.S. |
| Nichtlinearität | 0,5 % F.S. |
| Hysterese | 0,3 % F.S. |
| Wiederholbarkeit | 0,3 % F.S. |
| Kriechen (30 min) | 0,1 % F.S. |
| Temperatureinfluss auf Ausgang (Spanne) | 0,1 % F.S. / 10 °C |
| Temperatureinfluss auf Nullpunkt | 0,1 % F.S. / 10 °C |
| Antwortfrequenz | 10 kHz |
| Material | legierter Stahl |
| Impedanz | 350 Ω |
| Isolationswiderstand | ≥ 5000 MΩ / 100 V DC |
| Speisespannung | 5–10 V (im Datenblatt fälschlich „Betriebstemperaturbereich“) |
| Betriebstemperatur | −20 … 80 °C |
| Sichere Überlast | 120 % F.S. |
| Maximale (Bruch-)Überlast | 150 % F.S. |
| Kabel | Ø 3 mm, 2 m |
| Zugfestigkeit des Kabels | 10 kg |

Adernfarben: Rot = +Speisung, Schwarz = GND, Grün = Signal + (AIN0), Weiß = Signal − (AIN1).

### Was daraus für diesen Aufbau folgt (50 kg, 5,00 V Speisung)

| Größe | Wert bei 50 kg F.S. |
|---|---|
| Signal bei Nennlast | 5–10 mV (1,0–2,0 mV/V × 5 V), typisch ~7,5 mV → Startfaktor `STANDARD_FAKTOR` ≈ 16,1 Counts/g |
| Nullpunkt ohne Last | bis ± 500 g bzw. ± 50–100 µV → wird per Tara entfernt, weit innerhalb ± 78 mV (PGA 64) |
| Nichtlinearität | ± 250 g – mit einem Faktor (Einpunkt-Kalibrierung) nicht korrigierbar; Kalibriergewicht nahe am Arbeitsbereich wählen |
| Hysterese / Wiederholbarkeit | je ± 150 g – Untergrenze für die Aussagekraft einzelner Pressungen, unabhängig vom ADC-Rauschen |
| Kriechen | ± 50 g in 30 min unter konstanter Last |
| Temperatur | ± 50 g / 10 °C auf Nullpunkt und Spanne – nach Temperaturwechsel neu tarieren |
| Sichere Überlast | 60 kg = `ALARMGRENZE_G` (120 %) |
| Bruchlast | 75 kg (150 %) – mechanische Grenze, die Motorkraft muss sicher darunter bleiben |

Hinweise:

- Die Datenblattgenauigkeit (0,3–0,5 % F.S.) ist deutlich gröber als die ADC-Auflösung (≈ 0,06 g/Count).
  Rauschwerte im Bereich weniger Gramm sind erreichbar, absolute Kraftwerte sind aber nur auf ca. ± 0,5 % F.S.
  genau – Kraftgrenzen und Statistiken entsprechend bewerten.
- Speisung 5 V liegt am unteren Ende des zulässigen Bereichs (5–10 V); höhere Speisung gäbe mehr Signal, ist aber
  durch den LT3042-Aufbau und den ADS1256-Eingangsbereich (5-V-Versorgung, Puffer an) festgelegt.
- Die Antwortfrequenz (10 kHz) begrenzt die Messdynamik nicht: Die Datenrate des ADS1256 bei den verwendeten
  Raten liegt im selben Bereich bzw. darunter, die mechanische Anordnung (Werkzeug, Spindel) ist langsamer.
- Kabel nicht als Zugentlastung belasten (max. 10 kg).

## Antrieb: Treiber HB808C (Nema 23, Closed Loop)

Angaben laut Händlerbeschreibung.

| Parameter | Wert |
|---|---|
| Typ | DSP-Schrittmotortreiber mit geschlossenem Regelkreis (Encoder-Rückführung) |
| Max. Spitzenstrom | 13 A |
| Impulsantwortfrequenz | 200 kHz (max. Schrittfrequenz an PUL) |
| Mikroschritt | 200–25 600 Impulse/Umdrehung (ppr) |
| Eingangsspannung | 24–80 V DC |
| Schutzfunktionen | Überspannung, Überstrom, Verfolgungsfehler (Positionsabweichung), Überlast → Alarmausgang ALM |
| Signaleingang | PUL/DIR oder CW/CCW (umschaltbar), differenziell |
| Signalpegel | 4,5–28 V, kein Vorwiderstand nötig – 3,3 V vom Pico reichen **nicht**, daher Pegelwandler (siehe `README.md`) |

Folgen für die Firmware:

- `schritteProMm` (`mp spmm`) = am Treiber eingestellte ppr / Spindelsteigung, z. B. 1600 ppr / 5 mm = 320 Schritte/mm.
- Schrittfrequenz = Geschwindigkeit × `schritteProMm`; muss unter 200 kHz bleiben (bei 320 Schritte/mm und
  100 mm/s: 32 kHz).
- Bei Verfolgungsfehler/Überlast (z. B. Werkzeug blockiert, Kraft zu groß) geht der Treiber auf Alarm und der Motor
  wird stromlos → ALM an GP5 auswerten (`mp alm`), danach ist die Position nicht mehr gesichert (neu kalibrieren).
- Das Datenblatt nennt kein Drehmoment des Motors; die Presskraft wird deshalb nur über die Wägezelle begrenzt.

Im selben Angebot gibt es eine Open-Loop-Variante mit Treiber **DM556** (20–50 V DC, 400–25 600 Schritte/U) – sie ist
**nicht** verbaut.

## Linearmodul mit Kugelumlaufspindel

Angaben laut Händlerbeschreibung (Modell GGP5080, Baureihe mit Nema-17/23-Halterung).

| Parameter | Wert |
|---|---|
| Spindel | SFU1605 (Ø 16 mm, Steigung 5 mm); Baureihe auch mit SFU1204 / SFU1610 – **verbaute Variante prüfen** (Annahme: SFU1605) |
| Genauigkeitsklasse | C7 |
| Wiederholgenauigkeit | 0,02–0,05 mm |
| Wirkhub | 50–300 mm wählbar (Firmware: `hubMm` ≤ 45 mm) |
| Gesamtlänge | Wirkhub + 120 mm |
| Schlitten | 50 × 80 mm |
| Führung | 2 × Rundstange Ø 12 mm |
| Max. Last horizontal | 40 kg |
| Max. Last vertikal | 20 kg |
| Max. Geschwindigkeit | 100 mm/s |
| Material | Aluminium („Flugzeugaluminium“) |

Folgen für diesen Aufbau:

- **Kraftbegrenzung:** Das Modul ist vertikal nur für 20 kg Nutzlast angegeben, die Wägezelle und die Firmware
  erlauben bis 50 kg (`NENNLAST_G`, `maxKraftG`). Die Angabe bezieht sich auf die bewegte Last (Führung/Lager); die
  Spindel selbst (SFU1605, dyn. Tragzahl im kN-Bereich) hält deutlich mehr, aber Führung, Halterung und
  Motorkupplung sind die Schwachstellen. Bei Pressungen über ~20 kg Verformung und Spiel beobachten; `mp fmax`
  nicht unnötig hoch setzen.
- **Weg-Auflösung:** Bei 5 mm Steigung und 1600 ppr ist ein Schritt 3,1 µm, weit feiner als die
  Wiederholgenauigkeit (20–50 µm) – Wegangaben in den Presskurven sind auf etwa ± 0,05 mm genau, dazu kommt die
  elastische Verformung von Rahmen und Wägezelle unter Last.
- **Geschwindigkeit:** Eilgang und `vpress` höchstens 100 mm/s.
- Kugelgewindespindeln sind nicht selbsthemmend: Ohne Motorstrom (ENA aus, Not-Aus, Treiberalarm) kann der
  Schlitten bei vertikalem Einbau durch Eigengewicht oder Restkraft nach unten laufen.
