# OpenSmartPress

Präzise Kraft- und Gewichtsmessung mit einer 50-kg-Wägezelle, einem 24-Bit-ADC **ADS1256** und einem
**Raspberry Pi Pico W**. Die Messwerte erscheinen live im Browser über WLAN und über USB auf dem PC.
Datenraten von 2,5 bis 30 000 Messungen pro Sekunde lassen sich im Betrieb umschalten.

> Status: Testaufbau. Firmware und Weboberfläche laufen, Einstellungen und Grenzwerte sind auf den
> unten beschriebenen Aufbau zugeschnitten.

## Funktionen

- **Dauerbetrieb des ADS1256** (RDATAC) mit DRDY-Interrupt auf einem eigenen Prozessorkern, SPI mit 1,9 MHz,
  Ringpuffer für 4096 Messwerte. Verlorene Werte werden gezählt und angezeigt.
- **Weboberfläche** direkt vom Pico: Gewichtsanzeige, Live-Diagramm Zeit/Gewicht mit Min/Max-Band,
  Live-Statistik, Protokoll und Befehlszeile. Dazu eine Hilfeseite mit Erklärungen und Einstelltipps.
- **Glättung** als gleitender Mittelwert über eine frei wählbare Zeit (0–60 s), unabhängig von der Datenrate.
- **Tara und Kalibrierung** mit Plausibilitätsprüfung (Empfindlichkeit in mV/V, Unsicherheit des Faktors).
- **Rauschanalyse**: Standardabweichung in Counts, µV und Gramm, effektive und rauschfreie Auflösung,
  Streuung nach Mittelung auf 10 SPS. Mit `nv` werden mehrere Datenraten automatisch verglichen.
- **Überwachung**: Warnung ab 100 % Nennlast, Alarm ab 120 % (LED), Erkennung eines übersteuerten ADC.
- **PC-Programm** `tools/waegezelle_plot.py` für die Aufzeichnung jedes einzelnen Messwerts über USB.
- Einstellungen (Tara, Faktor, Glättung, Datenrate) bleiben im Flash gespeichert.

## Hardware

| Teil | Typ / Wert |
|---|---|
| Mikrocontroller | Raspberry Pi Pico W bzw. WH (RP2040) |
| ADC | ADS1256-Modul, 7,68-MHz-Quarz, Referenz ADR03 (2,5 V), 5 V Versorgung, 3,3-V-Logik |
| Wägezelle | 50 kg, Flachmembran, 350-Ω-Vollbrücke, 1,0–2,0 mV/V |
| Brückenspeisung | 5,00 V aus einem LT3042 (rauscharmer Linearregler) |

### Verdrahtung

Plan als Grafik: [docs/Verdrahtung_PicoWH_ADS1256.svg](docs/Verdrahtung_PicoWH_ADS1256.svg)

| Pico | ADS1256 | Funktion |
|---|---|---|
| GP18 | SCLK | SPI0 Takt |
| GP19 | DIN | SPI0 TX |
| GP16 | DOUT | SPI0 RX |
| GP17 | CS | Chip Select (als GPIO) |
| GP20 | DRDY | Daten bereit |
| GP21 | PDWN | Power-Down (optional) |
| VBUS / GND | 5V / GND | Versorgung |

| Wägezelle | Anschluss |
|---|---|
| Rot | +5,00 V (LT3042) |
| Schwarz | GND |
| Grün | AIN0 |
| Weiß | AIN1 |

Messung differenziell AIN0–AIN1, Eingangspuffer an, PGA 64: Messbereich ±78,125 mV, 1 Count = 9,313 nV.

### Antrieb

Nema-23-Closed-Loop-Schrittmotor mit Treiber **HB808C** (PUL/DIR, 24–80 V, Signaleingänge 4,5–28 V), Hub 45 mm,
„ab“ = Rechtslauf.

| Pico | Anschluss | Hinweis |
|---|---|---|
| GP2 | PUL | über 5-V-Pegelwandler (z. B. 74AHCT125) oder Transistor, 3,3 V reichen nicht |
| GP3 | DIR | wie PUL |
| GP4 | ENA | optional (`mp ena`) |
| GP5 | ALM | optional, Alarmausgang des Treibers (`mp alm`) |
| GP6 | START-Taster gegen GND | startet die Endpunkt-Kalibrierung |
| GP7 | STOPP-Taster gegen GND | hält sofort an |

