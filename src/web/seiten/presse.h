/*
 * presse.h - Pressseite der Weboberfläche (/presse, ohne externe Dateien)
 *
 * Ablauf: Werkzeug tarieren -> Hub kalibrieren -> Sitzung wählen -> Werkstück einlegen -> START.
 * Jede Pressung erscheint als Kurve Kraft über Weg; alle Pressungen der Sitzung liegen
 * übereinander. Klick auf eine Kurve (oder eine Tabellenzeile) zeigt ihre Kennwerte.
 *
 * Auswertung (im Browser, aus den gespeicherten Kurvenpunkten, Einstellungen im localStorage):
 *   Fügekraft ohne Anschlag, Einführspitze, Plateau-Gerade (Steigung, Achsenabschnitt,
 *   Laufruhe = Streuung um die Gerade, Arbeit im Fenster), größter Kraftabfall (Rissverdacht),
 *   Kraft in Wegfenstern ab Kontakt, Trend über die Pressungen, Export als CSV.
 *
 * Daten: GET /presse/status (4x pro Sekunde), Kurven binär über /sitzung/index und
 * /sitzung/presse. Das Binärformat (PressKopf, 128 Byte) steht in src/sitzungen.h.
 *
 * Rohdaten: Während einer Pressung holt die Seite alle 100 ms jede neue Probe über
 * GET /presse/roh (Format in src/web/presse_web.cpp) und legt sie nach dem Speichern
 * in der IndexedDB ab ("roh": alle Proben, "kurve": Mittel je Motorschritt). Ist eine
 * passende Rohkurve vorhanden (gleiche Probenzahl), wird sie statt der Pico-Kurve
 * (<= 960 Abschnitte) für Auswahl, Auswertung und Rohdaten-CSV benutzt.
 *
 * Sprache: deutsche Texte stehen hier, /sprache.js (sprache.h) übersetzt sie ins Englische
 * (statischer Text beim Start, Texte aus dem Skript über T(), Firmware-Texte über TF()).
 */
#pragma once

static const char PRESSE_HTML[] = R"PRESSE(<!doctype html>
<html lang="de"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Pressen</title>
<link rel="stylesheet" href="/stil.css">
<script>try{document.documentElement.dataset.thema=localStorage.getItem("wz_thema")||"";
document.documentElement.dataset.modus=JSON.parse(localStorage.getItem("wz_modus")||'"bedienen"');}catch(e){document.documentElement.dataset.modus="bedienen";}</script>
<script src="/sprache.js"></script>
<style>
main{max-width:none;margin:0;padding:14px 16px 28px;min-width:0}
/* Rahmen: Menü links, Inhalt rechts */
.rahmen{display:grid;grid-template-columns:232px minmax(0,1fr);min-height:100vh}
body.eng .rahmen{grid-template-columns:64px minmax(0,1fr)}
.menue{position:sticky;top:0;height:100vh;overflow:auto;display:flex;flex-direction:column;gap:16px;padding:16px 12px;
border-right:1px solid var(--rand);background:var(--menue);backdrop-filter:blur(8px)}
.menue .kopf{display:flex;align-items:center;gap:10px}
.menue .bild{flex:none;display:block;filter:drop-shadow(0 0 10px var(--glow))}
.menue h1{font-size:13px;line-height:1.25;color:var(--leise);font-weight:600}
.menue h1 em{display:block;font-size:16px;font-weight:750}
.menue h1 span{display:block;font-size:12px}
.menue nav{display:flex;flex-direction:column;gap:2px}
.menue nav a{display:flex;align-items:center;gap:9px;padding:6px 10px;border-radius:10px;color:var(--leise);font-weight:600;font-size:13px}
.menue nav a:hover{background:var(--bg2);color:var(--text);text-decoration:none}
.menue nav a.aktiv{color:var(--akzent);background:rgba(var(--ar),.08)}
.menue nav a::before{content:"";width:5px;height:5px;border-radius:50%;background:var(--rand2);flex:none}
.menue nav a.aktiv::before{background:var(--akzent);box-shadow:0 0 6px var(--akzent)}
.grp{font-size:10.5px;font-weight:650;letter-spacing:.9px;text-transform:uppercase;color:var(--leise);opacity:.75;padding:0 10px;margin-bottom:5px}
.menue .status{flex-direction:column;align-items:flex-start;padding:0 6px}
.fuss{margin-top:auto;display:flex;align-items:center;justify-content:space-between;gap:8px;padding:0 4px}
#einklappen{padding:3px 12px;font-size:16px;line-height:1.2}
body.eng .menue{padding:16px 8px;align-items:center}
body.eng .menue .text,body.eng .menue .block{display:none}
body.eng .fuss{justify-content:center}
@media (max-width:900px){
  .rahmen,body.eng .rahmen{grid-template-columns:minmax(0,1fr)}
  .menue,body.eng .menue{position:static;height:auto;flex-direction:row;flex-wrap:wrap;align-items:center;gap:10px 18px;
    border-right:0;border-bottom:1px solid var(--rand);padding:10px 16px}
  body.eng .menue .text{display:block}
  body.eng .menue .block{display:flex}
  .menue nav{flex-direction:row;flex-wrap:wrap}
  .menue .status{flex-direction:row;flex-wrap:wrap}
  .menue .grp,#einklappen{display:none}
  .fuss{margin-top:0}
  .menue > *{min-width:0}
}
/* Bühne: Steuerung + Diagramm auf dem ersten Bildschirm */
.buehne{display:grid;grid-template-columns:minmax(290px,350px) minmax(0,1fr);gap:14px;height:calc(100vh - 28px);min-height:540px}
.buehne > .karte{margin-top:0;display:flex;flex-direction:column;min-height:0}
.steuer{overflow:hidden auto}   /* waagrecht nie scrollen (der Leuchtkreis ragt über den Rand) */
body.faehrt .buehne{height:calc(100vh - 110px)}   /* Platz für die STOPP-Leiste */
.plotfl{position:relative;flex:1 1 0;min-height:0;margin-top:8px}
#plot{position:absolute;inset:0;width:100%;height:100%;cursor:crosshair}
.legende{font-size:11.5px;margin-top:6px}
.kurz{position:absolute;top:10px;right:22px;min-width:210px;padding:8px 12px;border:1px solid var(--rand2);border-radius:12px;
background:var(--menue);backdrop-filter:blur(6px);font-size:12.5px;font-variant-numeric:tabular-nums;cursor:pointer;user-select:none}
.kurz .kk{display:flex;justify-content:space-between;gap:10px;font-weight:700;color:var(--akzent)}
.kurz .kk span{font-weight:500;color:var(--leise)}
.kurz dl{display:grid;grid-template-columns:auto auto;gap:1px 14px;margin:5px 0 0}
.kurz dt{color:var(--leise)}
.kurz dd{margin:0;text-align:right}
.kurz.zu dl{display:none}
@media (max-width:900px){.buehne{grid-template-columns:1fr;height:auto;min-height:0}.plotfl{flex:none;height:62vh;min-height:300px}}
.live{display:flex;justify-content:space-between;align-items:flex-end;gap:12px;padding:10px 12px;margin-bottom:6px;
border:1px solid var(--rand2);border-radius:14px;background:var(--bg2)}
.live .gross{font-size:32px;font-weight:800;font-variant-numeric:tabular-nums;letter-spacing:-.5px;line-height:1.05}
.live .gross i{font-size:15px;font-style:italic;color:var(--akzent);margin-left:6px}
.live .neben{text-align:right;font-size:13px;color:var(--leise);font-variant-numeric:tabular-nums}
.live .neben b{color:var(--text)}
.abschnitt{display:flex;align-items:center;gap:12px;margin:26px 0 0;color:var(--leise);font-size:11px;font-weight:650;
letter-spacing:.9px;text-transform:uppercase}
.abschnitt::after{content:"";flex:1;height:1px;background:var(--rand)}
.pgrid{display:grid;grid-template-columns:minmax(320px,440px) minmax(0,1fr);gap:0 14px}
.pgrid2{display:grid;grid-template-columns:1fr 1fr;gap:0 14px}
.pgrid3{display:grid;grid-template-columns:minmax(0,1fr) minmax(300px,420px);gap:0 14px}
@media (max-width:900px){.pgrid,.pgrid2,.pgrid3{grid-template-columns:1fr}}
button.start{font-size:20px;padding:12px 40px;letter-spacing:1px}
#trend{width:100%;height:240px;display:block;cursor:pointer}
.details{display:grid;grid-template-columns:auto 1fr;gap:3px 14px;font-variant-numeric:tabular-nums}
.details dt{color:var(--leise)}
.details dd{margin:0;text-align:right}
.details dd i{color:var(--leise);font-style:normal;font-size:12px;margin-left:6px}
.details .gruppe{grid-column:1/-1;color:var(--akzent);font-size:11px;font-weight:650;letter-spacing:.8px;
text-transform:uppercase;margin-top:6px;border-bottom:1px solid var(--rand)}
.scroll{max-height:360px;overflow:auto}
#bPressungen .scroll{max-height:620px}
tr.sel td{background:rgba(var(--ar),.12)}
tbody tr{cursor:pointer}
tbody tr:hover td{background:rgba(var(--ar),.06)}
.balken{height:8px;border-radius:6px;background:var(--bg2);border:1px solid var(--rand2);overflow:hidden;margin-top:6px}
.balken > div{height:100%;background:var(--akzent)}
.form{display:flex;flex-wrap:wrap;gap:8px;align-items:center;margin-top:8px}
.form input[type=text]{flex:1 1 160px;min-width:0}
td .chip,td button{padding:2px 9px;font-size:12px}
#log{height:200px}
#awFenster{width:130px}
.simkopf{display:flex;gap:8px;align-items:center;flex-wrap:wrap;margin-top:4px}
:root[data-modus="bedienen"] .pgrid3{grid-template-columns:minmax(0,1fr)}
.sitzkurz{display:flex;flex-direction:column;gap:4px;margin-top:8px}
.sitzkurz button{text-align:left;border-radius:10px;display:flex;justify-content:space-between;gap:12px}
.sitzkurz button span{color:var(--leise);font-weight:500}
.fuss .modus{flex:none}
</style></head><body>
<div class="banner offline" id="offlineBanner" role="alert" hidden>Keine Verbindung zum Pico – Befehle sind gesperrt, die Seite verbindet sich von selbst neu.</div>
<div class="banner sim" id="simBanner" hidden>SIMULATION aktiv – die Kraft wird berechnet, nicht gemessen
  <button class="sendet" id="simAus">Simulation beenden</button></div>
<div class="rahmen">
<aside class="menue">
  <div class="kopf">
    <svg class="bild" width="40" height="40" viewBox="0 0 40 40" aria-hidden="true">
      <defs><linearGradient id="lg" x1="0" y1="0" x2="1" y2="1"><stop offset="0" style="stop-color:var(--akzent)"/><stop offset="1" style="stop-color:var(--akzent2)"/></linearGradient></defs>
      <rect width="40" height="40" rx="11" fill="url(#lg)"/>
      <g style="fill:var(--akzentText)">
        <rect x="17.5" y="5" width="5" height="9" rx="1"/>
        <path d="M10 14h20v4.5a1.5 1.5 0 0 1-1.5 1.5h-17a1.5 1.5 0 0 1-1.5-1.5z"/>
        <rect x="14" y="25" width="12" height="6" rx="1.5"/>
        <rect x="7" y="32" width="26" height="3" rx="1.5"/>
      </g>
      <path d="M15 22.5l5 0M25 22.5l-5 0" style="stroke:var(--akzentText)" stroke-width="1.6" stroke-linecap="round" opacity=".55"/>
    </svg>
    <h1 class="text">EcklC<em>OpenSmartPress</em><span>Pressen</span></h1>
  </div>
  <nav class="block">
    <div class="grp">Seiten</div>
    <a class="aktiv" href="#oben">Pressen</a>
    <a href="/">Messseite</a>
    <a href="https://github.com/EggCe/OpenSmartPress/blob/main/help.md#pressen" data-href-en="https://github.com/EggCe/OpenSmartPress/blob/main/help.en.md#pressing" target="_blank" rel="noopener">Hilfe ↗</a>
  </nav>
  <div class="block">
    <div class="grp">Status</div>
    <div class="status" id="status">verbinde …</div>
  </div>
  <nav class="block">
    <div class="grp">Auswertung</div>
    <a href="#bPressung">Pressung &amp; Liste</a>
    <a href="#bTrend">Trend &amp; Auswertung</a>
    <a href="#bStatistik">Sitzungsstatistik</a>
    <a href="#sitzungen">Sitzungen</a>
    <a class="einr" href="#bParameter">Parameter &amp; Protokoll</a>
  </nav>
  <div class="block"><div class="grp">Modus</div><div class="modus" title="Bedienen: nur was für die Produktion nötig ist · Einrichten: alle Einstellungen, Simulation, Protokoll"><button data-m="bedienen">Bedienen</button><button data-m="einrichten">Einrichten</button></div></div>
  <div class="block"><div class="grp">Sprache</div><div class="sprachwahl" title="Sprache / Language"><button data-l="de">DE</button><button data-l="en">EN</button></div></div>
  <div class="fuss"><div class="themen block" title="Farbschema"><button data-t="" title="Grün / Schwarz"></button><button data-t="rot" title="Rot / Schwarz"></button><button data-t="hell" title="Blau / Hell"></button></div><button id="einklappen" title="Menü ein-/ausklappen">«</button></div>
