/*
 * webinfo.h - Hilfeseite der Weboberfläche (/info)
 *
 * Erklärt alle Regler und Kennzahlen der Messseite und gibt Einstelltipps.
 * Nutzt das gemeinsame Stylesheet /stil.css (webstil.h).
 */
#pragma once

static const char WEBINFO_HTML[] = R"WEBINFO(<!doctype html>
<html lang="de"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Hilfe &amp; Tipps · Wägezelle</title>
<link rel="stylesheet" href="/stil.css"></head><body><main class="info">

<header>
  <div class="marke">
    <div class="logo"><svg width="20" height="20" viewBox="0 0 20 20" fill="none" stroke="#031003" stroke-width="2.2"
      stroke-linecap="round" stroke-linejoin="round"><polyline points="1,11 5,11 7.5,4 11,16 13.5,8 15.5,11 19,11"/></svg></div>
    <h1>EcklC <em>OpenSmartPress</em> <span>Wägezelle · ADS1256</span></h1>
  </div>
  <div class="status"><a class="chip" href="/">← zur Messung</a></div>
</header>

<div class="karte glow">
  <div class="titel">Hilfe &amp; Tipps</div>
  <h2>Was die Zahlen bedeuten und wie du <em>sinnvoll</em> einstellst</h2>
  <p>Diese Seite erklärt jeden Regler und jede Kennzahl der Messseite und gibt Empfehlungen für typische Aufgaben.
    Alle Werte beziehen sich auf deinen Aufbau: 50-kg-Wägezelle mit 1,0–2,0 mV/V, 5,00 V Speisung aus dem LT3042,
    ADS1256 mit PGA 64.</p>
  <nav class="inhalt" aria-label="Inhalt">
    <a class="chip" href="#schnellstart">Schnellstart</a>
    <a class="chip" href="#signalweg">Signalweg</a>
    <a class="chip" href="#regler">Regler</a>
    <a class="chip" href="#kennzahlen">Kennzahlen</a>
    <a class="chip" href="#datenrate">Datenrate</a>
    <a class="chip" href="#tipps">Tipps</a>
    <a class="chip" href="#kalibrieren">Kalibrieren</a>
    <a class="chip" href="#antrieb">Antrieb</a>
    <a class="chip" href="#presse">Pressen</a>
    <a class="chip" href="#befehle">Befehle</a>
  </nav>
</div>

<div class="karte" id="schnellstart">
  <div class="titel">Schnellstart</div>
  <h3>Empfohlene Grundeinstellung zum Wiegen</h3>
  <div class="tabelle"><table>
    <tr><th>Einstellung</th><th>Wert</th><th>Warum</th></tr>
    <tr><td>Datenrate</td><td>2000 SPS</td><td>In deinem Rauschvergleich war 2000 SPS mit Mittelung am ruhigsten (σ @10 SPS 0,59 g gegenüber 1,46 g bei 100 SPS).</td></tr>
    <tr><td>Glättung</td><td>0,5 s</td><td>Ruhige Anzeige, reagiert trotzdem nach etwa 0,25 s auf Laständerungen.</td></tr>
    <tr><td>Statistik</td><td>2–5 s</td><td>Genug Werte für ein stabiles σ, aber kurz genug, um Änderungen zu sehen.</td></tr>
    <tr><td>Fenster</td><td>10 s</td><td>Zeigt Einschwingen und Drift, ohne dass Details verschwinden.</td></tr>
    <tr><td>Min/Max-Band</td><td>an</td><td>Zeigt die Rauschbreite um die Linie.</td></tr>
  </table></div>
  <h3>Ablauf nach dem Einschalten</h3>
  <ol>
    <li>Gerät <strong>15–30 Minuten warmlaufen</strong> lassen. Zelle, Referenz und ADC driften in der ersten Zeit am stärksten.</li>
    <li>Datenrate und Glättung wie oben einstellen.</li>
    <li>Zelle entlasten, kurz warten, bis die Linie ruhig ist, dann <strong>Tara</strong> drücken.</li>
    <li>Falls noch nicht geschehen: einmal <a href="#kalibrieren">kalibrieren</a>.</li>
    <li>Mit <code>s</code> speichern. Gespeichert werden Tara, Faktor, Glättung, Datenrate und Ausgabemodus.</li>
  </ol>
</div>

<div class="karte" id="signalweg">
  <div class="titel">Signalweg</div>
  <h3>Vom Gewicht zur Zahl</h3>
  <div class="fluss">
    <div><b>Wägezelle</b>Vollbrücke 350 Ω. Bei 50 kg gibt sie 5–10 mV ab (1,0–2,0 mV/V × 5 V).</div>
    <div class="pfeil">→</div>
    <div><b>ADS1256</b>24 Bit, PGA 64. Messbereich ±78,125 mV, 1 Count = 9,313 nV.</div>
    <div class="pfeil">→</div>
    <div><b>Pico, Kern 1</b>Liest jeden Wert sofort nach dem DRDY-Signal und puffert ihn.</div>
    <div class="pfeil">→</div>
    <div><b>Pico, Kern 0</b>Glättung, Statistik, Überlastwarnung, WLAN und USB.</div>
    <div class="pfeil">→</div>
    <div><b>Browser / USB</b>Im Browser 50 Punkte pro Sekunde, über USB jeder einzelne Wert.</div>
  </div>
  <p>Bei 50 kg nutzt das Signal nur etwa ein Achtel des ADC-Bereichs. Der Rest ist Reserve, damit auch Überlast
    noch gemessen werden kann.</p>
  <p>Umrechnung: <code>Gramm = (Counts − Tara) / Faktor</code> und <code>µV = Counts × 0,0093132</code>.
    Tara ist der Nullpunkt in Counts, der Faktor sind Counts pro Gramm (bei dir zuletzt etwa 10,06).</p>
