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
.pgrid3{display:grid;grid-template-columns:1fr minmax(300px,420px);gap:0 14px}
@media (max-width:900px){.pgrid,.pgrid2,.pgrid3{grid-template-columns:1fr}}
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
#trend{width:100%;height:240px;display:block;cursor:pointer}
.details{display:grid;grid-template-columns:auto 1fr;gap:3px 14px;font-variant-numeric:tabular-nums}
.details dt{color:var(--leise)}
.details dd{margin:0;text-align:right}
.details dd i{color:var(--leise);font-style:normal;font-size:12px;margin-left:6px}
.details .gruppe{grid-column:1/-1;color:var(--akzent);font-size:11px;font-weight:650;letter-spacing:.8px;
text-transform:uppercase;margin-top:6px;border-bottom:1px solid var(--rand)}
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
.awgitter{display:grid;grid-template-columns:auto auto;gap:6px 10px;align-items:center}
.awgitter label{justify-content:space-between}
#awFenster{width:130px}
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
  <div class="sub">grün: Pressungen · hell: Auswahl mit Plateau-Gerade (Fenster schattiert), ● Fügekraft, ▲ Einführspitze,
    ✕ größter Kraftabfall · orange: Rissverdacht · gelb: laufende Pressung · rot: Abbruch</div>
  <canvas id="plot"></canvas>
</div>

<div class="pgrid3">
  <div class="karte">
    <div class="plotkopf"><div class="titel">Trend über die Pressungen</div>
      <label>Kennwert <select id="trendWert"></select></label></div>
    <div class="sub" id="trendSub">Mittelwert (Linie) und ±3σ (gestrichelt) der gültigen Pressungen · Klick: Pressung auswählen</div>
    <canvas id="trend"></canvas>
  </div>
  <div class="karte">
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

<div class="pgrid2">
  <div class="karte"><div class="titel">Sitzungsstatistik</div><div class="sub" id="statSub"></div><div class="scroll" id="stat"></div></div>
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
let sitId=-1,sitN=0,ladeNr=0,holt=false,sitName="";
let pressen=new Map();               // nr -> {k,x,y,a}  (a = Auswertung)
let auswahl=0,versteckt=new Set();
let live={id:-1,n:0,s:0,d:0,y:[],lauf:0};
let ansicht=null,geo=null,tgeo=null,neuZeichnen=true;

