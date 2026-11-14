/*
 * webstil.h - gemeinsames Stylesheet der Weboberflaeche (/stil.css)
 * Dunkles Design mit Neongruen. Wird von der Messseite (webui.h) und der
 * Pressseite (presse.h) benutzt.
 */
#pragma once

static const char WEBUI_CSS[] = R"WEBSTIL(/* Dunkles Design mit Neongrün (nach Vorlage) */
:root{color-scheme:dark;--bg:#050705;--bg2:#0a0d0a;--karte:#0d110d;--karte2:#131a13;--text:#f3f7f3;--leise:#93a393;
--rand:#1b251b;--rand2:#2a3a2a;--akzent:#3ae23a;--akzent2:#18a818;--akzentText:#031003;--glow:rgba(58,226,58,.32);
--band:rgba(58,226,58,.13);--linie:rgba(200,255,200,.5);--ma:#3ae23a;--gitter:#152015;--warn:#ffb020;--fehler:#ff5c5c;--ok:#3ae23a}
*{box-sizing:border-box}
html{background:var(--bg)}
body{margin:0;min-height:100vh;color:var(--text);font:14px/1.45 "Segoe UI Variable","Segoe UI",system-ui,Roboto,"Helvetica Neue",sans-serif;
background:radial-gradient(1100px 560px at 88% -12%,rgba(58,226,58,.15),transparent 62%),
radial-gradient(900px 520px at -12% 112%,rgba(24,168,24,.10),transparent 60%),var(--bg);background-attachment:fixed}
main{max-width:1400px;margin:0 auto;padding:14px 16px 28px}
header{display:flex;flex-wrap:wrap;align-items:center;gap:10px 16px;justify-content:space-between;padding:10px 14px;
border:1px solid var(--rand);border-radius:16px;background:rgba(13,17,13,.78);backdrop-filter:blur(8px)}
.marke{display:flex;align-items:center;gap:11px}
.logo{width:32px;height:32px;border-radius:10px;background:linear-gradient(135deg,var(--akzent),var(--akzent2));
box-shadow:0 0 18px var(--glow);display:grid;place-items:center;flex:none}
h1{font-size:17px;margin:0;font-weight:750;letter-spacing:.2px}
h1 em{color:var(--akzent);font-style:italic}
h1 span{color:var(--leise);font-weight:500}
.status{font-size:13px;color:var(--leise);display:flex;flex-wrap:wrap;gap:6px;align-items:center}
.status b{color:var(--text)}
.chip{display:inline-flex;align-items:center;gap:6px;padding:3px 11px;border-radius:999px;border:1px solid var(--rand2);
background:var(--bg2);font-size:12px;white-space:nowrap}
.chip.warn{border-color:rgba(255,176,32,.45)}
.punkt{width:8px;height:8px;border-radius:50%;background:var(--akzent);box-shadow:0 0 9px var(--akzent)}
.punkt.aus{background:var(--fehler);box-shadow:0 0 9px var(--fehler)}
.karte{position:relative;overflow:hidden;background:linear-gradient(160deg,var(--karte2),var(--karte) 62%);
border:1px solid var(--rand);border-radius:18px;padding:14px 16px;margin-top:14px}
.karte.glow::before{content:"";position:absolute;width:280px;height:280px;right:-100px;top:-120px;border-radius:50%;
background:radial-gradient(circle,var(--glow),transparent 70%);pointer-events:none}
.karte > *{position:relative}
.titel{display:inline-block;font-size:11px;font-weight:650;letter-spacing:.9px;text-transform:uppercase;color:var(--akzent);
border:1px solid rgba(58,226,58,.35);background:rgba(58,226,58,.07);border-radius:999px;padding:2px 10px;margin-bottom:10px}
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
select:focus,input:focus{border-color:var(--akzent);box-shadow:0 0 0 3px rgba(58,226,58,.18)}
input[type=number]{width:76px}
input[type=checkbox]{accent-color:var(--akzent);width:15px;height:15px;margin:0}
button{background:var(--bg2);color:var(--text);border:1px solid var(--rand2);border-radius:999px;padding:7px 17px;
font:inherit;font-weight:600;cursor:pointer;transition:border-color .15s,box-shadow .15s,background .15s}
button:hover{border-color:var(--akzent);box-shadow:0 0 14px rgba(58,226,58,.22)}
button:active{transform:translateY(1px)}
button.primaer{background:var(--akzent);color:var(--akzentText);border-color:var(--akzent);box-shadow:0 0 18px var(--glow)}
button.primaer:hover{background:#5cf05c}
.knoepfe{display:flex;flex-wrap:wrap;gap:8px;margin-top:14px}
#plot{width:100%;height:440px;display:block}
@media (max-width:820px){#plot{height:300px}.wert{font-size:42px}}
table{border-collapse:collapse;font-variant-numeric:tabular-nums;width:100%;margin-top:8px}
td,th{padding:4px 8px;text-align:right;border-bottom:1px solid var(--rand)}
th{color:var(--akzent);font-weight:650;font-size:12px}
td:first-child,th:first-child{text-align:left;color:var(--leise);font-weight:normal}
td b{color:var(--akzent)}
#log{height:280px;overflow:auto;white-space:pre-wrap;font:12px/1.42 ui-monospace,"Cascadia Mono",Consolas,monospace;
color:#cde6cd;background:#030503;border:1px solid var(--rand);border-radius:12px;padding:8px 10px;margin:0}
.warn{color:var(--warn)}.ok{color:var(--ok)}
::-webkit-scrollbar{width:10px;height:10px}
::-webkit-scrollbar-thumb{background:#1e2c1e;border-radius:10px}
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
button.stopp{background:var(--fehler);border-color:var(--fehler);color:#fff;font-weight:800;letter-spacing:.5px;
padding:7px 22px;box-shadow:0 0 16px rgba(255,92,92,.35)}
button.stopp:hover{background:#ff7676;border-color:#ff7676;box-shadow:0 0 20px rgba(255,92,92,.5)}
.motraster{display:grid;grid-template-columns:repeat(auto-fit,minmax(240px,1fr));gap:12px}
.motwert{font-size:30px;font-weight:800;font-variant-numeric:tabular-nums;letter-spacing:-.5px}
.motkal{font-size:14px;line-height:1.5;margin-top:4px}
.motkal b{color:var(--akzent)}

/* Links (beide Seiten) */
a{color:var(--akzent);text-decoration:none}
a:hover{text-decoration:underline}
a.chip{color:var(--text);font-weight:600}
a.chip:hover{border-color:var(--akzent);box-shadow:0 0 12px rgba(58,226,58,.22);text-decoration:none}

)WEBSTIL";