</div>

<div class="karte" id="regler">
  <div class="titel">Regler</div>
  <h3>Steuerung auf der Messseite</h3>
  <div class="tabelle"><table>
    <tr><th>Regler</th><th>Was er tut</th><th>Tipp</th></tr>
    <tr><td>Datenrate</td><td>Wie oft der ADC pro Sekunde misst (2,5 bis 30000 SPS). Nach jedem Wechsel kalibriert sich der ADC kurz selbst.</td><td>Siehe <a href="#datenrate">Datenrate</a>. Für Wägungen 2000 SPS.</td></tr>
    <tr><td>Anzeige</td><td>Einheit für Gewichtsanzeige und Diagramm: Gramm, µV (Brückenspannung) oder Counts (Rohwert).</td><td>µV und Counts helfen bei der Fehlersuche, weil sie nicht von Tara und Faktor abhängen.</td></tr>
    <tr><td>Fenster</td><td>Wie viele Sekunden das Diagramm zeigt (1–600 s).</td><td>Kurz (2–5 s) für schnelle Vorgänge, lang (60 s und mehr) für Drift.</td></tr>
    <tr><td>Glättung</td><td>Gleitender Mittelwert über die gewählte Zeit. Wirkt auf die Gewichtsanzeige und die grüne Linie, nicht auf die Statistik.</td><td>Die Anzeige hinkt etwa um die halbe Glättungszeit hinterher.</td></tr>
    <tr><td>Rohsignal</td><td>Blendet die blasse, ungeglättete Linie ein oder aus.</td><td>Aus, wenn nur der geglättete Verlauf interessiert.</td></tr>
    <tr><td>Statistik</td><td>Über wie viele Sekunden die Live-Statistik gerechnet wird. Danach beginnt eine neue Runde.</td><td>2–5 s für den Alltag, 30–60 s für genaue Rauschwerte.</td></tr>
    <tr><td>Min/Max-Band</td><td>Grüne Fläche zwischen kleinstem und größtem Rohwert je Diagrammpunkt (je 20 ms).</td><td>Wird das Band plötzlich breiter, kommt eine Störung dazu.</td></tr>
    <tr><td>Pause</td><td>Friert den angezeigten Verlauf ein. Die Messung läuft weiter.</td><td>Im angehaltenen Verlauf kannst du weiter zoomen und verschieben.</td></tr>
    <tr><td>Y-Skala</td><td><strong>Auto</strong> passt die Achse laufend an. <strong>Auto mit Null</strong> hält die Null-Linie im Bild.
      <strong>Fest</strong> nutzt deine Werte für „von“ und „bis“ in der gewählten Einheit.</td>
      <td>Im Diagramm: Mausrad zoomt um den Mauszeiger, Ziehen verschiebt, Doppelklick schaltet auf Auto.
      Bei fester Skala zeigt „▲ Werte darüber“ bzw. „▼ darunter“, dass etwas abgeschnitten ist.
      Beim Wechsel der Einheit springt die Skala auf Auto.</td></tr>
    <tr><td>Tara</td><td>Mittelt etwa 2 s und setzt diesen Wert als Nullpunkt.</td><td>Nur bei entlasteter, ruhiger Zelle drücken.</td></tr>
    <tr><td>Rauschtest</td><td>Misst etwa 5 s ungeglättet und schreibt eine ausführliche Rauschanalyse ins Protokoll.</td><td>Zelle dabei nicht berühren.</td></tr>
    <tr><td>Rauschvergleich</td><td>Misst nacheinander bei mehreren Datenraten und gibt eine Vergleichstabelle aus.</td><td>Dauert etwa 25 s. Danach wird die vorherige Datenrate wiederhergestellt.</td></tr>
    <tr><td>Info</td><td>Schreibt den aktuellen Gerätestatus ins Protokoll.</td><td>Zeigt auch verlorene Werte und die WLAN-Verbindung.</td></tr>
    <tr><td>Abbruch</td><td>Bricht Tara, Kalibrierung oder Rauschtest ab.</td><td>Läuft nichts, schaltet er die USB-Ausgabe aus.</td></tr>
    <tr><td>CSV speichern</td><td>Lädt die Diagrammdaten herunter: Zeit, Mittel, Min, Max und geglätteter Wert, 50 Zeilen pro Sekunde.</td><td>Für jeden einzelnen Messwert die Aufzeichnung über USB mit <code>waegezelle_plot.py</code> nutzen.</td></tr>
  </table></div>
</div>

