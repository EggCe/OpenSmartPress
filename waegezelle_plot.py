#!/usr/bin/env python3
"""
waegezelle_plot.py - Live-Anzeige für Waegezelle_Test.ino (ADS1256 + Pico W)

Schaltet die Firmware in den Binärmodus ("m 4") und zeigt die Rohwerte live an.
Die Umrechnung in Gramm/µV passiert hier am PC mit den Werten aus der
"#CFG"-Zeile der Firmware (Tara, Faktor, µV pro Count, Datenrate).

Binärrahmen (8 Byte):  A5 5A | seq uint16 LE | roh int24 LE | CRC-8 (Poly 0x07) über Byte 2..6
Textzeilen:            "#..." (Meldungen), "#CFG k=v ...", "#RAUSCH k=v ..."
CSV (Modus 2):         "seq,t_us,roh"

Benötigt:  pip install pyserial numpy pyqtgraph PyQt5     (oder PySide6 statt PyQt5)

Aufruf:    python waegezelle_plot.py                 (Port wird automatisch gesucht)
           python waegezelle_plot.py --port COM5 --fenster 10 --rate 2000
"""
import argparse
import queue
import sys
import threading
import time

import numpy as np
import serial
import serial.tools.list_ports
import pyqtgraph as pg
from pyqtgraph.Qt import QtCore, QtWidgets

RATEN = [2.5, 5, 10, 15, 25, 30, 50, 60, 100, 500, 1000, 2000, 3750, 7500, 15000, 30000]
VOLLBEREICH = 2 ** 24          # Counts über den ganzen ADC-Bereich (+-2*Vref/PGA)
RAHMEN = 8
MAX_RAHMEN_JE_BLOCK = 4096     # begrenzt die Arbeit je Prüfschritt


# ---------------------------------------------------------------------------
# CRC-8 (Polynom 0x07, Start 0x00), identisch zur Firmware
# ---------------------------------------------------------------------------
def _crc8_tabelle():
    tab = np.zeros(256, dtype=np.uint8)
    for i in range(256):
        c = i
        for _ in range(8):
            c = ((c << 1) ^ 0x07) & 0xFF if c & 0x80 else (c << 1) & 0xFF
        tab[i] = c
    return tab


CRC_TAB = _crc8_tabelle()


def crc8(daten: bytes) -> int:
    c = 0
    for b in daten:
        c = int(CRC_TAB[c ^ b])
    return c


def baue_rahmen(seq: int, roh: int) -> bytes:
    """Erzeugt einen Rahmen wie die Firmware (für Tests)."""
    nutz = bytes([seq & 0xFF, (seq >> 8) & 0xFF, roh & 0xFF, (roh >> 8) & 0xFF, (roh >> 16) & 0xFF])
    return b"\xA5\x5A" + nutz + bytes([crc8(nutz)])


def ist_textzeile(seg: bytes) -> bool:
    """Nur druckbares ASCII - Binärdaten enthalten immer 0xA5 und fallen raus."""
    return all(32 <= c < 127 or c == 9 or c == 13 for c in seg)