Ein Nema 23 an einer Spindel kann ein Vielfaches der 50 kg Nennlast drücken. Die Kraftgrenze im Pico ist schnell,
ersetzt aber keinen **Not-Aus**, der die Motorversorgung trennt, und keinen mechanischen Anschlag.

**Für wenig Rauschen:** ADS1256-Modul ebenfalls aus dem LT3042 versorgen, Sternmasse am AGND des ADS1256,
Signalleitungen Grün/Weiß verdrillen und vom Pico fernhalten. Den Einfluss des WLAN-Funks kannst du mit
`wlan aus` / `wlan an` und einem Rauschvergleich messen.

## Software einrichten

1. **Arduino IDE** installieren und den Core **„Raspberry Pi Pico/RP2040/RP2350“ von Earle Philhower**
   über den Boardverwalter hinzufügen (entwickelt mit Version 6.3.2). Weitere Bibliotheken sind nicht nötig.
2. Repository klonen. Der Ordner muss `Waegezelle_Test` heißen, damit die Arduino IDE den Sketch öffnet.
   ```
   git clone https://github.com/EggCe/OpenSmartPress.git Waegezelle_Test
   ```
3. `wlan_zugang.beispiel.h` nach **`wlan_zugang.h`** kopieren und SSID und Passwort eintragen
   (nur 2,4-GHz-Netze). Diese Datei wird nicht ins Repository übernommen.
4. `Waegezelle_Test.ino` öffnen, Board **„Raspberry Pi Pico W“** wählen und hochladen.
5. Seriellen Monitor öffnen (Zeilenende „Neue Zeile“). Nach dem Start erscheint die Adresse der Weboberfläche:
   ```
   # WLAN verbunden: http://192.168.x.x  bzw. http://waegezelle.local
   ```

## Bedienung

### Weboberfläche

| Adresse | Inhalt |
|---|---|
| `http://waegezelle.local/` | Messseite: Gewicht, Diagramm, Statistik, Protokoll, Befehle |
| `http://waegezelle.local/info` | Hilfe: alle Regler und Kennzahlen erklärt, Einstelltipps, Kalibrierablauf |

Funktioniert `waegezelle.local` nicht, die IP-Adresse aus dem seriellen Monitor verwenden. Die Seiten liegen
im Flash des Pico und brauchen kein Internet.

**Empfohlene Grundeinstellung zum Wiegen:** 2000 SPS, Glättung 0,5 s, Statistik 2–5 s.

### Erste Schritte

1. Gerät 15–30 Minuten warmlaufen lassen.
2. Zelle entlasten, **Tara** drücken (oder `t`).
3. Bekanntes Gewicht mittig auflegen, möglichst 5 kg oder mehr, dann kalibrieren: `k 5000 10000`
   (Gewicht in Gramm, Anzahl Messwerte).
4. Mit `s` speichern.

### Befehle

Im Eingabefeld der Weboberfläche oder im seriellen Monitor. `h` zeigt alle Befehle.

| Befehl | Funktion |
|---|---|
| `t [n]` | Tara aus n Messwerten |
| `k <g> [n]` | Kalibrieren mit bekanntem Gewicht in Gramm |
| `f <c/g>` | Kalibrierfaktor direkt setzen (Counts pro Gramm) |
| `r <sps>` | Datenrate: 2.5 5 10 15 25 30 50 60 100 500 1000 2000 3750 7500 15000 30000 |
| `fl <s>` | Glättung in Sekunden (0 = aus) |
| `ws <s>` | Dauer der Live-Statistik im Web |
| `n [n]` | Rauschanalyse |
| `nv [s] [sps …]` | Rauschvergleich mehrerer Datenraten |
| `m <0..4>` | USB-Ausgabe: 0 aus, 1 lesbar, 2 CSV, 3 Arduino-Plotter, 4 binär |
| `wlan [an\|aus]` | WLAN-Status bzw. ein-/ausschalten |
| `wd` / `wd test 0\|1` | Watchdog-Status / Kern 0 bzw. 1 zum Test aufhängen (löst Neustart aus) |
| `i` / `d` / `c` | Status / Register des ADS1256 / Selbstkalibrierung des ADC |
| `s` | Einstellungen im Flash speichern |
| `x` | Laufenden Auftrag abbrechen |
| `init` | ADS1256 neu initialisieren |
| `w ja` | Werkseinstellungen (löscht Tara und Kalibrierung) |

### Antrieb einrichten und Endpunkt kalibrieren

1. `mp spmm <Schritte pro mm>` setzen (Mikroschritte pro Umdrehung × Übersetzung / Spindelsteigung, z. B. 320).
   Vorher bewegt sich der Motor nicht.
