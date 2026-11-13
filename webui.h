/*
 * webui.h - Weboberfläche für Waegezelle_Test (eine Seite, ohne externe Dateien)
 *
 * Der Browser fragt 4x pro Sekunde /daten ab (JSON: Graphpunkte, Statistik,
 * Konfiguration, neue Log-Zeilen) und schickt Befehle per POST an /cmd.
 * Die Umrechnung Counts -> Gramm/µV passiert im Browser mit den Werten aus "cfg".
 */
#pragma once

static const char WEBUI_HTML[] = R"WEBUI(<!doctype html>
<html lang="de"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Wägezelle</title>
<link rel="stylesheet" href="/stil.css"></head><body><main>
<header>
  <div class="marke">
    <div class="logo"><svg width="20" height="20" viewBox="0 0 20 20" fill="none" stroke="#031003" stroke-width="2.2"
      stroke-linecap="round" stroke-linejoin="round"><polyline points="1,11 5,11 7.5,4 11,16 13.5,8 15.5,11 19,11"/></svg></div>
    <h1>EcklC <em>OpenSmartPress</em> <span>Wägezelle · ADS1256</span></h1>
  </div>
  <div class="kopfrechts">
    <div class="status" id="status">verbinde …</div>
    <a class="chip" href="/info">Hilfe &amp; Tipps</a>
  </div>
</header>

<div class="oben">
  <div class="karte glow">
    <div class="titel">Gewicht</div>
    <div><span class="wert" id="wert">–</span><span class="einh" id="wertEinh">g</span></div>
    <div class="sub" id="wertSub">&nbsp;</div>
  </div>
  <div class="karte">
    <div class="titel">Steuerung</div>
    <div class="leiste">
      <label>Datenrate <select id="rate"></select></label>
      <label>Anzeige <select id="einheit"><option>Gramm</option><option>µV</option><option>Counts</option></select></label>
      <label>Fenster [s] <input type="number" id="fenster" value="10" min="1" max="600" step="1"></label>
      <label>Glättung <select id="fl"><option value="0">aus</option><option value="0.02">0,02 s</option>
        <option value="0.05">0,05 s</option><option value="0.1">0,1 s</option><option value="0.2">0,2 s</option>
        <option value="0.5">0,5 s</option><option value="1">1 s</option><option value="2">2 s</option>
        <option value="5">5 s</option><option value="10">10 s</option><option value="30">30 s</option>
        <option value="60">60 s</option></select></label>
      <label><input type="checkbox" id="roh" checked> Rohsignal</label>
      <label>Statistik [s] <select id="ws"><option>0.5</option><option>1</option><option>2</option><option>5</option><option>10</option><option>30</option><option>60</option></select></label>
      <label><input type="checkbox" id="band" checked> Min/Max-Band</label>
      <label><input type="checkbox" id="pause"> Pause</label>
    </div>
    <div class="knoepfe">
      <button class="primaer" data-c="t">Tara</button>
      <button data-c="n">Rauschtest</button>
      <button data-c="nv">Rauschvergleich</button>
      <button data-c="i">Info</button>
      <button data-c="x">Abbruch</button>
      <button id="csv">CSV speichern</button>
    </div>
  </div>
</div>

<div class="karte glow"><div class="titel">Verlauf · Zeit / Gewicht</div><canvas id="plot"></canvas></div>

<div class="unten">
  <div class="karte"><div class="titel">Live-Statistik</div><div id="stat">warte auf Daten …</div></div>
  <div class="karte">
    <div class="titel">Protokoll</div>
    <pre id="log"></pre>
    <div style="display:flex;gap:6px;margin-top:8px">
      <input type="text" id="befehl" style="flex:1;min-width:0" placeholder="Befehl, z. B. r 2000 · nv 3 500 1000 2000 · k 5000">
      <button id="senden">Senden</button>
    </div>
  </div>
