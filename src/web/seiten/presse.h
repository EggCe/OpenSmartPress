/*
 * presse.h - Pressseite der Weboberfläche (/presse, ohne externe Dateien)
 *
 * Ablauf: Werkzeug tarieren -> Hub kalibrieren -> Sitzung wählen -> Werkstück einlegen -> START.
 * Jede Pressung erscheint als Kurve Kraft über Weg; alle Pressungen der Sitzung liegen
 * übereinander. Klick auf eine Kurve (oder eine Tabellenzeile) zeigt ihre Kennwerte.
 *
 * Daten: GET /presse/status (4x pro Sekunde), Kurven binär über /sitzung/index und
 * /sitzung/presse. Das Binärformat (PressKopf, 128 Byte) steht in src/sitzungen.h.
 */
#pragma once

static const char PRESSE_HTML[] = R"PRESSE(<!doctype html>
<html lang="de"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Pressen</title>
<link rel="stylesheet" href="/stil.css">
<style>
.pgrid{display:grid;grid-template-columns:minmax(320px,440px) 1fr;gap:0 14px}
.pgrid2{display:grid;grid-template-columns:1fr 1fr;gap:0 14px}
@media (max-width:900px){.pgrid,.pgrid2{grid-template-columns:1fr}}
.schritt{display:grid;grid-template-columns:30px 1fr;gap:4px 10px;padding:9px 0;border-bottom:1px solid var(--rand)}
.schritt:last-of-type{border-bottom:0}
.schritt .nr{width:26px;height:26px;border-radius:50%;display:grid;place-items:center;font-weight:800;font-size:13px;
border:1px solid var(--rand2);color:var(--leise)}
.schritt.ok .nr{background:var(--akzent);color:var(--akzentText);border-color:var(--akzent);box-shadow:0 0 10px var(--glow)}
.schritt b{font-size:14px}
.schritt .sub{margin:0}
.schritt .knoepfe{margin-top:6px}
button.start{font-size:20px;padding:12px 40px;letter-spacing:1px}
#plot{height:480px;cursor:crosshair}
.details{display:grid;grid-template-columns:auto 1fr;gap:3px 14px;font-variant-numeric:tabular-nums}
.details dt{color:var(--leise)}
.details dd{margin:0;text-align:right}
.details dd i{color:var(--leise);font-style:normal;font-size:12px;margin-left:6px}
.scroll{max-height:360px;overflow:auto}
tr.sel td{background:rgba(58,226,58,.12)}
tbody tr{cursor:pointer}
tbody tr:hover td{background:rgba(58,226,58,.06)}
.balken{height:8px;border-radius:6px;background:var(--bg2);border:1px solid var(--rand2);overflow:hidden;margin-top:6px}
.balken > div{height:100%;background:var(--akzent)}
.form{display:flex;flex-wrap:wrap;gap:8px;align-items:center;margin-top:8px}
.form input[type=text]{flex:1 1 160px;min-width:0}
td .chip,td button{padding:2px 9px;font-size:12px}
#log{height:200px}
</style></head><body><main>
<header>
  <div class="marke">
    <div class="logo"><svg width="20" height="20" viewBox="0 0 20 20" fill="none" stroke="#031003" stroke-width="2.2"
      stroke-linecap="round" stroke-linejoin="round"><polyline points="1,11 5,11 7.5,4 11,16 13.5,8 15.5,11 19,11"/></svg></div>
    <h1>EcklC <em>OpenSmartPress</em> <span>Pressen</span></h1>
  </div>
  <div class="kopfrechts">
    <div class="status" id="status">verbinde …</div>
    <a class="chip" href="/">Messseite</a>
    <a class="chip" href="/info#presse">Hilfe</a>
  </div>
</header>

<div class="pgrid">
  <div class="karte glow">
    <div class="titel">Ablauf</div>
    <label title="Ohne Motor: die Kraft wird aus der Position berechnet (Werkstück 25–35 kg)">
      <input type="checkbox" id="sim"> Simulation ohne Motor (25–35 kg)</label>
    <div class="schritt" id="s1"><div class="nr">1</div><div>
      <b>Werkzeug auflegen und tarieren</b>
      <div class="sub" id="s1t">–</div>
      <div class="knoepfe"><button data-c="t">Tara</button></div></div></div>
    <div class="schritt" id="s2"><div class="nr">2</div><div>
      <b>Hub kalibrieren</b>
      <div class="sub" id="s2t">–</div>
      <div class="knoepfe"><button data-c="mot kal">Hub kalibrieren</button></div></div></div>
    <div class="schritt" id="s3"><div class="nr">3</div><div>
      <b>Sitzung wählen</b>
      <div class="sub" id="s3t">–</div>
      <div class="knoepfe"><button id="zuSitzungen">Sitzungen …</button></div></div></div>
    <div class="schritt" id="s4"><div class="nr">4</div><div>
      <b>Werkstück einlegen und starten</b>
      <div class="sub" id="s4t">–</div>
      <div class="knoepfe"><button class="primaer start" id="start" disabled>START</button>
        <button class="stopp" data-c="x" title="Taste Esc">STOPP</button></div></div></div>
  </div>
  <div class="karte">
    <div class="plotkopf"><div class="titel" id="detTitel">Pressung</div><div class="sub" id="detSub"></div></div>
    <dl class="details" id="details"><dt>Noch keine Pressung ausgewählt.</dt><dd></dd></dl>
  </div>