2. Waage kalibriert, Datenrate 2000 SPS. Unter 500 SPS oder ohne kalibrierten Faktor fährt der Antrieb nicht.
3. Richtung prüfen: `mot ab 1` muss nach unten fahren, sonst `mp dir 0`/`1`. Weg mit `mot ab 10` nachmessen.
4. Erste Fahrten mit kleiner Kraftgrenze: `mp fmax 2000`. Mit `s` speichern.
5. **Endpunkt-Kalibrierung** mit dem START-Taster, dem Webknopf oder `mot kal`:
   - langsam abwärts, bis die Last um mehr als 100 g steigt: **Werkzeug-Endpunkt** = neuer Nullpunkt,
   - weiter bis Kontakt + 10 Schritte oder bis 50 kg (was zuerst kommt): **tatsächlicher Endpunkt**,
   - zurück zur Startposition; Kontakttiefe, Endpunkt, Kraft und Steifigkeit stehen im Protokoll.

Während jeder Abwärtsfahrt prüft Kern 1 jeden Messwert im ADC-Interrupt und stoppt innerhalb eines Schritts.
Stopp jederzeit mit `x`, der Esc-Taste im Browser, dem STOPP-Knopf oder dem STOPP-Taster.

| Befehl | Funktion |
|---|---|
| `mot` | Status, Position, letzte Kalibrierung |
| `mot kal` | Endpunkt-Kalibrierung |
| `mot ab <mm>` / `mot auf <mm>` / `mot s <n>` | Tippbetrieb |
| `mot null` / `mot ein` / `mot aus` | Position 0 setzen, Motorstrom ein/aus |
| `mp` / `mp <name> <wert>` | Parameter anzeigen/setzen: `spmm dir inv ena alm vsuch vfahrt acc hub ueber kontakt fmax` |

### Aufzeichnung am PC über USB

Die Weboberfläche zeigt 50 zusammengefasste Punkte pro Sekunde. Für jeden einzelnen Messwert das PC-Programm
verwenden. Es schaltet die Firmware in den Binärmodus und zeichnet auf Wunsch in eine CSV-Datei auf.

```
pip install pyserial numpy pyqtgraph PyQt5
python tools/waegezelle_plot.py                 # Port wird automatisch gesucht
python tools/waegezelle_plot.py --port COM5 --rate 2000 --fenster 10
```

Der serielle Monitor muss dabei geschlossen sein. Weboberfläche und PC-Programm können gleichzeitig laufen.

## Beispielmessung: Rauschen nach Datenrate

Unbelastete Zelle, PGA 64, Faktor 10,06 Counts/g, je 3 s (Befehl `nv 3 100 1000 2000`):

| SPS | σ Einzelwert | σ nach Mittelung auf 10 SPS | effektive Auflösung |
|---:|---:|---:|---:|
| 100 | 25,9 Counts (2,57 g) | 1,46 g | 19,3 Bit |
| 1000 | 59,6 Counts (5,92 g) | 1,03 g | 18,1 Bit |
| 2000 | 76,2 Counts (7,57 g) | 0,59 g | 17,8 Bit |

Bei hoher Datenrate ist jeder einzelne Wert unruhiger, nach dem Mitteln war 2000 SPS aber am genauesten.
Die Werte hängen stark von Aufbau, Versorgung und Umgebung ab.

## Aufbau der Software

Die beiden Kerne des RP2040 teilen sich die Arbeit:

| Kern | Aufgaben |
|---|---|
| Kern 1 | Alle Zugriffe auf den ADS1256: Initialisierung, Datenrate, Kalibrierung, DRDY-Interrupt. Jeder Messwert kommt mit laufender Nummer und Zeitstempel in einen Ringpuffer. Außerdem der Antrieb: Schritterzeugung per PWM, Rampen, Ablauf der Endpunkt-Kalibrierung, Kraftüberwachung im ADC-Interrupt, Taster und Alarmeingang. |
| Kern 0 | Verarbeitung (Glättung, Statistik, Überwachung, Tara/Kalibrierung), USB, WLAN und Webserver. Aufträge an den ADC gehen über ein Postfach an Kern 1. |

WLAN-Code darf im arduino-pico-Core ohne FreeRTOS nur auf Kern 0 laufen, daher diese Aufteilung.

Der Programmcode liegt in `src/`; die Arduino IDE übersetzt diesen Ordner automatisch mit.