</div>
</main>
<script>
"use strict";
const RATEN=[2.5,5,10,15,25,30,50,60,100,500,1000,2000,3750,7500,15000,30000];
const $=id=>document.getElementById(id);
let cfg=null,pn=null,ln=null,P=[],st=null,letzt=null,online=false,wsGesetzt=false,logRest="";

// ---------- Bedienelemente ----------
const selRate=$("rate");
for(const r of RATEN){const o=document.createElement("option");o.value=String(r);o.textContent=r+" SPS";selRate.appendChild(o);}
selRate.onchange=()=>befehl("r "+selRate.value);
$("ws").onchange=()=>befehl("ws "+$("ws").value);
const selFl=$("fl");
selFl.onchange=()=>befehl("fl "+selFl.value);
// Auswahl an die Firmware angleichen (auch wenn per Befehl ein freier Wert gesetzt wurde)
function flAngleichen(s){
  if(document.activeElement===selFl)return;
  const v=String(s);
  if(![...selFl.options].some(o=>o.value===v)){
    const o=document.createElement("option");o.value=v;o.textContent=v.replace(".",",")+" s";selFl.appendChild(o);
  }
  selFl.value=v;
}
document.querySelectorAll("button[data-c]").forEach(b=>b.onclick=()=>befehl(b.dataset.c));
$("senden").onclick=senden;
$("befehl").onkeydown=e=>{if(e.key==="Enter")senden();};
$("einheit").onchange=()=>{wertZeigen();};
function senden(){const t=$("befehl").value.trim();if(t){befehl(t);$("befehl").value="";}}
async function befehl(c){
  try{await fetch("/cmd",{method:"POST",body:new URLSearchParams({c})});}
  catch(e){logText("# Browser: Befehl '"+c+"' konnte nicht gesendet werden\n");}
}

// ---------- Log ----------
const logEl=$("log");
function logText(t){
  if(!t)return;
  const unten=logEl.scrollTop+logEl.clientHeight>=logEl.scrollHeight-4;
  logEl.textContent+=t;
  if(logEl.textContent.length>60000)logEl.textContent=logEl.textContent.slice(-40000);
  if(unten)logEl.scrollTop=logEl.scrollHeight;
}
function logAufnehmen(neu){
  const s=logRest+neu, k=s.lastIndexOf("\n");
  if(k<0){logRest=s;return;}
  logRest=s.slice(k+1);
  logText(s.slice(0,k+1).split("\n").filter(z=>!z.startsWith("#CFG")).join("\n"));
}

// ---------- Umrechnung ----------
function umrechnen(c){
  const e=$("einheit").value;
  if(e==="Gramm")return (c-cfg.tara)/cfg.faktor;
  if(e==="µV")return c*cfg.uv;
  return c;
}
function einheitKurz(){return {"Gramm":"g","µV":"µV","Counts":"Counts"}[$("einheit").value];}
const f=(v,d)=>Number(v).toFixed(d);
const de=v=>Math.round(v).toLocaleString("de-DE");

// ---------- Abfrage ----------
async function abfragen(){
  let mehr=false;
  try{
    const q=new URLSearchParams();
    if(pn!==null)q.set("p",pn);
    if(ln!==null)q.set("l",ln);
    const r=await fetch("/daten?"+q,{cache:"no-store"});
    const d=await r.json();
    online=true;verarbeiten(d);mehr=!!d.mehr;
  }catch(e){online=false;statusZeigen();}
  setTimeout(abfragen,mehr?0:250);
}
function verarbeiten(d){
  if(pn!==null&&d.pn<pn){P=[];logText("# Browser: Pico wurde neu gestartet\n");}
  cfg=d.cfg;letzt=d;
  for(const p of d.p)P.push(p);
  pn=d.pn;
  logAufnehmen(d.log);ln=d.ln;
  if(d.st&&(!st||d.st.id!==st.id)){st=d.st;statZeigen();}
  if(P.length){
    const tmin=P[P.length-1][0]-600000;let i=0;
    while(i<P.length&&P[i][0]<tmin)i++;
    if(i)P.splice(0,i);
  }
  if(document.activeElement!==selRate)selRate.value=String(cfg.rate);
  flAngleichen(cfg.filter);
  if(!wsGesetzt){$("ws").value=String(d.ws);wsGesetzt=true;}
  wertZeigen();statusZeigen();
}

