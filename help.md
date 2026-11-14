# Hilfe & Tipps – EcklC OpenSmartPress

Diese Seite erklärt jeden Regler und jede Kennzahl der Weboberfläche und gibt Empfehlungen für typische Aufgaben.
Alle Werte beziehen sich auf den Aufbau: 50-kg-Wägezelle mit 1,0–2,0 mV/V, 5,00 V Speisung aus dem LT3042,
ADS1256 mit PGA 64. Verdrahtung, Datenformate und Dateiaufbau stehen in der [README](README.md).

**Inhalt:** [Schnellstart](#schnellstart) · [Signalweg](#signalweg) · [Regler](#regler) ·
[Kennzahlen](#kennzahlen) · [Datenrate](#datenrate) · [Tipps](#tipps) · [Kalibrieren](#kalibrieren) ·
[Antrieb](#antrieb) · [Pressen](#pressen) · [Befehle](#befehle)

---

## Schnellstart

### Empfohlene Grundeinstellung zum Wiegen

| Einstellung | Wert | Warum |
|---|---|---|
| Datenrate | 2000 SPS | Im Rauschvergleich war 2000 SPS mit Mittelung am ruhigsten (σ @10 SPS 0,59 g gegenüber 1,46 g bei 100 SPS). |
| Glättung | 0,5 s | Ruhige Anzeige, reagiert trotzdem nach etwa 0,25 s auf Laständerungen. |
| Statistik | 2–5 s | Genug Werte für ein stabiles σ, aber kurz genug, um Änderungen zu sehen. |
| Fenster | 10 s | Zeigt Einschwingen und Drift, ohne dass Details verschwinden. |
| Min/Max-Band | an | Zeigt die Rauschbreite um die Linie. |

### Ablauf nach dem Einschalten

1. Gerät **15–30 Minuten warmlaufen** lassen. Zelle, Referenz und ADC driften in der ersten Zeit am stärksten.
2. Datenrate und Glättung wie oben einstellen.
3. Zelle entlasten, kurz warten, bis die Linie ruhig ist, dann **Tara** drücken.
4. Falls noch nicht geschehen: einmal [kalibrieren](#kalibrieren).
5. Mit `s` speichern. Gespeichert werden Tara, Faktor, Glättung, Datenrate und Ausgabemodus.

---

## Signalweg

### Vom Gewicht zur Zahl

**Wägezelle** (Vollbrücke 350 Ω, bei 50 kg 5–10 mV = 1,0–2,0 mV/V × 5 V)
→ **ADS1256** (24 Bit, PGA 64, Messbereich ±78,125 mV, 1 Count = 9,313 nV)
→ **Pico, Kern 1** (liest jeden Wert sofort nach dem DRDY-Signal und puffert ihn)
→ **Pico, Kern 0** (Glättung, Statistik, Überlastwarnung, WLAN und USB)
→ **Browser / USB** (Messseite 50 Punkte pro Sekunde, Pressseite jeder einzelne Wert, über USB jeder einzelne Wert)

Bei 50 kg nutzt das Signal nur etwa ein Achtel des ADC-Bereichs. Der Rest ist Reserve, damit auch Überlast
noch gemessen werden kann.

Umrechnung: `Gramm = (Counts − Tara) / Faktor` und `µV = Counts × 0,0093132`.
Tara ist der Nullpunkt in Counts, der Faktor sind Counts pro Gramm (zuletzt etwa 10,06).

---

## Regler

### Steuerung auf der Messseite

| Regler | Was er tut | Tipp |
|---|---|---|
| Datenrate | Wie oft der ADC pro Sekunde misst (2,5 bis 30000 SPS). Nach jedem Wechsel kalibriert sich der ADC kurz selbst. | Siehe [Datenrate](#datenrate). Für Wägungen 2000 SPS. |
| Anzeige | Einheit für Gewichtsanzeige und Diagramm: Gramm, µV (Brückenspannung) oder Counts (Rohwert). | µV und Counts helfen bei der Fehlersuche, weil sie nicht von Tara und Faktor abhängen. |
| Fenster | Wie viele Sekunden das Diagramm zeigt (1–600 s). | Kurz (2–5 s) für schnelle Vorgänge, lang (60 s und mehr) für Drift. |
| Glättung | Gleitender Mittelwert über die gewählte Zeit. Wirkt auf die Gewichtsanzeige und die grüne Linie, nicht auf die Statistik. | Die Anzeige hinkt etwa um die halbe Glättungszeit hinterher. |
| Rohsignal | Blendet die blasse, ungeglättete Linie ein oder aus. | Aus, wenn nur der geglättete Verlauf interessiert. |
| Statistik | Über wie viele Sekunden die Live-Statistik gerechnet wird. Danach beginnt eine neue Runde. | 2–5 s für den Alltag, 30–60 s für genaue Rauschwerte. |
| Min/Max-Band | Grüne Fläche zwischen kleinstem und größtem Rohwert je Diagrammpunkt (je 20 ms). | Wird das Band plötzlich breiter, kommt eine Störung dazu. |
| Pause | Friert den angezeigten Verlauf ein. Die Messung läuft weiter. | Im angehaltenen Verlauf kann weiter gezoomt und verschoben werden. |
| Y-Skala | **Auto** passt die Achse laufend an. **Auto mit Null** hält die Null-Linie im Bild. **Fest** nutzt die Werte für „von“ und „bis“ in der gewählten Einheit. | Im Diagramm: Mausrad zoomt um den Mauszeiger, Ziehen verschiebt, Doppelklick schaltet auf Auto. Bei fester Skala zeigt „▲ Werte darüber“ bzw. „▼ darunter“, dass etwas abgeschnitten ist. Beim Wechsel der Einheit springt die Skala auf Auto. |
| Tara | Mittelt etwa 2 s und setzt diesen Wert als Nullpunkt. | Nur bei entlasteter, ruhiger Zelle drücken. |
| Rauschtest | Misst etwa 5 s ungeglättet und schreibt eine ausführliche Rauschanalyse ins Protokoll. | Zelle dabei nicht berühren. |
| Rauschvergleich | Misst nacheinander bei mehreren Datenraten und gibt eine Vergleichstabelle aus. | Dauert etwa 25 s. Danach wird die vorherige Datenrate wiederhergestellt. |
| Info | Schreibt den aktuellen Gerätestatus ins Protokoll. | Zeigt auch verlorene Werte und die WLAN-Verbindung. |
| Abbruch | Bricht Tara, Kalibrierung oder Rauschtest ab. | Läuft nichts, schaltet er die USB-Ausgabe aus. |
| CSV speichern | Lädt die Diagrammdaten herunter: Zeit, Mittel, Min, Max und geglätteter Wert, 50 Zeilen pro Sekunde. | Für jeden einzelnen Messwert die Aufzeichnung über USB mit `waegezelle_plot.py` nutzen. |

---

## Kennzahlen

### Live-Statistik

Die Statistik rechnet mit **jedem einzelnen, ungeglätteten Messwert** der letzten Statistik-Runde,
nicht mit den 50 Diagrammpunkten pro Sekunde. Sie zeigt also, was der ADC wirklich liefert.

- **Mittelwert** – Durchschnitt aller Werte der Runde, in Counts, µV und Gramm.
- **Std.-Abw. σ** – Standardabweichung, also das typische Rauschen **eines einzelnen Messwerts**. Etwa zwei Drittel
  aller Werte liegen innerhalb ±1 σ um den Mittelwert. Steigt mit der Datenrate, weil der ADC bei hoher Rate weniger
  intern mittelt. Deshalb allein kein gutes Maß für die Ablesegenauigkeit.
- **Spitze-Spitze** – Abstand zwischen kleinstem und größtem Wert der Runde. Etwa 6–7 σ bei reinem Rauschen;
  deutlich mehr deutet auf Ausreißer, Stöße oder Störungen hin.
- **σ @10 SPS** – Streuung der Mittelwerte über je 0,1 s. Das ist **die wichtigste Zahl zum Vergleichen von
  Datenraten**, weil sie bei jeder Rate dasselbe misst: wie genau ein 0,1-s-Mittel ist. Sie ist gemessen, nicht
  geschätzt, enthält also auch langsames Rauschen und Drift. 50-Hz-Brummen fällt heraus, weil 0,1 s genau
  5 Netzperioden sind.
- **σ geglättet** – Streuung des geglätteten Werts, also **wie ruhig die Gewichtsanzeige mit der gewählten Glättung
  ist**. Erscheint nur bei eingeschalteter Glättung. Bei rein zufälligem Rauschen sinkt sie mit der Wurzel der
  Glättungszeit: viermal so lange glätten halbiert das Rauschen. Langsame Drift entfernt die Glättung nicht.
- **Effektive Auflösung** – `ENOB = log2(2²⁴ / σ)`, in Bit, bezogen auf den vollen ADC-Bereich ±78,125 mV.
  Jedes Bit mehr halbiert das Rauschen.
- **Rauschfrei** – `log2(2²⁴ / (6,6 × σ))`. Innerhalb ±3,3 σ liegen 99,9 % aller Werte; so viele Bit sind praktisch
  ohne Flackern ablesbar. Immer etwa 2,7 Bit weniger als die effektive Auflösung.
- **Bezogen auf Nennlast** – `50 000 g / σ in Gramm`: in wie viele unterscheidbare Schritte sich der Bereich 0–50 kg
  teilen lässt. Aussagekräftiger als die effektive Auflösung, weil die Zelle nur ein Achtel des ADC-Bereichs nutzt.
  Die Zeile „Nach Mittelung auf 10 SPS“ rechnet dasselbe mit σ @10 SPS.
- **Rate gemessen** – Tatsächliche Datenrate aus den Zeitstempeln. Kleine Abweichungen vom Sollwert (unter 0,01 %)
  kommen vom Quarz und sind normal.
- **verloren** – Wandlungen, die nicht gelesen wurden. Kurze Lücken entstehen beim Speichern (`s`), beim Ratenwechsel
  und beim Registerlesen. Steigt der Zähler im normalen Betrieb, ist die Datenrate zu hoch für die Verarbeitung.
- **ÜBERSTEUERT** – Der ADC war am Anschlag. Bei einer Wägezelle heißt das fast immer: Ader gebrochen, Kurzschluss
  oder fehlende Speisung. Werte aus dieser Runde nicht verwenden.

### Statusanzeige oben rechts

- **misst** (grüner Punkt): der ADC liefert Werte. Rot heißt angehalten, nicht initialisiert oder keine Verbindung zum Pico.
- **WLAN … dBm**: Signalstärke. Besser als −67 dBm ist gut, schlechter als −80 dBm wird es unzuverlässig.
- **verloren**: Summe aller verlorenen Werte seit dem Start.
- **Auftrag läuft**: Tara, Kalibrierung oder Rauschtest ist aktiv. Andere Befehle werden so lange abgelehnt.

---

## Datenrate

### Mehr Werte, mehr Rauschen pro Wert

Bei niedriger Rate mittelt der ADS1256 intern über viele Abtastungen, jeder Wert ist ruhig. Bei hoher Rate
kommen mehr, aber unruhigere Werte. Entscheidend ist, wie genau das Ergebnis **nach dem Mitteln** ist.
Das zeigt σ @10 SPS.

| SPS | σ Einzelwert | σ @10 SPS | Geeignet für |
|---|--:|--:|---|
| 10–60 | – | – | Langzeitmessung mit wenig Daten. 50 SPS unterdrückt 50-Hz-Netzbrummen besonders gut. |
| 100 | 25,9 Counts · 2,6 g | 1,46 g | Einfache Anzeige, wenig Datenvolumen. |
| 1000 | 59,6 Counts · 5,9 g | 1,03 g | Langsame Kraftverläufe. |
| 2000 | 76,2 Counts · 7,6 g | 0,59 g | **Wiegen und Kalibrieren**, bester Wert im Vergleich. |
| 3750–7500 | 138,6 Counts · 13,8 g (7500) | – | Schnelle Kraftverläufe, Pressvorgänge. |
| 15000–30000 | – | – | Nur für sehr kurze Ereignisse (Stöße). Hohe Last für Pico und WLAN. |

Werte aus eigenen Messungen: Rauschvergleich bei 100/1000/2000 SPS und Kalibrierlauf bei 7500 SPS, Faktor
10,06 Counts/g. Ein einzelner 3-s-Lauf ist eine grobe Schätzung; für belastbare Zahlen `nv 10 2000 100 2000 100`
laufen lassen.

---

## Tipps

**Statisch wiegen**
- 2000 SPS, Glättung 0,5–1 s.
- Gewicht mittig auflegen. Flachmembran-Zellen reagieren auf außermittige Last.
- Nach dem Auflegen 2–5 s warten, bis die grüne Linie flach ist.
- Für die letzte Stelle: Glättung 2 s und auf „σ geglättet“ achten.

**Kraftverläufe und Pressvorgänge**
- 2000–7500 SPS, Glättung aus oder 0,02–0,05 s, sonst werden Spitzen abgeflacht.
- Min/Max-Band an: zeigt Spitzen auch zwischen zwei Diagrammpunkten.
- Fenster 2–10 s, mit Pause eine Stelle ansehen.
- Pressungen: Pressseite geöffnet lassen, dann bekommt der Browser jeden einzelnen Messwert (siehe [Rohdaten](#rohdaten-im-browser)).

**Glättung richtig wählen**
- Verzögerung ≈ halbe Glättungszeit: bei 2 s reagiert die Anzeige etwa 1 s später.
- Viermal längere Glättung halbiert zufälliges Rauschen.
- Gegen Drift und Kriechen hilft Glättung nicht, nur Tara und Warmlaufen.
- Statistik und Aufträge (Tara, Kalibrierung) rechnen immer mit Rohwerten.

**Stabile Messwerte**
- 15–30 Minuten warmlaufen lassen, dann tarieren.
- Vor jeder Messreihe neu tarieren.
- Nach großer Last kriecht die Zelle einige Minuten nach. Vor dem Tarieren kurz warten.
- Luftzug, Vibration und Temperaturwechsel an der Zelle vermeiden.

**Rauschen messen**
- Zelle unbelastet, nichts berühren, Tisch nicht anstoßen.
- Datenraten abwechselnd messen, z. B. `nv 10 2000 100 2000 100`, damit Drift nicht eine Rate benachteiligt.
- WLAN-Einfluss prüfen: über USB `wlan aus`, `nv 5`, dann `wlan an`, `nv 5`.

**Aufbau**
- ADS1256-Versorgung aus dem LT3042 statt aus USB-5 V.
- Sternmasse am AGND des ADS1256, eigene Leitungen für Pico und Zelle.
- Grün/Weiß (AIN0/AIN1) verdrillen und vom Pico fernhalten.
- „ÜBERSTEUERT“ bei unbelasteter Zelle: Verdrahtung prüfen.

> **Überlast:** Ab 50 kg (100 %) erscheint eine Warnung, ab 60 kg (120 %) ein Alarm, und die LED am Pico leuchtet.
> Sofort entlasten. Die Grenzen nutzen den Faktor; ohne Kalibrierung sind sie nur geschätzt.

---

## Kalibrieren

### Faktor bestimmen

1. Datenrate 2000 SPS, Gerät warmgelaufen.
2. Zelle entlasten, warten bis ruhig, **Tara**.
3. Bekanntes Gewicht mittig auflegen, **möglichst 5 kg oder mehr** (10 % der Nennlast). Etwa 10 s warten.
4. Befehl `k 5000 10000` eingeben: Gewicht in Gramm, dann Anzahl Werte (10000 = 5 s bei 2000 SPS).
5. Ergebnis im Protokoll prüfen: Empfindlichkeit 1,0–2,0 mV/V gilt als plausibel, die Unsicherheit des Faktors
   sollte unter 0,05 % liegen.
6. Mit `s` speichern.

Kleine Gewichte funktionieren, aber Fehler wachsen bis 50 kg mit: Bei 100 g wird jeder Fehler etwa 500-fach vergrößert.
Ein Faktor von 10,06 Counts/g entspricht 0,94 mV/V, knapp unter der Angabe der Zelle. Mit einem großen, genau
bekannten Gewicht nachkalibrieren lohnt sich.

Ist die Änderung in Counts schon bekannt, geht es auch direkt: `f <Counts / Gramm>`, z. B. `f 10.06`.

---

## Antrieb

### Closed-Loop-Schrittmotor mit HB808C

Der Pico gibt Schritt- und Richtungsimpulse aus; Encoder und Regelkreis stecken im Treiber. Während jeder
Abwärtsfahrt prüft der Pico **jeden einzelnen Messwert** der Wägezelle und hält den Motor innerhalb eines Schritts
an, sobald Kontakt oder die Maximalkraft erreicht ist. Aufwärts darf der Motor immer fahren.

| Pico | Treiber / Taster | Hinweis |
|---|---|---|
| GP2 | PUL | Der HB808C braucht 4,5–28 V an den Eingängen (PUL, DIR, ENA). Über einen 5-V-Pegelwandler (z. B. 74AHCT125) oder gemeinsame Anode an 5 V mit Transistor gegen GND anschließen; dann `mp inv 1`. |
| GP3 | DIR | wie PUL |
| GP4 | ENA (empfohlen) | wie PUL. Über den Not-Aus-Kreis führen, damit der Not-Aus den Motor auch ohne Pico stromlos schaltet. |
| GP5 | ALM (optional) | Alarmausgang des Treibers, z. B. bei Schleppfehler. Mit `mp alm 1` bzw. `2` aktivieren. |
| GP6 | START-Taster gegen GND | Startet eine Pressung (wenn alles bereit ist). |
| GP7 | STOPP-Taster gegen GND | Hält sofort an (Software). Ersetzt keinen Not-Aus. |

### Einrichten

1. `mp spmm <Wert>`: Schritte pro mm = Mikroschritte pro Umdrehung × Übersetzung / Spindelsteigung.
   Beispiel: 1600 Schritte, direkt, 5 mm Steigung → `mp spmm 320`. Vorher bewegt sich der Motor nicht.
2. Waage kalibrieren und Datenrate auf 2000 SPS. Ohne kalibrierten Faktor und unter 500 SPS fährt der Antrieb nicht.
3. Richtung prüfen: `mot ab 1` muss nach unten fahren (Rechtslauf). Sonst `mp dir 0` bzw. `mp dir 1`.
4. Weg prüfen: `mot ab 10` mit dem Messschieber nachmessen, bei Abweichung `spmm` anpassen.
5. Stopp testen: langsam abwärts fahren und von Hand auf die Zelle drücken. Der Motor muss sofort stehen.
6. Mit `s` speichern.

### Endpunkt kalibrieren

Startet mit dem Knopf auf der Mess- oder Pressseite oder `mot kal`. Das Werkzeug liegt auf der Zelle, ohne
Werkstück. Nach jedem Einschalten nötig, weil die Position des Motors dann unbekannt ist.

1. Langsam abwärts (`vsuch`, Standard 1 mm/s), höchstens über den Hub von 45 mm.
2. Steigt die Last um mehr als `kontakt` (Standard 100 g) gegenüber dem Wert vor der Fahrt, ist der
   **Werkzeug-Endpunkt** erreicht. Er wird der neue Nullpunkt.
3. Weiter bis zum **tatsächlichen Endpunkt**: Kontakt + `ueber` Schritte (Standard 10), oder früher bei `fmax`
   (Standard und Obergrenze 50 kg).
4. Zurück zur Startposition. Im Protokoll stehen Kontakttiefe, Endpunkt, Kraft am Endpunkt und die Steifigkeit in kg/mm.

Ohne Lastanstieg im ganzen Hub bricht die Fahrt mit „kein Kontakt“ ab. Bei jedem Abbruch bleibt der Motor an der
Abbruchstelle stehen; mit `mot auf <mm>` wieder hochfahren.

**Sensorfehler:** Steht der Messwert am Anschlag des ADC (z. B. Aderbruch, fehlende Brückenspeisung) oder ändert er
sich 64 Werte lang überhaupt nicht (ADC oder SPI hängt), ist die Kraft unbekannt. Dann hält jede Abwärtsfahrt sofort
an („Sensorfehler“), und keine neue startet. Hochfahren mit `mot auf <mm>` bleibt möglich. Im Protokoll steht
„SENSORFEHLER“ und wieder „Sensor wieder plausibel“, sobald die Messwerte 0,5 s lang in Ordnung sind. In der
Simulation wird nicht geprüft.

> **Kraftreserve:** Ein Nema-23-Motor an einer Spindel kann ein Vielfaches der 50 kg Nennlast drücken.
> Die Kraftgrenze im Pico ist schnell, aber Software. Einen fest verdrahteten Not-Aus, der die Motorversorgung
> bzw. ENA trennt, und einen mechanischen Anschlag vorsehen. Die ersten Fahrten mit kleiner Grenze
> (`mp fmax 2000`) machen.

### Parameter

| Name | Bedeutung | Standard |
|---|---|---|
| `spmm` | Schritte pro mm, 0 = gesperrt | 0 |
| `dir` | DIR-Pegel für „ab“ | 1 |
| `inv` | Ausgänge invertiert (gemeinsame Anode mit Transistor) | 0 |
| `ena` | 0 unbenutzt, 1 Opto an = Motor aus, 2 Opto an = Motor ein | 0 |
| `alm` | 0 aus, 1 Alarm bei LOW, 2 Alarm bei HIGH | 0 |
| `vsuch` | Geschwindigkeit Kontaktsuche und Überlauf (mm/s) | 1 |
| `vfahrt` | Geschwindigkeit Tippbetrieb und Rückfahrt (mm/s) | 5 |
| `acc` | Beschleunigung (mm/s²) | 20 |
| `hub` | maximaler Suchweg (mm, höchstens 45) | 45 |
| `ueber` | Schritte nach dem Kontakt | 10 |
| `kontakt` | Laständerung, die als Kontakt gilt (g) | 100 |
| `fmax` | Maximalkraft, Abwärtsfahrt stoppt sofort (g, höchstens 50 000) | 50 000 |

Die Schrittfrequenz liegt zwischen 16 Hz und 20 kHz. Ein Mikroschritt von 1600–3200 pro Umdrehung genügt beim
Closed-Loop-Treiber.

---

## Pressen

### Ablauf auf der Pressseite (`/presse`)

Oben füllen Ablauf (mit Live-Kraft, START/STOPP) und das Diagramm den Bildschirm; Kennwerte, Pressungen, Trend,
Auswertung, Statistik, Sitzungen, Parameter und Protokoll folgen darunter. Das Menü links (Status, Sprungmarken)
lässt sich mit « einklappen, damit das Diagramm breiter wird.
Die drei Farbpunkte unten im Menü (auf der Messseite oben rechts) wählen das Farbschema: Grün/Schwarz,
Rot/Schwarz oder Blau/Hell. Der Browser merkt sich die Wahl für beide Seiten.

1. **Werkzeug auflegen und tarieren.** Das Gewicht des Werkzeugs wird abgezogen.
2. **Hub kalibrieren** (ohne Werkstück): Der Motor fährt auf das Werkzeug, merkt sich den Werkzeug-Endpunkt (Weg 0)
   und den tatsächlichen Endpunkt und fährt zurück.
3. **Sitzung wählen oder anlegen.** Jede Pressung wird sofort in der aktiven Sitzung gespeichert.
4. **Werkstück einlegen und START** (Knopf oder Taster): Eilgang bis zum Anfahrpunkt, dann mit Pressgeschwindigkeit
   bis zum kalibrierten Endpunkt (oder bis zur Maximalkraft), Haltezeit, zurück zum Start.

### Diagramm und Kennwerte

Jede Pressung erscheint als Kurve **Kraft über Weg**; alle Pressungen der Sitzung liegen übereinander. Der Weg zählt
ab dem Werkzeug-Endpunkt: Das Werkstück berührt also bei einem negativen Weg. Ein Klick auf eine Kurve oder eine
Tabellenzeile zeigt ihre Kennwerte und die Abweichung zum Mittel der Sitzung.

- **Spitzenkraft** – Größte Kraft der Pressung und der Weg, bei dem sie auftrat.
- **Endkraft** – Mittel der letzten ca. 10 ms am Endpunkt; mit Haltezeit zusätzlich die Kraft am Ende der Haltezeit.
- **Kontakt, Presstiefe** – Kontakt: erster Messwert über der Kontaktschwelle (`mp kontakt`). Presstiefe: Weg vom
  Kontakt bis zum Endpunkt.
- **σ, Spannweite** – Standardabweichung und Max − Min aller Kraftwerte vom Kontakt bis zum Endpunkt. Die
  Sitzungsstatistik zeigt zusätzlich Mittel, σ und Spannweite jedes Kennwerts über alle gültigen Pressungen – das
  Maß für die Gleichmäßigkeit.
- **Pressarbeit** – Fläche unter der Kurve ab Kontakt (Kraft × Weg) in Joule.

### Auflösung der Kurve

Die Kennwerte oben rechnet der Pico immer exakt aus **allen** Messwerten. Für die Kurve gibt es zwei Quellen:

| Quelle | Auflösung | Wo gespeichert |
|---|---|---|
| Pico-Kurve | höchstens 960 Wegabschnitte über den Pressweg (Anfahrpunkt bis Endpunkt, höchstens 10 mm, Standard 5 mm), Kraft je Abschnitt gemittelt, 0,02 N Stufen. Beispiel: 5 mm Pressweg bei 320 Schritten/mm → 2 Schritte = 6,25 µm je Punkt. | Flash des Pico (immer) |
| Rohdaten | **jeder einzelne Messwert** mit Zeit (4 µs), Position (1 Motorschritt) und Kraft (1 Count ≈ 0,001 N), inklusive Haltezeit. Die Kurve hat einen Punkt je Motorschritt (z. B. 1/320 mm = 3,1 µm). | im Browser (IndexedDB) |

Feiner als ein Motorschritt geht es nicht, weil der Pico nur die ausgegebenen Schritte kennt (der Encoder bleibt im
Treiber). Die Kraft ist durch das Rauschen begrenzt (σ ≈ 0,075 N je Einzelwert bei 2000 SPS), nicht durch die
24 Bit des ADC.

#### Rohdaten im Browser

Solange die Pressseite offen ist, holt der Browser während der Pressung alle 100 ms die neuen Messwerte vom Pico
(Puffer im Pico: 4096 Werte, also etwa 2 s bei 2000 SPS). Nach dem Speichern legt er sie unter Sitzung und
Pressnummer in der IndexedDB ab. Der Pico rechnet dafür nichts zusätzlich, die Kurve aus jedem Motorschritt, die
Auswertung und der Export laufen im Browser.

- Pressungen mit Rohdaten sind in der Tabelle mit **R** markiert. In den Details steht „Rohdaten CSV“: Zeit, Weg,
  Kraft und Phase jedes Messwerts.
- Rohdaten gibt es nur in dem Browser, der während der Pressung offen war. Fehlen sie (Seite zu, WLAN-Lücke länger
  als der Puffer), nutzt die Seite automatisch die Pico-Kurve.
- Die Kennwerte der Auswertung werden mit beiden Quellen auf derselben Wegbreite gemittelt (ein Pico-Abschnitt),
  damit Sitzungsstatistik und Trend vergleichbar bleiben. Die Positionen (z. B. Weg der Fügekraft) werden mit
  Rohdaten genauer.
- Browserdaten löschen entfernt auch die Rohdaten. Wichtige Pressungen als „Rohdaten CSV“ sichern.

### Auswertung (Karte „Auswertung“)

Berechnet im Browser aus den gespeicherten Kurven; Fenster und Schwellen sind einstellbar und gelten für alle
Pressungen. **Fügetiefe** = Weg vom Kontakt bis zum Endpunkt minus „Anschlag ausschließen“.

- **Weg ab Kontakt** – Kurven am Kontaktpunkt ausgerichtet: Unterschiede in Rohrlänge und Einlegehöhe verschwinden,
  man vergleicht das Material.
- **Fügekraft** – Größte Kraft bis vor den Anschlag – die Kraft der Verbindung, nicht des Werkzeugs.
- **Einführspitze** – Erstes lokales Maximum in den ersten 40 % der Fügetiefe (Fase, Aufweiten beginnt). Hinweis auf
  Fase, Grat und Durchmesser.
- **Plateau-Gerade** – Lineare Regression im Plateaufenster. **Steigung** (Kraft je mm) ≈ Reibwert ×
  Flächenpressung × Umfang; **Achsenabschnitt** = Grundkraft des Aufweitens; **Laufruhe** = Streuung um die Gerade
  (Ruckgleiten, beginnendes Kaltverschweißen). Dazu die Arbeit im Plateaufenster.
- **Kraftabfall, Rissverdacht** – Größter Kraftrückgang innerhalb der eingestellten Strecke im Fügebereich. Ab der
  Schwelle (Standard 5 % in 0,05 mm) wird die Pressung als Rissverdacht markiert – typisch für ein aufplatzendes
  dünnwandiges Rohr.
- **Wegfenster** – Kraft an festen Wegen ab Kontakt (Standard 1, 2, 3, 4 mm), Grundlage für spätere OK/NOK-Fenster.
- **Trend** – Ein Kennwert über die Pressnummer mit Mittel und ±3σ. Ein langsamer Anstieg der Plateaukraft oder der
  Laufruhe deutet auf Verschleiß oder nachlassende Schmierung.

„Auswertung als CSV“ exportiert alle Kennwerte und die Statistik in der gewählten Krafteinheit.
Die Krafteinheit (N, kg oder lbs) ist über dem Diagramm wählbar; sie gilt auch für den CSV-Export.
Gespeichert wird immer in N.

### Einstellungen

Der Weg des Werkstücks ist mechanisch durch eine Anschlagschraube am Presskopf begrenzt (ca. 5 mm). Gepresst und
aufgezeichnet wird deshalb höchstens **10 mm** über dem kalibrierten Werkzeug-Endpunkt (5 mm Weg plus Reserve,
`PRESSWEG_MAX_MM` in `src/konfig.h`); darüber fährt der Antrieb im Eilgang.

`mp anfahrt <mm>` (0–10, Standard 5): ab so vielen mm über dem Werkzeug-Endpunkt wird mit Pressgeschwindigkeit
gepresst; etwas höher als die Oberkante des Werkstücks wählen. Ältere gespeicherte Werte über 10 mm werden beim
Start auf 10 mm begrenzt. Die 960 Punkte der Pico-Kurve liegen bei 5 mm höchstens 5,2 µm, bei 10 mm höchstens
10,4 µm auseinander – so knapp wie möglich einstellen.
`mp vpress <mm/s>`: Pressgeschwindigkeit. `mp halt <ms>`: Haltezeit am Endpunkt. Danach `s`.

### Simulation ohne Motor

`sim an` oder der Schalter auf der Pressseite: Die Kraft wird aus der Position berechnet – virtuelles Werkzeug
unter der aktuellen Position, je Pressung ein Werkstück mit zufälliger Spitzenkraft. ADC und Zelle müssen
angeschlossen sein, `mp spmm` gesetzt. Danach Hub kalibrieren und pressen. Simulierte Pressungen sind markiert;
nach einem Neustart ist die Simulation aus.

Der Knopf **„Parameter …“** neben dem Schalter öffnet das Simulationsfenster (oder `sim par` / `sim <name> <wert>`):

| Feld | Befehl | Standard | Bedeutung |
|---|---|---|---|
| Pressgeschwindigkeit | `mp vpress <mm/s>` | 1 | wie in den Pressparametern |
| Werkzeugabstand | `sim werkzeug <mm>` | 15 | virtuelles Werkzeug so weit unter der Position beim Einschalten (= Hub bis zum Werkzeug); ändern setzt das Werkzeug neu, danach Hub kalibrieren |
| Fügeweg | `sim weg <mm>` | 3 | Höhe des Werkstücks über dem Werkzeug (höchstens 10 mm, höchstens 98 % des Anfahrpunkts) |
| Spitzenkraft von / bis | `sim fmin <g>` / `sim fmax <g>` | 25 / 35 kg | zufällige Spitzenkraft je Pressung (im Fenster in kg) |
| Rauschen | `sim rauschen <g>` | 30 | Rauschen ± in Gramm |
| Rissquote | `sim riss <%>` | 8,3 | Anteil der Pressungen mit simuliertem Riss (Kraftabfall 15 %) |
| Einführspitze | `sim spitze <%>` | 12 | Buckel an der Fase in % der Spitzenkraft, 0 = keine |

Der Pico hält die Werte nur bis zum Neustart. Der Browser merkt sich die zuletzt übernommenen Werte und schickt sie
beim Einschalten der Simulation wieder. Das Fenster zeigt auch, wie lange eine Pressung dauert und wie viele
Messwerte sie liefert.

### Sitzungen

Im Flash des Pico ist Platz für etwa 200 Pressungen. Sitzungen lassen sich als CSV (für Excel: Semikolon,
Dezimalkomma) oder JSON herunterladen. Danach alte Sitzungen löschen.

> Die Kraftgrenze stoppt den Motor schnell, ersetzt aber keine Schutzeinrichtung (Not-Aus, Schutzhaube,
> Zweihandbedienung).

---

## Befehle

Eingabe im Feld unter dem Protokoll oder im seriellen Monitor (Zeilenende „Neue Zeile“). Dezimalkomma geht auch.

| Befehl | Funktion |
|---|---|
| `t [n]` | Tara aus n Werten (Standard etwa 2 s) |
| `k <g> [n]` | Kalibrieren mit bekanntem Gewicht in Gramm |
| `f <c/g>` | Faktor direkt setzen (Counts pro Gramm) |
| `r <sps>` | Datenrate: 2.5 5 10 15 25 30 50 60 100 500 1000 2000 3750 7500 15000 30000 |
| `fl <s>` | Glättung in Sekunden (0–60, `fl 0` bzw. `fl aus` = keine) |
| `ws <s>` | Dauer der Live-Statistik (0,5–60 s) |
| `n [n]` | Rauschanalyse über n Werte (Standard etwa 5 s) |
| `nv [s] [sps …]` | Rauschvergleich mehrerer Datenraten, je s Sekunden |
| `s` | Einstellungen dauerhaft speichern |
| `i` | Status anzeigen |
| `m <0..4>` | USB-Ausgabe: 0 aus, 1 lesbar, 2 CSV, 3 Plotter, 4 binär |
| `mot` | Antrieb: Status, Position, letzte Kalibrierung |
| `mot kal` | Hub kalibrieren (Endpunkt-Kalibrierung, nach jedem Einschalten) |
| `mot ab\|auf <mm>` | Tippbetrieb; `mot s <n>` = n Schritte (+ ab) |
| `mot null\|ein\|aus` | Position 0 setzen, Motorstrom ein bzw. aus |
| `mot stop` | Anhalten (auch `x`, Esc im Browser, STOPP-Taster) |
| `mp [name wert]` | Antrieb- und Press-Parameter anzeigen bzw. setzen (u. a. `anfahrt`, `vpress`, `halt`) |
| `pr` / `pr x` | Pressung starten (wie START-Taster) / abbrechen |
| `sim [an\|aus]` | Simulation ohne Motor (Kraft aus der Position berechnet) |
| `sim par` / `sim <name> <wert>` | Simulationsparameter anzeigen / setzen: `werkzeug weg fmin fmax rauschen riss spitze` |
| `sit` | Sitzungen anzeigen; `sit neu <Name>`, `sit wahl <Nr>`, `sit zu`, `sit loeschen <Nr> ja` |
| `wlan [an\|aus]` | WLAN-Status bzw. ein-/ausschalten (wieder an nur über USB) |
| `wd` | Watchdog-Status, Grund des letzten Neustarts und höchste Stapelbelegung beider Kerne (`wd test 0\|1` hängt einen Kern zum Test auf und löst einen Neustart aus) |
| `c` | Selbstkalibrierung des ADC (Offset und Verstärkung) |
| `d` | Register des ADS1256 anzeigen |
| `init` | ADS1256 neu starten |
| `x` | Antrieb anhalten bzw. laufenden Auftrag abbrechen |
| `w ja` | Werkseinstellungen (löscht Tara und Kalibrierung) |
| `h` | Hilfe im Protokoll |