const fmt=(v,d)=>(v===null||v===undefined||!isFinite(v))?"–":v.toLocaleString("de-DE",{minimumFractionDigits:Math.max(0,d),maximumFractionDigits:Math.max(0,d)});
const zeitText=z=>z?new Date(z*1000).toLocaleString("de-DE",{dateStyle:"short",timeStyle:"medium"}):"–";
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
$("xmodus").onchange=()=>{lsSchreiben("wz_xmodus",$("xmodus").value);ansicht=null;neuZeichnen=true;};
$("einheit").onchange=()=>{
  E=EINHEITEN[$("einheit").value]||EINHEITEN.N;
  lsSchreiben("wz_einheit2",$("einheit").value);
  ansicht=null;
  anzeigenAktualisieren();listeLaden();if(S){ablaufZeigen();statusZeigen();parameterZeigen();}
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
  const p={k,x,y,a:{}};
  auswerten(p);
  pressen.set(nr,p);
  spaeterAktualisieren();
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

// ---------- Auswertung einer Pressung (Kräfte in N, Wege in mm ab Kontakt) ----------
function interp(s,f,x){
  if(!s.length||x<s[0]||x>s[s.length-1])return NaN;
  let lo=0,hi=s.length-1;
  while(hi-lo>1){const m=(lo+hi)>>1;if(s[m]<=x)lo=m;else hi=m;}
  return s[hi]===s[lo]?f[lo]:f[lo]+(f[hi]-f[lo])*(x-s[lo])/(s[hi]-s[lo]);
}
function auswerten(p){
  const k=p.k,a={};
  p.a=a;
  if(!p.x||!k.kontakt||!isFinite(k.kontaktMm)||!isFinite(k.endMm))return;
  const s=[],f=[];
  for(let i=0;i<p.x.length;i++){
    if(!isFinite(p.y[i]))continue;
    const w=p.x[i]-k.kontaktMm;
    if(w>=0){s.push(w);f.push(p.y[i]);}
  }
  if(s.length<5)return;
  const L=(k.endMm-AW.ende)-k.kontaktMm;                 // Fügetiefe (ohne Anschlag)
  a.L=L;
  if(!(L>0))return;
  const ds=Math.max(k.d,1e-4);
  // geglättet (gleitendes Mittel über 5 Punkte) für Spitze und Abfall
  const g=f.map((_,i)=>{let sum=0,n=0;for(let j=Math.max(0,i-2);j<=Math.min(f.length-1,i+2);j++){sum+=f[j];n++;}return sum/n;});
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
    ["fuege","Fügekraft (ohne Anschlag)","F",0,p=>p.a.fuegeN],
    ["spitze","Einführspitze","F",0,p=>p.a.spitzeN],
    ["steigung","Plateau-Steigung","F/mm",0,p=>p.a.steigung],
    ["achse","Plateau-Achsenabschnitt","F",0,p=>p.a.achse],
    ["laufruhe","Laufruhe (σ um die Gerade)","F",1,p=>p.a.laufruhe],
    ["abfall","Größter Kraftabfall","%",1,p=>p.a.abfallPct],
    ["parbeit","Arbeit im Plateaufenster","J",3,p=>p.a.parbeit]];
  fensterListe().forEach((x,i)=>l.push(["f"+i,"Kraft bei "+fmt(x,2)+" mm","F",0,p=>p.a.fenster?p.a.fenster[i]:NaN]));
  l.push(["fend","Endkraft (am Anschlag)","F",0,p=>p.k.fendN],
    ["kontakt","Kontaktposition","mm",3,p=>p.k.kontaktMm],
    ["tiefe","Presstiefe","mm",3,p=>p.k.tiefeMm],
    ["arbeit","Pressarbeit gesamt","J",3,p=>p.k.arbeitJ],
    ["zeit","Presszeit","s",2,p=>p.k.presszeitS]);
  return l;
}
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
  sitName=d.sit?d.sit.name:"";
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