// ---------- Anzeigen ----------
function statusZeigen(){
  const s=$("status");
  if(!online||!letzt){s.innerHTML='<span class="chip"><span class="punkt aus"></span>keine Verbindung zum Pico</span>';return;}
  const d=letzt;
  const zustand=d.ok?(d.kont?'<span class="punkt"></span>misst':'<span class="punkt aus"></span>ADC angehalten')
                     :'<span class="punkt aus"></span>ADC nicht initialisiert';
  let h='<span class="chip">'+zustand+'</span>'+
        '<span class="chip"><b>'+cfg.rate+'</b>SPS</span>'+
        '<span class="chip">WLAN <b>'+d.rssi+'</b>dBm</span>'+
        '<span class="chip">verloren <b>'+d.verl+'</b></span>';
  if(d.auftrag)h+='<span class="chip warn"><span class="warn">Auftrag läuft</span></span>';
  s.innerHTML=h;
}
function wertZeigen(){
  if(!cfg||!letzt)return;
  const e=$("einheit").value,v=umrechnen(letzt.w);
  const dez=e==="Gramm"?1:e==="µV"?3:0;
  $("wert").textContent=f(v,dez);
  $("wertEinh").textContent=einheitKurz();
  let sub=e==="Gramm"?"= "+f(v/1000,3)+" kg":"";
  if(!cfg.kal)sub+=' <span class="warn">(Faktor unkalibriert)</span>';
  sub+=cfg.filter>0?" · geglättet über "+f(cfg.filter_ist,3).replace(".",",")+" s":" · ungeglättet";
  $("wertSub").innerHTML=sub;
}
function statZeigen(){
  if(!st||!cfg)return;
  const uv=cfg.uv,fa=Math.abs(cfg.faktor)||1,g=c=>(c-cfg.tara)/cfg.faktor;
  let h="<b>Statistik über "+de(st.n)+" Werte ("+f(st.n/cfg.rate,2)+" s, ungefiltert)</b>"+
    "<table><tr><th></th><th>Counts</th><th>µV</th><th>g</th></tr>"+
    "<tr><td>Mittelwert</td><td>"+f(st.mittel,1)+"</td><td>"+f(st.mittel*uv,3)+"</td><td>"+f(g(st.mittel),3)+"</td></tr>"+
    "<tr><td>Std.-Abw. σ</td><td>"+f(st.sigma,3)+"</td><td>"+f(st.sigma*uv,4)+"</td><td>"+f(st.sigma/fa,4)+"</td></tr>"+
    "<tr><td>Spitze-Spitze</td><td>"+f(st.pp,0)+"</td><td>"+f(st.pp*uv,3)+"</td><td>"+f(st.pp/fa,3)+"</td></tr>";
  if(st.s10>0)h+="<tr><td>σ @10 SPS</td><td>"+f(st.s10,3)+"</td><td>"+f(st.s10*uv,4)+"</td><td>"+f(st.s10/fa,4)+"</td></tr>";
  if(cfg.filter>0&&st.sg>0)h+="<tr><td>σ geglättet</td><td>"+f(st.sg,3)+"</td><td>"+f(st.sg*uv,4)+"</td><td><b>"+f(st.sg/fa,4)+"</b></td></tr>";
  h+="</table>";
  const enob=st.sigma>0?Math.log2(16777216/st.sigma):24,nfb=st.sigma>0?Math.log2(16777216/(6.6*st.sigma)):24;
  h+='<div style="margin-top:6px"><b>Effektive Auflösung:</b> '+f(enob,2)+" Bit · <b>rauschfrei:</b> "+f(nfb,2)+" Bit</div>";
  if(st.sigma>0){const teile=cfg.nennlast*fa/st.sigma;
    h+="<div><b>Bezogen auf Nennlast:</b> "+de(teile)+" Teile = "+f(Math.log2(teile),2)+" Bit</div>";}
  if(st.s10>0){const t10=cfg.nennlast*fa/st.s10;
    h+="<div><b>Nach Mittelung auf 10 SPS:</b> "+de(t10)+" Teile = "+f(Math.log2(t10),2)+" Bit</div>";}
  h+='<div class="sub" style="margin-top:4px">Rate gemessen '+f(st.ri,2)+" SPS · verloren "+st.verl+
    (st.clip?' · <span class="warn">ÜBERSTEUERT</span>':"")+(cfg.kal?"":' · <span class="warn">g unkalibriert</span>')+"</div>";
  $("stat").innerHTML=h;
}