</aside>
<main id="oben">

<section class="buehne">
  <div class="karte glow steuer">
    <div class="titel">Ablauf</div>
    <div class="live">
      <div><div class="sub" style="margin:0">Kraft</div><div class="gross" id="lvKraft">–</div></div>
      <div class="neben"><div id="lvZust">–</div><div>Pos. <b id="lvPos">–</b> mm</div></div>
    </div>
    <div class="simkopf einr">
      <label title="Ohne Motor: die Kraft wird aus der Position berechnet">
        <input type="checkbox" id="sim"> Simulation ohne Motor</label>
      <button id="simOeffnen" title="Geschwindigkeit, Weg, Kraft, Werkzeugabstand der Simulation">Parameter …</button>
    </div>
    <div class="schritt" id="s1"><div class="nr">1</div><div>
      <b>Werkzeug auflegen und tarieren</b>
      <div class="sub" id="s1t">–</div>
      <div class="knoepfe"><button data-c="t" title="Mittelt etwa 2 s und setzt den Wert als Nullpunkt – Werkzeug liegt auf, kein Werkstück">Tara</button></div></div></div>
    <div class="schritt" id="s2"><div class="nr">2</div><div>
      <b>Hub kalibrieren</b>
      <div class="sub" id="s2t">–</div>
      <div class="knoepfe"><button data-c="mot kal" title="Fährt langsam auf das Werkzeug, merkt sich den Werkzeug-Endpunkt (Weg 0) und fährt zurück">Hub kalibrieren</button></div></div></div>
    <div class="schritt" id="s3"><div class="nr">3</div><div>
      <b>Sitzung wählen</b>
      <div class="sub" id="s3t">–</div>
      <div class="knoepfe"><button id="zuSitzungen">Sitzung wählen …</button></div></div></div>
    <div class="schritt" id="s4"><div class="nr">4</div><div>
      <b>Werkstück einlegen und starten</b>
      <div class="sub" id="s4t">–</div>
      <div class="knoepfe"><button class="primaer start sendet" id="start" disabled>START</button>
        <button class="stopp" data-c="x" title="Taste Esc">STOPP</button></div></div></div>
  </div>

  <div class="karte glow">
    <div class="plotkopf"><div class="titel">Kraft über Weg</div>
      <div class="sub" id="maus">Klick: Pressung auswählen · Mausrad: zoomen (Umschalt: nur Weg) · Ziehen: verschieben · Doppelklick: alles</div></div>
    <div class="leiste">
      <label>Weg <select id="xmodus"><option value="kontakt">ab Kontakt</option><option value="ende">ab Werkzeug-Endpunkt</option></select></label>
      <label>Krafteinheit <select id="einheit"><option value="N">N</option><option value="kg">kg</option>
        <option value="lbs">lbs</option></select></label>
      <label>Anzeigen <select id="zeige"><option value="0">alle</option><option value="10">letzte 10</option>
        <option value="25">letzte 25</option><option value="50">letzte 50</option></select></label>
      <label><input type="checkbox" id="abbr"> Abbrüche zeigen</label>
      <label><input type="checkbox" id="nurKontakt" checked> ab Kontakt zoomen</label>
    </div>
    <div class="plotfl" id="plotfl"><canvas id="plot"></canvas>
      <div class="kurz" id="kurz" hidden title="Klick: ein-/ausklappen"></div></div>
    <div class="sub legende"><span class="fl" style="background:var(--akzent)"></span>Pressungen ·
      <span class="fl" style="background:var(--hell)"></span>Auswahl mit Plateau-Gerade (Fenster schattiert), ● Fügekraft, ▲ Einführspitze,
      ✕ größter Kraftabfall · <span class="fl" style="background:rgb(var(--wr))"></span>Rissverdacht / laufende Pressung ·
      <span class="fl" style="background:rgb(var(--fr))"></span>Abbruch</div>
  </div>
</section>

<div class="abschnitt">Auswertung</div>

<div class="pgrid" id="bPressung">
  <div class="karte">
    <div class="plotkopf"><div class="titel" id="detTitel">Pressung</div><div class="sub" id="detSub"></div></div>
    <dl class="details" id="details"><dt>Noch keine Pressung ausgewählt.</dt><dd></dd></dl>
  </div>
  <div class="karte" id="bPressungen"><div class="titel">Pressungen</div><div class="scroll"><table id="tab"></table></div></div>
</div>

<div class="pgrid3" id="bTrend">
  <div class="karte">
    <div class="plotkopf"><div class="titel">Trend über die Pressungen</div>
      <label>Kennwert <select id="trendWert"></select></label></div>
    <div class="sub" id="trendSub">Mittelwert (Linie) und ±3σ (gestrichelt) der gültigen Pressungen · Klick: Pressung auswählen</div>
    <canvas id="trend"></canvas>
  </div>
  <div class="karte einr">
    <div class="titel">Auswertung</div>
    <div class="awgitter">
      <label>Plateau von [% der Fügetiefe]</label><input type="number" id="awVon" min="0" max="95" step="5">
      <label>Plateau bis [% der Fügetiefe]</label><input type="number" id="awBis" min="5" max="100" step="5">
      <label>Anschlag ausschließen [mm vor Endpunkt]</label><input type="number" id="awEnde" min="0" max="5" step="0.05">
      <label>Wegfenster [mm ab Kontakt, mit ; getrennt]</label><input type="text" id="awFenster">
      <label>Rissverdacht ab Kraftabfall [%]</label><input type="number" id="awAbfall" min="0.5" max="50" step="0.5">
      <label>… innerhalb von [mm]</label><input type="number" id="awStrecke" min="0.01" max="1" step="0.01">
    </div>
    <div class="knoepfe"><button id="awStandard">Standardwerte</button><button class="primaer" id="awCsv">Auswertung als CSV</button></div>
    <div class="sub">Fügetiefe = Kontakt bis Endpunkt minus Anschlag. Die Einstellungen gelten für alle Pressungen und werden in
      diesem Browser gespeichert.</div>
  </div>
</div>

<div class="karte" id="bStatistik"><div class="titel">Sitzungsstatistik</div><div class="sub" id="statSub"></div><div class="scroll" id="stat"></div></div>

<div class="abschnitt">Verwaltung</div>

<div class="karte" id="sitzungen">
  <div class="plotkopf"><div class="titel">Sitzungen</div><div class="sub" id="speicher"></div></div>
  <div class="balken"><div id="speicherBalken" style="width:0"></div></div>
  <div class="form">
    <input type="text" id="neuName" maxlength="47" placeholder="Name der neuen Sitzung, z. B. Charge 2026-11-14 / Werkzeug A">
    <input type="text" id="neuNotiz" maxlength="95" placeholder="Notiz (optional)">
    <button class="primaer sendet" id="neu">Neue Sitzung anlegen</button>
    <button class="sendet" id="schliessen">Aktive Sitzung schließen</button>
  </div>
  <div class="scroll"><table id="liste"></table></div>
</div>

<div class="pgrid2 einr" id="bParameter">
  <div class="karte">
    <div class="titel">Pressparameter</div>
    <div class="leiste">
      <label>Pressen ab [mm über Endpunkt, max. 10] <input type="number" id="pAnfahrt" min="0" max="10" step="0.1"></label>
      <label>Pressgeschw. [mm/s] <input type="number" id="pVpress" min="0.01" max="20" step="0.01"></label>
      <label>Haltezeit [ms] <input type="number" id="pHalt" min="0" max="60000" step="100"></label>
      <label title="Bei dieser Kraft hält der Motor sofort an (Pressung, Kalibrierung, Fahrt). Höchstens Nennlast."><span>Maximalkraft [<span class="fe">N</span>]</span> <input type="number" id="pFmax" min="0" step="any"></label>
      <label title="Laständerung, ab der beim Kalibrieren und Pressen ein Kontakt erkannt wird"><span>Kontakt ab [<span class="fe">N</span>]</span> <input type="number" id="pKontakt" min="0" step="any"></label>
    </div>
    <div class="knoepfe"><button class="sendet" id="pSetzen">Übernehmen</button><button data-c="s" title="Alle Einstellungen dauerhaft speichern (Befehl s)">Im Flash speichern</button></div>
    <div class="sub" id="pInfo">&nbsp;</div>
  </div>
  <div class="karte">
    <div class="titel">Protokoll</div>
    <pre id="log"></pre>
    <div style="display:flex;gap:6px;margin-top:8px">
      <input type="text" id="befehl" style="flex:1;min-width:0" placeholder="Befehl, z. B. mp · sit · pr">
      <button id="senden">Senden</button>
    </div>
  </div>
</div>
<dialog id="simDlg">
  <div class="titel">Simulation</div>
  <div class="sub">Kraftmodell ohne Motor: virtuelles Werkzeug, je Pressung ein Werkstück mit zufälliger Spitzenkraft
    zwischen „von“ und „bis“. Die Werte gelten bis zum Neustart des Pico; dieser Browser merkt sie sich und schickt sie
    beim Einschalten der Simulation wieder.</div>
  <div class="awgitter" style="margin-top:10px">
    <label>Pressgeschwindigkeit [mm/s]</label><input type="number" id="sVpress" min="0.01" max="20" step="0.05">
    <label>Werkzeugabstand (Hub bis zum Werkzeug) [mm]</label><input type="number" id="sWerkzeug" min="1" max="45" step="0.5">
    <label>Fügeweg (Werkstückhöhe) [mm]</label><input type="number" id="sWeg" min="0.1" max="10" step="0.1">
    <label>Spitzenkraft von [kg]</label><input type="number" id="sFmin" min="0.1" max="50" step="0.5">
    <label>Spitzenkraft bis [kg]</label><input type="number" id="sFmax" min="0.1" max="50" step="0.5">
    <label>Rauschen ± [g]</label><input type="number" id="sRauschen" min="0" max="2000" step="5">
    <label>Rissquote [% der Pressungen]</label><input type="number" id="sRiss" min="0" max="100" step="1">
    <label>Einführspitze [% der Spitzenkraft]</label><input type="number" id="sSpitze" min="0" max="50" step="1">
  </div>
  <div class="sub" id="simInfo" style="margin-top:8px"></div>
  <div class="knoepfe" style="margin-top:10px"><button class="primaer" id="simSetzen">Übernehmen</button>
    <button id="simStandard">Standardwerte</button><button id="simZu">Schließen</button></div>
  <div class="sub">Ein geänderter Werkzeugabstand setzt das virtuelle Werkzeug neu: danach Hub kalibrieren.
    Anfahrpunkt und Haltezeit stehen unter „Pressparameter“.</div>
</dialog>
<dialog id="sitzDlg">
  <div class="titel">Sitzung wählen</div>
  <div class="sub">Jede Pressung wird sofort in der aktiven Sitzung gespeichert.</div>
  <div class="form">
    <input type="text" id="dlgName" maxlength="47" placeholder="Name der neuen Sitzung">
    <input type="text" id="dlgNotiz" maxlength="95" placeholder="Notiz (optional)">
    <button class="primaer sendet" id="dlgNeu">Neu anlegen</button>
  </div>
  <div class="sub" style="margin-top:12px">… oder eine vorhandene öffnen:</div>
  <div class="sitzkurz" id="dlgListe"></div>
  <div class="knoepfe"><button id="dlgAlle">Alle Sitzungen verwalten</button><button id="dlgZu">Schließen</button></div>
</dialog>
<dialog id="umbDlg">
  <div class="titel">Sitzung umbenennen</div>
  <div class="awgitter" style="margin-top:6px">
    <label for="umbName">Name</label><input type="text" id="umbName" maxlength="47">
    <label for="umbNotiz">Notiz</label><input type="text" id="umbNotiz" maxlength="95">
  </div>
  <div class="knoepfe"><button class="primaer sendet" id="umbOk">Speichern</button><button id="umbZu">Abbrechen</button></div>
</dialog>
</main></div>
<div class="stoppleiste" id="stoppleiste" hidden><span id="stoppText">Antrieb fährt</span>
  <button class="stopp" data-c="x" title="Taste Esc">STOPP</button></div>
<div class="meldungen" id="meldungen" role="status" aria-live="polite"></div>
<script>
"use strict";
const $=id=>document.getElementById(id);
const STUFE=0.02;
const GRUND=["-","Endpunkt erreicht","Kontakt","Maximalkraft","Alarm vom Treiber","STOPP-Taster","Stopp-Befehl","keine Messwerte","kein Kontakt","Sensorfehler"];
const STATUS=["abgeschlossen","Abbruch","Kraft im Eilgang"];   // 0 = ohne Abbruch beendet (keine Gut/Schlecht-Aussage)
let S=null,online=false,ln=null,logRest="",offlineSeit=Date.now();
let sitId=-1,sitN=0,ladeNr=0,holt=false,sitName="";
let pressen=new Map();               // nr -> {k,x,y,a}  (a = Auswertung)
let auswahl=0,versteckt=new Set();
let live={id:-1,n:0,s:0,d:0,y:[],lauf:0};
let ansicht=null,geo=null,tgeo=null,neuZeichnen=true;
let roh={id:-1,n:0,luecke:false,teile:[],fertig:false,ende:false},rohHolt=false;