<div class="karte" id="kennzahlen">
  <div class="titel">Kennzahlen</div>
  <h3>Live-Statistik</h3>
  <p>Die Statistik rechnet mit <strong>jedem einzelnen, ungeglätteten Messwert</strong> der letzten Statistik-Runde,
    nicht mit den 50 Diagrammpunkten pro Sekunde. Sie zeigt also, was der ADC wirklich liefert.</p>
  <dl>
    <div class="kennzahl"><dt>Mittelwert</dt><dd>
      <p>Durchschnitt aller Werte der Runde, in Counts, µV und Gramm.</p></dd></div>
    <div class="kennzahl"><dt>Std.-Abw. σ</dt><dd>
      <p>Standardabweichung, also das typische Rauschen <strong>eines einzelnen Messwerts</strong>. Etwa zwei Drittel aller
        Werte liegen innerhalb ±1 σ um den Mittelwert.</p>
      <p>Steigt mit der Datenrate, weil der ADC bei hoher Rate weniger intern mittelt. Deshalb allein kein gutes Maß
        für die Ablesegenauigkeit.</p></dd></div>
    <div class="kennzahl"><dt>Spitze-Spitze</dt><dd>
      <p>Abstand zwischen kleinstem und größtem Wert der Runde. Etwa 6–7 σ bei reinem Rauschen; deutlich mehr deutet
        auf Ausreißer, Stöße oder Störungen hin.</p></dd></div>
    <div class="kennzahl"><dt>σ @10 SPS</dt><dd>
      <p>Streuung der Mittelwerte über je 0,1 s. Das ist <strong>die wichtigste Zahl zum Vergleichen von Datenraten</strong>,
        weil sie bei jeder Rate dasselbe misst: wie genau ein 0,1-s-Mittel ist.</p>
      <p>Sie ist gemessen, nicht geschätzt, enthält also auch langsames Rauschen und Drift. 50-Hz-Brummen fällt heraus,
        weil 0,1 s genau 5 Netzperioden sind.</p></dd></div>
    <div class="kennzahl"><dt>σ geglättet</dt><dd>
      <p>Streuung des geglätteten Werts, also <strong>wie ruhig die Gewichtsanzeige mit der gewählten Glättung ist</strong>.
        Erscheint nur bei eingeschalteter Glättung.</p>
      <p>Bei rein zufälligem Rauschen sinkt sie mit der Wurzel der Glättungszeit: viermal so lange glätten halbiert
        das Rauschen. Langsame Drift entfernt die Glättung nicht.</p></dd></div>
    <div class="kennzahl"><dt>Effektive Auflösung</dt><dd>
      <p><code>ENOB = log2(2²⁴ / σ)</code>, in Bit, bezogen auf den vollen ADC-Bereich ±78,125 mV.
        Jedes Bit mehr halbiert das Rauschen.</p></dd></div>
    <div class="kennzahl"><dt>Rauschfrei</dt><dd>
      <p><code>log2(2²⁴ / (6,6 × σ))</code>. Innerhalb ±3,3 σ liegen 99,9 % aller Werte; so viele Bit sind praktisch
        ohne Flackern ablesbar. Immer etwa 2,7 Bit weniger als die effektive Auflösung.</p></dd></div>
    <div class="kennzahl"><dt>Bezogen auf Nennlast</dt><dd>
      <p><code>50 000 g / σ in Gramm</code>: in wie viele unterscheidbare Schritte sich der Bereich 0–50 kg teilen
        lässt. Aussagekräftiger als die effektive Auflösung, weil die Zelle nur ein Achtel des ADC-Bereichs nutzt.</p>
      <p>Die Zeile „Nach Mittelung auf 10 SPS“ rechnet dasselbe mit σ @10 SPS.</p></dd></div>
    <div class="kennzahl"><dt>Rate gemessen</dt><dd>
      <p>Tatsächliche Datenrate aus den Zeitstempeln. Kleine Abweichungen vom Sollwert (unter 0,01 %) kommen vom Quarz und sind normal.</p></dd></div>
    <div class="kennzahl"><dt>verloren</dt><dd>
      <p>Wandlungen, die nicht gelesen wurden. Kurze Lücken entstehen beim Speichern (<code>s</code>), beim Ratenwechsel
        und beim Registerlesen. Steigt der Zähler im normalen Betrieb, ist die Datenrate zu hoch für die Verarbeitung.</p></dd></div>
    <div class="kennzahl"><dt>ÜBERSTEUERT</dt><dd>
      <p>Der ADC war am Anschlag. Bei einer Wägezelle heißt das fast immer: Ader gebrochen, Kurzschluss oder fehlende
        Speisung. Werte aus dieser Runde nicht verwenden.</p></dd></div>
  </dl>
  <h3>Statusanzeige oben rechts</h3>
  <ul>
    <li><strong>misst</strong> (grüner Punkt): der ADC liefert Werte. Rot heißt angehalten, nicht initialisiert oder keine Verbindung zum Pico.</li>
    <li><strong>WLAN … dBm</strong>: Signalstärke. Besser als −67 dBm ist gut, schlechter als −80 dBm wird es unzuverlässig.</li>
    <li><strong>verloren</strong>: Summe aller verlorenen Werte seit dem Start.</li>
    <li><strong>Auftrag läuft</strong>: Tara, Kalibrierung oder Rauschtest ist aktiv. Andere Befehle werden so lange abgelehnt.</li>
  </ul>
</div>

