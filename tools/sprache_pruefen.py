#!/usr/bin/env python3
"""
sprache_pruefen.py - prüft, ob jeder deutsche Text der Weboberfläche eine englische Übersetzung hat.

Liest die Seiten src/web/seiten/webui.h und presse.h und sammelt
  - statischen Text (Textknoten außerhalb von <script>/<style>/<pre>), title- und placeholder-Attribute,
  - die Schlüssel aller Aufrufe T("...") im Skript sowie die Texte in GRUND, STATUS und TIPP,
und vergleicht sie mit dem Wörterbuch WOERTER in src/web/seiten/sprache.h.
Firmware-Texte (TF) stehen nur im Wörterbuch und werden nicht geprüft.

Aufruf:  python tools/sprache_pruefen.py          (Rückgabe 1, wenn Übersetzungen fehlen)
         python tools/sprache_pruefen.py --liste  (alle benötigten Schlüssel ausgeben)
"""
import json
import re
import sys
from html.parser import HTMLParser
from pathlib import Path

WURZEL = Path(__file__).resolve().parent.parent
SEITEN = WURZEL / "src" / "web" / "seiten"
BUCHSTABE = re.compile(r"[A-Za-zÄÖÜäöüß]")


def norm(s):
    return re.sub(r"\s+", " ", s).strip()


def literal(datei):
    """Inhalt des Raw-String-Literals R"X( ... )X" einer Seite."""
    text = datei.read_text(encoding="utf-8")
    m = re.search(r'R"(\w+)\((.*)\)\1"', text, re.S)
    if not m:
        raise SystemExit(f"{datei}: kein Raw-String gefunden")
    return m.group(2)


class Sammler(HTMLParser):
    def __init__(self):
        super().__init__(convert_charrefs=True)
        self.texte, self.skripte, self.tiefe = [], [], []

    def handle_starttag(self, tag, attrs):
        if tag in ("script", "style", "pre", "title"):
            self.tiefe.append(tag)
        for name, wert in attrs:
            if name in ("title", "placeholder", "aria-label") and wert and BUCHSTABE.search(wert):
                self.texte.append(norm(wert))

    def handle_endtag(self, tag):
        if self.tiefe and self.tiefe[-1] == tag:
            self.tiefe.pop()

    def handle_data(self, data):
        if self.tiefe and self.tiefe[-1] == "script":
            self.skripte.append(data)
            return
        if self.tiefe and self.tiefe[-1] in ("style", "pre"):
            return
        t = norm(data)
        if t and BUCHSTABE.search(t):
            self.texte.append(t)


STRING = r'"((?:[^"\\]|\\.)*)"|\'((?:[^\'\\]|\\.)*)\''


def js_strings(ausschnitt):
    return [bytes(a or b, "utf-8").decode("unicode_escape").encode("latin-1").decode("utf-8")
            if "\\" in (a or b) else (a or b) for a, b in re.findall(STRING, ausschnitt)]


def schluessel_aus_skript(js):
    keys = []
    for m in re.finditer(r"\bT\(\s*(?:" + STRING + r")", js):
        keys.append(m.group(1) if m.group(1) is not None else m.group(2))
    for name in ("GRUND", "STATUS"):
        m = re.search(r"const " + name + r"=\[(.*?)\];", js, re.S)
        if m:
            keys += js_strings(m.group(1))
    m = re.search(r"const TIPP=\{(.*?)\};", js, re.S)
    if m:
        keys += js_strings(m.group(1))
    keys += re.findall(r'gr\("([^"]+)"\)', js) + re.findall(r'dt\("([^"]+)"\)', js)
    return [k for k in keys if BUCHSTABE.search(k) and k not in ("?",)]


def woerter():
    text = (SEITEN / "sprache.h").read_text(encoding="utf-8")
    m = re.search(r"/\*WOERTER\*/(.*?)/\*ENDE\*/", text, re.S)
    if not m:
        raise SystemExit("sprache.h: Marken /*WOERTER*/ ... /*ENDE*/ fehlen")
    return json.loads(m.group(1))


def main():
    benoetigt = {}
    for datei in ("webui.h", "presse.h"):
        s = Sammler()
        s.feed(literal(SEITEN / datei))
        for t in s.texte + schluessel_aus_skript("\n".join(s.skripte)):
            benoetigt.setdefault(t, datei)
    if "--liste" in sys.argv:
        for k, d in benoetigt.items():
            print(f"{d}\t{k}")
        return 0
    w = woerter()
    # Werte ohne Übersetzung (z. B. Einheiten, Eigennamen) sind erlaubt, wenn sie in BLEIBT stehen
    bleibt = {"EcklC", "OpenSmartPress", "ADS1256", "HB808C", "Info", "STOPP", "Counts", "µV", "Pause", "Auto",
              "CSV", "JSON", "SPS", "Pos.", "mm", "g", "N", "kg", "lbs", "Status", "Name", "Min", "Max", "σ",
              "Sprache / Language", "DE", "EN", "Live-Statistik", "Position", "Simulation", "R", "START"}
    zahl = re.compile(r"^[\d.,]+ s$")                       # Glättungs-Optionen, setzt das Skript neu
    fehlt = [(d, k) for k, d in benoetigt.items() if k not in w and k not in bleibt and not zahl.match(k)]
    for d, k in fehlt:
        print(f"FEHLT ({d}): {k}")
    leer = [k for k, v in w.items() if not v.strip()]
    for k in leer:
        print(f"LEER: {k}")
    print(f"{len(benoetigt)} Texte, {len(w)} Übersetzungen, {len(fehlt)} fehlen")
    return 1 if fehlt or leer else 0


if __name__ == "__main__":
    sys.exit(main())