const fmt=(v,d)=>(v===null||v===undefined||!isFinite(v))?"–":v.toLocaleString(LOC(),{minimumFractionDigits:Math.max(0,d),maximumFractionDigits:Math.max(0,d)});
const zeitText=z=>z?new Date(z*1000).toLocaleString(LOC(),{dateStyle:"short",timeStyle:"medium"}):"–";
// CSV für Excel: Deutsch mit ; und Dezimalkomma, Englisch mit , und Dezimalpunkt
const csvSep=()=>SPR==="en"?",":";";
const csvZ=(v,d)=>isFinite(v)?(s=>SPR==="en"?s:s.replace(".",","))(Number(v).toFixed(Math.max(0,d))):"";
const csvZeile=a=>a.map(v=>{v=String(v);return v.includes(csvSep())||/["\r\n]/.test(v)?'"'+v.replace(/"/g,'""')+'"':v;}).join(csvSep());
const esc=s=>String(s).replace(/[&<>"]/g,c=>({"&":"&amp;","<":"&lt;",">":"&gt;","\"":"&quot;"}[c]));
const lsLesen=(k,s)=>{try{const v=localStorage.getItem(k);return v===null?s:JSON.parse(v);}catch(e){return s;}};
const lsSchreiben=(k,v)=>{try{localStorage.setItem(k,JSON.stringify(v));}catch(e){}};

// ---------- Krafteinheit: gespeichert und übertragen wird in N ----------
const EINHEITEN={N:{f:1,d:1,n:"N"},kg:{f:1/9.80665,d:2,n:"kg"},lbs:{f:1/4.4482216152605,d:1,n:"lbs"}};
let E=EINHEITEN[lsLesen("wz_einheit2","N")]||EINHEITEN.N;
$("einheit").value=E.n;
const kr=vN=>(vN===null||vN===undefined)?NaN:vN*E.f;                  // N -> Anzeigeeinheit
const fk=(vN,mehr)=>fmt(kr(vN),E.d+(mehr||0))+" "+E.n;                 // formatierte Kraft mit Einheit
$("xmodus").value=lsLesen("wz_xmodus","kontakt");

// ---------- Auswertungs-Einstellungen ----------
const AW_STANDARD={von:20,bis:80,ende:0.2,fenster:"1;2;3;4",abfall:5,strecke:0.05};
let AW=Object.assign({},AW_STANDARD,lsLesen("wz_auswertung",{}));
const AWFELD={awVon:"von",awBis:"bis",awEnde:"ende",awFenster:"fenster",awAbfall:"abfall",awStrecke:"strecke"};
function awZeigen(){for(const [id,k] of Object.entries(AWFELD))$(id).value=AW[k];}
function fensterListe(){
  return String(AW.fenster).split(/[;\s]+/).map(v=>parseFloat(v.replace(",","."))).filter(v=>isFinite(v)&&v>=0).slice(0,8);
}
function awUebernehmen(){
  for(const [id,k] of Object.entries(AWFELD)){
    const el=$(id);
    if(k==="fenster"){AW[k]=el.value;continue;}
    const v=parseFloat(String(el.value).replace(",","."));
    if(isFinite(v))AW[k]=v;
  }
  if(!(AW.bis>AW.von)){AW.von=AW_STANDARD.von;AW.bis=AW_STANDARD.bis;}
  lsSchreiben("wz_auswertung",AW);
  allesAuswerten();
}
for(const id of Object.keys(AWFELD))$(id).onchange=awUebernehmen;
$("awStandard").onclick=()=>{AW=Object.assign({},AW_STANDARD);awZeigen();awUebernehmen();};
awZeigen();

// Uhr des Pico stellen (hat keine eigene)
fetch("/zeit",{method:"POST",headers:{"X-Waegezelle":"1"},body:new URLSearchParams({t:Math.floor(Date.now()/1000),tz:-new Date().getTimezoneOffset()})}).catch(()=>{});

// ---------- Rückmeldung: Antworten des Pico nach einem Befehl, Fehler immer (unten rechts) ----------
let rmBis=0;
function meldung(text,art){
  const box=$("meldungen"),el=document.createElement("div");
  el.className="meldung"+(art?" "+art:"");el.textContent=text;box.appendChild(el);
  while(box.children.length>4)box.firstChild.remove();
  setTimeout(()=>el.remove(),art==="fehler"?9000:art==="warn"?6000:4000);
}
const RM_FEHLER=/FEHLER|ALARM|Sensorfehler|Ungueltig|Unbekannt/i;
const RM_WARN=/abgebrochen|abgelehnt|verweigert|nicht |erst |kein|zuerst|Hinweis|Warn|faehrt/i;
let logGeladen=false;                                         // erste Antwort = alter Verlauf: keine Meldungen
function rueckmeldung(zeilen){
  if(!logGeladen)return;
  const jetzt=Date.now(),l=[];
  for(const z of zeilen){
    if(!z.startsWith("#")||z.startsWith("#CFG"))continue;
    const t=z.replace(/^#\s*/,"").trim();
    if(!t||/^[-=]{3}/.test(t))continue;
    const art=RM_FEHLER.test(t)?"fehler":RM_WARN.test(t)?"warn":"";
    if(jetzt<rmBis||art==="fehler")l.push([t,art]);
  }
  const zeig=l.length>3?l.slice(0,2):l;
  for(const [t,art] of zeig)meldung(t,art);
  if(l.length>3)meldung(T("… {0} weitere Zeilen im Protokoll",l.length-2),"");
}
async function befehl(c){
  rmBis=Date.now()+3500;
  try{const r=await fetch("/cmd",{method:"POST",headers:{"X-Waegezelle":"1"},body:new URLSearchParams({c})});
    if(!r.ok)meldung(T("Befehl „{0}“ abgelehnt (HTTP {1})",c,r.status),"fehler");}
  catch(e){meldung(T("Befehl „{0}“ nicht gesendet – keine Verbindung",c),"fehler");
    logText(T("# Browser: Befehl '{0}' konnte nicht gesendet werden",c)+"\n");}
}
async function aktion(o){
  rmBis=Date.now()+3500;
  try{const r=await fetch("/sitzung",{method:"POST",headers:{"X-Waegezelle":"1"},body:new URLSearchParams(o)});const j=await r.json();listeLaden();
    if(j.ok!==1)meldung(T("Sitzung: Aktion nicht ausgeführt – Grund im Protokoll"),"warn");
    return j.ok===1;}
  catch(e){meldung(T("Sitzung: keine Verbindung zum Pico"),"fehler");return false;}
}
document.querySelectorAll("button[data-c]").forEach(b=>b.onclick=()=>befehl(b.dataset.c));
$("start").onclick=()=>befehl("pr");
document.addEventListener("keydown",e=>{if(e.key==="Escape")befehl("x");});
// Während der Antrieb fährt nicht neu laden oder wegnavigieren (der Pico liefert dann ohnehin nur eine Warteseite)
window.addEventListener("beforeunload",e=>{if(online&&S&&(S.bes||S.pr.lauf)){e.preventDefault();e.returnValue="";}});
$("senden").onclick=senden;
$("befehl").onkeydown=e=>{if(e.key==="Enter")senden();};
function senden(){const t=$("befehl").value.trim();if(t){befehl(t);$("befehl").value="";}}
$("zuSitzungen").onclick=sitzDialog;
$("zeige").onchange=$("abbr").onchange=$("nurKontakt").onchange=()=>{ansicht=null;neuZeichnen=true;};
$("xmodus").onchange=()=>{lsSchreiben("wz_xmodus",$("xmodus").value);ansicht=null;neuZeichnen=true;};
$("einheit").onchange=()=>{
  E=EINHEITEN[$("einheit").value]||EINHEITEN.N;
  lsSchreiben("wz_einheit2",$("einheit").value);
  ansicht=null;
  anzeigenAktualisieren();listeLaden();if(S){ablaufZeigen();statusZeigen();parameterZeigen(true);}
};
$("trendWert").onchange=()=>{lsSchreiben("wz_trend",$("trendWert").value);neuZeichnen=true;};

// ---------- Binärdaten (PressKopf, siehe src/sitzungen.h) ----------
function kopf(dv,o){
  const f=a=>dv.getFloat32(o+a,true),u=a=>dv.getUint32(o+a,true);
  return {punkte:dv.getUint16(o+6,true),nr:u(8),zeit:u(12),grund:dv.getUint8(o+16),status:dv.getUint8(o+17),
    kontakt:dv.getUint8(o+18),sim:dv.getUint8(o+19),s:f(20),d:f(24),kontaktMm:f(28),endMm:f(32),tiefeMm:f(36),fmaxN:f(40),fmaxMm:f(44),
    fendN:f(48),fhaltN:f(52),fmittelN:f(56),fsigmaN:f(60),fminN:f(64),fspanneN:f(68),arbeitJ:f(72),presszeitS:f(76),
    zykluszeitS:f(80),proben:u(84),verloren:u(88),sps:f(104),vPress:f(116),anfahrt:f(120),haltMs:u(124)};
}
async function kurveLaden(id,nr,token){
  const r=await fetch("/sitzung/presse?id="+id+"&nr="+nr,{cache:"no-store"});
  if(!r.ok)return false;
  const b=await r.arrayBuffer();
  if(token!==ladeNr||b.byteLength<128)return false;
  const dv=new DataView(b),k=kopf(dv,0),n=Math.min(k.punkte,(b.byteLength-128)>>2);
  const x=new Float32Array(n),y=new Float32Array(n);
  for(let i=0;i<n;i++){const v=dv.getInt16(128+i*4,true);x[i]=k.s+i*k.d;y[i]=v===-32768?NaN:v*STUFE;}
  const p={k,x,y:gleit(x,y,GLAETT_MM),a:{}};
  auswerten(p);
  pressen.set(nr,p);
  spaeterAktualisieren();
  dbHolen("kurve",rohSchluessel(id,nr)).then(m=>{if(token===ladeNr&&pressen.get(nr)===p)rohAnwenden(p,m);});
  return true;
}
async function sitzungLaden(id){
  const token=++ladeNr;
  sitId=id;sitN=0;pressen=new Map();auswahl=0;versteckt=new Set();ansicht=null;
  anzeigenAktualisieren();
  if(!id)return;
  holt=true;
  try{
    const r=await fetch("/sitzung/index?id="+id,{cache:"no-store"});
    const b=await r.arrayBuffer();
    if(token!==ladeNr)return;
    const dv=new DataView(b),n=Math.floor(b.byteLength/128);
    for(let i=0;i<n;i++){const k=kopf(dv,i*128);pressen.set(k.nr,{k,x:null,y:null,a:{}});}
    sitN=n;auswahl=n;anzeigenAktualisieren();
    for(const nr of [...pressen.keys()].reverse()){        // neueste zuerst
      if(token!==ladeNr)return;
      await kurveLaden(id,nr,token);
    }
  }catch(e){}
  finally{if(token===ladeNr)holt=false;}
}
async function neuePressen(n){
  if(holt)return;
  holt=true;
  const token=ladeNr;
  try{
    for(let nr=sitN+1;nr<=n;nr++){
      if(!await kurveLaden(sitId,nr,token))break;
      sitN=nr;auswahl=nr;
    }
    anzeigenAktualisieren();listeLaden();
  }catch(e){}
  finally{holt=false;}
}

// ---------- Rohdaten im Browser (IndexedDB): jede Probe, Kurve mit einem Punkt je Motorschritt ----------
const ROH_MAGIC=0x31484F52;                                  // "ROH1"
const dbBereit=new Promise(res=>{try{const r=indexedDB.open("opensmartpress",1);
  r.onupgradeneeded=()=>{r.result.createObjectStore("roh");r.result.createObjectStore("kurve");};
  r.onsuccess=()=>res(r.result);r.onerror=()=>res(null);}catch(e){res(null);}});
async function dbHolen(store,key){
  const db=await dbBereit;if(!db)return null;
  return new Promise(res=>{try{const q=db.transaction(store).objectStore(store).get(key);
    q.onsuccess=()=>res(q.result||null);q.onerror=()=>res(null);}catch(e){res(null);}});
}
async function dbAblegen(eintraege){
  const db=await dbBereit;if(!db)return false;
  return new Promise(res=>{try{const tx=db.transaction(["roh","kurve"],"readwrite");
    for(const [st,k,v] of eintraege)tx.objectStore(st).put(v,k);
    tx.oncomplete=()=>res(true);tx.onerror=tx.onabort=()=>res(false);}catch(e){res(false);}});
}
const rohSchluessel=(sit,nr)=>sit+"/"+nr;
// RohProbe (8 Byte): t4 u16 (4 µs, läuft über) | last i24 (Counts über Tara) | pos 23 Bit (Schritte ab start) + Bit 23 Halten
function rohDekodieren(r){
  const dv=new DataView(r.daten),n=r.n,t=new Float64Array(n),schritt=new Int32Array(n),F=new Float32Array(n),halt=new Uint8Array(n);
  const nProCount=0.00980665/r.fa;
  let prev=(r.t0>>>2)&0xFFFF,tu=0;
  for(let i=0;i<n;i++){
    const o=i*8,t4=dv.getUint16(o,true);
    tu+=((t4-prev)&0xFFFF)*4;prev=t4;t[i]=tu*1e-6;
    let l=dv.getUint8(o+2)|dv.getUint8(o+3)<<8|dv.getUint8(o+4)<<16;if(l&0x800000)l-=0x1000000;
    const w=dv.getUint8(o+5)|dv.getUint8(o+6)<<8|dv.getUint8(o+7)<<16;
    let p=w&0x7FFFFF;if(p&0x400000)p-=0x800000;
    halt[i]=w>>>23;schritt[i]=r.startSchritt+p;F[i]=l*nProCount;
  }
  return {t,schritt,F,halt};
}
// Kurve: Mittel der Kraft je Motorschritt (nur Drücken) - feiner geht es nicht, der Pico kennt nur Schritte
function schrittKurve(r){
  const d=rohDekodieren(r),xs=[],ys=[];
  let akt=NaN,sum=0,k=0;
  for(let i=0;i<r.n;i++){
    if(d.halt[i])continue;
    if(d.schritt[i]!==akt){if(k){xs.push(akt/r.spmm);ys.push(sum/k);}akt=d.schritt[i];sum=0;k=0;}
    sum+=d.F[i];k++;
  }
  if(k){xs.push(akt/r.spmm);ys.push(sum/k);}
  return {x:Float32Array.from(xs),y:Float32Array.from(ys)};
}
// Rohkurve übernehmen, wenn sie zu dieser Pressung gehört (gleiche Probenzahl, Zeit passt)
function rohAnwenden(p,m){
  if(!m||m.n!==p.k.proben||m.x.length<2||(p.k.zeit&&m.zeit&&Math.abs(m.zeit-p.k.zeit)>600))return false;
  p.rx=m.x;p.ry=gleit(m.x,m.y,GLAETT_MM);p.rds=1/m.spmm;
  auswerten(p);spaeterAktualisieren();
  return true;
}
async function rohAblegen(sit,nr){
  const key=rohSchluessel(sit,nr),p=sitId===sit?pressen.get(nr):null;
  const alt=await dbHolen("kurve",key);
  if(alt&&alt.n===roh.n){if(p)rohAnwenden(p,alt);return;}   // schon gespeichert (z. B. Seite neu geladen)
  const daten=new Uint8Array(roh.n*8);
  let o=0;for(const t of roh.teile){daten.set(t,o);o+=t.length;}
  const zeit=p&&p.k.zeit?p.k.zeit:Math.floor(Date.now()/1000);
  const r={sit,nr,n:roh.n,zeit,t0:roh.t0,startSchritt:roh.start,spmm:roh.spmm,fa:roh.fa,sps:roh.sps,daten:daten.buffer};
  const kv=schrittKurve(r);
  const m={sit,nr,n:r.n,zeit,sps:r.sps,spmm:r.spmm,x:kv.x,y:kv.y};
  roh.teile=[];
  if(!await dbAblegen([["roh",key,r],["kurve",key,m]]))logText(T("# Browser: Rohdaten konnten nicht gespeichert werden (IndexedDB)")+"\n");
  const q=sitId===sit?pressen.get(nr):null;                  // Kopf evtl. erst jetzt geladen
  if(q)rohAnwenden(q,m);
}
// Proben der laufenden bzw. letzten Pressung abholen (während der Pressung alle 100 ms)
async function rohHolen(){
  if(rohHolt)return;
  rohHolt=true;
  try{
    for(;;){
      const r=await fetch("/presse/roh?id="+roh.id+"&ab="+roh.n,{cache:"no-store"});
      if(!r.ok)break;
      const b=await r.arrayBuffer();
      if(b.byteLength<48)break;
      const dv=new DataView(b);
      if(dv.getUint32(0,true)!==ROH_MAGIC)break;
      const id=dv.getUint32(4,true),ab=dv.getUint32(8,true),n=dv.getUint32(12,true),ges=dv.getUint32(16,true);
      if(id!==roh.id)roh={id,n:0,luecke:false,teile:[],fertig:false,ende:false};
      if(ab!==roh.n){roh.luecke=true;roh.teile=[];}             // Proben dazwischen überschrieben
      Object.assign(roh,{t0:dv.getUint32(20,true),start:dv.getInt32(24,true),spmm:dv.getFloat32(28,true),
        fa:dv.getFloat32(32,true),sps:dv.getFloat32(44,true)});
      if(n&&!roh.luecke)roh.teile.push(new Uint8Array(b.slice(48,48+n*8)));
      roh.n=ab+n;
      if(roh.n<ges)continue;                                    // Rest sofort holen
      const lauf=dv.getUint8(38),nr=dv.getUint32(40,true),sit=dv.getUint16(36,true);
      if(!lauf){
        if(nr&&!roh.luecke&&!roh.fertig){roh.fertig=true;await rohAblegen(sit,nr);}
        roh.teile=[];roh.ende=true;
        break;
      }
      await new Promise(res=>setTimeout(res,100));
    }
  }catch(e){}
  finally{rohHolt=false;}
}
function herunterladen(text,name){
  const blob=new Blob(["﻿"+text],{type:"text/csv;charset=utf-8"});
  const link=document.createElement("a");
  link.href=URL.createObjectURL(blob);link.download=name;
  document.body.appendChild(link);link.click();link.remove();
  setTimeout(()=>URL.revokeObjectURL(link.href),5000);
}
async function rohCsv(p){
  const r=await dbHolen("roh",rohSchluessel(sitId,p.k.nr));
  if(!r){logText(T("# Browser: keine Rohdaten für Pressung {0} gefunden",p.k.nr)+"\n");return;}
  const d=rohDekodieren(r),z=csvZ,sp=csvSep(),halten=T("Halten"),druecken=T("Drücken");
  const zeilen=[csvZeile([T("Sitzung"),sitId,sitName]),csvZeile([T("Pressung"),p.k.nr]),csvZeile([T("Datenrate [SPS]"),z(r.sps,0)]),
    csvZeile([T("Schritte/mm"),z(r.spmm,3)]),"",csvZeile([T("Zeit [s]"),T("Weg [mm]"),T("Kraft [{0}]",E.n),T("Phase")])];
  for(let i=0;i<r.n;i++)zeilen.push(z(d.t[i],6)+sp+z(d.schritt[i]/r.spmm,5)+sp+z(d.F[i]*E.f,E.d+2)+sp+(d.halt[i]?halten:druecken));
  herunterladen(zeilen.join("\r\n"),T("sitzung")+"_"+String(sitId).padStart(4,"0")+"_"+T("pressung")+"_"+String(p.k.nr).padStart(4,"0")+"_"+T("roh")+".csv");
}

// ---------- Auswertung einer Pressung (Kräfte in N, Wege in mm ab Kontakt) ----------
function interp(s,f,x){
  if(!s.length||x<s[0]||x>s[s.length-1])return NaN;
  let lo=0,hi=s.length-1;
  while(hi-lo>1){const m=(lo+hi)>>1;if(s[m]<=x)lo=m;else hi=m;}
  return s[hi]===s[lo]?f[lo]:f[lo]+(f[hi]-f[lo])*(x-s[lo])/(s[hi]-s[lo]);
}
// gleitendes Mittel über eine Wegbreite b (s aufsteigend); ungültige Werte (NaN) zählen nicht und bleiben NaN
function gleit(s,f,b){
  const n=s.length,o=new Float32Array(n);
  let lo=0,hi=0,sum=0,k=0;
  for(let i=0;i<n;i++){
    while(hi<n&&s[hi]<=s[i]+b/2){if(isFinite(f[hi])){sum+=f[hi];k++;}hi++;}
    while(s[lo]<s[i]-b/2){if(isFinite(f[lo])){sum-=f[lo];k--;}lo++;}
    o[i]=isFinite(f[i])&&k?sum/k:NaN;
  }
  return o;
}
// Glättung aller Kurven auf der Pressseite (Anzeige und Auswertung): genau eine Periode der Kraftwelligkeit
// des Antriebs (40 Schritte bei 800 Schritten/mm = 1/100 Umdrehung der 5-mm-Spindel), die sich so herausmittelt.
// Die Kennwerte des Pico (Fmax, Fmittel, ...) bleiben ungeglättet.
const GLAETT_MM=0.05;
function auswerten(p){
  const k=p.k,a={};
  p.a=a;
  const X=p.rx||p.x,Y=p.ry||p.y;                         // Rohkurve (je Motorschritt), sonst Pico-Kurve
  if(!X||!k.kontakt||!isFinite(k.kontaktMm)||!isFinite(k.endMm))return;
  const s=[],fr=[];
  for(let i=0;i<X.length;i++){
    if(!isFinite(Y[i]))continue;
    const w=X[i]-k.kontaktMm;
    if(w>=0){s.push(w);fr.push(Y[i]);}
  }
  if(s.length<5)return;
  // beide Kurven sind schon über GLAETT_MM geglättet: Kennwerte bleiben vergleichbar,
  // mit Rohkurve werden die Wege (Fügekraft, Spitze, Abfall) genauer
  const f=fr;
  const L=(k.endMm-AW.ende)-k.kontaktMm;                 // Fügetiefe (ohne Anschlag)
  a.L=L;
  if(!(L>0))return;
  const ds=p.rx?p.rds:Math.max(k.d,1e-4);
  // Spitze und Abfall: die Glättung über GLAETT_MM reicht (früher 5 Pico-Abschnitte = 0,031 mm)
  const g=f;
  // Fügekraft: Maximum bis vor den Anschlag
  let fm=-Infinity,sm=NaN;
  for(let i=0;i<s.length&&s[i]<=L;i++)if(f[i]>fm){fm=f[i];sm=s[i];}
  if(isFinite(fm)){a.fuegeN=fm;a.fuegeMm=sm;}
  // Einführspitze: erstes lokales Maximum in den ersten 40 % mit Rückgang um >= 1 % (mind. 0,3 N) danach
  const w=Math.max(2,Math.round(0.03/ds));
  for(let i=w;i<s.length&&s[i]<=0.4*L;i++){
    let istMax=true;
    for(let j=i-w;j<=Math.min(s.length-1,i+w);j++)if(g[j]>g[i]){istMax=false;break;}
    if(!istMax)continue;
    let tief=g[i];
    for(let j=i;j<=Math.min(s.length-1,i+3*w);j++)tief=Math.min(tief,g[j]);
    if(g[i]-tief>=Math.max(0.01*g[i],0.3)){a.spitzeN=g[i];a.spitzeMm=s[i];break;}
  }
  // Plateau-Gerade: lineare Regression im Fenster von..bis % der Fügetiefe
  const p0=L*AW.von/100,p1=L*AW.bis/100;
  let n=0,sx=0,sy=0,sxx=0,sxy=0,arb=0,sPrev=NaN,fPrev=NaN;
  for(let i=0;i<s.length;i++){
    if(s[i]<p0||s[i]>p1)continue;
    n++;sx+=s[i];sy+=f[i];sxx+=s[i]*s[i];sxy+=s[i]*f[i];
    if(isFinite(sPrev))arb+=(f[i]+fPrev)/2*(s[i]-sPrev);
    sPrev=s[i];fPrev=f[i];
  }
  if(n>=5){
    const den=n*sxx-sx*sx;
    if(den>0){
      const m=(n*sxy-sx*sy)/den,c=(sy-m*sx)/n;
      let q=0;
      for(let i=0;i<s.length;i++){if(s[i]<p0||s[i]>p1)continue;const r=f[i]-(m*s[i]+c);q+=r*r;}
      a.steigung=m;a.achse=c;a.laufruhe=Math.sqrt(q/(n-2));a.p0=p0;a.p1=p1;
    }
    a.parbeit=arb/1000;                                  // N·mm -> J
  }
  // Größter Kraftabfall im Fügebereich (ab Plateaubeginn bis vor den Anschlag)
  let best=0;
  for(let i=0;i<s.length&&s[i]<=L;i++){
    if(s[i]<p0||!(g[i]>=0.1*(a.fuegeN||0)))continue;
    let mn=g[i];
    for(let j=i+1;j<s.length&&s[j]-s[i]<=AW.strecke;j++)mn=Math.min(mn,g[j]);
    const drop=g[i]-mn;
    if(drop>best){best=drop;a.abfallN=drop;a.abfallPct=100*drop/g[i];a.abfallMm=s[i];}
  }
  if(!(best>0)){a.abfallN=0;a.abfallPct=0;}
  a.riss=a.abfallPct>=AW.abfall;
  // Kraft in den Wegfenstern
  a.fenster=fensterListe().map(x=>x<=L+AW.ende?interp(s,f,x):NaN);
}
function allesAuswerten(){
  for(const p of pressen.values())if(p.x)auswerten(p);
  trendAuswahl();
  anzeigenAktualisieren();
}

// ---------- Kennwerte (Getter liefern N, mm, J, s oder %) ----------
// Typ "F" = Kraft, "F/mm" = Kraft je mm (beide in der gewählten Einheit); d = Stellen (bei F zusätzlich zu E.d)
function kennListe(){
  const l=[
    ["fuege",T("Fügekraft (ohne Anschlag)"),"F",0,p=>p.a.fuegeN],
    ["spitze",T("Einführspitze"),"F",0,p=>p.a.spitzeN],
    ["steigung",T("Plateau-Steigung"),"F/mm",0,p=>p.a.steigung],
    ["achse",T("Plateau-Achsenabschnitt"),"F",0,p=>p.a.achse],
    ["laufruhe",T("Laufruhe (σ um die Gerade)"),"F",1,p=>p.a.laufruhe],
    ["abfall",T("Größter Kraftabfall"),"%",1,p=>p.a.abfallPct],
    ["parbeit",T("Arbeit im Plateaufenster"),"J",3,p=>p.a.parbeit]];
  fensterListe().forEach((x,i)=>l.push(["f"+i,T("Kraft bei {0} mm",fmt(x,2)),"F",0,p=>p.a.fenster?p.a.fenster[i]:NaN]));
  l.push(["fend",T("Endkraft (am Anschlag)"),"F",0,p=>p.k.fendN],
    ["kontakt",T("Kontaktposition"),"mm",3,p=>p.k.kontaktMm],
    ["tiefe",T("Presstiefe"),"mm",3,p=>p.k.tiefeMm],
    ["arbeit",T("Pressarbeit gesamt"),"J",3,p=>p.k.arbeitJ],
    ["zeit",T("Presszeit"),"s",2,p=>p.k.presszeitS]);
  return l;
}
// Erklärungen der Kennwerte (title, erscheint beim Darüberfahren)
const TIPP={
  fuege:"Höchste Kraft ab Kontakt bis vor den Anschlag – die eigentliche Einpresskraft",
  spitze:"Erstes deutliches Kraftmaximum beim Einführen (in den ersten 40 % der Fügetiefe)",
  steigung:"Steigung der Ausgleichsgeraden im Plateaufenster: wie stark die Kraft mit dem Weg zunimmt",
  achse:"Kraft der Plateau-Geraden beim Kontakt (Weg 0)",
  laufruhe:"Streuung der Kraft um die Plateau-Gerade – klein = gleichmäßiges Fügen, groß = Ruckeln oder Fressen",
  abfall:"Größter Kraftrückgang innerhalb der eingestellten Strecke – ab dem eingestellten Prozentwert Rissverdacht",
  parbeit:"Fläche unter der Kurve im Plateaufenster",
  fend:"Kraft am Ende des Pressens (am Anschlag)",
  kontakt:"Weg, bei dem das Werkstück berührt wurde (0 = Werkzeug-Endpunkt)",
  tiefe:"Weg vom Kontakt bis zum Ende des Pressens",
  arbeit:"Fläche unter der ganzen Kurve ab Kontakt",
  zeit:"Dauer des Pressens ohne Eilgang und Rückfahrt",
  fx:"Kraft genau bei diesem Weg ab Kontakt",
  status:"abgeschlossen = ohne Abbruch beendet. Sagt nichts über die Qualität des Teils."};
const tt=id=>{const s=TIPP[id]||(/^f\d/.test(id)?TIPP.fx:"");return s?' title="'+esc(T(s))+'"':'';};
const istF=t=>t==="F"||t==="F/mm";
const kwert=(p,m)=>{const v=m[4](p);return (v===undefined||v===null)?NaN:(istF(m[2])?v*E.f:v);};
const kstellen=m=>istF(m[2])?E.d+m[3]:m[3];
const keinheit=t=>t==="F"?E.n:t==="F/mm"?E.n+"/mm":t;
function kennStat(){
  const ok=[...pressen.values()].filter(p=>p.k.status===0&&p.x);
  const r={};
  for(const m of kennListe()){
    const v=ok.map(p=>kwert(p,m)).filter(x=>isFinite(x));
    if(!v.length)continue;
    const n=v.length,mw=v.reduce((a,b)=>a+b,0)/n,s=n>1?Math.sqrt(v.reduce((a,b)=>a+(b-mw)*(b-mw),0)/(n-1)):NaN;
    r[m[0]]={n,m:mw,s,min:Math.min(...v),max:Math.max(...v)};
  }
  return {r,n:ok.length,riss:ok.filter(p=>p.a.riss).length};
}

let aktTimer=null;
function spaeterAktualisieren(){clearTimeout(aktTimer);aktTimer=setTimeout(anzeigenAktualisieren,150);}
function anzeigenAktualisieren(){clearTimeout(aktTimer);tabelle();statistik();details();neuZeichnen=true;}

// ---------- Status ----------
async function abfragen(){
  try{
    const q=new URLSearchParams({ab:live.n,id:live.id});
    if(ln!==null)q.set("l",ln);
    const ac=new AbortController(),zl=setTimeout(()=>ac.abort(),2500);   // hängt der Pico, nicht ewig warten
    try{
      const r=await fetch("/presse/status?"+q,{cache:"no-store",signal:ac.signal});
      const d=await r.json();
      online=true;offlineSeit=0;
      verarbeiten(d);
    }finally{clearTimeout(zl);}
  }catch(e){online=false;if(!offlineSeit)offlineSeit=Date.now();statusZeigen();}
  setTimeout(abfragen,250);
}
// Verbindung, STOPP-Leiste, Simulationshinweis
function leistenZeigen(){
  const off=!!offlineSeit&&Date.now()-offlineSeit>1200;
  $("offlineBanner").hidden=!off;document.body.classList.toggle("offline",off);
  const faehrt=!off&&!!S&&!!(S.bes||S.pr.lauf);
  $("stoppleiste").hidden=!faehrt;document.body.classList.toggle("faehrt",faehrt);
  if(faehrt)$("stoppText").textContent=TF(S.zust)+(S.pos!==null?" · "+fmt(S.pos,2)+" mm":"")+" · "+fk(S.kraft);
  $("simBanner").hidden=off||!S||!S.sim;
}
setInterval(leistenZeigen,500);
function verarbeiten(d){
  S=d;
  const p=d.pr;
  if(p.id!==live.id||p.ab===0){live={id:p.id,n:0,s:p.s,d:p.d,y:[],lauf:p.lauf};}
  live.lauf=p.lauf;live.s=p.s;live.d=p.d;
  if(p.ab===live.n){for(const v of p.p)live.y.push(v===null?NaN:v*STUFE);live.n=live.y.length;}
  if(p.lauf||p.p.length)neuZeichnen=true;
  if(d.roh&&(d.roh.id!==roh.id||d.roh.n>roh.n||p.lauf||!roh.ende))rohHolen();
  const id=d.sit?d.sit.id:0;
  sitName=d.sit?d.sit.name:"";
  if(id!==sitId){sitzungLaden(id);listeLaden();}
  else if(d.sit&&d.sit.n>sitN&&!holt)neuePressen(d.sit.n);
  if(d.log)logAufnehmen(d.log);
  ln=d.ln;logGeladen=true;
  ablaufZeigen();statusZeigen();parameterZeigen();leistenZeigen();
}
function statusZeigen(){
  const el=$("status");
  if(!online||!S){el.innerHTML='<span class="chip warn"><span class="punkt aus"></span>'+T("keine Verbindung")+'</span>';
    $("lvKraft").textContent="–";$("lvZust").textContent=T("keine Verbindung");$("lvPos").textContent="–";return;}
  $("lvKraft").innerHTML=fmt(kr(S.kraft),E.d+1)+'<i>'+E.n+'</i>';
  $("lvZust").innerHTML=(S.alarm?'<span class="warn">ALARM</span> · ':'')+(S.sim?'<span class="warn">'+T("Simulation")+'</span> · ':'')+esc(TF(S.zust));
  $("lvPos").textContent=S.pos!==null?fmt(S.pos,3):"–";
  el.innerHTML='<span class="chip"><span class="punkt'+(S.ok?'':' aus')+'"></span>'+fmt(S.rate,0)+' SPS</span>'+
    '<span class="chip">'+esc(TF(S.zust))+'</span>'+
    (S.pos!==null?'<span class="chip">Pos. '+fmt(S.pos,3)+' mm</span>':'')+
    '<span class="chip">'+fk(S.kraft)+'</span>'+(S.alarm?'<span class="chip warn">ALARM</span>':'')+
    (S.sim?'<span class="chip warn">SIMULATION</span>':'');
}
function schritt(n,ok,text){$("s"+n).classList.toggle("ok",ok);$("s"+n+"t").innerHTML=text;}
function ablaufZeigen(){
  const d=S;
  const nahNull=Math.abs(d.kraft)<Math.max(0.5,d.par.kontakt*0.5);
  if(document.activeElement!==$("sim"))$("sim").checked=!!d.sim;
  schritt(1,d.ok&&(d.kal||d.sim)&&nahNull,!d.ok?'<span class="warn">'+T("ADC misst nicht")+'</span>':
    !d.kal&&!d.sim?T('<span class="warn">Waage nicht kalibriert</span> – <a href="/#kalibrieren">jetzt kalibrieren</a> (Messseite)'):
    T("Kraft jetzt: <b>{0}</b>",fk(d.kraft,1))+(nahNull?T(" – tariert"):T(" – mit Werkzeug „Tara“ drücken")));
  schritt(2,!!d.end.akt,d.end.akt?T("Werkzeug-Kontakt {0} mm unter Start, Endpunkt +{1} mm bei {2}",fmt(d.end.tiefe,3),fmt(d.end.ende,4),
    fk(d.end.kraft)):T("nach jedem Einschalten nötig – Werkzeug aufgelegt, Werkstück entnommen"));
  schritt(3,!!d.sit,d.sit?'<b>'+esc(d.sit.name)+'</b> · '+T("{0} Pressungen",d.sit.n):T("keine Sitzung aktiv"));
  $("start").disabled=!d.bereit;
  schritt(4,!!d.bereit,d.pr.lauf?'<b>'+esc(TF(d.zust))+'</b> …':d.bereit?T("bereit – START drücken (Knopf oder Taster)"):
    '<span class="warn">'+esc(TF(d.grund))+'</span>');
}

// ---------- Tabelle, Statistik, Details ----------
function statistik(){
  const {r,n,riss}=kennStat();
  $("statSub").innerHTML=T("{0} gültige Pressungen (ohne Abbrüche)",n)+(riss?' · <span class="warn">'+T("{0} mit Rissverdacht",riss)+'</span>':'');
  if(!n){$("stat").innerHTML="";return;}
  let h='<table><tr><th>'+T("Kennwert")+'</th><th>'+T("Mittel")+'</th><th>σ</th><th>σ %</th><th>Min</th><th>Max</th><th>'+T("Spannweite")+'</th></tr>';
  for(const m of kennListe()){
    const x=r[m[0]];if(!x)continue;
    const d=kstellen(m);
    h+='<tr><td'+tt(m[0])+'>'+m[1]+' ['+keinheit(m[2])+']</td><td><b>'+fmt(x.m,d)+'</b></td><td>'+fmt(x.s,d+1)+'</td><td>'+
      (x.m!==0&&m[2]!=="%"?fmt(100*x.s/Math.abs(x.m),2):"–")+'</td><td>'+fmt(x.min,d)+'</td><td>'+fmt(x.max,d)+'</td><td>'+fmt(x.max-x.min,d)+'</td></tr>';
  }
  $("stat").innerHTML=h+'</table>';
}
function tabelle(){
  const F=E.n;
  let h='<thead><tr><th></th><th>'+T("Nr")+'</th><th>'+T("Zeit")+'</th><th'+tt("fuege")+'>'+T("Fügekraft")+' '+F+'</th><th'+tt("spitze")+'>'+T("Spitze")+' '+F+'</th><th'+tt("steigung")+'>'+T("Steigung")+' '+F+'/mm</th>'+
    '<th'+tt("laufruhe")+'>'+T("Laufruhe")+' '+F+'</th><th'+tt("abfall")+'>'+T("Abfall")+' %</th><th'+tt("fend")+'>'+T("Fend")+' '+F+'</th><th'+tt("status")+'>Status</th></tr></thead><tbody>';
  for(const p of [...pressen.values()].sort((a,b)=>b.k.nr-a.k.nr)){
    const k=p.k,a=p.a;
    const st=T(STATUS[k.status]||"?")+(k.sim?' (Sim.)':'');
    h+='<tr data-nr="'+k.nr+'"'+(k.nr===auswahl?' class="sel"':'')+'><td><input type="checkbox" title="'+T("Kurve anzeigen")+'"'+
      (versteckt.has(k.nr)?'':' checked')+'></td><td>'+k.nr+(p.rx?' <span class="ok" title="'+T("Rohdaten (jeder Messwert) in diesem Browser")+'">R</span>':'')+'</td><td>'+(k.zeit?new Date(k.zeit*1000).toLocaleTimeString(LOC()):"–")+
      '</td><td><b>'+fmt(kr(a.fuegeN),E.d)+'</b></td><td>'+fmt(kr(a.spitzeN),E.d)+'</td><td>'+fmt(kr(a.steigung),E.d)+
      '</td><td>'+fmt(kr(a.laufruhe),E.d+1)+'</td><td'+(a.riss?' class="warn"':'')+'>'+fmt(a.abfallPct,1)+(a.riss?' ⚠':'')+
      '</td><td>'+fmt(kr(k.fendN),E.d)+'</td><td'+(k.status?' class="warn"':'')+'>'+st+'</td></tr>';
  }
  $("tab").innerHTML=h+'</tbody>';
  $("tab").querySelectorAll("tbody tr").forEach(tr=>{
    const nr=+tr.dataset.nr;
    tr.querySelector("input").onclick=e=>{e.stopPropagation();if(e.target.checked)versteckt.delete(nr);else versteckt.add(nr);neuZeichnen=true;};
    tr.onclick=()=>waehlen(nr);
  });
}
function waehlen(nr){auswahl=nr;details();tabelle();neuZeichnen=true;}
function details(){
  const p=pressen.get(auswahl);
  if(!p){$("detTitel").textContent=T("Pressung");$("detSub").textContent="";
    $("details").innerHTML="<dt>"+T("Noch keine Pressung ausgewählt.")+"</dt><dd></dd>";kurzwerte();return;}
  const k=p.k,a=p.a,{r}=kennStat(),liste=kennListe();
  $("detTitel").textContent=T("Pressung {0}",k.nr);
  $("detSub").innerHTML=zeitText(k.zeit)+' · <span class="'+(k.status?'warn':'ok')+'">'+T(STATUS[k.status]||"?")+'</span>'+
    (k.sim?' · <span class="warn">'+T("Simulation")+'</span>':'')+(a.riss?' · <span class="warn">⚠ '+T("Rissverdacht")+'</span>':'');
  // Zeile für einen Kennwert aus der Liste, mit Abweichung zum Sitzungsmittel
  const zeile=(id,zusatz)=>{
    const m=liste.find(x=>x[0]===id);if(!m)return'';
    const v=kwert(p,m),d=kstellen(m),x=r[id];
    let abw='';
    if(x&&x.n>=2&&isFinite(v)&&!k.status){const dv=v-x.m;
      abw='<i>'+(dv>=0?"+":"")+fmt(dv,d)+(x.m&&m[2]!=="%"?' ('+(dv>=0?"+":"")+fmt(100*dv/Math.abs(x.m),1)+' %)':'')+' '+T("zum Mittel")+'</i>';}
    return '<dt'+tt(id)+'>'+m[1]+'</dt><dd><b>'+fmt(v,d)+'</b> '+keinheit(m[2])+(zusatz||'')+abw+'</dd>';
  };
  const bei=mm=>isFinite(mm)?' <i>'+T("bei {0} mm",fmt(mm,3))+'</i>':'';
  const gr=t=>'<div class="gruppe">'+T(t)+'</div>';
  const dt=t=>'<dt>'+T(t)+'</dt>';
  let h=gr("Fügen")+zeile("fuege",bei(a.fuegeMm))+zeile("spitze",bei(a.spitzeMm))+
    zeile("steigung")+zeile("achse")+zeile("laufruhe")+zeile("parbeit")+
    (isFinite(a.p0)?dt("Plateaufenster")+'<dd>'+T("{0} – {1} mm ab Kontakt",fmt(a.p0,3),fmt(a.p1,3))+'</dd>':'')+
    zeile("abfall",(isFinite(a.abfallN)&&a.abfallN>0?' <i>('+fk(a.abfallN)+bei(a.abfallMm).replace(' <i>',' ').replace('</i>','')+')</i>':''));
  const fl=fensterListe();
  if(fl.length){h+=gr("Wegfenster ab Kontakt");fl.forEach((_,i)=>{h+=zeile("f"+i);});}
  h+=gr("Pressung")+zeile("kontakt")+zeile("tiefe")+zeile("fend")+
    dt("Höchstkraft gesamt (inkl. Anschlag)")+'<dd>'+fk(k.fmaxN)+bei(k.fmaxMm-(k.kontakt?k.kontaktMm:0))+'</dd>'+
    (k.haltMs?dt("Kraft nach Haltezeit")+'<dd>'+fk(k.fhaltN)+'</dd>':'')+
    zeile("arbeit")+zeile("zeit")+dt("Zykluszeit")+'<dd>'+fmt(k.zykluszeitS,1)+' s</dd>'+
    dt("Ende durch")+'<dd>'+T(GRUND[k.grund]||"?")+'</dd>'+
    dt("Proben")+'<dd>'+T("{0} bei {1} SPS",k.proben,fmt(k.sps,0))+(k.verloren?' <span class="warn">('+T("{0} verloren",k.verloren)+')</span>':'')+'</dd>'+
    dt("Kurve")+'<dd>'+(p.rx?T("Rohdaten: {0} Punkte, 1 Schritt = {1} µm",p.rx.length,fmt(p.rds*1000,1))+' <button id="rohCsv">'+T("Rohdaten CSV")+'</button>':
      T("Pico: {0} Punkte à {1} µm",p.x?p.x.length:0,fmt(k.d*1000,0)))+'</dd>'+
    dt("Einstellungen")+'<dd>'+T("{0} mm/s, Haltezeit {1} ms",fmt(k.vPress,2),k.haltMs)+'</dd>';
  $("details").innerHTML=h;
  if($("rohCsv"))$("rohCsv").onclick=()=>rohCsv(p);
  kurzwerte();
}
// Grundwerte der ausgewählten Kurve oben rechts im Diagramm
function kurzwerte(){
  const el=$("kurz"),p=pressen.get(auswahl);
  if(!p){el.hidden=true;return;}
  const k=p.k,a=p.a,liste=kennListe();
  const z=id=>{const m=liste.find(x=>x[0]===id);return m?'<dt'+tt(id)+'>'+m[1].replace(/ \(.*\)$| gesamt$/,"")+'</dt><dd><b>'+fmt(kwert(p,m),kstellen(m))+'</b> '+keinheit(m[2])+'</dd>':'';};
  const st=k.status?'<span class="warn">'+T(STATUS[k.status]||"?")+'</span>':a.riss?'<span class="warn">⚠ '+T("Rissverdacht")+'</span>':'<span class="ok">'+T("abgeschlossen")+'</span>';
  el.innerHTML='<div class="kk">'+T("Pressung {0}",k.nr)+'<span>'+st+(k.sim?' · Sim.':'')+'</span></div>'+
    '<dl>'+z("fuege")+z("spitze")+'<dt>'+T("Höchstkraft")+'</dt><dd><b>'+fmt(kr(k.fmaxN),E.d)+'</b> '+E.n+'</dd>'+
    z("fend")+z("tiefe")+z("arbeit")+z("abfall")+'</dl>';
  el.hidden=false;
}
$("kurz").classList.toggle("zu",!!lsLesen("wz_kurzZu",false));
$("kurz").onclick=()=>{const zu=!$("kurz").classList.contains("zu");$("kurz").classList.toggle("zu",zu);lsSchreiben("wz_kurzZu",zu);};

// ---------- Diagramm Kraft über Weg ----------
const cv=$("plot"),ctx=cv.getContext("2d");
const abKontakt=()=>$("xmodus").value==="kontakt";
function versatz(p){return abKontakt()?(p.k.kontakt&&isFinite(p.k.kontaktMm)?p.k.kontaktMm:NaN):0;}
function liveVersatz(){
  if(!abKontakt())return 0;
  const grenze=S?S.par.kontakt:1;
  for(let i=0;i<live.y.length;i++)if(live.y[i]>=grenze)return live.s+i*live.d;
  return NaN;
}
function aktuelleKurven(){
  let ks=[...pressen.values()].filter(p=>p.x&&!versteckt.has(p.k.nr)&&($("abbr").checked||p.k.status===0)&&isFinite(versatz(p)));
  ks.sort((a,b)=>a.k.nr-b.k.nr);
  const z=+$("zeige").value;
  if(z>0)ks=ks.slice(-z);
  const a=pressen.get(auswahl);
  if(a&&a.x&&isFinite(versatz(a))&&!ks.includes(a))ks.push(a);
  return ks;
}
function schrittweite(span,n){const r=span/n,p=Math.pow(10,Math.floor(Math.log10(r))),m=r/p;return (m<1.5?1:m<3.5?2:m<7.5?5:10)*p;}
function bereich(ks){
  let x0=Infinity,x1=-Infinity,y0=0,y1=-Infinity;
  const nk=$("nurKontakt").checked;
  const mit=(x,y,off,kx)=>{for(let i=0;i<x.length;i++){if(!isFinite(y[i]))continue;const xv=x[i]-off;
    if(nk&&isFinite(kx)&&xv<kx-0.3)continue;
    if(xv<x0)x0=xv;if(xv>x1)x1=xv;const v=y[i]*E.f;if(v>y1)y1=v;if(v<y0)y0=v;}};
  for(const p of ks){const off=versatz(p);mit(p.x,p.y,off,p.k.kontakt?p.k.kontaktMm-off:NaN);}
  const lv=liveVersatz();
  if(live.lauf&&live.y.length&&isFinite(lv)){const x=live.y.map((_,i)=>live.s+i*live.d);mit(x,live.y,lv,NaN);}
  if(!isFinite(x0)){x0=-1;x1=0.2;y1=10;}
  if(x1-x0<0.01){x0-=0.05;x1+=0.05;}
  if(!(y1>y0))y1=y0+10;
  const px=(x1-x0)*0.03;
  return {x0:x0-px,x1:x1+px,y0:y0,y1:y1+(y1-y0)*0.08};
}
function achsen(c,g,xEinheit,yEinheit,xStellen){
  const st=getComputedStyle(document.documentElement),{L,T,pw,ph,x0,x1,y0,y1}=g;
  const X=v=>L+(v-x0)/(x1-x0)*pw,Y=v=>T+ph-(v-y0)/(y1-y0)*ph;
  c.font="12px system-ui,sans-serif";c.fillStyle=st.getPropertyValue("--leise");c.strokeStyle=st.getPropertyValue("--gitter");c.lineWidth=1;
  const sx=schrittweite(x1-x0,8),sy=schrittweite(y1-y0,5);
  c.textAlign="center";c.textBaseline="top";
  for(let v=Math.ceil(x0/sx)*sx;v<=x1;v+=sx){const x=X(v);c.beginPath();c.moveTo(x,T);c.lineTo(x,T+ph);c.stroke();
    c.fillText(fmt(v,xStellen!==undefined?xStellen:(sx<0.01?3:sx<0.1?2:sx<1?1:0)),x,T+ph+5);}
  c.textAlign="right";c.textBaseline="middle";
  for(let v=Math.ceil(y0/sy)*sy;v<=y1;v+=sy){const y=Y(v);c.beginPath();c.moveTo(L,y);c.lineTo(L+pw,y);c.stroke();
    c.fillText(fmt(v,sy<0.1?2:sy<1?1:0),L-6,y);}
  c.textAlign="center";c.textBaseline="bottom";c.fillText(xEinheit,L+pw/2,c.canvas.clientHeight-1);
  c.save();c.translate(13,T+ph/2);c.rotate(-Math.PI/2);c.textBaseline="top";c.fillText(yEinheit,0,0);c.restore();
  return {X,Y,st};
}
function leinwand(c){
  const el=c.canvas,dpr=window.devicePixelRatio||1,w=el.clientWidth,h=el.clientHeight;
  if(el.width!==Math.round(w*dpr)||el.height!==Math.round(h*dpr)){el.width=Math.round(w*dpr);el.height=Math.round(h*dpr);}
  c.setTransform(dpr,0,0,dpr,0,0);c.clearRect(0,0,w,h);
  return {w,h};
}
function zeichnen(){
  requestAnimationFrame(zeichnen);
  if(!neuZeichnen)return;
  neuZeichnen=false;
  trendZeichnen();
  const {w,h}=leinwand(ctx);
  const L=62,R=14,T0=12,B=38,pw=w-L-R,ph=h-T0-B;
  const ks=aktuelleKurven();
  const b=ansicht||bereich(ks);
  geo={L,T:T0,pw,ph,...b};
  const {X,Y,st}=achsen(ctx,geo,abKontakt()?T("Weg ab Kontakt [mm]"):T("Weg [mm]  (0 = Werkzeug-Endpunkt, + = tiefer)"),T("Kraft [{0}]",E.n));

  ctx.save();ctx.beginPath();ctx.rect(L,T0,pw,ph);ctx.clip();
  const a=pressen.get(auswahl);
  // Plateaufenster der Auswahl schattieren
  if(a&&a.x&&isFinite(a.a.p0)){
    const off=versatz(a)-(a.k.kontaktMm||0);           // Weg ab Kontakt -> Anzeige
    ctx.fillStyle="rgba("+st.getPropertyValue("--ar")+",.07)";
    ctx.fillRect(X(a.a.p0-off),T0,X(a.a.p1-off)-X(a.a.p0-off),ph);
  }
  if(b.x0<0&&b.x1>0){ctx.setLineDash([5,5]);ctx.strokeStyle=st.getPropertyValue("--linie");ctx.beginPath();ctx.moveTo(X(0),T0);ctx.lineTo(X(0),T0+ph);ctx.stroke();ctx.setLineDash([]);}
  const linie=(x,y,off,farbe,breite,strich)=>{
    ctx.strokeStyle=farbe;ctx.lineWidth=breite;ctx.setLineDash(strich?[6,4]:[]);ctx.beginPath();let auf=false;
    for(let i=0;i<y.length;i++){if(!isFinite(y[i]))continue;const px=X(x[i]-off),py=Y(y[i]*E.f);if(auf)ctx.lineTo(px,py);else{ctx.moveTo(px,py);auf=true;}}
    ctx.stroke();ctx.setLineDash([]);};
  const neueste=Math.max(0,...ks.map(p=>p.k.nr));
  const ar=st.getPropertyValue("--ar"),wr=st.getPropertyValue("--wr"),fr=st.getPropertyValue("--fr"),hell=st.getPropertyValue("--hell");
  for(const p of ks){
    if(p.k.nr===auswahl)continue;
    const farbe="rgba("+(p.k.status?fr+",.55":p.a.riss?wr+",.8":p.k.nr===neueste?ar+",.85":ar+",.28")+")";
    linie(p.x,p.y,versatz(p),farbe,p.k.nr===neueste?1.6:1.1,!!p.k.status);
  }
  if(a&&a.x&&isFinite(versatz(a))){
    const off=versatz(a),ok=a.k.kontaktMm||0,sx=v=>X(v+ok-off);   // sx: Weg ab Kontakt -> Pixel
    const ax=a.rx||a.x,ay=a.ry||a.y;                                  // Auswahl in voller Auflösung
    linie(ax,ay,off,hell,2.4,false);
    const aa=a.a;
    if(isFinite(aa.steigung)){                                       // Plateau-Gerade
      ctx.strokeStyle=st.getPropertyValue("--akzent");ctx.lineWidth=1.5;ctx.setLineDash([4,3]);ctx.beginPath();
      ctx.moveTo(sx(aa.p0),Y((aa.achse+aa.steigung*aa.p0)*E.f));ctx.lineTo(sx(aa.p1),Y((aa.achse+aa.steigung*aa.p1)*E.f));
      ctx.stroke();ctx.setLineDash([]);
    }
    ctx.fillStyle=hell;
    if(isFinite(aa.fuegeN)){ctx.beginPath();ctx.arc(sx(aa.fuegeMm),Y(aa.fuegeN*E.f),4.5,0,7);ctx.fill();}
    if(isFinite(aa.spitzeN)){const px=sx(aa.spitzeMm),py=Y(aa.spitzeN*E.f);
      ctx.beginPath();ctx.moveTo(px,py-9);ctx.lineTo(px-5,py-2);ctx.lineTo(px+5,py-2);ctx.closePath();ctx.fill();}
    if(aa.abfallN>0&&isFinite(aa.abfallMm)){                         // ✕ am Beginn des größten Kraftabfalls
      let fy=NaN;
      for(let i=0;i<ax.length;i++)if(isFinite(ay[i])&&ax[i]-ok>=aa.abfallMm){fy=ay[i];break;}
      if(isFinite(fy)){const px=sx(aa.abfallMm),qy=Y(fy*E.f);
        ctx.strokeStyle=aa.riss?st.getPropertyValue("--fehler"):hell;ctx.lineWidth=2;
        ctx.beginPath();ctx.moveTo(px-5,qy-5);ctx.lineTo(px+5,qy+5);ctx.moveTo(px+5,qy-5);ctx.lineTo(px-5,qy+5);ctx.stroke();}
    }
  }
  const lv=liveVersatz();
  if(live.lauf&&live.y.length&&isFinite(lv)){const lx=live.y.map((_,i)=>live.s+i*live.d);linie(lx,gleit(lx,live.y,GLAETT_MM),lv,st.getPropertyValue("--warn"),2.2,false);}
  ctx.restore();
}
function mausWerte(e,g,el){const r=el.getBoundingClientRect();const mx=e.clientX-r.left,my=e.clientY-r.top;
  return {mx,my,x:g.x0+(mx-g.L)/g.pw*(g.x1-g.x0),y:g.y1-(my-g.T)/g.ph*(g.y1-g.y0)};}
let zieh=null;
cv.addEventListener("wheel",e=>{
  if(!geo)return;e.preventDefault();
  const m=mausWerte(e,geo,cv),k=e.deltaY>0?1.25:0.8,b=geo;
  ansicht={x0:m.x-(m.x-b.x0)*k,x1:m.x+(b.x1-m.x)*k,y0:e.shiftKey?b.y0:m.y-(m.y-b.y0)*k,y1:e.shiftKey?b.y1:m.y+(b.y1-m.y)*k};
  neuZeichnen=true;
},{passive:false});
cv.addEventListener("pointerdown",e=>{if(!geo||e.button!==0)return;zieh={cx:e.clientX,cy:e.clientY,b:{x0:geo.x0,x1:geo.x1,y0:geo.y0,y1:geo.y1},weg:0};cv.setPointerCapture(e.pointerId);});
cv.addEventListener("pointermove",e=>{
  if(geo){const m=mausWerte(e,geo,cv);$("maus").textContent=T("Weg {0} mm · Kraft {1}",fmt(m.x,3),fmt(m.y,E.d)+" "+E.n);}
  if(!zieh)return;
  const dx=e.clientX-zieh.cx,dy=e.clientY-zieh.cy;zieh.weg=Math.max(zieh.weg,Math.abs(dx)+Math.abs(dy));
  if(zieh.weg<4)return;
  const b=zieh.b,fx=(b.x1-b.x0)/geo.pw,fy=(b.y1-b.y0)/geo.ph;
  ansicht={x0:b.x0-dx*fx,x1:b.x1-dx*fx,y0:b.y0+dy*fy,y1:b.y1+dy*fy};neuZeichnen=true;
});
cv.addEventListener("pointerup",e=>{
  const z=zieh;zieh=null;
  if(!z||z.weg>=4||!geo)return;
  // Klick: nächstgelegene Kurve wählen
  const m=mausWerte(e,geo,cv),X=v=>geo.L+(v-geo.x0)/(geo.x1-geo.x0)*geo.pw,Y=v=>geo.T+geo.ph-(v-geo.y0)/(geo.y1-geo.y0)*geo.ph;
  let best=0,bd=16;
  for(const p of aktuelleKurven()){
    const off=versatz(p);
    for(let i=0;i<p.x.length;i++){if(!isFinite(p.y[i]))continue;const px=X(p.x[i]-off);if(Math.abs(px-m.mx)>bd)continue;
      const d=Math.hypot(px-m.mx,Y(p.y[i]*E.f)-m.my);if(d<bd){bd=d;best=p.k.nr;}}
  }
  if(best)waehlen(best);
});
cv.addEventListener("pointercancel",()=>{zieh=null;});
cv.addEventListener("dblclick",()=>{ansicht=null;neuZeichnen=true;});
window.addEventListener("resize",()=>{neuZeichnen=true;});
if(window.ResizeObserver)new ResizeObserver(()=>{neuZeichnen=true;}).observe($("plotfl"));

// ---------- Farbschema (gilt für beide Seiten, im Browser gespeichert) ----------
function themaSetzen(t){
  document.documentElement.dataset.thema=t;
  try{localStorage.setItem("wz_thema",t);}catch(e){}
  document.querySelectorAll(".themen button").forEach(b=>b.classList.toggle("aktiv",b.dataset.t===t));
}
document.querySelectorAll(".themen button").forEach(b=>b.onclick=()=>{themaSetzen(b.dataset.t);neuZeichnen=true;});
themaSetzen(document.documentElement.dataset.thema||"");

// ---------- Modus Bedienen / Einrichten (gilt für beide Seiten, im Browser gespeichert) ----------
function modusSetzen(m){
  document.documentElement.dataset.modus=m;lsSchreiben("wz_modus",m);
  document.querySelectorAll(".modus button").forEach(b=>b.classList.toggle("aktiv",b.dataset.m===m));
  neuZeichnen=true;
}
document.querySelectorAll(".modus button").forEach(b=>b.onclick=()=>modusSetzen(b.dataset.m));
modusSetzen(document.documentElement.dataset.modus==="einrichten"?"einrichten":"bedienen");

// ---------- Menü links ein-/ausklappen (mehr Platz für das Diagramm) ----------
function menueEng(eng){document.body.classList.toggle("eng",eng);$("einklappen").textContent=eng?"»":"«";neuZeichnen=true;}
menueEng(!!lsLesen("wz_menueEng",false));
$("einklappen").onclick=()=>{const eng=!document.body.classList.contains("eng");lsSchreiben("wz_menueEng",eng);menueEng(eng);};

// ---------- Trend über die Pressungen ----------
const tcv=$("trend"),tctx=tcv.getContext("2d");
function trendAuswahl(){
  const sel=$("trendWert"),alt=sel.value||lsLesen("wz_trend","fuege");
  sel.innerHTML=kennListe().map(m=>'<option value="'+m[0]+'">'+esc(m[1])+'</option>').join("");
  sel.value=[...sel.options].some(o=>o.value===alt)?alt:"fuege";
}
function trendPunkte(){
  const m=kennListe().find(x=>x[0]===$("trendWert").value);
  if(!m)return {m:null,pk:[]};
  const pk=[...pressen.values()].filter(p=>p.k.status===0&&p.x).map(p=>({nr:p.k.nr,v:kwert(p,m),riss:p.a.riss}))
    .filter(q=>isFinite(q.v)).sort((a,b)=>a.nr-b.nr);
  return {m,pk};
}
function trendZeichnen(){
  const {w,h}=leinwand(tctx);
  const {m,pk}=trendPunkte();
  tgeo=null;
  if(!m||!pk.length){tctx.fillStyle=getComputedStyle(document.documentElement).getPropertyValue("--leise");tctx.font="13px system-ui,sans-serif";tctx.fillText(T("noch keine ausgewerteten Pressungen"),70,40);return;}
  const n=pk.length,mw=pk.reduce((a,q)=>a+q.v,0)/n,s=n>1?Math.sqrt(pk.reduce((a,q)=>a+(q.v-mw)*(q.v-mw),0)/(n-1)):0;
  let y0=Math.min(...pk.map(q=>q.v)),y1=Math.max(...pk.map(q=>q.v));
  if(n>=3){y0=Math.min(y0,mw-3*s);y1=Math.max(y1,mw+3*s);}
  if(!(y1>y0)){y0-=1;y1+=1;}
  const pad=(y1-y0)*0.08;
  const L=62,R=14,T0=10,B=34;
  tgeo={L,T:T0,pw:w-L-R,ph:h-T0-B,x0:pk[0].nr-0.5,x1:pk[n-1].nr+0.5,y0:y0-pad,y1:y1+pad,pk};
  const {X,Y,st}=achsen(tctx,tgeo,T("Pressung Nr."),m[1]+" ["+keinheit(m[2])+"]",0);
  tctx.save();tctx.beginPath();tctx.rect(L,T0,tgeo.pw,tgeo.ph);tctx.clip();
  const hl=(v,strich)=>{tctx.strokeStyle=st.getPropertyValue("--akzent");tctx.lineWidth=1;tctx.setLineDash(strich?[5,4]:[]);
    tctx.beginPath();tctx.moveTo(L,Y(v));tctx.lineTo(L+tgeo.pw,Y(v));tctx.stroke();tctx.setLineDash([]);};
  hl(mw,false);
  if(n>=3&&s>0){hl(mw+3*s,true);hl(mw-3*s,true);}
  tctx.strokeStyle="rgba("+st.getPropertyValue("--ar")+",.35)";tctx.lineWidth=1;tctx.beginPath();
  pk.forEach((q,i)=>{const x=X(q.nr),y=Y(q.v);if(i)tctx.lineTo(x,y);else tctx.moveTo(x,y);});tctx.stroke();
  for(const q of pk){
    const aus=n>=3&&s>0&&Math.abs(q.v-mw)>3*s;
    tctx.fillStyle=q.nr===auswahl?st.getPropertyValue("--hell"):(q.riss||aus)?st.getPropertyValue("--warn"):st.getPropertyValue("--akzent");
    tctx.beginPath();tctx.arc(X(q.nr),Y(q.v),q.nr===auswahl?5:3.2,0,7);tctx.fill();
  }
  tctx.restore();
  const d=kstellen(m);
  $("trendSub").textContent=T("Mittel")+" "+fmt(mw,d)+" · σ "+fmt(s,d+1)+" "+keinheit(m[2])+" · n = "+n+
    " · "+T("gestrichelt: ±3σ, orange: außerhalb ±3σ oder Rissverdacht · Klick: Pressung auswählen");
}
tcv.addEventListener("click",e=>{
  if(!tgeo)return;
  const m=mausWerte(e,tgeo,tcv);
  let best=0,bd=Infinity;
  for(const q of tgeo.pk){const d=Math.abs(q.nr-m.x);if(d<bd){bd=d;best=q.nr;}}
  if(best&&bd<Math.max(1,(tgeo.x1-tgeo.x0)/40))waehlen(best);
});

// ---------- Auswertung als CSV (Excel: Deutsch ; und Dezimalkomma, Englisch , und Dezimalpunkt) ----------
$("awCsv").onclick=()=>{
  const z=csvZ,ja=T("ja");
  const liste=kennListe(),{r}=kennStat();
  const zeilen=[];
  zeilen.push(csvZeile([T("Auswertung Sitzung"),sitId,sitName]));
  zeilen.push(csvZeile([T("Erstellt"),new Date().toLocaleString(LOC())]));
  zeilen.push(csvZeile([T("Krafteinheit"),E.n]));
  zeilen.push(csvZeile([T("Plateaufenster [% der Fügetiefe]"),AW.von+" - "+AW.bis]));
  zeilen.push(csvZeile([T("Anschlag ausgeschlossen [mm]"),z(AW.ende,2)]));
  zeilen.push(csvZeile([T("Rissverdacht ab Kraftabfall [%]"),z(AW.abfall,1),T("innerhalb [mm]"),z(AW.strecke,3)]));
  zeilen.push("");
  zeilen.push(csvZeile([T("Nr"),T("Zeit"),"Status",T("Simulation"),T("Rissverdacht"),T("Weg Fügekraft [mm]"),T("Weg Einführspitze [mm]"),T("Weg Kraftabfall [mm]")]
    .concat(liste.map(m=>m[1]+" ["+keinheit(m[2])+"]"))));
  for(const p of [...pressen.values()].sort((a,b)=>a.k.nr-b.k.nr)){
    const k=p.k,a=p.a;
    zeilen.push(csvZeile([k.nr,k.zeit?new Date(k.zeit*1000).toLocaleString(LOC()):"",T(STATUS[k.status]||"?"),k.sim?ja:"",a.riss?ja:"",
      z(a.fuegeMm,3),z(a.spitzeMm,3),z(a.abfallMm,3)].concat(liste.map(m=>z(kwert(p,m),kstellen(m)+1)))));
  }
  zeilen.push("");
  zeilen.push(csvZeile([T("Statistik (gültige Pressungen)"),"n",T("Mittel"),"Sigma","Sigma %","Min","Max",T("Spannweite")]));
  for(const m of liste){const x=r[m[0]];if(!x)continue;const d=kstellen(m)+1;
    zeilen.push(csvZeile([m[1]+" ["+keinheit(m[2])+"]",x.n,z(x.m,d),z(x.s,d+1),x.m&&m[2]!=="%"?z(100*x.s/Math.abs(x.m),2):"",z(x.min,d),z(x.max,d),z(x.max-x.min,d)]));}
  herunterladen(zeilen.join("\r\n"),T("sitzung")+"_"+String(sitId).padStart(4,"0")+"_"+T("auswertung")+".csv");
};

// ---------- Sitzungen ----------
let sitzListe=[],sitzAktiv=0;
async function listeLaden(){
  try{
    const d=await (await fetch("/sitzungen",{cache:"no-store"})).json();
    sitzListe=d.liste;sitzAktiv=d.aktiv;
    $("speicher").textContent=T("Speicher: {0} von {1} KB belegt",fmt(d.sp.b/1024,0),fmt(d.sp.g/1024,0));
    $("speicherBalken").style.width=(d.sp.g?Math.min(100,100*d.sp.b/d.sp.g):0)+"%";
    let h='<thead><tr><th>'+T("Nr")+'</th><th>Name</th><th>'+T("Erstellt")+'</th><th>'+T("Pressungen")+'</th><th>'+T("Notiz")+'</th><th></th></tr></thead><tbody>';
    for(const s of d.liste.slice().reverse()){
      const akt=s.id===d.aktiv;
      h+='<tr'+(akt?' class="sel"':'')+'><td>'+s.id+'</td><td>'+(akt?'<b>'+esc(s.name)+'</b> ('+T("aktiv")+')':esc(s.name))+'</td><td>'+esc(s.erst)+
        '</td><td>'+s.n+'</td><td>'+esc(s.notiz)+'</td><td style="white-space:nowrap">'+
        (akt?'':'<button class="sendet" data-a="wahl" data-id="'+s.id+'">'+T("Öffnen")+'</button> ')+
        '<a class="chip" href="/sitzung/export?id='+s.id+'&f=csv&e='+$("einheit").value+'" download>CSV</a> '+
        '<a class="chip" href="/sitzung/export?id='+s.id+'&f=json" download>JSON</a> '+
        '<button class="einr sendet" data-a="aendern" data-id="'+s.id+'" data-name="'+esc(s.name)+'" data-notiz="'+esc(s.notiz)+'">'+T("Umbenennen")+'</button> '+
        '<button class="einr sendet" data-a="loeschen" data-id="'+s.id+'" data-name="'+esc(s.name)+'" data-n="'+s.n+'">'+T("Löschen")+'</button></td></tr>';
    }
    $("liste").innerHTML=h+'</tbody>';
    $("liste").querySelectorAll("button[data-a]").forEach(b=>b.onclick=async e=>{
      e.stopPropagation();
      const a=b.dataset.a,id=b.dataset.id;
      if(a==="wahl")aktion({a,id});
      else if(a==="loeschen"){if(confirm(T("Sitzung {0} „{1}“ mit {2} Pressungen endgültig löschen?",id,b.dataset.name,b.dataset.n)+"\n"+T("Vorher exportieren!")))aktion({a,id});}
      else if(a==="aendern"){$("umbName").value=b.dataset.name;$("umbNotiz").value=b.dataset.notiz;$("umbDlg").dataset.id=id;
        $("umbDlg").showModal();$("umbName").focus();}
    });
  }catch(e){}
}
// Umbenennen
$("umbZu").onclick=()=>$("umbDlg").close();
$("umbOk").onclick=async()=>{
  const name=$("umbName").value.trim();
  if(!name){$("umbName").focus();return;}
  if(await aktion({a:"aendern",id:$("umbDlg").dataset.id,name,notiz:$("umbNotiz").value.trim()}))$("umbDlg").close();
};
// Schritt 3: Sitzung wählen oder anlegen, ohne die Seite zu verlassen
function sitzDialog(){
  const heute=new Date().toLocaleDateString("sv-SE");                 // JJJJ-MM-TT
  if(!$("dlgName").value)$("dlgName").value=T("Charge {0}",heute);
  const l=sitzListe.slice(-6).reverse();
  $("dlgListe").innerHTML=l.length?l.map(s=>'<button class="sendet" data-id="'+s.id+'"'+(s.id===sitzAktiv?' disabled':'')+'>'+esc(s.name)+
    '<span>'+(s.id===sitzAktiv?T("aktiv")+' · ':'')+T("{0} Pressungen",s.n)+' · '+esc(s.erst)+'</span></button>').join(""):'<div class="sub">'+T("noch keine Sitzungen")+'</div>';
  $("dlgListe").querySelectorAll("button").forEach(b=>b.onclick=async()=>{if(await aktion({a:"wahl",id:b.dataset.id}))$("sitzDlg").close();});
  $("sitzDlg").showModal();$("dlgName").select();
}
$("dlgNeu").onclick=async()=>{
  const name=$("dlgName").value.trim();
  if(!name){$("dlgName").focus();return;}
  if(await aktion({a:"neu",name,notiz:$("dlgNotiz").value.trim()})){$("dlgName").value="";$("dlgNotiz").value="";$("sitzDlg").close();}
};
$("dlgName").onkeydown=e=>{if(e.key==="Enter")$("dlgNeu").click();};
$("dlgZu").onclick=()=>$("sitzDlg").close();
$("dlgAlle").onclick=()=>{$("sitzDlg").close();$("sitzungen").scrollIntoView({behavior:"smooth"});};
$("neu").onclick=async()=>{
  const name=$("neuName").value.trim();
  if(!name){$("neuName").focus();return;}
  if(await aktion({a:"neu",name,notiz:$("neuNotiz").value.trim()})){$("neuName").value="";$("neuNotiz").value="";}
};
$("schliessen").onclick=()=>aktion({a:"zu"});

// ---------- Simulationsfenster: Parameter des Kraftmodells (Pico: nur RAM, Browser merkt sie sich) ----------
// [Feld, Parameter, Pico-Einheit je Anzeigeeinheit, Stellen]
const SIMFELD=[["sWerkzeug","werkzeug",1,1],["sWeg","weg",1,2],["sFmin","fmin",1000,1],["sFmax","fmax",1000,1],
  ["sRauschen","rauschen",1,0],["sRiss","riss",1,1],["sSpitze","spitze",1,0]];
const SIM_STANDARD={werkzeug:15,weg:3,fmin:25000,fmax:35000,rauschen:30,riss:8.3,spitze:12};
async function simSenden(w){                                  // nur geänderte Werte schicken (Pico-Einheiten)
  const ist=S&&S.simp?S.simp:{};
  for(const [,k] of SIMFELD){
    if(!isFinite(w[k]))continue;
    if(isFinite(ist[k])&&Math.abs(ist[k]-w[k])<1e-3)continue;
    await befehl("sim "+k+" "+w[k]);
  }
}
function simWerte(){                                          // Werte aus dem Fenster (Pico-Einheiten)
  const w={};
  for(const [id,k,f] of SIMFELD){const v=parseFloat(String($(id).value).replace(",","."));if(isFinite(v))w[k]=Math.round(v*f*1000)/1000;}
  return w;
}
function simFuellen(w){
  for(const [id,k,f,d] of SIMFELD)if(isFinite(w[k]))$(id).value=+(w[k]/f).toFixed(d);
  if(S)$("sVpress").value=S.par.vpress;
  simInfo();
}
function simInfo(){
  if(!S){$("simInfo").textContent="";return;}
  const w=simWerte(),v=parseFloat(String($("sVpress").value).replace(",","."))||S.par.vpress,an=S.par.anfahrt;
  const dauer=(an+0.05)/v,proben=dauer*S.rate;
  let t=T("Pressweg {0} mm (Anfahrpunkt, max. {1} mm) → ca. {2} s und {3} Messwerte bei {4} SPS.",
    fmt(an,2),fmt(S.simp?S.simp.wegmax:5,1),fmt(dauer,1),fmt(proben,0),fmt(S.rate,0));
  if(w.weg>an*0.98)t+=" "+T("Hinweis: Fügeweg größer als der Anfahrpunkt – das Werkstück wird auf {0} mm gekürzt.",fmt(an*0.98,2));
  if(w.fmax>S.par.fmax/0.00980665)t+=" "+T("Hinweis: Spitzenkraft über der Maximalkraft – die Pressung endet früh.");
  $("simInfo").textContent=t;
}
$("simOeffnen").onclick=()=>{
  // laufende Simulation: Werte vom Pico (die gelten); sonst die im Browser gemerkten
  const ls=lsLesen("wz_sim",null);
  simFuellen(Object.assign({},SIM_STANDARD,S&&S.simp?S.simp:{},S&&S.sim?{}:(ls||{})));
  $("simDlg").showModal();
};
$("simDlg").addEventListener("input",simInfo);
$("simStandard").onclick=()=>simFuellen(SIM_STANDARD);
$("simZu").onclick=()=>$("simDlg").close();
$("simSetzen").onclick=async()=>{
  const w=simWerte();
  lsSchreiben("wz_sim",w);
  await simSenden(w);
  const v=parseFloat(String($("sVpress").value).replace(",","."));
  if(isFinite(v)&&S&&Math.abs(v-S.par.vpress)>1e-4)await befehl("mp vpress "+v);
  $("simDlg").close();
};
$("simAus").onclick=()=>befehl("sim aus");
$("sim").onchange=async e=>{
  if(e.target.checked){const ls=lsLesen("wz_sim",null);if(ls)await simSenden(ls);befehl("sim an");}
  else befehl("sim aus");
};

// ---------- Parameter ----------
// [Feld, mp-Name, Wert aus S.par, Anzeige -> Pico-Einheit]; Kräfte: Anzeige in der gewählten Einheit, Pico in Gramm
const G_PRO_N=1/0.00980665;
const PFELD=[["pAnfahrt","anfahrt",p=>p.anfahrt,v=>v],["pVpress","vpress",p=>p.vpress,v=>v],["pHalt","halt",p=>p.halt,v=>v],
  ["pFmax","fmax",p=>+kr(p.fmax).toFixed(E.d),v=>Math.round(v/E.f*G_PRO_N)],
  ["pKontakt","kontakt",p=>+kr(p.kontakt).toFixed(E.d+1),v=>Math.round(v/E.f*G_PRO_N)]];
function parameterZeigen(neuEinheit){
  const p=S.par;
  document.querySelectorAll(".fe").forEach(e=>{e.textContent=E.n;});
  for(const [id,,wert] of PFELD){
    const el=$(id);
    if(neuEinheit){el.classList.remove("geaendert");}
    if(document.activeElement!==el&&!el.classList.contains("geaendert"))el.value=wert(p);
  }
  const offen=PFELD.some(([id])=>$(id).classList.contains("geaendert"));
  $("pInfo").innerHTML=(offen?'<span class="warn">'+T("Geänderte Werte (markiert) erst mit „Übernehmen“ senden.")+'</span> ':'')+
    T("Eilgang {0} mm/s. Übernommene Werte gelten bis zum Neustart; dauerhaft mit „Im Flash speichern“.",fmt(p.vfahrt,1));
}
for(const [id] of PFELD)$(id).oninput=e=>{e.target.classList.add("geaendert");if(S)parameterZeigen();};
$("pSetzen").onclick=async()=>{
  for(const [id,name,,umr] of PFELD){                       // nur geänderte Werte schicken
    const el=$(id);if(!el.classList.contains("geaendert"))continue;
    const v=parseFloat(String(el.value).replace(",","."));
    el.classList.remove("geaendert");
    if(isFinite(v))await befehl("mp "+name+" "+umr(v));
  }
  if(S)parameterZeigen();
};

// ---------- Protokoll ----------
const logEl=$("log");
function logText(t){
  if(!t)return;
  const unten=logEl.scrollTop+logEl.clientHeight>=logEl.scrollHeight-4;
  logEl.textContent+=t;
  if(logEl.textContent.length>60000)logEl.textContent=logEl.textContent.slice(-40000);
  if(unten)logEl.scrollTop=logEl.scrollHeight;
}
function logAufnehmen(neu){
  const t=logRest+neu,i=t.lastIndexOf("\n");
  if(i<0){logRest=t;return;}
  logRest=t.slice(i+1);
  const zeilen=t.slice(0,i+1).split("\n");
  rueckmeldung(zeilen);
  logText(zeilen.filter(z=>!z.startsWith("#CFG")).join("\n"));
}

// ---------- Sprache: statische Texte übersetzen, beim Umschalten alles neu aufbauen ----------
spracheStart(()=>{
  trendAuswahl();anzeigenAktualisieren();listeLaden();
  if(S){ablaufZeigen();parameterZeigen();leistenZeigen();}
  statusZeigen();
});

trendAuswahl();
listeLaden();
abfragen();
requestAnimationFrame(zeichnen);
</script>
</body></html>
)PRESSE";