// ---------- Diagramm Zeit / Gewicht ----------
function schritt(span,n){
  const roh=span/n,p=Math.pow(10,Math.floor(Math.log10(roh))),q=roh/p;
  return (q<1.5?1:q<3?2:q<7?5:10)*p;
}
let letztesBild=0;
function zeichnen(ts){
  requestAnimationFrame(zeichnen);
  if(ts-letztesBild<50)return;            // max. 20 Bilder/s
  letztesBild=ts;
  if($("pause").checked||!cfg)return;
  const cv=$("plot"),dpr=window.devicePixelRatio||1,W=cv.clientWidth,H=cv.clientHeight;
  if(cv.width!==Math.round(W*dpr)||cv.height!==Math.round(H*dpr)){cv.width=Math.round(W*dpr);cv.height=Math.round(H*dpr);}
  const ctx=cv.getContext("2d");
  ctx.setTransform(dpr,0,0,dpr,0,0);ctx.clearRect(0,0,W,H);
  const css=getComputedStyle(document.documentElement),farbe=n=>css.getPropertyValue(n).trim();
  ctx.font="12px system-ui,sans-serif";ctx.fillStyle=farbe("--leise");
  if(!P.length){ctx.fillText("warte auf Messwerte …",20,30);return;}

  const fs=Math.max(1,+$("fenster").value||10),tEnde=P[P.length-1][0],tStart=tEnde-fs*1000;
  let i0=P.length-1;while(i0>0&&P[i0-1][0]>=tStart)i0--;
  const n=P.length-i0,band=$("band").checked,roh=$("roh").checked,glatt=cfg.filter>0;
  const t=new Float64Array(n),ym=new Float64Array(n),ylo=new Float64Array(n),yhi=new Float64Array(n),yg=new Float64Array(n);
  let lo=Infinity,hi=-Infinity;
  const nimm=v=>{if(v<lo)lo=v;if(v>hi)hi=v;};
  for(let k=0;k<n;k++){
    const p=P[i0+k];t[k]=(p[0]-tEnde)/1000;ym[k]=umrechnen(p[1]);
    const a=umrechnen(p[2]),b=umrechnen(p[3]);ylo[k]=Math.min(a,b);yhi[k]=Math.max(a,b);
    yg[k]=p.length>4?umrechnen(p[4]):NaN;          // geglätteter Wert aus der Firmware
    if(band){nimm(ylo[k]);nimm(yhi[k]);}
    if(roh||!glatt)nimm(ym[k]);
    if(glatt&&isFinite(yg[k]))nimm(yg[k]);
  }
  if(!(hi>lo)){const m=isFinite(lo)?lo:0;lo=m-1;hi=m+1;}
  const pad=(hi-lo)*0.06;lo-=pad;hi+=pad;

  // Achsen und Gitter
  const ys=schritt(hi-lo,6),dez=Math.max(0,-Math.floor(Math.log10(ys)+1e-9));
  const yt=[];for(let v=Math.ceil(lo/ys)*ys;v<=hi;v+=ys)yt.push(v);
  let lw=0;for(const v of yt)lw=Math.max(lw,ctx.measureText(f(v,dez)).width);
  const L=lw+14,R=12,T=18,B=26,pw=W-L-R,ph=H-T-B;
  const X=tt=>L+(tt+fs)/fs*pw,Y=v=>T+(hi-v)/(hi-lo)*ph;
  ctx.strokeStyle=farbe("--gitter");ctx.lineWidth=1;ctx.textAlign="right";ctx.textBaseline="middle";
  for(const v of yt){const y=Math.round(Y(v))+.5;ctx.beginPath();ctx.moveTo(L,y);ctx.lineTo(L+pw,y);ctx.stroke();ctx.fillText(f(v,dez),L-6,y);}
  const xs=schritt(fs,8),xdez=Math.max(0,-Math.floor(Math.log10(xs)+1e-9));
  ctx.textAlign="center";ctx.textBaseline="top";
  for(let v=-Math.floor(fs/xs)*xs;v<=1e-9;v+=xs){const x=Math.round(X(v))+.5;
    ctx.beginPath();ctx.moveTo(x,T);ctx.lineTo(x,T+ph);ctx.stroke();ctx.fillText(f(v,xdez)+" s",x,T+ph+6);}
  ctx.textAlign="left";ctx.textBaseline="alphabetic";ctx.fillText(einheitKurz(),4,12);

  ctx.save();ctx.beginPath();ctx.rect(L,T,pw,ph);ctx.clip();
  // Lücken (verlorene Daten, Pause) nicht verbinden
  const intervall=1000*cfg.gl/cfg.rate,luecke=Math.max(5*intervall,200)/1000;
  const laeufe=[];let s0=0;
  for(let k=1;k<=n;k++){if(k===n||t[k]-t[k-1]>luecke){laeufe.push([s0,k]);s0=k;}}
  if(band){ctx.fillStyle=farbe("--band");
    for(const [a,b] of laeufe){ctx.beginPath();ctx.moveTo(X(t[a]),Y(yhi[a]));
      for(let k=a+1;k<b;k++)ctx.lineTo(X(t[k]),Y(yhi[k]));
      for(let k=b-1;k>=a;k--)ctx.lineTo(X(t[k]),Y(ylo[k]));ctx.closePath();ctx.fill();}}
  function linie(y,farb,breite){ctx.strokeStyle=farb;ctx.lineWidth=breite;ctx.beginPath();
    for(const [a,b] of laeufe){let neu=true;for(let k=a;k<b;k++){if(!isFinite(y[k])){neu=true;continue;}
      if(neu){ctx.moveTo(X(t[k]),Y(y[k]));neu=false;}else ctx.lineTo(X(t[k]),Y(y[k]));}}ctx.stroke();}
  if(roh||!glatt)linie(ym,glatt?farbe("--linie"):farbe("--ma"),glatt?1.25:1.6);
  if(glatt){                                   // geglättete Linie mit Neon-Leuchten
    ctx.shadowColor=farbe("--glow");ctx.shadowBlur=10;
    linie(yg,farbe("--ma"),2.2);
    ctx.shadowBlur=0;
  }
  ctx.restore();
}

// ---------- CSV der Graphdaten ----------
$("csv").onclick=()=>{
  if(!P.length||!cfg)return;
  let s="# Waegezelle Graphdaten (Mittel/Min/Max je Punkt), rate="+cfg.rate+" SPS, Einheit="+einheitKurz()+"\n";
  s+="# tara="+cfg.tara+" faktor="+cfg.faktor+" uv_count="+cfg.uv+" glaettung_s="+cfg.filter+"\nt_ms,mittel,min,max,geglaettet\n";
  for(const p of P)s+=p[0]+","+umrechnen(p[1])+","+umrechnen(p[2])+","+umrechnen(p[3])+","+(p.length>4?umrechnen(p[4]):"")+"\n";
  const a=document.createElement("a");
  a.href=URL.createObjectURL(new Blob([s],{type:"text/csv"}));
  a.download="waegezelle_"+new Date().toISOString().replace(/[:.]/g,"-")+".csv";
  document.body.appendChild(a);a.click();a.remove();
  setTimeout(()=>URL.revokeObjectURL(a.href),2000);
};

abfragen();
requestAnimationFrame(zeichnen);
</script>
</body></html>
)WEBUI";