# ---------------------------------------------------------------------------
# Parser für den gemischten Datenstrom (Binärrahmen + Textzeilen)
# ---------------------------------------------------------------------------
class StromParser:
    def __init__(self):
        self.rest = b""
        self.crc_fehler = 0
        self.muell_bytes = 0

    def feed(self, neu: bytes):
        """Liefert (seq: uint16-Array, roh: int32-Array, zeilen: list[str])."""
        data = self.rest + neu
        n = len(data)
        i = 0
        seq_teile, roh_teile, zeilen = [], [], []
        csv_seq, csv_roh = [], []

        while i < n:
            b = data[i]
            if b == 0xA5:
                if n - i < RAHMEN:
                    break                                  # Rahmen unvollständig
                m = min((n - i) // RAHMEN, MAX_RAHMEN_JE_BLOCK)
                blk = np.frombuffer(data, dtype=np.uint8, count=m * RAHMEN, offset=i).reshape(m, RAHMEN)
                ok = (blk[:, 0] == 0xA5) & (blk[:, 1] == 0x5A)
                crc = np.zeros(m, dtype=np.uint8)
                for spalte in range(2, 7):
                    crc = CRC_TAB[crc ^ blk[:, spalte]]
                ok &= crc == blk[:, 7]
                k = m if ok.all() else int(np.argmin(ok))  # Anzahl gültiger Rahmen am Stück
                if k == 0:
                    if blk[0, 1] == 0x5A:
                        self.crc_fehler += 1
                    else:
                        self.muell_bytes += 1
                    i += 1                                 # neu synchronisieren
                    continue
                g = blk[:k]
                seq_teile.append(g[:, 2].astype(np.uint16) | (g[:, 3].astype(np.uint16) << 8))
                roh = g[:, 4].astype(np.int32) | (g[:, 5].astype(np.int32) << 8) | (g[:, 6].astype(np.int32) << 16)
                roh_teile.append(np.where(roh & 0x800000, roh - 0x1000000, roh).astype(np.int32))
                i += k * RAHMEN
            elif 32 <= b < 127:
                j = data.find(b"\n", i)
                if j < 0:
                    if n - i > 2048:                       # keine Zeile, sondern Müll
                        self.muell_bytes += 1
                        i += 1
                        continue
                    break                                  # Zeile unvollständig
                seg = data[i:j]
                if not ist_textzeile(seg):
                    self.muell_bytes += 1
                    i += 1
                    continue
                zeile = seg.decode("ascii").rstrip("\r")
                i = j + 1
                teile = zeile.split(",")
                if len(teile) == 3 and all(t.strip().lstrip("-").isdigit() for t in teile):
                    csv_seq.append(int(teile[0]) & 0xFFFF)  # CSV-Modus: seq,t_us,roh
                    csv_roh.append(int(teile[2]))
                else:
                    zeilen.append(zeile)
            else:
                if b not in (10, 13):
                    self.muell_bytes += 1
                i += 1

        self.rest = data[i:]
        if csv_seq:
            seq_teile.append(np.array(csv_seq, dtype=np.uint16))
            roh_teile.append(np.array(csv_roh, dtype=np.int32))
        if seq_teile:
            return np.concatenate(seq_teile), np.concatenate(roh_teile), zeilen
        return np.zeros(0, np.uint16), np.zeros(0, np.int32), zeilen


def parse_kv(zeile: str) -> dict:
    """'#CFG a=1 b=2.5' -> {'a': 1.0, 'b': 2.5}"""
    erg = {}
    for teil in zeile.split()[1:]:
        if "=" in teil:
            k, v = teil.split("=", 1)
            try:
                erg[k] = float(v)
            except ValueError:
                erg[k] = v
    return erg


# ---------------------------------------------------------------------------
# Ringpuffer für Messwerte (laufende Nummer entfaltet + Rohwert)
# ---------------------------------------------------------------------------
class Ring:
    def __init__(self, kapazitaet=2_000_000):
        self.kap = kapazitaet
        self.idx = np.zeros(kapazitaet, dtype=np.int64)
        self.roh = np.zeros(kapazitaet, dtype=np.int32)
        self.pos = 0
        self.anzahl = 0

    def leeren(self):
        self.pos = 0
        self.anzahl = 0

    def anhaengen(self, idx, roh):
        n = len(idx)
        if n >= self.kap:
            idx, roh, n = idx[-self.kap:], roh[-self.kap:], self.kap
        ende = self.pos + n
        if ende <= self.kap:
            self.idx[self.pos:ende] = idx
            self.roh[self.pos:ende] = roh
        else:
            t = self.kap - self.pos
            self.idx[self.pos:] = idx[:t]
            self.roh[self.pos:] = roh[:t]
            self.idx[:n - t] = idx[t:]
            self.roh[:n - t] = roh[t:]
        self.pos = ende % self.kap
        self.anzahl = min(self.anzahl + n, self.kap)

    def letzte(self, n):
        n = min(n, self.anzahl)
        if n <= 0:
            return np.zeros(0, np.int64), np.zeros(0, np.int32)
        start = self.pos - n
        if start >= 0:
            return self.idx[start:self.pos], self.roh[start:self.pos]
        return (np.concatenate((self.idx[start:], self.idx[:self.pos])),
                np.concatenate((self.roh[start:], self.roh[:self.pos])))


# ---------------------------------------------------------------------------
# Lese-Thread: serielle Schnittstelle -> Parser -> Queue
# ---------------------------------------------------------------------------
class Leser(threading.Thread):
    def __init__(self, port: str):
        super().__init__(daemon=True)
        self.ser = serial.Serial(port, 115200, timeout=0.05)   # USB-CDC: Baudrate egal
        self.ser.dtr = True                                     # Pico sendet nur mit DTR
        self.parser = StromParser()
        self.q = queue.Queue()
        self.lauf = True
        self.lock = threading.Lock()

    def senden(self, befehl: str):
        with self.lock:
            try:
                self.ser.write((befehl.strip() + "\n").encode("ascii", "replace"))
            except serial.SerialException as e:
                self.q.put(("zeile", f"# PC: Senden fehlgeschlagen: {e}"))

    def run(self):
        while self.lauf:
            try:
                daten = self.ser.read(max(1, self.ser.in_waiting))
            except serial.SerialException as e:
                self.q.put(("zeile", f"# PC: Verbindung verloren: {e}"))
                break
            if not daten:
                continue
            seq, roh, zeilen = self.parser.feed(daten)
            for z in zeilen:
                self.q.put(("zeile", z))
            if len(seq):
                self.q.put(("werte", seq, roh))

    def stopp(self):
        self.lauf = False
        self.join(timeout=1.0)
        try:
            self.ser.close()
        except serial.SerialException:
            pass


def finde_port():
    for p in serial.tools.list_ports.comports():
        if p.vid == 0x2E8A:                                     # Raspberry Pi (Pico)
            return p.device
    ports = serial.tools.list_ports.comports()
    return ports[0].device if ports else None


# ---------------------------------------------------------------------------
# Statistik über ein Fenster von Rohwerten
# ---------------------------------------------------------------------------
def statistik(roh: np.ndarray, rate: float):
    if len(roh) < 2:
        return None
    x = roh.astype(np.float64)
    sigma = float(np.std(x, ddof=1))
    erg = {"n": len(x), "mittel": float(np.mean(x)), "sigma": sigma,
           "pp": float(x.max() - x.min()), "sigma10": None}
    erg["enob"] = np.log2(VOLLBEREICH / sigma) if sigma > 0 else 24.0
    erg["nfbit"] = np.log2(VOLLBEREICH / (6.6 * sigma)) if sigma > 0 else 24.0
    blk = int(round(rate / 10.0))                               # Blockmittel auf 10 SPS
    if rate >= 20 and len(x) >= 5 * blk:
        m = len(x) // blk
        mittel = x[-m * blk:].reshape(m, blk).mean(axis=1)
        erg["sigma10"] = float(np.std(mittel, ddof=1))
        erg["bloecke"] = m
    return erg


def gleitender_mittelwert(y: np.ndarray, n: int) -> np.ndarray:
    if n <= 1 or len(y) < n:
        return y
    c = np.cumsum(np.insert(y, 0, 0.0))
    aus = np.empty_like(y)
    aus[:n - 1] = np.nan
    aus[n - 1:] = (c[n:] - c[:-n]) / n
    return aus


# ---------------------------------------------------------------------------
# Hauptfenster
# ---------------------------------------------------------------------------
class Hauptfenster(QtWidgets.QMainWindow):
    def __init__(self, leser: Leser, fenster_s: float, start_rate):
        super().__init__()
        self.leser = leser
        self.cfg = {"rate": 100.0, "uv_count": 2 * 2.5e6 / 64 / 8388607, "tara": 0.0,
                    "faktor": 16.1, "kalibriert": 0}
        self.ring = Ring()
        self.letzte_seq = None
        self.idx_letzt = 0
        self.verloren = 0
        self.empfangen = 0
        self.rate_mess = 0.0
        self._zaehl_t = time.monotonic()
        self._zaehl_n = 0
        self.datei = None
        self.bildzaehler = 0
        self.start_rate = start_rate

        self.setWindowTitle(f"Waegezelle - {leser.ser.port}")
        self.resize(1300, 850)
        zentral = QtWidgets.QWidget()
        self.setCentralWidget(zentral)
        haupt = QtWidgets.QVBoxLayout(zentral)

        # --- Bedienleiste ---
        leiste = QtWidgets.QHBoxLayout()
        haupt.addLayout(leiste)
        leiste.addWidget(QtWidgets.QLabel("Datenrate:"))
        self.cb_rate = QtWidgets.QComboBox()
        for r in RATEN:
            self.cb_rate.addItem(f"{r:g} SPS", float(r))
        self.cb_rate.activated.connect(self.rate_gewaehlt)
        leiste.addWidget(self.cb_rate)

        leiste.addWidget(QtWidgets.QLabel("Anzeige:"))
        self.cb_einheit = QtWidgets.QComboBox()
        self.cb_einheit.addItems(["Gramm", "µV", "Counts"])
        leiste.addWidget(self.cb_einheit)

        leiste.addWidget(QtWidgets.QLabel("Fenster [s]:"))
        self.sp_fenster = QtWidgets.QDoubleSpinBox()
        self.sp_fenster.setRange(0.05, 600)
        self.sp_fenster.setValue(fenster_s)
        leiste.addWidget(self.sp_fenster)

        leiste.addWidget(QtWidgets.QLabel("PC-Mittelwert [Werte]:"))
        self.sp_filter = QtWidgets.QSpinBox()
        self.sp_filter.setRange(1, 100000)
        self.sp_filter.setValue(1)
        leiste.addWidget(self.sp_filter)

        self.cb_pause = QtWidgets.QCheckBox("Pause")
        leiste.addWidget(self.cb_pause)
        leiste.addStretch(1)

        for text, befehl in [("Tara", "t"), ("Rauschtest", "n"), ("Rauschvergleich", "nv"),
                             ("Info", "i"), ("Abbruch", "x")]:
            k = QtWidgets.QPushButton(text)
            k.clicked.connect(lambda _=False, b=befehl: self.senden(b))
            leiste.addWidget(k)
        self.kn_aufz = QtWidgets.QPushButton("Aufzeichnen")
        self.kn_aufz.setCheckable(True)
        self.kn_aufz.toggled.connect(self.aufzeichnen)
        leiste.addWidget(self.kn_aufz)

        # --- Plot ---
        pg.setConfigOptions(antialias=False)
        self.plot = pg.PlotWidget()
        self.plot.showGrid(x=True, y=True, alpha=0.3)
        self.plot.setLabel("bottom", "Zeit", units="s")
        self.plot.setDownsampling(auto=True, mode="peak")
        self.plot.setClipToView(True)
        self.kurve = self.plot.plot(pen=pg.mkPen((80, 160, 255), width=1))
        self.kurve_f = self.plot.plot(pen=pg.mkPen((255, 170, 0), width=2))
        haupt.addWidget(self.plot, stretch=5)

        # --- Statistik + Log ---
        unten = QtWidgets.QHBoxLayout()
        haupt.addLayout(unten, stretch=2)
        self.lbl_stat = QtWidgets.QLabel()
        self.lbl_stat.setTextFormat(QtCore.Qt.RichText)
        self.lbl_stat.setAlignment(QtCore.Qt.AlignTop | QtCore.Qt.AlignLeft)
        self.lbl_stat.setMinimumWidth(430)
        self.lbl_stat.setTextInteractionFlags(QtCore.Qt.TextSelectableByMouse)
        unten.addWidget(self.lbl_stat)

        rechts = QtWidgets.QVBoxLayout()
        unten.addLayout(rechts, stretch=1)
        self.log = QtWidgets.QPlainTextEdit()
        self.log.setReadOnly(True)
        self.log.setMaximumBlockCount(3000)
        schrift = self.log.font()
        schrift.setFamily("Consolas")
        self.log.setFont(schrift)
        rechts.addWidget(self.log)
        zeile = QtWidgets.QHBoxLayout()
        rechts.addLayout(zeile)
        self.eingabe = QtWidgets.QLineEdit()
        self.eingabe.setPlaceholderText("Befehl an die Firmware, z. B. 'r 2000', 'nv 3 500 1000 2000', 'k 5000'")
        self.eingabe.returnPressed.connect(self.eingabe_senden)
        zeile.addWidget(self.eingabe)

        # --- Zeitgeber ---
        self.timer = QtCore.QTimer()
        self.timer.timeout.connect(self.aktualisieren)
        self.timer.start(33)

        # Firmware in den Binärmodus schalten und Konfiguration anfordern
        QtCore.QTimer.singleShot(300, self.verbindung_starten)

    # ----- Befehle -----
    def senden(self, befehl: str):
        self.log.appendPlainText(f"> {befehl}")
        self.leser.senden(befehl)

    def verbindung_starten(self):
        self.senden("m 4")
        if self.start_rate:
            self.senden(f"r {self.start_rate:g}")
        else:
            self.senden("cfg")

    def eingabe_senden(self):
        t = self.eingabe.text().strip()
        if t:
            self.senden(t)
        self.eingabe.clear()

    def rate_gewaehlt(self, _index):
        self.senden(f"r {self.cb_rate.currentData():g}")

    def aufzeichnen(self, an: bool):
        if an:
            name = time.strftime("waegezelle_%Y%m%d_%H%M%S.csv")
            self.datei = open(name, "w", encoding="ascii")
            self.datei.write(f"# rate={self.cfg['rate']:g} tara={self.cfg['tara']} faktor={self.cfg['faktor']} "
                             f"uv_count={self.cfg['uv_count']}\n")
            self.datei.write("idx,roh,gramm\n")
            self.log.appendPlainText(f"# PC: Aufzeichnung nach {name}")
            self.kn_aufz.setText("Aufzeichnung stoppen")
        else:
            if self.datei:
                self.datei.close()
                self.log.appendPlainText("# PC: Aufzeichnung beendet")
            self.datei = None
            self.kn_aufz.setText("Aufzeichnen")

    # ----- Datenaufnahme -----
    def neue_cfg(self, kv: dict):
        alte_rate = self.cfg.get("rate")
        self.cfg.update(kv)
        if kv.get("rate") is not None and kv["rate"] != alte_rate:
            self.ring.leeren()                 # andere Zeitbasis -> neu beginnen
            self.letzte_seq = None
            self.idx_letzt = 0
            if self.datei:
                self.datei.write(f"# rate={kv['rate']:g}\n")
        i = self.cb_rate.findData(float(self.cfg["rate"]))
        if i >= 0:
            self.cb_rate.blockSignals(True)
            self.cb_rate.setCurrentIndex(i)
            self.cb_rate.blockSignals(False)

    def werte_aufnehmen(self, seq: np.ndarray, roh: np.ndarray):
        # laufende Nummer (16 Bit) entfalten und Lücken zählen
        s = seq.astype(np.int64)
        if self.letzte_seq is None:
            d0 = 1
        else:
            d0 = (int(s[0]) - self.letzte_seq) & 0xFFFF
            if d0 == 0:
                d0 = 1
        d = np.empty(len(s), dtype=np.int64)
        d[0] = d0
        d[1:] = (s[1:] - s[:-1]) & 0xFFFF
        d[d == 0] = 1
        self.verloren += int(np.sum(d - 1))
        idx = self.idx_letzt + np.cumsum(d)
        self.idx_letzt = int(idx[-1])
        self.letzte_seq = int(s[-1])
        self.ring.anhaengen(idx, roh)
        self.empfangen += len(roh)
        self._zaehl_n += len(roh)
        if self.datei:
            g = (roh - self.cfg["tara"]) / self.cfg["faktor"]
            np.savetxt(self.datei, np.column_stack((idx, roh, g)), fmt=["%d", "%d", "%.4f"], delimiter=",")

    def umrechnen(self, roh: np.ndarray) -> np.ndarray:
        e = self.cb_einheit.currentText()
        x = roh.astype(np.float64)
        if e == "Gramm":
            return (x - self.cfg["tara"]) / self.cfg["faktor"]
        if e == "µV":
            return x * self.cfg["uv_count"]
        return x

    # ----- Zeitgeber: Queue leeren, zeichnen, Statistik -----
    def aktualisieren(self):
        t_ende = time.monotonic() + 0.02
        while time.monotonic() < t_ende:
            try:
                eintrag = self.leser.q.get_nowait()
            except queue.Empty:
                break
            if eintrag[0] == "werte":
                self.werte_aufnehmen(eintrag[1], eintrag[2])
            else:
                z = eintrag[1]
                self.log.appendPlainText(z)
                if z.startswith("#CFG"):
                    self.neue_cfg(parse_kv(z))

        jetzt = time.monotonic()
        if jetzt - self._zaehl_t >= 1.0:
            self.rate_mess = self._zaehl_n / (jetzt - self._zaehl_t)
            self._zaehl_n = 0
            self._zaehl_t = jetzt

        if self.cb_pause.isChecked():
            return
        rate = float(self.cfg["rate"])
        n = max(2, int(self.sp_fenster.value() * rate))
        idx, roh = self.ring.letzte(n)
        if len(roh) < 2:
            self.statistik_anzeigen(None, rate)
            return
        t = (idx - idx[-1]) / rate
        y = self.umrechnen(roh)
        self.kurve.setData(t, y)
        nf = self.sp_filter.value()
        if nf > 1:
            self.kurve_f.setData(t, gleitender_mittelwert(y, nf), connect="finite")
        else:
            self.kurve_f.setData([], [])
        name = self.cb_einheit.currentText()
        einheit = {"Gramm": "g", "µV": "µV", "Counts": "Counts"}[name]
        self.plot.setLabel("left", f"{name} [{einheit}]")   # ohne units=, sonst SI-Präfix-Skalierung

        self.bildzaehler += 1
        if self.bildzaehler % 5 == 0:                   # Statistik ca. 6x pro Sekunde
            self.statistik_anzeigen(statistik(roh, rate), rate)

    def statistik_anzeigen(self, s, rate):
        uv = self.cfg["uv_count"]
        fa = abs(self.cfg["faktor"]) or 1.0
        kal = "" if self.cfg.get("kalibriert") else " <span style='color:#c60'>(unkalibriert)</span>"
        p = self.leser.parser
        kopf = (f"<b>Datenrate:</b> {rate:g} SPS &nbsp; <b>empfangen:</b> {self.rate_mess:,.0f} /s<br>"
                f"<b>Verloren:</b> {self.verloren} &nbsp; <b>CRC-Fehler:</b> {p.crc_fehler} "
                f"&nbsp; <b>Müll-Bytes:</b> {p.muell_bytes}<br>"
                f"<b>Faktor:</b> {self.cfg['faktor']:.4f} Counts/g{kal} &nbsp; <b>Tara:</b> {self.cfg['tara']:.1f}<br>")
        if not s:
            self.lbl_stat.setText(kopf + "<i>warte auf Messwerte ...</i>")
            return
        zeilen = [
            f"<b>Statistik über {s['n']:,} Werte ({s['n'] / rate:.2f} s, ungefiltert)</b>",
            "<table cellspacing=0 cellpadding=1>",
            "<tr><td></td><td align=right><b>Counts</b></td><td align=right><b>µV</b></td><td align=right><b>g</b></td></tr>",
            f"<tr><td>Mittelwert&nbsp;</td><td align=right>{s['mittel']:.1f}</td><td align=right>{s['mittel'] * uv:.3f}</td>"
            f"<td align=right>{(s['mittel'] - self.cfg['tara']) / self.cfg['faktor']:.3f}</td></tr>",
            f"<tr><td>Std.-Abw. σ&nbsp;</td><td align=right>{s['sigma']:.3f}</td><td align=right>{s['sigma'] * uv:.4f}</td>"
            f"<td align=right>{s['sigma'] / fa:.4f}</td></tr>",
            f"<tr><td>Spitze-Spitze&nbsp;</td><td align=right>{s['pp']:.0f}</td><td align=right>{s['pp'] * uv:.3f}</td>"
            f"<td align=right>{s['pp'] / fa:.3f}</td></tr>",
        ]
        if s["sigma10"] is not None:
            zeilen.append(f"<tr><td>σ @10 SPS&nbsp;</td><td align=right>{s['sigma10']:.3f}</td>"
                          f"<td align=right>{s['sigma10'] * uv:.4f}</td><td align=right>{s['sigma10'] / fa:.4f}</td></tr>")
        zeilen.append("</table>")
        zeilen.append(f"<b>Effektive Auflösung:</b> {s['enob']:.2f} Bit &nbsp; <b>rauschfrei:</b> {s['nfbit']:.2f} Bit")
        if s["sigma"] > 0:
            teile = float(self.cfg.get("nennlast_g", 50000)) * fa / s["sigma"]
            zeilen.append(f"<br><b>Bezogen auf Nennlast:</b> {teile:,.0f} Teile = {np.log2(teile):.2f} Bit")
        self.lbl_stat.setText(kopf + "".join(zeilen))

    def closeEvent(self, ev):
        self.timer.stop()
        if self.datei:
            self.datei.close()
        self.leser.senden("m 1")             # Firmware wieder lesbar für den seriellen Monitor
        time.sleep(0.05)
        self.leser.stopp()
        super().closeEvent(ev)


def main():
    ap = argparse.ArgumentParser(description="Live-Anzeige Wägezelle (ADS1256 + Pico W)")
    ap.add_argument("--port", help="serielle Schnittstelle, z. B. COM5 (Standard: automatisch)")
    ap.add_argument("--fenster", type=float, default=10.0, help="sichtbares Zeitfenster in s")
    ap.add_argument("--rate", type=float, choices=RATEN, help="Datenrate beim Start setzen")
    args = ap.parse_args()

    port = args.port or finde_port()
    if not port:
        sys.exit("Keine serielle Schnittstelle gefunden. Mit --port angeben.")
    try:
        leser = Leser(port)
    except serial.SerialException as e:
        sys.exit(f"Port {port} kann nicht geöffnet werden (serieller Monitor noch offen?): {e}")
    leser.start()

    app = QtWidgets.QApplication(sys.argv)
    fenster = Hauptfenster(leser, args.fenster, args.rate)
    fenster.show()
    sys.exit(app.exec() if hasattr(app, "exec") else app.exec_())


if __name__ == "__main__":
    main()