<div class="karte" id="datenrate">
  <div class="titel">Datenrate</div>
  <h3>Mehr Werte, mehr Rauschen pro Wert</h3>
  <p>Bei niedriger Rate mittelt der ADS1256 intern über viele Abtastungen, jeder Wert ist ruhig. Bei hoher Rate
    kommen mehr, aber unruhigere Werte. Entscheidend ist, wie genau das Ergebnis <strong>nach dem Mitteln</strong>
    ist. Das zeigt σ @10 SPS.</p>
  <div class="tabelle"><table>
    <tr><th>SPS</th><th class="zahl">σ Einzelwert</th><th class="zahl">σ @10 SPS</th><th>Geeignet für</th></tr>
    <tr><td>10–60</td><td class="zahl">–</td><td class="zahl">–</td><td>Langzeitmessung mit wenig Daten. 50 SPS unterdrückt 50-Hz-Netzbrummen besonders gut.</td></tr>
    <tr><td>100</td><td class="zahl">25,9 Counts · 2,6 g</td><td class="zahl">1,46 g</td><td>Einfache Anzeige, wenig Datenvolumen.</td></tr>
    <tr><td>1000</td><td class="zahl">59,6 Counts · 5,9 g</td><td class="zahl">1,03 g</td><td>Langsame Kraftverläufe.</td></tr>
    <tr><td>2000</td><td class="zahl">76,2 Counts · 7,6 g</td><td class="zahl">0,59 g</td><td><strong>Wiegen und Kalibrieren</strong>, bester Wert in deinem Vergleich.</td></tr>
    <tr><td>3750–7500</td><td class="zahl">138,6 Counts · 13,8 g (7500)</td><td class="zahl">–</td><td>Schnelle Kraftverläufe, Pressvorgänge.</td></tr>
    <tr><td>15000–30000</td><td class="zahl">–</td><td class="zahl">–</td><td>Nur für sehr kurze Ereignisse (Stöße). Hohe Last für Pico und WLAN.</td></tr>
  </table></div>
  <p class="sub">Werte aus deinen Messungen: Rauschvergleich bei 100/1000/2000 SPS und Kalibrierlauf bei 7500 SPS, Faktor
    10,06 Counts/g. Ein einzelner 3-s-Lauf ist eine grobe Schätzung; für belastbare Zahlen <code>nv 10 2000 100 2000 100</code>
    laufen lassen.</p>
</div>

<div class="karte" id="tipps">
  <div class="titel">Tipps</div>
  <div class="raster">
    <div>
      <h3>Statisch wiegen</h3>
      <ul>
        <li>2000 SPS, Glättung 0,5–1 s.</li>
        <li>Gewicht mittig auflegen. Flachmembran-Zellen reagieren auf außermittige Last.</li>
        <li>Nach dem Auflegen 2–5 s warten, bis die grüne Linie flach ist.</li>
        <li>Für die letzte Stelle: Glättung 2 s und auf „σ geglättet“ achten.</li>
      </ul>
    </div>
    <div>
      <h3>Kraftverläufe und Pressvorgänge</h3>
      <ul>
        <li>2000–7500 SPS, Glättung aus oder 0,02–0,05 s, sonst werden Spitzen abgeflacht.</li>
        <li>Min/Max-Band an: zeigt Spitzen auch zwischen zwei Diagrammpunkten.</li>
        <li>Fenster 2–10 s, mit Pause eine Stelle ansehen.</li>
        <li>Zum Auswerten jeden einzelnen Wert über USB mit <code>waegezelle_plot.py</code> aufzeichnen.</li>
      </ul>
    </div>
    <div>
      <h3>Glättung richtig wählen</h3>
      <ul>
        <li>Verzögerung ≈ halbe Glättungszeit: bei 2 s reagiert die Anzeige etwa 1 s später.</li>
        <li>Viermal längere Glättung halbiert zufälliges Rauschen.</li>
        <li>Gegen Drift und Kriechen hilft Glättung nicht, nur Tara und Warmlaufen.</li>
        <li>Statistik und Aufträge (Tara, Kalibrierung) rechnen immer mit Rohwerten.</li>
      </ul>
    </div>
    <div>
      <h3>Stabile Messwerte</h3>
      <ul>
        <li>15–30 Minuten warmlaufen lassen, dann tarieren.</li>
        <li>Vor jeder Messreihe neu tarieren.</li>
        <li>Nach großer Last kriecht die Zelle einige Minuten nach. Vor dem Tarieren kurz warten.</li>
        <li>Luftzug, Vibration und Temperaturwechsel an der Zelle vermeiden.</li>
      </ul>
    </div>
    <div>
      <h3>Rauschen messen</h3>
      <ul>
        <li>Zelle unbelastet, nichts berühren, Tisch nicht anstoßen.</li>
        <li>Datenraten abwechselnd messen, z. B. <code>nv 10 2000 100 2000 100</code>, damit Drift nicht eine Rate benachteiligt.</li>
        <li>WLAN-Einfluss prüfen: über USB <code>wlan aus</code>, <code>nv 5</code>, dann <code>wlan an</code>, <code>nv 5</code>.</li>
      </ul>
    </div>
    <div>
      <h3>Aufbau</h3>
      <ul>
        <li>ADS1256-Versorgung aus dem LT3042 statt aus USB-5 V.</li>
        <li>Sternmasse am AGND des ADS1256, eigene Leitungen für Pico und Zelle.</li>
        <li>Grün/Weiß (AIN0/AIN1) verdrillen und vom Pico fernhalten.</li>
        <li>„ÜBERSTEUERT“ bei unbelasteter Zelle: Verdrahtung prüfen.</li>
      </ul>
    </div>
  </div>
  <div class="hinweis"><strong>Überlast:</strong> Ab 50 kg (100 %) erscheint eine Warnung, ab 60 kg (120 %) ein Alarm, und die LED am Pico leuchtet.
    Sofort entlasten. Die Grenzen nutzen den Faktor; ohne Kalibrierung sind sie nur geschätzt.</div>