// ---------- Tabelle, Statistik, Details ----------
function statistik(){
  const {r,n,riss}=kennStat();
  $("statSub").innerHTML=n+" gültige Pressungen (ohne Abbrüche)"+(riss?' · <span class="warn">'+riss+' mit Rissverdacht</span>':'');
  if(!n){$("stat").innerHTML="";return;}
  let h='<table><tr><th>Kennwert</th><th>Mittel</th><th>σ</th><th>σ %</th><th>Min</th><th>Max</th><th>Spannweite</th></tr>';
  for(const m of kennListe()){
    const x=r[m[0]];if(!x)continue;
    const d=kstellen(m);
    h+='<tr><td>'+m[1]+' ['+keinheit(m[2])+']</td><td><b>'+fmt(x.m,d)+'</b></td><td>'+fmt(x.s,d+1)+'</td><td>'+
      (x.m!==0&&m[2]!=="%"?fmt(100*x.s/Math.abs(x.m),2):"–")+'</td><td>'+fmt(x.min,d)+'</td><td>'+fmt(x.max,d)+'</td><td>'+fmt(x.max-x.min,d)+'</td></tr>';
  }
  $("stat").innerHTML=h+'</table>';
}
function tabelle(){
  const F=E.n;
  let h='<thead><tr><th></th><th>Nr</th><th>Zeit</th><th>Fügekraft '+F+'</th><th>Spitze '+F+'</th><th>Steigung '+F+'/mm</th>'+
    '<th>Laufruhe '+F+'</th><th>Abfall %</th><th>Fend '+F+'</th><th>Status</th></tr></thead><tbody>';
  for(const p of [...pressen.values()].sort((a,b)=>b.k.nr-a.k.nr)){
    const k=p.k,a=p.a;
    const st=(STATUS[k.status]||"?")+(k.sim?' (Sim.)':'');
    h+='<tr data-nr="'+k.nr+'"'+(k.nr===auswahl?' class="sel"':'')+'><td><input type="checkbox" title="Kurve anzeigen"'+
      (versteckt.has(k.nr)?'':' checked')+'></td><td>'+k.nr+'</td><td>'+(k.zeit?new Date(k.zeit*1000).toLocaleTimeString("de-DE"):"–")+
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
  if(!p){$("detTitel").textContent="Pressung";$("detSub").textContent="";
    $("details").innerHTML="<dt>Noch keine Pressung ausgewählt.</dt><dd></dd>";return;}
  const k=p.k,a=p.a,{r}=kennStat(),liste=kennListe();
  $("detTitel").textContent="Pressung "+k.nr;
  $("detSub").innerHTML=zeitText(k.zeit)+' · <span class="'+(k.status?'warn':'ok')+'">'+(STATUS[k.status]||"?")+'</span>'+
    (k.sim?' · <span class="warn">Simulation</span>':'')+(a.riss?' · <span class="warn">⚠ Rissverdacht</span>':'');
  // Zeile für einen Kennwert aus der Liste, mit Abweichung zum Sitzungsmittel
  const zeile=(id,zusatz)=>{
    const m=liste.find(x=>x[0]===id);if(!m)return'';
    const v=kwert(p,m),d=kstellen(m),x=r[id];
    let abw='';
    if(x&&x.n>=2&&isFinite(v)&&!k.status){const dv=v-x.m;
      abw='<i>'+(dv>=0?"+":"")+fmt(dv,d)+(x.m&&m[2]!=="%"?' ('+(dv>=0?"+":"")+fmt(100*dv/Math.abs(x.m),1)+' %)':'')+' zum Mittel</i>';}
    return '<dt>'+m[1]+'</dt><dd><b>'+fmt(v,d)+'</b> '+keinheit(m[2])+(zusatz||'')+abw+'</dd>';
  };
  const bei=mm=>isFinite(mm)?' <i>bei '+fmt(mm,3)+' mm</i>':'';
  const gr=t=>'<div class="gruppe">'+t+'</div>';
  let h=gr("Fügen")+zeile("fuege",bei(a.fuegeMm))+zeile("spitze",bei(a.spitzeMm))+
    zeile("steigung")+zeile("achse")+zeile("laufruhe")+zeile("parbeit")+
    (isFinite(a.p0)?'<dt>Plateaufenster</dt><dd>'+fmt(a.p0,3)+' – '+fmt(a.p1,3)+' mm ab Kontakt</dd>':'')+
    zeile("abfall",(isFinite(a.abfallN)&&a.abfallN>0?' <i>('+fk(a.abfallN)+bei(a.abfallMm).replace(' <i>',' ').replace('</i>','')+')</i>':''));
  const fl=fensterListe();
  if(fl.length){h+=gr("Wegfenster ab Kontakt");fl.forEach((_,i)=>{h+=zeile("f"+i);});}
  h+=gr("Pressung")+zeile("kontakt")+zeile("tiefe")+zeile("fend")+
    '<dt>Höchstkraft gesamt (inkl. Anschlag)</dt><dd>'+fk(k.fmaxN)+bei(k.fmaxMm-(k.kontakt?k.kontaktMm:0))+'</dd>'+
    (k.haltMs?'<dt>Kraft nach Haltezeit</dt><dd>'+fk(k.fhaltN)+'</dd>':'')+
    zeile("arbeit")+zeile("zeit")+'<dt>Zykluszeit</dt><dd>'+fmt(k.zykluszeitS,1)+' s</dd>'+
    '<dt>Ende durch</dt><dd>'+(GRUND[k.grund]||"?")+'</dd>'+
    '<dt>Proben</dt><dd>'+k.proben+' bei '+fmt(k.sps,0)+' SPS'+(k.verloren?' <span class="warn">('+k.verloren+' verloren)</span>':'')+'</dd>'+
    '<dt>Einstellungen</dt><dd>'+fmt(k.vPress,2)+' mm/s, Haltezeit '+k.haltMs+' ms</dd>';
  $("details").innerHTML=h;
}

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
  const L=62,R=14,T=12,B=38,pw=w-L-R,ph=h-T-B;
  const ks=aktuelleKurven();
  const b=ansicht||bereich(ks);
  geo={L,T,pw,ph,...b};
  const {X,Y,st}=achsen(ctx,geo,abKontakt()?"Weg ab Kontakt [mm]":"Weg [mm]  (0 = Werkzeug-Endpunkt, + = tiefer)","Kraft ["+E.n+"]");

  ctx.save();ctx.beginPath();ctx.rect(L,T,pw,ph);ctx.clip();
  const a=pressen.get(auswahl);
  // Plateaufenster der Auswahl schattieren
  if(a&&a.x&&isFinite(a.a.p0)){
    const off=versatz(a)-(a.k.kontaktMm||0);           // Weg ab Kontakt -> Anzeige
    ctx.fillStyle="rgba(58,226,58,.07)";
    ctx.fillRect(X(a.a.p0-off),T,X(a.a.p1-off)-X(a.a.p0-off),ph);
  }
  if(b.x0<0&&b.x1>0){ctx.setLineDash([5,5]);ctx.strokeStyle=st.getPropertyValue("--linie");ctx.beginPath();ctx.moveTo(X(0),T);ctx.lineTo(X(0),T+ph);ctx.stroke();ctx.setLineDash([]);}
  const linie=(x,y,off,farbe,breite,strich)=>{
    ctx.strokeStyle=farbe;ctx.lineWidth=breite;ctx.setLineDash(strich?[6,4]:[]);ctx.beginPath();let auf=false;
    for(let i=0;i<y.length;i++){if(!isFinite(y[i]))continue;const px=X(x[i]-off),py=Y(y[i]*E.f);if(auf)ctx.lineTo(px,py);else{ctx.moveTo(px,py);auf=true;}}
    ctx.stroke();ctx.setLineDash([]);};
  const neueste=Math.max(0,...ks.map(p=>p.k.nr));
  for(const p of ks){
    if(p.k.nr===auswahl)continue;
    const farbe=p.k.status?"rgba(255,92,92,.55)":p.a.riss?"rgba(255,176,32,.8)":p.k.nr===neueste?"rgba(58,226,58,.85)":"rgba(58,226,58,.28)";
    linie(p.x,p.y,versatz(p),farbe,p.k.nr===neueste?1.6:1.1,!!p.k.status);
  }
  if(a&&a.x&&isFinite(versatz(a))){
    const off=versatz(a),ok=a.k.kontaktMm||0,sx=v=>X(v+ok-off);   // sx: Weg ab Kontakt -> Pixel
    linie(a.x,a.y,off,"#ffffff",2.4,false);
    const aa=a.a;
    if(isFinite(aa.steigung)){                                       // Plateau-Gerade
      ctx.strokeStyle=st.getPropertyValue("--akzent");ctx.lineWidth=1.5;ctx.setLineDash([4,3]);ctx.beginPath();
      ctx.moveTo(sx(aa.p0),Y((aa.achse+aa.steigung*aa.p0)*E.f));ctx.lineTo(sx(aa.p1),Y((aa.achse+aa.steigung*aa.p1)*E.f));
      ctx.stroke();ctx.setLineDash([]);
    }
    ctx.fillStyle="#ffffff";
    if(isFinite(aa.fuegeN)){ctx.beginPath();ctx.arc(sx(aa.fuegeMm),Y(aa.fuegeN*E.f),4.5,0,7);ctx.fill();}
    if(isFinite(aa.spitzeN)){const px=sx(aa.spitzeMm),py=Y(aa.spitzeN*E.f);
      ctx.beginPath();ctx.moveTo(px,py-9);ctx.lineTo(px-5,py-2);ctx.lineTo(px+5,py-2);ctx.closePath();ctx.fill();}
    if(aa.abfallN>0&&isFinite(aa.abfallMm)){                         // ✕ am Beginn des größten Kraftabfalls
      let fy=NaN;
      for(let i=0;i<a.x.length;i++)if(isFinite(a.y[i])&&a.x[i]-ok>=aa.abfallMm){fy=a.y[i];break;}
      if(isFinite(fy)){const px=sx(aa.abfallMm),qy=Y(fy*E.f);
        ctx.strokeStyle=aa.riss?st.getPropertyValue("--fehler"):"#ffffff";ctx.lineWidth=2;
        ctx.beginPath();ctx.moveTo(px-5,qy-5);ctx.lineTo(px+5,qy+5);ctx.moveTo(px+5,qy-5);ctx.lineTo(px-5,qy+5);ctx.stroke();}
    }
  }
  const lv=liveVersatz();
  if(live.lauf&&live.y.length&&isFinite(lv))linie(live.y.map((_,i)=>live.s+i*live.d),live.y,lv,st.getPropertyValue("--warn"),2.2,false);
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
  if(geo){const m=mausWerte(e,geo,cv);$("maus").textContent="Weg "+fmt(m.x,3)+" mm · Kraft "+fmt(m.y,E.d)+" "+E.n;}
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
  if(!m||!pk.length){tctx.fillStyle="#93a393";tctx.font="13px system-ui,sans-serif";tctx.fillText("noch keine ausgewerteten Pressungen",70,40);return;}
  const n=pk.length,mw=pk.reduce((a,q)=>a+q.v,0)/n,s=n>1?Math.sqrt(pk.reduce((a,q)=>a+(q.v-mw)*(q.v-mw),0)/(n-1)):0;
  let y0=Math.min(...pk.map(q=>q.v)),y1=Math.max(...pk.map(q=>q.v));
  if(n>=3){y0=Math.min(y0,mw-3*s);y1=Math.max(y1,mw+3*s);}
  if(!(y1>y0)){y0-=1;y1+=1;}
  const pad=(y1-y0)*0.08;
  const L=62,R=14,T=10,B=34;
  tgeo={L,T,pw:w-L-R,ph:h-T-B,x0:pk[0].nr-0.5,x1:pk[n-1].nr+0.5,y0:y0-pad,y1:y1+pad,pk};
  const {X,Y,st}=achsen(tctx,tgeo,"Pressung Nr.",m[1]+" ["+keinheit(m[2])+"]",0);
  tctx.save();tctx.beginPath();tctx.rect(L,T,tgeo.pw,tgeo.ph);tctx.clip();
  const hl=(v,strich)=>{tctx.strokeStyle=st.getPropertyValue("--akzent");tctx.lineWidth=1;tctx.setLineDash(strich?[5,4]:[]);
    tctx.beginPath();tctx.moveTo(L,Y(v));tctx.lineTo(L+tgeo.pw,Y(v));tctx.stroke();tctx.setLineDash([]);};
  hl(mw,false);
  if(n>=3&&s>0){hl(mw+3*s,true);hl(mw-3*s,true);}
  tctx.strokeStyle="rgba(58,226,58,.35)";tctx.lineWidth=1;tctx.beginPath();
  pk.forEach((q,i)=>{const x=X(q.nr),y=Y(q.v);if(i)tctx.lineTo(x,y);else tctx.moveTo(x,y);});tctx.stroke();
  for(const q of pk){
    const aus=n>=3&&s>0&&Math.abs(q.v-mw)>3*s;
    tctx.fillStyle=q.nr===auswahl?"#ffffff":(q.riss||aus)?st.getPropertyValue("--warn"):st.getPropertyValue("--akzent");
    tctx.beginPath();tctx.arc(X(q.nr),Y(q.v),q.nr===auswahl?5:3.2,0,7);tctx.fill();
  }
  tctx.restore();
  const d=kstellen(m);
  $("trendSub").textContent="Mittel "+fmt(mw,d)+" · σ "+fmt(s,d+1)+" "+keinheit(m[2])+" · n = "+n+
    " · gestrichelt: ±3σ, orange: außerhalb ±3σ oder Rissverdacht · Klick: Pressung auswählen";
}
tcv.addEventListener("click",e=>{
  if(!tgeo)return;
  const m=mausWerte(e,tgeo,tcv);
  let best=0,bd=Infinity;
  for(const q of tgeo.pk){const d=Math.abs(q.nr-m.x);if(d<bd){bd=d;best=q.nr;}}
  if(best&&bd<Math.max(1,(tgeo.x1-tgeo.x0)/40))waehlen(best);
});