</div>

<div class="karte glow">
  <div class="plotkopf"><div class="titel">Kraft über Weg</div>
    <div class="sub" id="maus">Klick: Pressung auswählen · Mausrad: zoomen · Ziehen: verschieben · Doppelklick: alles</div></div>
  <div class="leiste">
    <label>Krafteinheit <select id="einheit"><option value="N">N</option><option value="kg">kg</option>
      <option value="lbs">lbs</option></select></label>
    <label>Anzeigen <select id="zeige"><option value="0">alle</option><option value="10">letzte 10</option>
      <option value="25">letzte 25</option><option value="50">letzte 50</option></select></label>
    <label><input type="checkbox" id="abbr"> Abbrüche zeigen</label>
    <label><input type="checkbox" id="nurKontakt" checked> ab Kontakt zoomen</label>
    <span class="sub">grün: Pressungen · hell: Auswahl · gelb: laufende Pressung · rot: Abbruch · gestrichelt: Werkzeug-Endpunkt</span>
  </div>
  <canvas id="plot"></canvas>
</div>

<div class="pgrid2">
  <div class="karte"><div class="titel">Sitzungsstatistik</div><div class="sub" id="statSub"></div><div id="stat"></div></div>
  <div class="karte"><div class="titel">Pressungen</div><div class="scroll"><table id="tab"></table></div></div>
</div>

<div class="karte" id="sitzungen">
  <div class="plotkopf"><div class="titel">Sitzungen</div><div class="sub" id="speicher"></div></div>
  <div class="balken"><div id="speicherBalken" style="width:0"></div></div>
  <div class="form">
    <input type="text" id="neuName" maxlength="47" placeholder="Name der neuen Sitzung, z. B. Charge 2026-11-14 / Werkzeug A">
    <input type="text" id="neuNotiz" maxlength="95" placeholder="Notiz (optional)">
    <button class="primaer" id="neu">Neue Sitzung anlegen</button>
    <button id="schliessen">Aktive Sitzung schließen</button>
  </div>
  <div class="scroll"><table id="liste"></table></div>
</div>

<div class="pgrid2">
  <div class="karte">
    <div class="titel">Pressparameter</div>
    <div class="leiste">
      <label>Eilgang bis [mm über Endpunkt] <input type="number" id="pAnfahrt" min="0" max="45" step="0.1"></label>
      <label>Pressgeschw. [mm/s] <input type="number" id="pVpress" min="0.01" max="20" step="0.01"></label>
      <label>Haltezeit [ms] <input type="number" id="pHalt" min="0" max="60000" step="100"></label>
    </div>
    <div class="knoepfe"><button id="pSetzen">Übernehmen</button><button data-c="s">Im Flash speichern</button></div>
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
</main>
<script>
"use strict";
const $=id=>document.getElementById(id);
const STUFE=0.02;
const GRUND=["-","Endpunkt erreicht","Kontakt","Maximalkraft","Alarm vom Treiber","STOPP-Taster","Stopp-Befehl","keine Messwerte","kein Kontakt"];
const STATUS=["OK","Abbruch","Kraft im Eilgang"];
let S=null,online=false,ln=null,logRest="";
let sitId=-1,sitN=0,ladeNr=0,holt=false;
let pressen=new Map();               // nr -> {k,x,y}
let auswahl=0,versteckt=new Set();
let live={id:-1,n:0,s:0,d:0,y:[],lauf:0};
let ansicht=null,geo=null,neuZeichnen=true;