</div>

<div class="karte" id="kalibrieren">
  <div class="titel">Kalibrieren</div>
  <h3>Faktor bestimmen</h3>
  <ol>
    <li>Datenrate 2000 SPS, Gerät warmgelaufen.</li>
    <li>Zelle entlasten, warten bis ruhig, <strong>Tara</strong>.</li>
    <li>Bekanntes Gewicht mittig auflegen, <strong>möglichst 5 kg oder mehr</strong> (10 % der Nennlast). Etwa 10 s warten.</li>
    <li>Befehl <code>k 5000 10000</code> eingeben: Gewicht in Gramm, dann Anzahl Werte (10000 = 5 s bei 2000 SPS).</li>
    <li>Ergebnis im Protokoll prüfen: Empfindlichkeit 1,0–2,0 mV/V gilt als plausibel, die Unsicherheit des Faktors sollte unter 0,05 % liegen.</li>
    <li>Mit <code>s</code> speichern.</li>
  </ol>
  <p>Kleine Gewichte funktionieren, aber Fehler wachsen bis 50 kg mit: Bei 100 g wird jeder Fehler etwa 500-fach vergrößert.
    Dein letzter Faktor von 10,06 Counts/g entspricht 0,94 mV/V, knapp unter der Angabe der Zelle. Mit einem großen,
    genau bekannten Gewicht nachkalibrieren lohnt sich.</p>
  <p>Kennst du die Änderung in Counts schon, geht es auch direkt: <code>f &lt;Counts / Gramm&gt;</code>, z. B. <code>f 10.06</code>.</p>
</div>

