/*
 * webstil.h - gemeinsames Stylesheet der Weboberflaeche (/stil.css)
 * Dunkles Design mit Neongruen. Wird von der Messseite (webui.h) und der
 * Pressseite (presse.h) benutzt.
 */
#pragma once

static const char WEBUI_CSS[] = R"WEBSTIL(/* Dunkles Design mit Neongrün (nach Vorlage) */
:root{color-scheme:dark;--bg:#050705;--bg2:#0a0d0a;--karte:#0d110d;--karte2:#131a13;--text:#f3f7f3;--leise:#93a393;
--rand:#1b251b;--rand2:#2a3a2a;--akzent:#3ae23a;--akzent2:#18a818;--akzentText:#031003;--glow:rgba(58,226,58,.32);
--band:rgba(58,226,58,.13);--linie:rgba(200,255,200,.5);--ma:#3ae23a;--gitter:#152015;--warn:#ffb020;--fehler:#ff5c5c;--ok:#3ae23a;
--ar:58,226,58;--ar2:24,168,24;--wr:255,176,32;--fr:255,92,92;--hover:#5cf05c;--hell:#ffffff;
--stopp:#ff5c5c;--stoppHover:#ff7676;--stoppText:#fff;--sr:255,92,92;--fehlerText:#1a0000;
--menue:rgba(10,13,10,.85);--kopf:rgba(13,17,13,.78);--logBg:#030503;--logText:#cde6cd;--scroll:#1e2c1e}
/* Rot / Schwarz: Fehler und Abbrüche violett, STOPP hell, damit sie sich vom roten Akzent abheben */
:root[data-thema="rot"]{--bg:#070505;--bg2:#0d0909;--karte:#110c0c;--karte2:#1a1212;--text:#f7f1f1;--leise:#a89494;
--rand:#261a1a;--rand2:#3a2828;--akzent:#ff3b3b;--akzent2:#b81818;--akzentText:#140303;--glow:rgba(255,59,59,.32);
--band:rgba(255,59,59,.13);--linie:rgba(255,210,210,.5);--ma:#ff3b3b;--gitter:#201414;--warn:#ffc83d;--fehler:#c58cff;--ok:#6fdc8c;
--ar:255,59,59;--ar2:184,24,24;--wr:255,200,61;--fr:197,140,255;--hover:#ff6161;--hell:#ffffff;
--stopp:#f3eded;--stoppHover:#ffffff;--stoppText:#b00000;--sr:255,255,255;--fehlerText:#12001f;
--menue:rgba(13,9,9,.85);--kopf:rgba(17,12,12,.78);--logBg:#050303;--logText:#ecd2d2;--scroll:#3a2424}
/* Blau / Hell */
:root[data-thema="hell"]{color-scheme:light;--bg:#eef3f9;--bg2:#f6f9fc;--karte:#ffffff;--karte2:#f7faff;--text:#0f1d2e;--leise:#5b6c82;
--rand:#dde5ef;--rand2:#c5d2e2;--akzent:#1f6fe5;--akzent2:#1652b5;--akzentText:#ffffff;--glow:rgba(31,111,229,.2);
--band:rgba(31,111,229,.12);--linie:rgba(15,29,46,.45);--ma:#1f6fe5;--gitter:#e6ecf3;--warn:#9a5c00;--fehler:#d6343a;--ok:#1a8f4a;
--ar:31,111,229;--ar2:22,82,181;--wr:154,92,0;--fr:214,52,58;--hover:#3d85f0;--hell:#0f1d2e;
--stopp:#d6343a;--stoppHover:#e24a50;--stoppText:#fff;--sr:214,52,58;--fehlerText:#fff;
--menue:rgba(255,255,255,.88);--kopf:rgba(255,255,255,.85);--logBg:#f6f9fc;--logText:#1d2b3c;--scroll:#c5d2e2}
*{box-sizing:border-box}
html{background:var(--bg)}
body{margin:0;min-height:100vh;color:var(--text);font:14px/1.45 "Segoe UI Variable","Segoe UI",system-ui,Roboto,"Helvetica Neue",sans-serif;
background:radial-gradient(1100px 560px at 88% -12%,rgba(var(--ar),.15),transparent 62%),
radial-gradient(900px 520px at -12% 112%,rgba(var(--ar2),.10),transparent 60%),var(--bg);background-attachment:fixed}
main{max-width:1400px;margin:0 auto;padding:14px 16px 28px}
header{display:flex;flex-wrap:wrap;align-items:center;gap:10px 16px;justify-content:space-between;padding:10px 14px;
border:1px solid var(--rand);border-radius:16px;background:var(--kopf);backdrop-filter:blur(8px)}
.marke{display:flex;align-items:center;gap:11px}
.logo{color:var(--akzentText);width:32px;height:32px;border-radius:10px;background:linear-gradient(135deg,var(--akzent),var(--akzent2));
box-shadow:0 0 18px var(--glow);display:grid;place-items:center;flex:none}
h1{font-size:17px;margin:0;font-weight:750;letter-spacing:.2px}
h1 em{color:var(--akzent);font-style:italic}
h1 span{color:var(--leise);font-weight:500}
.status{font-size:13px;color:var(--leise);display:flex;flex-wrap:wrap;gap:6px;align-items:center}
.status b{color:var(--text)}
.chip{display:inline-flex;align-items:center;gap:6px;padding:3px 11px;border-radius:999px;border:1px solid var(--rand2);
background:var(--bg2);font-size:12px;white-space:nowrap}
.chip.warn{border-color:rgba(var(--wr),.45)}
.punkt{width:8px;height:8px;border-radius:50%;background:var(--akzent);box-shadow:0 0 9px var(--akzent)}
.punkt.aus{background:var(--fehler);box-shadow:0 0 9px var(--fehler)}
.karte{position:relative;overflow:hidden;background:linear-gradient(160deg,var(--karte2),var(--karte) 62%);
border:1px solid var(--rand);border-radius:18px;padding:14px 16px;margin-top:14px}
.karte.glow::before{content:"";position:absolute;width:280px;height:280px;right:-100px;top:-120px;border-radius:50%;
background:radial-gradient(circle,var(--glow),transparent 70%);pointer-events:none}
.karte > *{position:relative}
.titel{display:inline-block;font-size:11px;font-weight:650;letter-spacing:.9px;text-transform:uppercase;color:var(--akzent);
border:1px solid rgba(var(--ar),.35);background:rgba(var(--ar),.07);border-radius:999px;padding:2px 10px;margin-bottom:10px}
.oben{display:grid;grid-template-columns:minmax(240px,340px) 1fr;gap:0 14px}
.unten{display:grid;grid-template-columns:minmax(300px,480px) 1fr;gap:0 14px}
@media (max-width:820px){.oben,.unten{grid-template-columns:1fr}}
.wert{font-size:52px;font-weight:800;font-variant-numeric:tabular-nums;letter-spacing:-1px;line-height:1.1}
.einh{font-size:22px;font-weight:750;font-style:italic;color:var(--akzent);margin-left:8px}
.sub{color:var(--leise);font-size:13px;margin-top:4px}
.leiste{display:flex;flex-wrap:wrap;gap:10px 16px;align-items:center}
label{color:var(--leise);font-size:13px;display:flex;gap:7px;align-items:center}
select,input[type=number],input[type=text]{background:var(--bg2);color:var(--text);border:1px solid var(--rand2);
border-radius:10px;padding:5px 9px;font:inherit;outline:none}
select:focus,input:focus{border-color:var(--akzent);box-shadow:0 0 0 3px rgba(var(--ar),.18)}
input[type=number]{width:76px}
input[type=checkbox]{accent-color:var(--akzent);width:15px;height:15px;margin:0}
button{background:var(--bg2);color:var(--text);border:1px solid var(--rand2);border-radius:999px;padding:7px 17px;
font:inherit;font-weight:600;cursor:pointer;transition:border-color .15s,box-shadow .15s,background .15s}
button:hover{border-color:var(--akzent);box-shadow:0 0 14px rgba(var(--ar),.22)}
button:active{transform:translateY(1px)}
button.primaer{background:var(--akzent);color:var(--akzentText);border-color:var(--akzent);box-shadow:0 0 18px var(--glow)}
button.primaer:hover{background:var(--hover);border-color:var(--hover)}
.knoepfe{display:flex;flex-wrap:wrap;gap:8px;margin-top:14px}
#plot{width:100%;height:440px;display:block}
@media (max-width:820px){#plot{height:300px}.wert{font-size:42px}}
table{border-collapse:collapse;font-variant-numeric:tabular-nums;width:100%;margin-top:8px}
td,th{padding:4px 8px;text-align:right;border-bottom:1px solid var(--rand)}
th{color:var(--akzent);font-weight:650;font-size:12px}
td:first-child,th:first-child{text-align:left;color:var(--leise);font-weight:normal}
td b{color:var(--akzent)}
#log{height:280px;overflow:auto;white-space:pre-wrap;font:12px/1.42 ui-monospace,"Cascadia Mono",Consolas,monospace;
color:var(--logText);background:var(--logBg);border:1px solid var(--rand);border-radius:12px;padding:8px 10px;margin:0}
.warn{color:var(--warn)}.ok{color:var(--ok)}
::-webkit-scrollbar{width:10px;height:10px}
::-webkit-scrollbar-thumb{background:var(--scroll);border-radius:10px}
::-webkit-scrollbar-track{background:transparent}
:focus-visible{outline:2px solid var(--akzent);outline-offset:2px}

.kopfrechts{display:flex;flex-wrap:wrap;gap:6px;align-items:center}
[hidden]{display:none!important}
.yfest{display:flex;flex-wrap:wrap;gap:8px;align-items:center}
.yfest button{padding:4px 12px}
.plotkopf{display:flex;flex-wrap:wrap;gap:4px 14px;align-items:baseline;justify-content:space-between}
.plotkopf .titel{margin-bottom:6px}
.plotkopf .sub{margin:0 0 6px;font-size:12px}
#plot{cursor:ns-resize}
button:disabled{opacity:.4;cursor:not-allowed;box-shadow:none}
button.stopp{background:var(--stopp);border-color:var(--stopp);color:var(--stoppText);font-weight:800;letter-spacing:.5px;
padding:7px 22px;box-shadow:0 0 16px rgba(var(--sr),.35)}
button.stopp:hover{background:var(--stoppHover);border-color:var(--stoppHover);box-shadow:0 0 20px rgba(var(--sr),.5)}
.motraster{display:grid;grid-template-columns:repeat(auto-fit,minmax(240px,1fr));gap:12px}
.motwert{font-size:30px;font-weight:800;font-variant-numeric:tabular-nums;letter-spacing:-.5px}
.motkal{font-size:14px;line-height:1.5;margin-top:4px}
.motkal b{color:var(--akzent)}

/* Links (beide Seiten) */
a{color:var(--akzent);text-decoration:none}
a:hover{text-decoration:underline}
a.chip{color:var(--text);font-weight:600}
a.chip:hover{border-color:var(--akzent);box-shadow:0 0 12px rgba(var(--ar),.22);text-decoration:none}

/* Farbschema-Wahl (drei Punkte) */
.themen{display:flex;gap:7px;align-items:center}
.themen button{width:20px;height:20px;padding:0;border-radius:50%;border:2px solid var(--rand2);box-shadow:none}
.themen button[data-t=""]{background:linear-gradient(135deg,#3ae23a 50%,#050705 50%)}
.themen button[data-t="rot"]{background:linear-gradient(135deg,#ff3b3b 50%,#070505 50%)}
.themen button[data-t="hell"]{background:linear-gradient(135deg,#1f6fe5 50%,#eef3f9 50%)}
.themen button.aktiv{border-color:var(--text);box-shadow:0 0 0 2px var(--akzent)}
/* Farbfelder in Legenden */
.fl{display:inline-block;width:14px;height:3px;border-radius:2px;vertical-align:middle;margin:0 3px 0 2px}
/* Dialoge und Schrittlisten (beide Seiten) */
dialog{background:var(--karte);color:var(--text);border:1px solid var(--rand2);border-radius:14px;padding:16px 18px;
max-width:min(560px,calc(100vw - 32px));box-shadow:0 0 30px var(--glow)}
dialog::backdrop{background:rgba(0,0,0,.65)}
dialog input[type=number]{width:110px}
.awgitter{display:grid;grid-template-columns:auto auto;gap:6px 10px;align-items:center}
.awgitter label{justify-content:space-between}
.schritt{display:grid;grid-template-columns:30px 1fr;gap:4px 10px;padding:8px 0;border-bottom:1px solid var(--rand)}
.schritt:last-of-type{border-bottom:0}
.schritt .nr{width:26px;height:26px;border-radius:50%;display:grid;place-items:center;font-weight:800;font-size:13px;
border:1px solid var(--rand2);color:var(--leise)}
.schritt.ok .nr{background:var(--akzent);color:var(--akzentText);border-color:var(--akzent);box-shadow:0 0 10px var(--glow)}
.schritt b{font-size:14px}
.schritt .sub{margin:0}
.schritt .knoepfe{margin-top:6px}

/* Hinweisleisten oben: keine Verbindung, Simulation */
.banner{position:sticky;top:0;z-index:40;display:flex;flex-wrap:wrap;gap:6px 14px;align-items:center;justify-content:center;
padding:9px 16px;font-weight:700;text-align:center}
.banner.offline{background:var(--fehler);color:var(--fehlerText)}
.banner.sim{background:repeating-linear-gradient(135deg,rgb(var(--wr)) 0 14px,rgba(var(--wr),.82) 14px 28px);color:#1a1000}
.banner button{padding:3px 12px;font-size:13px}
/* Ohne Verbindung: Befehlsknöpfe sperren (STOPP bleibt bedienbar) */
body.offline [data-c]:not(.stopp),body.offline .sendet{opacity:.4;pointer-events:none}

/* STOPP-Leiste, solange der Antrieb fährt - immer sichtbar, auch wenn weiter unten gescrollt */
.stoppleiste{position:fixed;left:50%;bottom:16px;transform:translateX(-50%);z-index:50;display:flex;align-items:center;gap:14px;
padding:8px 8px 8px 18px;border:1px solid rgba(var(--sr),.6);border-radius:999px;background:var(--menue);backdrop-filter:blur(8px);
box-shadow:0 6px 30px rgba(0,0,0,.45);max-width:calc(100vw - 32px)}
.stoppleiste span{font-weight:650;white-space:nowrap;overflow:hidden;text-overflow:ellipsis;min-width:0}
.stoppleiste button.stopp{font-size:18px;padding:10px 30px;flex:none}
body.faehrt main{padding-bottom:96px}

/* Rückmeldungen zu Befehlen unten rechts */
.meldungen{position:fixed;right:16px;bottom:16px;z-index:45;display:flex;flex-direction:column;gap:6px;align-items:flex-end;
max-width:min(460px,calc(100vw - 32px));pointer-events:none}
body.faehrt .meldungen{bottom:92px}
.meldung{pointer-events:auto;padding:8px 12px;border:1px solid var(--rand2);border-left:4px solid var(--akzent);border-radius:10px;
background:var(--karte);color:var(--text);box-shadow:0 4px 18px rgba(0,0,0,.35);font-size:13px;white-space:pre-wrap;animation:rein .15s ease-out}
.meldung.warn{border-left-color:var(--warn)}
.meldung.fehler{border-left-color:var(--fehler)}
@keyframes rein{from{opacity:0;transform:translateY(6px)}}

/* Modus Bedienen / Einrichten: Einrichte-Elemente nur im Modus Einrichten */
:root[data-modus="bedienen"] .einr{display:none!important}
.modus,.sprachwahl{display:inline-flex;border:1px solid var(--rand2);border-radius:999px;padding:2px;background:var(--bg2)}
.modus button,.sprachwahl button{border:0;padding:3px 11px;font-size:12px;box-shadow:none;background:transparent;color:var(--leise)}
.modus button.aktiv,.sprachwahl button.aktiv{background:var(--akzent);color:var(--akzentText)}
/* Begriffe mit Erklärung (title) */
dt[title],th[title],td[title],label[title]{text-decoration:underline dotted var(--rand2);text-underline-offset:3px;cursor:help}
/* geänderter, noch nicht übernommener Wert */
input.geaendert{border-color:var(--warn);box-shadow:0 0 0 3px rgba(var(--wr),.18)}

/* Touch (Tablet an der Maschine): größere Bedienflächen */
@media (pointer:coarse){
  button{min-height:40px}
  td button,td .chip{min-height:36px;padding:6px 12px}
  select,input[type=number],input[type=text]{min-height:40px}
  input[type=checkbox]{width:22px;height:22px}
  .themen button{width:30px;height:30px;min-height:0}
  .modus button,.sprachwahl button{min-height:34px}
}
)WEBSTIL";