const fmt=(v,d)=>(v===null||v===undefined||!isFinite(v))?"–":v.toLocaleString("de-DE",{minimumFractionDigits:d,maximumFractionDigits:d});
const zeitText=z=>z?new Date(z*1000).toLocaleString("de-DE",{dateStyle:"short",timeStyle:"medium"}):"–";
// Krafteinheit: gespeichert und übertragen wird in N, umgerechnet nur für die Anzeige
const EINHEITEN={N:{f:1,d:1,n:"N"},kg:{f:1/9.80665,d:2,n:"kg"},lbs:{f:1/4.4482216152605,d:1,n:"lbs"}};
let E=EINHEITEN.N;
try{const e=localStorage.getItem("wz_einheit");if(EINHEITEN[e]){E=EINHEITEN[e];document.getElementById("einheit").value=e;}}catch(e){}
const kr=vN=>(vN===null||vN===undefined)?NaN:vN*E.f;                  // N -> Anzeigeeinheit
const fk=(vN,mehr)=>fmt(kr(vN),E.d+(mehr||0))+" "+E.n;                 // formatierte Kraft mit Einheit
const esc=s=>String(s).replace(/[&<>"]/g,c=>({"&":"&amp;","<":"&lt;",">":"&gt;","\"":"&quot;"}[c]));

// Uhr des Pico stellen (hat keine eigene)
fetch("/zeit",{method:"POST",body:new URLSearchParams({t:Math.floor(Date.now()/1000),tz:-new Date().getTimezoneOffset()})}).catch(()=>{});

async function befehl(c){
  try{await fetch("/cmd",{method:"POST",body:new URLSearchParams({c})});}
  catch(e){logText("# Browser: Befehl '"+c+"' konnte nicht gesendet werden\n");}
}
async function aktion(o){
  try{const r=await fetch("/sitzung",{method:"POST",body:new URLSearchParams(o)});const j=await r.json();listeLaden();return j.ok===1;}
  catch(e){return false;}
}
document.querySelectorAll("button[data-c]").forEach(b=>b.onclick=()=>befehl(b.dataset.c));
$("start").onclick=()=>befehl("pr");
$("sim").onchange=e=>befehl(e.target.checked?"sim an":"sim aus");
document.addEventListener("keydown",e=>{if(e.key==="Escape")befehl("x");});
$("senden").onclick=senden;
$("befehl").onkeydown=e=>{if(e.key==="Enter")senden();};
function senden(){const t=$("befehl").value.trim();if(t){befehl(t);$("befehl").value="";}}
$("zuSitzungen").onclick=()=>{$("sitzungen").scrollIntoView({behavior:"smooth"});$("neuName").focus();};
$("zeige").onchange=$("abbr").onchange=$("nurKontakt").onchange=()=>{ansicht=null;neuZeichnen=true;};
$("einheit").onchange=()=>{
  E=EINHEITEN[$("einheit").value]||EINHEITEN.N;
  try{localStorage.setItem("wz_einheit",$("einheit").value);}catch(e){}
  ansicht=null;neuZeichnen=true;
  tabelle();statistik();details();listeLaden();if(S){ablaufZeigen();statusZeigen();parameterZeigen();}
};

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
  pressen.set(nr,{k,x,y});neuZeichnen=true;
  return true;
}
async function sitzungLaden(id){
  const token=++ladeNr;
  sitId=id;sitN=0;pressen=new Map();auswahl=0;versteckt=new Set();ansicht=null;
  tabelle();statistik();details();neuZeichnen=true;
  if(!id)return;
  holt=true;
  try{
    const r=await fetch("/sitzung/index?id="+id,{cache:"no-store"});
    const b=await r.arrayBuffer();
    if(token!==ladeNr)return;
    const dv=new DataView(b),n=Math.floor(b.byteLength/128);
    for(let i=0;i<n;i++){const k=kopf(dv,i*128);pressen.set(k.nr,{k,x:null,y:null});}
    sitN=n;auswahl=n;tabelle();statistik();details();
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
    tabelle();statistik();details();listeLaden();
  }catch(e){}
  finally{holt=false;}
}