<div class="karte" id="antrieb">
  <div class="titel">Antrieb</div>
  <h3>Closed-Loop-Schrittmotor mit HB808C</h3>
  <p>Der Pico gibt Schritt- und Richtungsimpulse aus; Encoder und Regelkreis stecken im Treiber. Während jeder
    Abwärtsfahrt prüft der Pico <strong>jeden einzelnen Messwert</strong> der Wägezelle und hält den Motor innerhalb
    eines Schritts an, sobald Kontakt oder die Maximalkraft erreicht ist. Aufwärts darf der Motor immer fahren.</p>
  <div class="tabelle"><table>
    <tr><th>Pico</th><th>Treiber / Taster</th><th>Hinweis</th></tr>
    <tr><td>GP2</td><td>PUL</td><td rowspan="3">Der HB808C braucht 4,5–28 V an den Eingängen. Über einen 5-V-Pegelwandler (z. B. 74AHCT125)
      oder gemeinsame Anode an 5 V mit Transistor gegen GND anschließen; dann <code>mp inv 1</code>.</td></tr>
    <tr><td>GP3</td><td>DIR</td></tr>
    <tr><td>GP4</td><td>ENA (optional)</td></tr>
    <tr><td>GP5</td><td>ALM (optional)</td><td>Alarmausgang des Treibers, z. B. bei Schleppfehler. Mit <code>mp alm 1</code> bzw. <code>2</code> aktivieren.</td></tr>
    <tr><td>GP6</td><td>START-Taster gegen GND</td><td>Startet eine Pressung (wenn alles bereit ist).</td></tr>
    <tr><td>GP7</td><td>STOPP-Taster gegen GND</td><td>Hält sofort an. Ersetzt keinen Not-Aus.</td></tr>
  </table></div>

  <h3>Einrichten</h3>
  <ol>
    <li><code>mp spmm &lt;Wert&gt;</code>: Schritte pro mm = Mikroschritte pro Umdrehung × Übersetzung / Spindelsteigung.
      Beispiel: 1600 Schritte, direkt, 5 mm Steigung → <code>mp spmm 320</code>. Vorher bewegt sich der Motor nicht.</li>
    <li>Waage kalibrieren und Datenrate auf 2000 SPS. Ohne kalibrierten Faktor und unter 500 SPS fährt der Antrieb nicht.</li>
    <li>Richtung prüfen: <code>mot ab 1</code> muss nach unten fahren (Rechtslauf). Sonst <code>mp dir 0</code> bzw. <code>mp dir 1</code>.</li>
    <li>Weg prüfen: <code>mot ab 10</code> mit dem Messschieber nachmessen, bei Abweichung <code>spmm</code> anpassen.</li>
    <li>Stopp testen: langsam abwärts fahren und von Hand auf die Zelle drücken. Der Motor muss sofort stehen.</li>
    <li>Mit <code>s</code> speichern.</li>
  </ol>

  <h3>Endpunkt kalibrieren</h3>
  <p>Startet mit dem Knopf auf der Mess- oder Pressseite oder <code>mot kal</code>. Das Werkzeug liegt auf der Zelle,
    ohne Werkstück. Nach jedem Einschalten nötig, weil die Position des Motors dann unbekannt ist.</p>
  <ol>
    <li>Langsam abwärts (<code>vsuch</code>, Standard 1 mm/s), höchstens über den Hub von 45 mm.</li>
    <li>Steigt die Last um mehr als <code>kontakt</code> (Standard 100 g) gegenüber dem Wert vor der Fahrt, ist der
      <strong>Werkzeug-Endpunkt</strong> erreicht. Er wird der neue Nullpunkt.</li>
    <li>Weiter bis zum <strong>tatsächlichen Endpunkt</strong>: Kontakt + <code>ueber</code> Schritte (Standard 10), oder früher
      bei <code>fmax</code> (Standard und Obergrenze 50 kg).</li>
    <li>Zurück zur Startposition. Im Protokoll stehen Kontakttiefe, Endpunkt, Kraft am Endpunkt und die Steifigkeit in kg/mm.</li>
  </ol>
  <p>Ohne Lastanstieg im ganzen Hub bricht die Fahrt mit „kein Kontakt“ ab. Bei jedem Abbruch bleibt der Motor an der
    Abbruchstelle stehen; mit <code>mot auf &lt;mm&gt;</code> wieder hochfahren.</p>
  <div class="hinweis"><strong>Kraftreserve:</strong> Ein Nema-23-Motor an einer Spindel kann ein Vielfaches der 50 kg Nennlast drücken.
    Die Kraftgrenze im Pico ist schnell, aber Software. Einen Not-Aus, der die Motorversorgung trennt, und einen mechanischen
    Anschlag vorsehen. Die ersten Fahrten mit kleiner Grenze (<code>mp fmax 2000</code>) machen.</div>

  <h3>Parameter</h3>
  <div class="tabelle"><table>
    <tr><th>Name</th><th>Bedeutung</th><th>Standard</th></tr>
    <tr><td><code>spmm</code></td><td>Schritte pro mm, 0 = gesperrt</td><td>0</td></tr>
    <tr><td><code>dir</code></td><td>DIR-Pegel für „ab“</td><td>1</td></tr>
    <tr><td><code>inv</code></td><td>Ausgänge invertiert (gemeinsame Anode mit Transistor)</td><td>0</td></tr>
    <tr><td><code>ena</code></td><td>0 unbenutzt, 1 Opto an = Motor aus, 2 Opto an = Motor ein</td><td>0</td></tr>
    <tr><td><code>alm</code></td><td>0 aus, 1 Alarm bei LOW, 2 Alarm bei HIGH</td><td>0</td></tr>
    <tr><td><code>vsuch</code></td><td>Geschwindigkeit Kontaktsuche und Überlauf (mm/s)</td><td>1</td></tr>
    <tr><td><code>vfahrt</code></td><td>Geschwindigkeit Tippbetrieb und Rückfahrt (mm/s)</td><td>5</td></tr>
    <tr><td><code>acc</code></td><td>Beschleunigung (mm/s²)</td><td>20</td></tr>
    <tr><td><code>hub</code></td><td>maximaler Suchweg (mm, höchstens 45)</td><td>45</td></tr>
    <tr><td><code>ueber</code></td><td>Schritte nach dem Kontakt</td><td>10</td></tr>
    <tr><td><code>kontakt</code></td><td>Laständerung, die als Kontakt gilt (g)</td><td>100</td></tr>
    <tr><td><code>fmax</code></td><td>Maximalkraft, Abwärtsfahrt stoppt sofort (g, höchstens 50 000)</td><td>50 000</td></tr>
  </table></div>
  <p>Die Schrittfrequenz liegt zwischen 16 Hz und 20 kHz. Ein Mikroschritt von 1600–3200 pro Umdrehung genügt beim Closed-Loop-Treiber.</p>
</div>