// ---------- Auswertung als CSV (Excel: ; und Dezimalkomma) ----------
$("awCsv").onclick=()=>{
  const z=(v,d)=>isFinite(v)?Number(v).toFixed(Math.max(0,d)).replace(".",","):"";
  const liste=kennListe(),{r}=kennStat();
  const zeilen=[];
  zeilen.push("Auswertung Sitzung;"+sitId+";"+sitName);
  zeilen.push("Erstellt;"+new Date().toLocaleString("de-DE"));
  zeilen.push("Krafteinheit;"+E.n);
  zeilen.push("Plateaufenster [% der Fügetiefe];"+AW.von+" - "+AW.bis);
  zeilen.push("Anschlag ausgeschlossen [mm];"+z(AW.ende,2));
  zeilen.push("Rissverdacht ab Kraftabfall [%];"+z(AW.abfall,1)+";innerhalb [mm];"+z(AW.strecke,3));
  zeilen.push("");
  zeilen.push(["Nr","Zeit","Status","Simulation","Rissverdacht","Weg Fügekraft [mm]","Weg Einführspitze [mm]","Weg Kraftabfall [mm]"]
    .concat(liste.map(m=>m[1]+" ["+keinheit(m[2])+"]")).join(";"));
  for(const p of [...pressen.values()].sort((a,b)=>a.k.nr-b.k.nr)){
    const k=p.k,a=p.a;
    zeilen.push([k.nr,k.zeit?new Date(k.zeit*1000).toLocaleString("de-DE"):"",STATUS[k.status]||"?",k.sim?"ja":"",a.riss?"ja":"",
      z(a.fuegeMm,3),z(a.spitzeMm,3),z(a.abfallMm,3)].concat(liste.map(m=>z(kwert(p,m),kstellen(m)+1))).join(";"));
  }
  zeilen.push("");
  zeilen.push("Statistik (gültige Pressungen);n;Mittel;Sigma;Sigma %;Min;Max;Spannweite");
  for(const m of liste){const x=r[m[0]];if(!x)continue;const d=kstellen(m)+1;
    zeilen.push([m[1]+" ["+keinheit(m[2])+"]",x.n,z(x.m,d),z(x.s,d+1),x.m&&m[2]!=="%"?z(100*x.s/Math.abs(x.m),2):"",z(x.min,d),z(x.max,d),z(x.max-x.min,d)].join(";"));}
  const blob=new Blob(["﻿"+zeilen.join("\r\n")],{type:"text/csv;charset=utf-8"});
  const link=document.createElement("a");
  link.href=URL.createObjectURL(blob);
  link.download="sitzung_"+String(sitId).padStart(4,"0")+"_auswertung.csv";
  document.body.appendChild(link);link.click();link.remove();
  setTimeout(()=>URL.revokeObjectURL(link.href),5000);
};

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

trendAuswahl();
listeLaden();
abfragen();
requestAnimationFrame(zeichnen);
</script>
</body></html>
)PRESSE";