// ---------- Status ----------
async function abfragen(){
  try{
    const q=new URLSearchParams({ab:live.n,id:live.id});
    if(ln!==null)q.set("l",ln);
    const r=await fetch("/presse/status?"+q,{cache:"no-store"});
    verarbeiten(await r.json());
    online=true;
  }catch(e){online=false;statusZeigen();}
  setTimeout(abfragen,250);
}
function verarbeiten(d){
  S=d;
  const p=d.pr;
  if(p.id!==live.id||p.ab===0){live={id:p.id,n:0,s:p.s,d:p.d,y:[],lauf:p.lauf};}
  live.lauf=p.lauf;live.s=p.s;live.d=p.d;
  if(p.ab===live.n){for(const v of p.p)live.y.push(v===null?NaN:v*STUFE);live.n=live.y.length;}
  if(p.lauf||p.p.length)neuZeichnen=true;
  const id=d.sit?d.sit.id:0;
  if(id!==sitId){sitzungLaden(id);listeLaden();}
  else if(d.sit&&d.sit.n>sitN&&!holt)neuePressen(d.sit.n);
  if(d.log)logAufnehmen(d.log);
  ln=d.ln;
  ablaufZeigen();statusZeigen();parameterZeigen();
}
function statusZeigen(){
  const el=$("status");
  if(!online||!S){el.innerHTML='<span class="chip warn"><span class="punkt aus"></span>keine Verbindung</span>';return;}
  el.innerHTML='<span class="chip"><span class="punkt'+(S.ok?'':' aus')+'"></span>'+fmt(S.rate,0)+' SPS</span>'+
    '<span class="chip">'+esc(S.zust)+'</span>'+
    (S.pos!==null?'<span class="chip">Pos. '+fmt(S.pos,3)+' mm</span>':'')+
    '<span class="chip">'+fk(S.kraft)+'</span>'+(S.alarm?'<span class="chip warn">ALARM</span>':'')+
    (S.sim?'<span class="chip warn">SIMULATION</span>':'');
}
function schritt(n,ok,text){$("s"+n).classList.toggle("ok",ok);$("s"+n+"t").innerHTML=text;}
function ablaufZeigen(){
  const d=S;
  const nahNull=Math.abs(d.kraft)<Math.max(0.5,d.par.kontakt*0.5);
  if(document.activeElement!==$("sim"))$("sim").checked=!!d.sim;
  schritt(1,d.ok&&(d.kal||d.sim)&&nahNull,!d.ok?'<span class="warn">ADC misst nicht</span>':
    !d.kal&&!d.sim?'<span class="warn">Waage nicht kalibriert – auf der Messseite kalibrieren</span>':
    'Kraft jetzt: <b>'+fk(d.kraft,1)+'</b>'+(nahNull?' – tariert':' – mit Werkzeug „Tara“ drücken'));
  schritt(2,!!d.end.akt,d.end.akt?'Werkzeug-Kontakt '+fmt(d.end.tiefe,3)+' mm unter Start, Endpunkt +'+fmt(d.end.ende,4)+
    ' mm bei '+fk(d.end.kraft):'nach jedem Einschalten nötig – Werkzeug aufgelegt, Werkstück entnommen');
  schritt(3,!!d.sit,d.sit?'<b>'+esc(d.sit.name)+'</b> · '+d.sit.n+' Pressungen':'keine Sitzung aktiv');
  $("start").disabled=!d.bereit;
  schritt(4,!!d.bereit,d.pr.lauf?'<b>'+esc(d.zust)+'</b> …':d.bereit?'bereit – START drücken (Knopf oder Taster)':
    '<span class="warn">'+esc(d.grund)+'</span>');
}

// ---------- Kennwerte ----------
function aktuelleKurven(){
  let ks=[...pressen.values()].filter(p=>p.x&&!versteckt.has(p.k.nr)&&($("abbr").checked||p.k.status===0));
  ks.sort((a,b)=>a.k.nr-b.k.nr);
  const z=+$("zeige").value;
  if(z>0)ks=ks.slice(-z);
  const a=pressen.get(auswahl);
  if(a&&a.x&&!ks.includes(a))ks.push(a);
  return ks;
}
// Einheit "F" = Kraft (in N gespeichert, angezeigt in der gewählten Einheit); Stellen bei Kraft: zusätzlich zu E.d
const KENN=[["fmaxN","Spitzenkraft","F",0],["fendN","Endkraft","F",0],["fhaltN","Kraft nach Haltezeit","F",0],
  ["kontaktMm","Kontaktposition","mm",3],["tiefeMm","Presstiefe","mm",3],["fmittelN","Mittlere Kraft","F",0],
  ["fsigmaN","Streuung im Pressen (σ)","F",1],["fspanneN","Spannweite im Pressen","F",0],["arbeitJ","Pressarbeit","J",3],
  ["presszeitS","Presszeit","s",2]];