<div class="karte" id="presse">
  <div class="titel">Pressen</div>
  <h3>Ablauf auf der Pressseite (<a href="/presse">/presse</a>)</h3>
  <ol>
    <li><strong>Werkzeug auflegen und tarieren.</strong> Das Gewicht des Werkzeugs wird abgezogen.</li>
    <li><strong>Hub kalibrieren</strong> (ohne Werkstück): Der Motor fährt auf das Werkzeug, merkt sich den
      Werkzeug-Endpunkt (Weg 0) und den tatsächlichen Endpunkt und fährt zurück.</li>
    <li><strong>Sitzung wählen oder anlegen.</strong> Jede Pressung wird sofort in der aktiven Sitzung gespeichert.</li>
    <li><strong>Werkstück einlegen und START</strong> (Knopf oder Taster): Eilgang bis zum Anfahrpunkt, dann mit
      Pressgeschwindigkeit bis zum kalibrierten Endpunkt (oder bis zur Maximalkraft), Haltezeit, zurück zum Start.</li>
  </ol>
  <h3>Diagramm und Kennwerte</h3>
  <p>Jede Pressung erscheint als Kurve <strong>Kraft über Weg</strong>; alle Pressungen der Sitzung liegen übereinander.
    Der Weg zählt ab dem Werkzeug-Endpunkt: Das Werkstück berührt also bei einem negativen Weg. Ein Klick auf eine Kurve
    oder eine Tabellenzeile zeigt ihre Kennwerte und die Abweichung zum Mittel der Sitzung.</p>
  <dl>
    <div class="kennzahl"><dt>Spitzenkraft</dt><dd><p>Größte Kraft der Pressung und der Weg, bei dem sie auftrat.</p></dd></div>
    <div class="kennzahl"><dt>Endkraft</dt><dd><p>Mittel der letzten ca. 10 ms am Endpunkt; mit Haltezeit zusätzlich die Kraft am Ende der Haltezeit.</p></dd></div>
    <div class="kennzahl"><dt>Kontakt, Presstiefe</dt><dd><p>Kontakt: erster Messwert über der Kontaktschwelle (<code>mp kontakt</code>).
      Presstiefe: Weg vom Kontakt bis zum Endpunkt.</p></dd></div>
    <div class="kennzahl"><dt>σ, Spannweite</dt><dd><p>Standardabweichung und Max − Min aller Kraftwerte vom Kontakt bis zum Endpunkt.
      Die Sitzungsstatistik zeigt zusätzlich Mittel, σ und Spannweite jedes Kennwerts über alle gültigen Pressungen –
      das Maß für die Gleichmäßigkeit.</p></dd></div>
    <div class="kennzahl"><dt>Pressarbeit</dt><dd><p>Fläche unter der Kurve ab Kontakt (Kraft × Weg) in Joule.</p></dd></div>
  </dl>
  <h3>Auswertung (Karte „Auswertung“)</h3>
  <p>Berechnet im Browser aus den gespeicherten Kurven; Fenster und Schwellen sind einstellbar und gelten für alle
    Pressungen. <strong>Fügetiefe</strong> = Weg vom Kontakt bis zum Endpunkt minus „Anschlag ausschließen“.</p>
  <dl>
    <div class="kennzahl"><dt>Weg ab Kontakt</dt><dd><p>Kurven am Kontaktpunkt ausgerichtet: Unterschiede in Rohrlänge und
      Einlegehöhe verschwinden, man vergleicht das Material.</p></dd></div>
    <div class="kennzahl"><dt>Fügekraft</dt><dd><p>Größte Kraft bis vor den Anschlag – die Kraft der Verbindung, nicht des Werkzeugs.</p></dd></div>
    <div class="kennzahl"><dt>Einführspitze</dt><dd><p>Erstes lokales Maximum in den ersten 40 % der Fügetiefe (Fase, Aufweiten
      beginnt). Hinweis auf Fase, Grat und Durchmesser.</p></dd></div>
    <div class="kennzahl"><dt>Plateau-Gerade</dt><dd><p>Lineare Regression im Plateaufenster. <strong>Steigung</strong> (Kraft je mm)
      ≈ Reibwert × Flächenpressung × Umfang; <strong>Achsenabschnitt</strong> = Grundkraft des Aufweitens;
      <strong>Laufruhe</strong> = Streuung um die Gerade (Ruckgleiten, beginnendes Kaltverschweißen).
      Dazu die Arbeit im Plateaufenster.</p></dd></div>
    <div class="kennzahl"><dt>Kraftabfall, Rissverdacht</dt><dd><p>Größter Kraftrückgang innerhalb der eingestellten Strecke im
      Fügebereich. Ab der Schwelle (Standard 5 % in 0,05 mm) wird die Pressung als Rissverdacht markiert – typisch für ein
      aufplatzendes dünnwandiges Rohr.</p></dd></div>
    <div class="kennzahl"><dt>Wegfenster</dt><dd><p>Kraft an festen Wegen ab Kontakt (Standard 1, 2, 3, 4 mm), Grundlage für spätere
      OK/NOK-Fenster.</p></dd></div>
    <div class="kennzahl"><dt>Trend</dt><dd><p>Ein Kennwert über die Pressnummer mit Mittel und ±3σ. Ein langsamer Anstieg der
      Plateaukraft oder der Laufruhe deutet auf Verschleiß oder nachlassende Schmierung.</p></dd></div>
  </dl>
  <p>„Auswertung als CSV“ exportiert alle Kennwerte und die Statistik in der gewählten Krafteinheit.</p>
  <p>Die Krafteinheit (N, kg oder lbs) ist über dem Diagramm wählbar; sie gilt auch für den CSV-Export.
    Gespeichert wird immer in N.</p>
  <h3>Einstellungen</h3>
  <p><code>mp anfahrt &lt;mm&gt;</code>: Eilgang bis so viele mm über dem Werkzeug-Endpunkt; etwas höher als die
    Oberkante des Werkstücks wählen (Standard 45 = kein Eilgang). <code>mp vpress &lt;mm/s&gt;</code>: Pressgeschwindigkeit.
    <code>mp halt &lt;ms&gt;</code>: Haltezeit am Endpunkt. Danach <code>s</code>.</p>
  <h3>Simulation ohne Motor</h3>
  <p><code>sim an</code> oder der Schalter auf der Pressseite: Die Kraft wird aus der Position berechnet – virtuelles
    Werkzeug 15 mm unter der aktuellen Position, je Pressung ein Werkstück mit zufälliger Spitzenkraft zwischen
    25 und 35 kg. ADC und Zelle müssen angeschlossen sein, <code>mp spmm</code> gesetzt. Danach Hub kalibrieren und
    pressen. Simulierte Pressungen sind markiert; nach einem Neustart ist die Simulation aus.</p>
  <h3>Sitzungen</h3>
  <p>Im Flash des Pico ist Platz für etwa 200 Pressungen. Sitzungen lassen sich als CSV (für Excel: Semikolon,
    Dezimalkomma) oder JSON herunterladen. Danach alte Sitzungen löschen.</p>
  <p class="hinweis">Die Kraftgrenze stoppt den Motor schnell, ersetzt aber keine Schutzeinrichtung (Not-Aus, Schutzhaube, Zweihandbedienung).</p>