```
Waegezelle_Test/
├── Waegezelle_Test.ino        setup/loop (Kern 0) und setup1/loop1 (Kern 1)
├── wlan_zugang.beispiel.h     Vorlage für wlan_zugang.h (WLAN-Zugangsdaten)
├── src/
│   ├── konfig.h               Pins, Referenz, Nennlast, Grenzwerte
│   ├── adc.*                  ADS1256-Objekt, Postfach Kern 0 → Kern 1, Init, Datenrate
│   ├── verarbeitung.*         Messwerte abholen, Überlast-/Übersteuerungswarnung, DRDY-Timeout
│   ├── filter.*               Glättung (gleitender Mittelwert über eine Zeit)
│   ├── statistik.h            Welford-Statistik, Ergebnis einer Rauschmessung
│   ├── auftraege.*            Tara, Kalibrierung, Rauschanalyse, Rauschvergleich
│   ├── usb_ausgabe.*          Ausgabemodi lesbar/CSV/Plotter/binär, #CFG-Zeile
│   ├── befehle.*              Befehle aus seriellem Monitor und Weboberfläche
│   ├── motor.*                Antrieb auf Kern 0: Parameter, Sicherheitsprüfungen, Meldungen
│   ├── einstellungen.*        Einstellungen im Flash (Tara, Faktor, Antrieb)
│   ├── meldung.*              Meldungen an USB und Web-Protokoll
│   ├── pruefsumme.*           CRC-32 (Flash) und CRC-8 (Binärrahmen)
│   ├── watchdog.*             Hardware-Watchdog für Kern 0, Herzschlag-Überwachung von Kern 1
│   ├── treiber/
│   │   ├── ADS1256.*          RDATAC-Dauerbetrieb, Interrupt, Ringpuffer, Lückenerkennung
│   │   └── Antrieb.*          HB808C: PWM-Schritte, Rampen, Endpunkt-Kalibrierung, Kraftüberwachung
│   └── web/
│       ├── wlan.*             WLAN-Verbindung, mDNS
│       ├── webserver.*        HTTP-Routen, JSON für /daten
│       ├── livedaten.*        Diagrammpunkte und Live-Statistik
│       └── seiten/            webui.h (Messseite), webinfo.h (Hilfe), webstil.h (/stil.css)
├── tools/waegezelle_plot.py   PC-Programm für Live-Anzeige und Aufzeichnung über USB
└── docs/                      Verdrahtungsplan
```

### Datenformate

**USB, Binärmodus (`m 4`):** 8 Byte je Messwert

| Byte | Inhalt |
|---|---|
| 0–1 | Synchronwort `A5 5A` |
| 2–3 | laufende Nummer, uint16 little-endian (Lücke = verlorene Werte) |
| 4–6 | Messwert in Counts, int24 little-endian, Zweierkomplement |
| 7 | CRC-8 (Polynom 0x07, Startwert 0x00) über Byte 2–6 |

Textmeldungen beginnen immer mit `#` und können zwischen den Rahmen stehen. Die Zeile `#CFG …` enthält alle
Umrechnungsgrößen (Datenrate, µV pro Count, Tara, Faktor) und wird nach jeder Änderung gesendet:

```
Gramm = (Counts − tara) / faktor
µV    = Counts × uv_count
```

**USB, CSV-Modus (`m 2`):** `seq,t_us,roh` mit Ganzzahlen.

**HTTP:**

| Adresse | Inhalt |
|---|---|
| `GET /daten?p=<punkt>&l=<log>` | JSON: Konfiguration, Diagrammpunkte (Zeit, Mittel, Min, Max, geglättet), Statistik, neue Protokollzeilen |
| `POST /cmd` mit `c=<befehl>` | führt einen Befehl aus |

## Hinweise

- Die Weboberfläche hat **keinen Passwortschutz**. Jeder im selben WLAN kann tarieren, kalibrieren oder die
  Datenrate ändern.
- Beim Speichern (`s`) und bei Ratenwechseln gehen kurz Messwerte verloren; sie werden gezählt.
- `wlan aus` lässt sich nur über USB rückgängig machen.
- **Watchdog:** Hängt Kern 0 länger als 8 s, startet der Pico neu. Kern 1 meldet sich ständig mit einem
  Herzschlag; bleibt er länger als 1 s aus, sperrt Kern 0 sofort die Schrittimpulse und startet neu. Nach so einem
  Neustart steht eine Warnung im Protokoll, `wd` zeigt den Grund. Ungespeicherte Tara/Kalibrierung geht dabei verloren.

## Lizenz

Noch nicht festgelegt.