const istKraft=f=>KENN.some(k=>k[0]===f&&k[2]==="F");
const wert=(k,f)=>istKraft(f)?kr(k[f]):k[f];                          // Kennwert in Anzeigeeinheit
const stellen=(e,d)=>e==="F"?E.d+d:d;
const einh=e=>e==="F"?E.n:e;
function kennStat(){
  const ok=[...pressen.values()].filter(p=>p.k.status===0).map(p=>p.k);
  const r={};
  for(const [f] of KENN){
    const v=ok.map(k=>wert(k,f)).filter(x=>isFinite(x));
    if(!v.length)continue;
    const n=v.length,m=v.reduce((a,b)=>a+b,0)/n,s=n>1?Math.sqrt(v.reduce((a,b)=>a+(b-m)*(b-m),0)/(n-1)):NaN;
    r[f]={n,m,s,min:Math.min(...v),max:Math.max(...v)};
  }
  return {r,n:ok.length};
}
function statistik(){
  const {r,n}=kennStat();
  $("statSub").textContent=n+" gültige Pressungen (ohne Abbrüche)";
  if(!n){$("stat").innerHTML="";return;}
  let h='<table><tr><th>Kennwert</th><th>Mittel</th><th>σ</th><th>σ %</th><th>Min</th><th>Max</th><th>Spannweite</th></tr>';
  for(const [f,name,e,d0] of KENN){
    const x=r[f];if(!x)continue;
    const d=stellen(e,d0);
    h+='<tr><td>'+name+' ['+einh(e)+']</td><td><b>'+fmt(x.m,d)+'</b></td><td>'+fmt(x.s,d+1)+'</td><td>'+
      (x.m!==0?fmt(100*x.s/Math.abs(x.m),2):"–")+'</td><td>'+fmt(x.min,d)+'</td><td>'+fmt(x.max,d)+'</td><td>'+fmt(x.max-x.min,d)+'</td></tr>';
  }
  $("stat").innerHTML=h+'</table>';
}
function tabelle(){
  let h='<thead><tr><th></th><th>Nr</th><th>Zeit</th><th>Fmax '+E.n+'</th><th>Fend '+E.n+'</th><th>Kontakt mm</th><th>Tiefe mm</th>'+
    '<th>σ '+E.n+'</th><th>Arbeit J</th><th>Status</th></tr></thead><tbody>';
  for(const p of [...pressen.values()].sort((a,b)=>b.k.nr-a.k.nr)){
    const k=p.k;
    h+='<tr data-nr="'+k.nr+'"'+(k.nr===auswahl?' class="sel"':'')+'><td><input type="checkbox" title="Kurve anzeigen"'+
      (versteckt.has(k.nr)?'':' checked')+'></td><td>'+k.nr+'</td><td>'+(k.zeit?new Date(k.zeit*1000).toLocaleTimeString("de-DE"):"–")+
      '</td><td><b>'+fmt(kr(k.fmaxN),E.d)+'</b></td><td>'+fmt(kr(k.fendN),E.d)+'</td><td>'+fmt(k.kontaktMm,3)+'</td><td>'+fmt(k.tiefeMm,3)+
      '</td><td>'+fmt(kr(k.fsigmaN),E.d+1)+'</td><td>'+fmt(k.arbeitJ,3)+'</td><td'+(k.status?' class="warn"':'')+'>'+(STATUS[k.status]||"?")+(k.sim?' (Sim.)':'')+'</td></tr>';
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
  if(!p){$("detTitel").textContent="Pressung";$("detSub").textContent="";
    $("details").innerHTML="<dt>Noch keine Pressung ausgewählt.</dt><dd></dd>";return;}
  const k=p.k,{r}=kennStat();
  $("detTitel").textContent="Pressung "+k.nr;
  $("detSub").innerHTML=zeitText(k.zeit)+' · <span class="'+(k.status?'warn':'ok')+'">'+(STATUS[k.status]||"?")+'</span>'+
    (k.sim?' · <span class="warn">Simulation</span>':'');
  const abw=(f,d)=>{const x=r[f],w=wert(k,f);if(!x||!isFinite(w)||x.n<2||k.status)return"";const dv=w-x.m;
    return '<i>'+(dv>=0?"+":"")+fmt(dv,d)+(x.m?' ('+(dv>=0?"+":"")+fmt(100*dv/Math.abs(x.m),1)+' %)':'')+' zum Mittel</i>';};
  // e === "F": v in N, Anzeige in der gewählten Einheit, d = zusätzliche Stellen
  const z=(t,v,e,d,f)=>{const F=e==="F",dd=F?E.d+d:d;
    return '<dt>'+t+'</dt><dd><b>'+fmt(F?kr(v):v,dd)+'</b> '+(F?E.n:e)+(f?abw(f,dd):'')+'</dd>';};
  $("details").innerHTML=
    z("Spitzenkraft",k.fmaxN,"F",0,"fmaxN")+'<dt>… bei Weg</dt><dd>'+fmt(k.fmaxMm,3)+' mm</dd>'+
    z("Endkraft (am Endpunkt)",k.fendN,"F",0,"fendN")+(k.haltMs?z("Kraft nach Haltezeit",k.fhaltN,"F",0,"fhaltN"):'')+
    z("Kontakt bei",k.kontaktMm,"mm",3,"kontaktMm")+z("Endpunkt",k.endMm,"mm",4)+z("Presstiefe",k.tiefeMm,"mm",3,"tiefeMm")+
    z("Mittlere Kraft",k.fmittelN,"F",0,"fmittelN")+z("Standardabweichung (σ)",k.fsigmaN,"F",1,"fsigmaN")+
    z("Minimum",k.fminN,"F",0)+z("Spannweite (max − min)",k.fspanneN,"F",0,"fspanneN")+
    z("Pressarbeit",k.arbeitJ,"J",3,"arbeitJ")+z("Presszeit (ab Kontakt)",k.presszeitS,"s",2,"presszeitS")+
    z("Zykluszeit",k.zykluszeitS,"s",1)+
    '<dt>Ende durch</dt><dd>'+(GRUND[k.grund]||"?")+'</dd>'+
    '<dt>Proben</dt><dd>'+k.proben+' bei '+fmt(k.sps,0)+' SPS'+(k.verloren?' <span class="warn">('+k.verloren+' verloren)</span>':'')+'</dd>'+
    '<dt>Einstellungen</dt><dd>'+fmt(k.vPress,2)+' mm/s, Haltezeit '+k.haltMs+' ms</dd>';
}

// ---------- Diagramm ----------
const cv=$("plot"),ctx=cv.getContext("2d");
function schrittweite(span,n){const r=span/n,p=Math.pow(10,Math.floor(Math.log10(r))),m=r/p;return (m<1.5?1:m<3.5?2:m<7.5?5:10)*p;}
function bereich(ks){
  let x0=Infinity,x1=-Infinity,y0=0,y1=-Infinity;
  const nk=$("nurKontakt").checked;
  const mit=(x,y,kx)=>{for(let i=0;i<x.length;i++){if(!isFinite(y[i]))continue;if(nk&&isFinite(kx)&&x[i]<kx-0.3)continue;
    if(x[i]<x0)x0=x[i];if(x[i]>x1)x1=x[i];const v=y[i]*E.f;if(v>y1)y1=v;if(v<y0)y0=v;}};
  for(const p of ks)mit(p.x,p.y,p.k.kontaktMm);
  if(live.lauf&&live.y.length){const x=live.y.map((_,i)=>live.s+i*live.d);mit(x,live.y,NaN);}
  if(!isFinite(x0)){x0=-1;x1=0.2;y1=10;}
  if(x1-x0<0.01){x0-=0.05;x1+=0.05;}
  if(!(y1>y0))y1=y0+10;
  const px=(x1-x0)*0.03;
  return {x0:x0-px,x1:x1+px,y0:y0,y1:y1+(y1-y0)*0.08};
}
function zeichnen(){
  requestAnimationFrame(zeichnen);
  if(!neuZeichnen)return;
  neuZeichnen=false;
  const dpr=window.devicePixelRatio||1,w=cv.clientWidth,h=cv.clientHeight;
  if(cv.width!==Math.round(w*dpr)||cv.height!==Math.round(h*dpr)){cv.width=Math.round(w*dpr);cv.height=Math.round(h*dpr);}
  ctx.setTransform(dpr,0,0,dpr,0,0);ctx.clearRect(0,0,w,h);
  const L=62,R=14,T=12,B=38,pw=w-L-R,ph=h-T-B;
  const ks=aktuelleKurven();
  const b=ansicht||bereich(ks);
  geo={L,T,pw,ph,...b};
  const X=v=>L+(v-b.x0)/(b.x1-b.x0)*pw,Y=v=>T+ph-(v-b.y0)/(b.y1-b.y0)*ph;
  const st=getComputedStyle(document.documentElement);
  ctx.font="12px system-ui,sans-serif";ctx.fillStyle=st.getPropertyValue("--leise");ctx.strokeStyle=st.getPropertyValue("--gitter");ctx.lineWidth=1;
  const sx=schrittweite(b.x1-b.x0,8),sy=schrittweite(b.y1-b.y0,6);
  ctx.textAlign="center";ctx.textBaseline="top";
  for(let v=Math.ceil(b.x0/sx)*sx;v<=b.x1;v+=sx){const x=X(v);ctx.beginPath();ctx.moveTo(x,T);ctx.lineTo(x,T+ph);ctx.stroke();
    ctx.fillText(fmt(v,sx<0.01?3:sx<0.1?2:sx<1?1:0),x,T+ph+5);}
  ctx.textAlign="right";ctx.textBaseline="middle";
  for(let v=Math.ceil(b.y0/sy)*sy;v<=b.y1;v+=sy){const y=Y(v);ctx.beginPath();ctx.moveTo(L,y);ctx.lineTo(L+pw,y);ctx.stroke();
    ctx.fillText(fmt(v,sy<1?1:0),L-6,y);}
  ctx.textAlign="center";ctx.textBaseline="bottom";ctx.fillText("Weg [mm]  (0 = Werkzeug-Endpunkt, + = tiefer)",L+pw/2,h-1);
  ctx.save();ctx.translate(13,T+ph/2);ctx.rotate(-Math.PI/2);ctx.textBaseline="top";ctx.fillText("Kraft ["+E.n+"]",0,0);ctx.restore();

  ctx.save();ctx.beginPath();ctx.rect(L,T,pw,ph);ctx.clip();
  if(b.x0<0&&b.x1>0){ctx.setLineDash([5,5]);ctx.strokeStyle=st.getPropertyValue("--linie");ctx.beginPath();ctx.moveTo(X(0),T);ctx.lineTo(X(0),T+ph);ctx.stroke();ctx.setLineDash([]);}
  const linie=(x,y,farbe,breite,strich)=>{
    ctx.strokeStyle=farbe;ctx.lineWidth=breite;ctx.setLineDash(strich?[6,4]:[]);ctx.beginPath();let auf=false;
    for(let i=0;i<y.length;i++){if(!isFinite(y[i])){continue;}const px=X(x[i]),py=Y(y[i]*E.f);if(auf)ctx.lineTo(px,py);else{ctx.moveTo(px,py);auf=true;}}
    ctx.stroke();ctx.setLineDash([]);};
  const neueste=Math.max(0,...ks.map(p=>p.k.nr));
  for(const p of ks){if(p.k.nr===auswahl)continue;
    linie(p.x,p.y,p.k.status?"rgba(255,92,92,.55)":p.k.nr===neueste?"rgba(58,226,58,.85)":"rgba(58,226,58,.28)",p.k.nr===neueste?1.6:1.1,!!p.k.status);}
  const a=pressen.get(auswahl);
  if(a&&a.x){linie(a.x,a.y,"#ffffff",2.4,false);
    if(isFinite(a.k.fmaxN)){ctx.fillStyle="#ffffff";ctx.beginPath();ctx.arc(X(a.k.fmaxMm),Y(a.k.fmaxN*E.f),4,0,7);ctx.fill();}}
  if(live.lauf&&live.y.length){linie(live.y.map((_,i)=>live.s+i*live.d),live.y,st.getPropertyValue("--warn"),2.2,false);}
  ctx.restore();
}
function mausWerte(e){const r=cv.getBoundingClientRect();const mx=e.clientX-r.left,my=e.clientY-r.top;
  return {mx,my,x:geo.x0+(mx-geo.L)/geo.pw*(geo.x1-geo.x0),y:geo.y1-(my-geo.T)/geo.ph*(geo.y1-geo.y0)};}
let zieh=null;
cv.addEventListener("wheel",e=>{
  if(!geo)return;e.preventDefault();
  const m=mausWerte(e),k=e.deltaY>0?1.25:0.8,b=geo;
  ansicht={x0:m.x-(m.x-b.x0)*k,x1:m.x+(b.x1-m.x)*k,y0:e.shiftKey?b.y0:m.y-(m.y-b.y0)*k,y1:e.shiftKey?b.y1:m.y+(b.y1-m.y)*k};
  neuZeichnen=true;
},{passive:false});
cv.addEventListener("pointerdown",e=>{if(!geo||e.button!==0)return;zieh={cx:e.clientX,cy:e.clientY,b:{x0:geo.x0,x1:geo.x1,y0:geo.y0,y1:geo.y1},weg:0};cv.setPointerCapture(e.pointerId);});
cv.addEventListener("pointermove",e=>{
  if(geo){const m=mausWerte(e);$("maus").textContent="Weg "+fmt(m.x,3)+" mm · Kraft "+fmt(m.y,E.d)+" "+E.n;}
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
  const m=mausWerte(e),X=v=>geo.L+(v-geo.x0)/(geo.x1-geo.x0)*geo.pw,Y=v=>geo.T+geo.ph-(v-geo.y0)/(geo.y1-geo.y0)*geo.ph;
  let best=0,bd=16;
  for(const p of aktuelleKurven()){
    for(let i=0;i<p.x.length;i++){if(!isFinite(p.y[i]))continue;const px=X(p.x[i]);if(Math.abs(px-m.mx)>bd)continue;
      const d=Math.hypot(px-m.mx,Y(p.y[i]*E.f)-m.my);if(d<bd){bd=d;best=p.k.nr;}}
  }
  if(best)waehlen(best);
});
cv.addEventListener("pointercancel",()=>{zieh=null;});
cv.addEventListener("dblclick",()=>{ansicht=null;neuZeichnen=true;});
window.addEventListener("resize",()=>{neuZeichnen=true;});

// ---------- Sitzungen ----------
async function listeLaden(){
  try{
    const d=await (await fetch("/sitzungen",{cache:"no-store"})).json();
    $("speicher").textContent="Speicher: "+fmt(d.sp.b/1024,0)+" von "+fmt(d.sp.g/1024,0)+" KB belegt";
    $("speicherBalken").style.width=(d.sp.g?Math.min(100,100*d.sp.b/d.sp.g):0)+"%";
    let h='<thead><tr><th>Nr</th><th>Name</th><th>Erstellt</th><th>Pressungen</th><th>Notiz</th><th></th></tr></thead><tbody>';
    for(const s of d.liste.slice().reverse()){
      const akt=s.id===d.aktiv;
      h+='<tr'+(akt?' class="sel"':'')+'><td>'+s.id+'</td><td>'+(akt?'<b>'+esc(s.name)+'</b> (aktiv)':esc(s.name))+'</td><td>'+esc(s.erst)+
        '</td><td>'+s.n+'</td><td>'+esc(s.notiz)+'</td><td style="white-space:nowrap">'+
        (akt?'':'<button data-a="wahl" data-id="'+s.id+'">Öffnen</button> ')+
        '<a class="chip" href="/sitzung/export?id='+s.id+'&f=csv&e='+$("einheit").value+'" download>CSV</a> '+
        '<a class="chip" href="/sitzung/export?id='+s.id+'&f=json" download>JSON</a> '+
        '<button data-a="aendern" data-id="'+s.id+'" data-name="'+esc(s.name)+'" data-notiz="'+esc(s.notiz)+'">Umbenennen</button> '+
        '<button data-a="loeschen" data-id="'+s.id+'" data-name="'+esc(s.name)+'" data-n="'+s.n+'">Löschen</button></td></tr>';
    }
    $("liste").innerHTML=h+'</tbody>';
    $("liste").querySelectorAll("button[data-a]").forEach(b=>b.onclick=async e=>{
      e.stopPropagation();
      const a=b.dataset.a,id=b.dataset.id;
      if(a==="wahl")aktion({a,id});
      else if(a==="loeschen"){if(confirm("Sitzung "+id+" „"+b.dataset.name+"“ mit "+b.dataset.n+" Pressungen endgültig löschen?\nVorher exportieren!"))aktion({a,id});}
      else if(a==="aendern"){const name=prompt("Neuer Name:",b.dataset.name);if(name===null)return;
        const notiz=prompt("Notiz:",b.dataset.notiz);aktion({a,id,name,notiz:notiz===null?b.dataset.notiz:notiz});}
    });
  }catch(e){}
}
$("neu").onclick=async()=>{
  const name=$("neuName").value.trim();
  if(!name){$("neuName").focus();return;}
  if(await aktion({a:"neu",name,notiz:$("neuNotiz").value.trim()})){$("neuName").value="";$("neuNotiz").value="";}
};
$("schliessen").onclick=()=>aktion({a:"zu"});

// ---------- Parameter ----------
function parameterZeigen(){
  const p=S.par;
  for(const [id,v] of [["pAnfahrt",p.anfahrt],["pVpress",p.vpress],["pHalt",p.halt]]){
    const el=$(id);if(document.activeElement!==el&&!el.dataset.geaendert)el.value=v;
  }
  $("pInfo").textContent="Maximalkraft "+fk(p.fmax,-1)+" · Kontakt ab "+fk(p.kontakt)+" · Eilgang "+fmt(p.vfahrt,1)+
    " mm/s · Ändern von fmax/kontakt mit 'mp fmax <g>' bzw. 'mp kontakt <g>'";
}
for(const id of ["pAnfahrt","pVpress","pHalt"])$(id).oninput=e=>{e.target.dataset.geaendert="1";};
$("pSetzen").onclick=async()=>{
  await befehl("mp anfahrt "+$("pAnfahrt").value);
  await befehl("mp vpress "+$("pVpress").value);
  await befehl("mp halt "+$("pHalt").value);
  for(const id of ["pAnfahrt","pVpress","pHalt"])delete $(id).dataset.geaendert;
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
  logText(t.slice(0,i+1).split("\n").filter(z=>!z.startsWith("#CFG")).join("\n"));
}

listeLaden();
abfragen();
requestAnimationFrame(zeichnen);
</script>
</body></html>
)PRESSE";