</div>

<div class="karte" id="befehle">
  <div class="titel">Befehle</div>
  <p>Eingabe im Feld unter dem Protokoll oder im seriellen Monitor (Zeilenende „Neue Zeile“). Dezimalkomma geht auch.</p>
  <div class="tabelle"><table>
    <tr><th>Befehl</th><th>Funktion</th></tr>
    <tr><td><code>t [n]</code></td><td>Tara aus n Werten (Standard etwa 2 s)</td></tr>
    <tr><td><code>k &lt;g&gt; [n]</code></td><td>Kalibrieren mit bekanntem Gewicht in Gramm</td></tr>
    <tr><td><code>f &lt;c/g&gt;</code></td><td>Faktor direkt setzen (Counts pro Gramm)</td></tr>
    <tr><td><code>r &lt;sps&gt;</code></td><td>Datenrate: 2.5 5 10 15 25 30 50 60 100 500 1000 2000 3750 7500 15000 30000</td></tr>
    <tr><td><code>fl &lt;s&gt;</code></td><td>Glättung in Sekunden (0–60, <code>fl 0</code> bzw. <code>fl aus</code> = keine)</td></tr>
    <tr><td><code>ws &lt;s&gt;</code></td><td>Dauer der Live-Statistik (0,5–60 s)</td></tr>
    <tr><td><code>n [n]</code></td><td>Rauschanalyse über n Werte (Standard etwa 5 s)</td></tr>
    <tr><td><code>nv [s] [sps …]</code></td><td>Rauschvergleich mehrerer Datenraten, je s Sekunden</td></tr>
    <tr><td><code>s</code></td><td>Einstellungen dauerhaft speichern</td></tr>
    <tr><td><code>i</code></td><td>Status anzeigen</td></tr>
    <tr><td><code>m &lt;0..4&gt;</code></td><td>USB-Ausgabe: 0 aus, 1 lesbar, 2 CSV, 3 Plotter, 4 binär</td></tr>
    <tr><td><code>mot</code></td><td>Antrieb: Status, Position, letzte Kalibrierung</td></tr>
    <tr><td><code>mot kal</code></td><td>Hub kalibrieren (Endpunkt-Kalibrierung, nach jedem Einschalten)</td></tr>
    <tr><td><code>mot ab|auf &lt;mm&gt;</code></td><td>Tippbetrieb; <code>mot s &lt;n&gt;</code> = n Schritte (+ ab)</td></tr>
    <tr><td><code>mot null|ein|aus</code></td><td>Position 0 setzen, Motorstrom ein bzw. aus</td></tr>
    <tr><td><code>mot stop</code></td><td>Anhalten (auch <code>x</code>, Esc im Browser, STOPP-Taster)</td></tr>
    <tr><td><code>mp [name wert]</code></td><td>Antrieb- und Press-Parameter anzeigen bzw. setzen (u. a. <code>anfahrt</code>, <code>vpress</code>, <code>halt</code>)</td></tr>
    <tr><td><code>pr</code> / <code>pr x</code></td><td>Pressung starten (wie START-Taster) / abbrechen</td></tr>
    <tr><td><code>sim [an|aus]</code></td><td>Simulation ohne Motor (Kraft 25–35 kg berechnet)</td></tr>
    <tr><td><code>sit</code></td><td>Sitzungen anzeigen; <code>sit neu &lt;Name&gt;</code>, <code>sit wahl &lt;Nr&gt;</code>, <code>sit zu</code>, <code>sit loeschen &lt;Nr&gt; ja</code></td></tr>
    <tr><td><code>wlan [an|aus]</code></td><td>WLAN-Status bzw. ein-/ausschalten (wieder an nur über USB)</td></tr>
    <tr><td><code>wd</code></td><td>Watchdog-Status und Grund des letzten Neustarts (<code>wd test 0|1</code> hängt einen Kern zum Test auf und löst einen Neustart aus)</td></tr>
    <tr><td><code>c</code></td><td>Selbstkalibrierung des ADC (Offset und Verstärkung)</td></tr>
    <tr><td><code>d</code></td><td>Register des ADS1256 anzeigen</td></tr>
    <tr><td><code>init</code></td><td>ADS1256 neu starten</td></tr>
    <tr><td><code>x</code></td><td>Antrieb anhalten bzw. laufenden Auftrag abbrechen</td></tr>
    <tr><td><code>w ja</code></td><td>Werkseinstellungen (löscht Tara und Kalibrierung)</td></tr>
    <tr><td><code>h</code></td><td>Hilfe im Protokoll</td></tr>
  </table></div>
</div>

<p class="fuss">Diese Seite liegt im Flash des Pico und funktioniert ohne Internet. <a href="/">Zurück zur Messung</a></p>
</main></body></html>
)WEBINFO";
