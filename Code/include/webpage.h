#pragma once
#include <pgmspace.h>

// The whole configuration UI, served from flash as one page. Nothing is fetched
// from the network: the board is usually the only thing the phone can reach.
//
// The form is built from /api/config at runtime and knows nothing about which
// fields exist, so adding a setting to the firmware needs no edit here.

static const char kConfigPage[] PROGMEM = R"HTML(<!doctype html>
<html lang="en"><head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover">
<meta name="color-scheme" content="dark">
<title>Claude Notifier</title>
<style>
:root{--bg:#151210;--pan:#1e1a17;--ln:#332b26;--fg:#ece5de;--dim:#9b8f86;--acc:#f0620c;--bad:#e0483a;--ok:#7bc86c;--ink:#0d0b0a;
 --fld:#f0620c}
*{box-sizing:border-box}
body{margin:0 auto;max-width:640px;padding:0 12px calc(84px + env(safe-area-inset-bottom));
 background:var(--bg);color:var(--fg);
 font:15px/1.45 ui-monospace,SFMono-Regular,Menlo,Consolas,monospace;-webkit-text-size-adjust:100%}
h1{font-size:15px;letter-spacing:.14em;text-transform:uppercase;margin:0}
h2{font-size:11px;letter-spacing:.18em;text-transform:uppercase;color:var(--acc);margin:0 0 4px}
h3{font-size:11px;letter-spacing:.18em;text-transform:uppercase;color:var(--dim);margin:14px 0 6px}
p{margin:0}
.off{display:none;position:sticky;top:0;z-index:9;margin:0 -12px;padding:9px;background:var(--bad);color:#1a0705;
 font-weight:700;text-align:center;letter-spacing:.06em}
body.down .off{display:block}
.msg{display:none;margin:12px 0 0;padding:10px 12px;border:2px solid var(--acc);border-left-width:6px;
 background:#241609;font-size:13px}
.msg.on{display:block}
header{display:flex;align-items:center;gap:12px;padding:14px 0 2px}
#mood{color:var(--acc);font-weight:700;letter-spacing:.12em;font-size:13px}

/* Mascot. Two bar eyes on an orange field, same as the LCD, driven by a class
   per mood so the panel shows the live face without asking for pixels. */
.face{flex:0 0 auto;position:relative;width:62px;height:68px;background:var(--acc);border:3px solid #000;
 box-shadow:4px 4px 0 #000;display:flex;align-items:center;justify-content:center;gap:9px}
.eye{width:10px;height:26px;background:var(--ink);transition:height .2s,transform .2s}
.m-chill .eye{animation:blink 4.4s infinite}
.m-sleeping .eye{height:5px;animation:breathe 3.6s ease-in-out infinite}
.m-waking .eye{animation:wake .9s ease-out infinite}
.m-bored .eye{height:15px;transform:translateY(7px)}
.m-working .eye{animation:sweep 1.3s ease-in-out infinite alternate}
.m-confused .eye:last-child{height:11px}
.m-angry{background:var(--bad)}
.m-angry .eye{height:19px;transform:rotate(20deg)}
.m-angry .eye:last-child{transform:rotate(-20deg)}
.m-excited .eye{display:none}
.m-excited:after{content:"><";color:var(--ink);font-size:27px;font-weight:700;letter-spacing:5px;
 animation:bounce .45s ease-in-out infinite alternate}
@keyframes blink{0%,92%,100%{height:26px}96%{height:4px}}
@keyframes breathe{0%,100%{height:5px}50%{height:8px}}
@keyframes wake{0%{height:6px}100%{height:26px}}
@keyframes sweep{0%{transform:translateX(-5px)}100%{transform:translateX(5px)}}
@keyframes bounce{0%{transform:translateY(2px)}100%{transform:translateY(-4px)}}

/* Chips size to their content and wrap: an IP is much wider than an uptime,
   and a fixed grid clips it. */
.stats{display:flex;flex-wrap:wrap;gap:6px;margin:12px 0}
.st{flex:1 1 auto;background:var(--pan);border:2px solid var(--ln);padding:5px 8px;white-space:nowrap}
.st b{display:block;font-size:10px;font-weight:400;letter-spacing:.14em;text-transform:uppercase;color:var(--dim)}
.st span{font-size:14px}
.card{background:var(--pan);border:2px solid var(--ln);box-shadow:4px 4px 0 #0b0908;padding:11px 12px;margin:12px 0}
.card.dgr{border-color:#4a2b26}
.fld{display:flex;flex-wrap:wrap;align-items:center;gap:10px;padding:11px 0}
.fld+.fld{border-top:2px solid var(--ln)}
.fld.col>label{flex:1 1 100%}
label{flex:1 1 auto;min-width:0}
.hint{flex:0 0 100%;color:var(--dim);font-size:12px}
input,select,.btn{font:inherit;border-radius:0}
input[type=number],input[type=text],input[type=password],select{background:#0f0d0c;color:var(--fg);
 border:2px solid var(--ln);padding:10px;min-height:46px}
input[type=number]{width:104px;text-align:right;flex:0 0 auto}
input[type=text],input[type=password]{flex:1 1 auto;min-width:0;width:100%}
input::placeholder{color:#6d635b}
.sel{position:relative;flex:1 1 100%}
.sel:after{content:"\25BC";position:absolute;right:12px;top:50%;transform:translateY(-50%);color:var(--acc);
 font-size:10px;pointer-events:none}
select{width:100%;-webkit-appearance:none;appearance:none;padding-right:34px}
.rng{flex:1 1 100%;display:flex;align-items:center;gap:12px}
input[type=range]{flex:1 1 auto;min-width:0;height:44px;accent-color:var(--acc);background:none}
.out{flex:0 0 52px;text-align:right;color:var(--acc);font-weight:700}
.sw{position:relative;flex:0 0 auto;width:68px;height:40px}
.sw input{position:absolute;inset:0;width:100%;height:100%;margin:0;opacity:0}
.sw i{position:absolute;inset:0;background:#0f0d0c;border:2px solid var(--ln);pointer-events:none}
.sw i:after{content:"";position:absolute;top:3px;left:3px;width:28px;height:28px;background:var(--dim);
 transition:left .12s,background .12s}
.sw input:checked+i{border-color:var(--acc)}
.sw input:checked+i:after{left:33px;background:var(--acc)}
.btn{display:inline-block;font-weight:700;letter-spacing:.06em;background:#241f1b;color:var(--fg);
 border:2px solid var(--ln);box-shadow:3px 3px 0 #0b0908;padding:12px 14px;min-height:46px;cursor:pointer}
.btn:active{transform:translate(3px,3px);box-shadow:none}
.btn.pri{background:var(--acc);color:#1a0d04;border-color:#000}
.btn.red{color:#ffb9b0;border-color:var(--bad)}
.btn.sm{flex:0 0 auto;padding:12px 10px;font-size:12px}
.btn[disabled]{opacity:.4;box-shadow:none;transform:none;cursor:default}
.row{display:flex;flex-wrap:wrap;gap:10px}
.row .btn{flex:1 1 130px}
.row.eq .btn{flex:1 1 auto;min-width:0}

/* Pixel editor. The canvas has no border of its own, so a client rect maps
   straight onto cells; the bezel frames it. --fld is the board's field colour. */
#fpad{display:flex;justify-content:center;margin:11px 0}
#fwhich,#fnote,.row.mt{margin-top:10px}
#fbez{background:var(--fld);border:3px solid #000;box-shadow:4px 4px 0 #000;max-width:100%}
#fcv{display:block;touch-action:none;cursor:crosshair;-webkit-user-select:none;user-select:none}
.chip{display:inline-block;width:11px;height:11px;margin-right:7px;vertical-align:-1px;
 border:1px solid var(--dim);background:var(--fg)}
.chip.e{background:transparent}
.hint.ok{color:var(--ok)}
.hint.bad{color:var(--bad)}
#moods{display:grid;grid-template-columns:repeat(auto-fit,minmax(88px,1fr));gap:8px}
#moods .btn{padding:11px 4px;font-size:12px;overflow:hidden;text-overflow:ellipsis}
.bar{position:fixed;left:0;right:0;bottom:0;z-index:8;display:flex;align-items:center;gap:10px;
 max-width:640px;margin:0 auto;padding:9px 12px calc(9px + env(safe-area-inset-bottom));
 background:#100e0d;border-top:2px solid var(--ln)}
#note{flex:1 1 auto;min-width:0;font-size:12px;color:var(--dim);overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
#note.ok{color:var(--ok)}
#note.bad{color:var(--bad)}
footer{color:var(--dim);font-size:11px;text-align:center;padding:4px 0 8px}
</style></head><body>

<div class="off" id="off">Lost connection to the board. Retrying.</div>
<div class="msg" id="ap">Fallback access point. The board could not join the configured Wi-Fi, so it is
 serving this page itself at 192.168.4.1. Fix the Wi-Fi settings below and save.</div>
<div class="msg" id="wmsg"></div>

<header>
 <div class="face" id="face"><span class="eye"></span><span class="eye"></span></div>
 <div><h1>Claude Notifier</h1><div id="mood">--</div></div>
</header>

<div class="stats" id="stats"></div>
<main id="form"><p class="hint">Reading settings from the board.</p></main>

<section class="card">
 <h2>Custom face</h2>
 <p class="hint">Twenty-four by twenty-eight cells, ten screen pixels each -- the whole panel. Only
  slots with something drawn in them join the swipe rotation; custom1 to custom4 hold one.</p>
 <h3>Slot</h3>
 <div class="row eq" id="fslot"></div>
 <h3>Frame</h3>
 <div class="row">
  <button class="btn pri" id="f0">Resting</button>
  <button class="btn" id="f1">Blink</button>
 </div>
 <p class="hint" id="fwhich"></p>
 <div id="fpad"><div id="fbez"><canvas id="fcv"></canvas></div></div>
 <div class="row eq">
  <button class="btn sm pri" id="tk"><i class="chip" style="background:#000"></i>Black</button>
  <button class="btn sm" id="tw"><i class="chip" style="background:#fff"></i>White</button>
  <button class="btn sm" id="te"><i class="chip" style="background:var(--fld)"></i>Erase</button>
 </div>
 <div class="row eq mt">
  <button class="btn sm" id="fcp">Copy resting to blink</button>
  <button class="btn sm" id="fcl">Clear frame</button>
 </div>
 <p class="hint" id="fnote">Reading the face from the board.</p>
 <div class="row mt">
  <button class="btn pri" id="fsv" disabled>Save slot</button>
  <button class="btn red" id="fer">Erase slot</button>
 </div>
</section>

<section class="card">
 <h2>Test</h2>
 <div class="row"><button class="btn" id="buzz">Buzz</button><button class="btn" id="bell">Bell</button></div>
 <h3>Force mood</h3>
 <div id="moods"></div>
</section>

<section class="card dgr">
 <h2>Factory reset</h2>
 <p class="hint">Puts every setting back to its firmware default.</p>
 <div class="row" id="rst" style="margin-top:10px"></div>
</section>

<footer>configure once, then leave it alone</footer>
<div class="bar"><span id="note">Loading</span><button class="btn pri" id="save" disabled>Save</button></div>

<script>
(function(){
var CFG='/api/config',ctls={},grp={},sig='',busy=0,fails=0,tmr=0,rtmr=0,wipe=0,nid=0;
var $=function(s){return document.querySelector(s)};
var face=$('#face'),noteEl=$('#note'),saveEl=$('#save');

function el(t,c,x){var e=document.createElement(t);if(c)e.className=c;if(x!=null)e.textContent=x;return e}
function note(t,c){noteEl.textContent=t;noteEl.className=c||''}
function why(e){return e&&e.name=='AbortError'?'timed out':(e&&e.message)||'no route to the board'}

// Every request is bounded. The board is one blocking handler behind a Wi-Fi
// extender, so a request that never answers is normal and must not wedge the UI.
function req(u,o,ms){
 var a=new AbortController(),k=setTimeout(function(){a.abort()},ms||7000);
 o=o||{};o.signal=a.signal;o.cache='no-store';
 return fetch(u,o).then(function(r){clearTimeout(k);if(!r.ok)throw new Error('HTTP '+r.status);return r},
                        function(e){clearTimeout(k);throw e});
}
function post(u,b,ms){
 return req(u,{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:b},ms);
}
// encodeURIComponent, not manual escaping: Wi-Fi passwords are full of &, +, %
// and spaces, and a half-escaped one corrupts silently. Space goes out as %20
// rather than +, which any urlencoded decoder accepts.
function enc(o){var a=[],k;for(k in o)a.push(encodeURIComponent(k)+'='+encodeURIComponent(o[k]));return a.join('&')}

function get(e){
 if(e.type=='checkbox')return e.checked?1:0;
 if(e.tagName=='SELECT')return e.selectedIndex;
 if(e.dataset.txt)return e.value;
 var v=parseInt(e.value,10);
 if(isNaN(v))return +e.dataset.base;
 if(e.min!==''&&v<+e.min)v=+e.min;
 if(e.max!==''&&v>+e.max)v=+e.max;
 return v;
}
function set(e,v){
 if(e.type=='checkbox')e.checked=!!+v;
 else if(e.tagName=='SELECT')e.selectedIndex=+v;
 else{e.value=v;if(e.out)e.out.textContent=v}
}
// dataset.base is what the board last told us. Anything that differs from it is
// an unsaved edit, which is both the save payload and the do-not-touch list.
function pending(){var d={},k,e,v;for(k in ctls){e=ctls[k];v=get(e);if(String(v)!==e.dataset.base)d[k]=v}return d}
function bar(){var n=Object.keys(pending()).length;saveEl.disabled=!!busy||!n;
 saveEl.textContent=busy?'Saving':(n?'Save '+n:'Save')}
function touched(ev){var e=ev.target;if(e.out)e.out.textContent=e.value;bar()}
// One shape for a field's value however the firmware spells it: a bool as 0/1
// even if it arrives as true, a password always as the empty "unchanged".
function nrm(f){return f.type=='password'?'':f.type=='bool'?(f.value?1:0):(f.value==null?'':f.value)}

function field(f){
 var w=el('div','fld'),lab=el('label',null,f.label||f.key),c,id='c'+(++nid),lo=f.min,hi=f.max;
 var pw=f.type=='password',base=nrm(f);
 w.appendChild(lab);
 if(f.type=='text'||pw){
  c=el('input');c.type=f.type;c.value=base;
  if(f.maxlen)c.maxLength=f.maxlen;
  // Credentials and SSIDs: no autocorrect, no capitalisation, no manager fill.
  c.autocapitalize='off';c.autocomplete='off';c.spellcheck=false;c.dataset.txt=1;
  var box=el('div','rng');box.appendChild(c);
  if(pw){
   // An empty password box means "leave it alone" -- the board never sends the
   // stored secret back, so an untouched box has nothing to save.
   c.placeholder='unchanged';
   var tg=el('button','btn sm','Show');tg.type='button';
   tg.onclick=function(){var h=c.type=='password';c.type=h?'text':'password';tg.textContent=h?'Hide':'Show'};
   box.appendChild(tg);
  }
  w.className='fld col';w.appendChild(box);
 }else if(f.type=='bool'){
  var sw=el('label','sw');c=el('input');c.type='checkbox';c.checked=!!base;
  sw.appendChild(c);sw.appendChild(el('i'));w.appendChild(sw);
 }else if(f.type=='enum'){
  var esel=el('div','sel');c=el('select');
  (f.options||[]).forEach(function(o,i){var op=el('option',null,String(o));op.value=i;c.appendChild(op)});
  c.selectedIndex=+base||0;esel.appendChild(c);w.appendChild(esel);w.className='fld col';
 }else{
  // A slider beats the numeric keypad on a phone, but only where the range is
  // short enough to land on an exact value. Tone frequencies get a real input.
  var span=(lo!=null&&hi!=null)?hi-lo:1e9;
  c=el('input');c.type=span<=24?'range':'number';c.step=1;
  if(lo!=null)c.min=lo;
  if(hi!=null)c.max=hi;
  c.value=base;
  if(c.type=='range'){
   var o=el('span','out',String(base));c.out=o;w.className='fld col';
   var r=el('div','rng');r.appendChild(c);r.appendChild(o);w.appendChild(r);
  }else{
   c.inputMode='numeric';w.appendChild(c);
   c.addEventListener('blur',function(){c.value=get(c);bar()});
  }
 }
 c.id=id;lab.htmlFor=id;c.dataset.base=String(base);ctls[f.key]=c;
 c.addEventListener('input',touched);c.addEventListener('change',touched);
 if(f.hint)w.appendChild(el('p','hint',f.hint));
 return w;
}

function render(j){
 var host=$('#form');host.textContent='';ctls={};grp={};nid=0;
 (j.groups||[]).forEach(function(g){
  var card=el('section','card');card.appendChild(el('h2',null,g.name||'Settings'));
  (g.fields||[]).forEach(function(f){if(f&&f.key){grp[f.key]=g.name||'';card.appendChild(field(f))}});
  host.appendChild(card);
 });
 if(!host.children.length)host.appendChild(el('p','hint','The board reported no settings.'));
}

function moods(j){
 // The board sends its own mood list, so this stays correct when moods are
 // added to the firmware. The enum scan is a fallback for older firmware.
 var names=(j.device&&j.device.moods&&j.device.moods.length)?j.device.moods:null;
 if(!names)(j.groups||[]).forEach(function(g){(g.fields||[]).forEach(function(f){
  if(!names&&f.type=='enum'&&f.options&&/mood/i.test(f.key+' '+(f.label||'')))names=f.options;
 })});
 var host=$('#moods');host.textContent='';
 var n=names?names.length:9;
 for(var i=0;i<n;i++)(function(i){
  var lbl=names?String(names[i]):String(i);
  var b=el('button','btn',lbl);b.onclick=function(){fire('mood='+i,lbl)};host.appendChild(b);
 })(i);
}

function up(s){s=+s||0;var d=(s/86400)|0,h=(s/3600|0)%24,m=(s/60|0)%60;
 return d?d+'d '+h+'h':h?h+'h '+m+'m':m?m+'m '+(s%60)+'s':s+'s'}

function device(d){
 face.className='face m-'+String(d.mood||'chill').toLowerCase();
 $('#mood').textContent=d.mood||'--';
 $('#ap').className=d.ap_mode?'msg on':'msg';
 var host=$('#stats');host.textContent='';
 [['ip',d.ip],['rssi',d.rssi==null?null:d.rssi+' dBm'],['uptime',d.uptime_s==null?null:up(d.uptime_s)],
  ['heap',d.heap==null?null:Math.round(d.heap/1024)+' KB'],['face',d.style],['fw',d.version]]
 .forEach(function(p){
  if(p[1]==null||p[1]==='')return;
  var s=el('div','st');s.appendChild(el('b',null,p[0]));s.appendChild(el('span',null,String(p[1])));host.appendChild(s);
 });
 fdev(d);
}

// The refresh-while-editing rule.
//
// A poll must never overwrite what the user is doing. Two things protect a
// control: it is focused (they are in it right now, even with nothing typed
// yet), or it is pending (its value differs from the board's last known value).
// Everything else is safe to resync, which is what makes an external change --
// swiping the face style on the device itself -- show up here.
//
// A pending control also keeps its old base, so it stays pending; the board's
// value for it is only adopted after a save, or after a reset wipes the form.
// The DOM is rebuilt only when the shape of the config changes, because a
// rebuild is the one thing that does destroy focus.
function apply(j){
 device(j.device||{});
 var s=JSON.stringify((j.groups||[]).map(function(g){
  return [g.name,(g.fields||[]).map(function(f){
   return [f.key,f.type,f.label,f.hint,f.min,f.max,f.maxlen,f.options]})]}));
 if(s!==sig||wipe){
  var keep=wipe?{}:pending();
  if(!sig)note('Ready');
  sig=s;wipe=0;render(j);moods(j);
  for(var k in keep)if(ctls[k])set(ctls[k],keep[k]);
 }else{
  var d=pending(),act=document.activeElement,m={},k2;
  (j.groups||[]).forEach(function(g){(g.fields||[]).forEach(function(f){m[f.key]=nrm(f)})});
  for(k2 in ctls){
   var e=ctls[k2];
   if(k2 in d||e===act||!(k2 in m))continue;
   e.dataset.base=String(m[k2]);
   if(String(get(e))!==String(m[k2]))set(e,m[k2]);
  }
 }
 bar();
}

function online(v){document.body.classList.toggle('down',!v)}
function poll(ms){clearTimeout(tmr);tmr=setTimeout(tick,ms==null?4000:ms)}
function tick(){
 if(document.hidden&&sig){poll(5000);return}   // idle in a background tab, but always load once
 if(busy||fbusy){poll(1200);return}            // never read the board mid-save, of either kind
 req(CFG+'?t='+Date.now(),null,6000).then(function(r){return r.json()})
 .then(function(j){fails=0;online(1);apply(j)},
       function(){if(++fails>=2)online(0)})   // one lost packet is not an outage
 .then(function(){poll(fails?2500:4000)});
}

// Saving a Wi-Fi setting costs us the connection: the board restarts to join
// the new network and may come back on a different address, or not at all.
function wifiNotice(t){
 var m=$('#wmsg');
 m.textContent=t+' The board restarts to join the network, so this page may need reopening at its new'
  +' address -- or at 192.168.4.1 if it cannot join and falls back to its own access point.';
 m.className='msg on';
 $('#off').textContent='No answer from the board. It may have moved to a new address.';
}

function save(){
 var d=pending(),ks=Object.keys(d);
 if(!ks.length||busy)return;
 var wifi=ks.some(function(k){return /wi-?fi/i.test(grp[k]||'')});
 busy=1;bar();note('Saving '+ks.length+'...');
 post(CFG,enc(d),9000).then(function(){
  // Adopt the sent values as the new base. Anything the firmware clamped comes
  // back on the next poll, which is now free to overwrite these.
  ks.forEach(function(k){if(ctls[k])ctls[k].dataset.base=String(d[k])});
  note('Saved','ok');
  if(wifi)wifiNotice('Wi-Fi settings saved.');
 },function(e){
  // A Wi-Fi change often reboots the board before it can answer, so a dropped
  // request there is expected rather than a failure -- but say so plainly, and
  // keep the edits, because it might equally not have landed.
  if(wifi&&why(e).indexOf('HTTP')<0){note('No reply -- a Wi-Fi change usually restarts the board first.');
   wifiNotice('Wi-Fi settings sent, but the board did not confirm.');}
  else note('Save failed: '+why(e)+'. Changes kept.','bad');   // never looks like success
 }).then(function(){busy=0;bar();poll(700)});
}

function fire(body,what){
 note(what+'...');
 post('/api/test',body,6000).then(function(){note(what+' sent','ok');poll(400)},
                                  function(e){note(what+' failed: '+why(e),'bad')});
}

function resetUI(armed){
 var h=$('#rst');h.textContent='';
 if(!armed){
  var b=el('button','btn red','Reset to defaults');
  b.onclick=function(){resetUI(1);rtmr=setTimeout(function(){resetUI(0)},8000)};
  h.appendChild(b);return;
 }
 var y=el('button','btn red','Yes, erase settings'),n=el('button','btn','Cancel');
 y.onclick=function(){
  clearTimeout(rtmr);y.disabled=true;n.disabled=true;note('Resetting...');
  post('/api/reset','',9000).then(function(){
   wipe=1;note('Reset. Reloading settings.','ok');poll(900);   // drop pending edits, they are gone from the board
  },function(e){note('Reset failed: '+why(e),'bad')}).then(function(){resetUI(0)});
 };
 n.onclick=function(){clearTimeout(rtmr);resetUI(0)};
 h.appendChild(y);h.appendChild(n);
}

// ------------------------------------------------------------ custom face --
//
// 24 x 28 cells at 10 screen pixels each is exactly the 240 x 280 panel, so a
// cell here is a block there. Two frames, resting and blink, two bits a cell
// (0 background, 1 black, 2 white), four cells to a byte with the first in the
// high bits, frame 0 then frame 1, as 672 hex characters. One slot at a time.
//
// The grid is the editor's alone: a slot is read when opened, and all a poll may
// change here is the field colour and the slot markers.
var FACE='/api/face',FW=24,FH=28,CELLS=FW*FH,FBYTES=CELLS/4,HEXLEN=FBYTES*2*2,SLOTS=4;
var fr=[new Uint8Array(CELLS),new Uint8Array(CELLS)],occ=[];
var cur=0,slot=0,pend=-1,tool=1,px=10,drawing=0,lastc=-1,fok=0,fbusy=0,fdirty=0,fbase='';
// The tuning.h oranges and the alert red: an empty cell is the panel showing through.
var ORANGE=['#f0620c','#ff6a00','#e05508','#ff7a1a'],ALERT='#c81410',fld=ORANGE[0];
var LINE='rgba(0,0,0,.13)',cv=$('#fcv'),cx=cv.getContext('2d'),fnoteEl=$('#fnote'),fsv=$('#fsv');

function fnote(t,c){fnoteEl.textContent=t;fnoteEl.className='hint'+(c?' '+c:'')}
function used(f){var a=fr[f],i;for(i=0;i<CELLS;i++)if(a[i])return true;return false}

function encode(){
 var s='',f,i,b,a;
 for(f=0;f<2;f++){a=fr[f];
  for(i=0;i<CELLS;i+=4){
   b=(a[i]<<6)|(a[i+1]<<4)|(a[i+2]<<2)|a[i+3];      // first cell in bits 7-6
   s+=(b<16?'0':'')+b.toString(16);
  }}
 return s;
}
function decode(s){
 var f,i,v;
 for(f=0;f<2;f++)for(i=0;i<CELLS;i++){
  v=parseInt(s.substr((f*FBYTES+(i>>2))*2,2),16);
  v=(v>>(6-2*(i&3)))&3;
  fr[f][i]=v>2?0:v;                                 // 3 is unused; treat it as background
 }
}

function cell(i){
 var v=fr[cur][i],x=(i%FW)*px,y=((i/FW)|0)*px;
 cx.fillStyle=v==1?'#000':v==2?'#fff':fld;
 cx.fillRect(x,y,px,px);
 // A blink is drawn against the face it interrupts, so that face ghosts through.
 if(!v&&cur&&fr[0][i]){
  cx.globalAlpha=.22;cx.fillStyle=fr[0][i]==1?'#000':'#fff';cx.fillRect(x,y,px,px);cx.globalAlpha=1;
 }
 cx.fillStyle=LINE;cx.fillRect(x,y,px,1);cx.fillRect(x,y,1,px);
}
function redraw(){for(var i=0;i<CELLS;i++)cell(i)}

// A whole number of CSS pixels a cell, or the grid comes out with uneven columns.
function layout(){
 var w=($('#fpad').clientWidth||300)-6;             // less the bezel
 var cs=Math.max(6,Math.min(14,Math.floor(w/FW)));
 px=Math.max(1,Math.round(cs*Math.min(3,devicePixelRatio||1)));
 cv.style.width=(cs*FW)+'px';cv.style.height=(cs*FH)+'px';
 cv.width=px*FW;cv.height=px*FH;
 redraw();
}

function at(e){
 var r=cv.getBoundingClientRect();
 var x=Math.floor((e.clientX-r.left)/r.width*FW),y=Math.floor((e.clientY-r.top)/r.height*FH);
 return(x<0||y<0||x>=FW||y>=FH)?-1:y*FW+x;
}
function put(i){
 var a=fr[cur];
 if(i<0||i>=CELLS||a[i]===tool)return;
 a[i]=tool;cell(i);
 if(!fdirty){fdirty=1;fmark()}
}
// Samples arrive far apart on a quick drag; unfilled, a fast stroke is dotted.
function stroke(i){
 if(i<0){lastc=-1;return}                           // left the grid: do not join across the gap
 if(lastc<0)put(i);
 else{
  var x0=lastc%FW,y0=(lastc/FW)|0,x1=i%FW,y1=(i/FW)|0,s,
      n=Math.max(Math.abs(x1-x0),Math.abs(y1-y0));
  if(!n)put(i);
  for(s=1;s<=n;s++)put((y0+Math.round((y1-y0)*s/n))*FW+(x0+Math.round((x1-x0)*s/n)));
 }
 lastc=i;
}
function endStroke(){
 if(!drawing)return;
 drawing=0;lastc=-1;pend=-1;
 fdirty=(encode()!==fbase);
 fnote(fdirty?'Unsaved changes.':'Matches the board.');fmark();
}
// One path for mouse, finger and stylus. Capture keeps a stroke alive off the edge.
cv.addEventListener('pointerdown',function(e){
 if(e.button)return;
 e.preventDefault();
 try{cv.setPointerCapture(e.pointerId)}catch(x){}
 drawing=1;lastc=-1;stroke(at(e));
});
cv.addEventListener('pointermove',function(e){if(drawing){e.preventDefault();stroke(at(e))}});
cv.addEventListener('pointerup',endStroke);
cv.addEventListener('pointercancel',endStroke);
cv.addEventListener('contextmenu',function(e){e.preventDefault()});   // long press must not pop a menu

function fmark(){fsv.disabled=!!fbusy||!fok||!fdirty;fsv.textContent=fbusy?'Saving':'Save slot '+(slot+1)}
function press(b,on,c){b.className=c+(on?' pri':'');b.setAttribute('aria-pressed',on?'true':'false')}
function tabs(){
 press($('#f0'),!cur,'btn');press($('#f1'),!!cur,'btn');
 $('#fwhich').textContent='Slot '+(slot+1)+', '+(cur
  ?'blink frame. The resting face ghosts through; draw the eyes shut over it. Empty means no blink.'
  :'resting frame. What the board shows between blinks.');
}
function frame(f){if(f!==cur){cur=f;tabs();redraw()}}

// Four slots; a filled chip means the board holds a face there. Empty slots stay
// out of the swipe rotation, so they are not dead screens on the device.
function slotUI(){
 var h=$('#fslot'),i,b;
 for(i=0;i<SLOTS;i++){
  b=h.children[i];
  if(!b){b=el('button','btn sm');b.appendChild(el('i','chip'));b.appendChild(el('span',null,i+1));
   b.onclick=(function(i){return function(){pick(i)}})(i);h.appendChild(b)}
  press(b,i===slot,'btn sm');b.firstChild.className='chip'+(occ[i]?'':' e');
 }
 tabs();
}
// Opening a slot loads it over the grid, so unsaved work would go without a
// word. Say what would be lost, and let the second tap discard it.
function pick(n){
 if(n===slot||fbusy)return;
 if(fdirty&&pend!==n){
  pend=n;
  fnote('Slot '+(slot+1)+' has unsaved changes. Save, or tap slot '+(n+1)+' again to discard.','bad');
  return;
 }
 pend=-1;slot=n;slotUI();fload(2);
}
function tools(){press($('#tk'),tool==1,'btn sm');press($('#tw'),tool==2,'btn sm');press($('#te'),!tool,'btn sm')}
$('#f0').onclick=function(){frame(0)};
$('#f1').onclick=function(){frame(1)};
$('#tk').onclick=function(){tool=1;tools()};
$('#tw').onclick=function(){tool=2;tools()};
$('#te').onclick=function(){tool=0;tools()};

// A tap that would destroy work asks again first, as the factory reset does.
function arm(b,ask,risky,go){
 var t=0,lab=b.textContent,base=b.className;
 function calm(){t=0;b.textContent=lab;b.className=base}
 b.onclick=function(){
  if(t){clearTimeout(t);calm();go();return}
  if(!risky()){go();return}
  b.textContent=ask;b.className=base+' red';t=setTimeout(calm,4000);
 };
}
function edited(what){pend=-1;fdirty=(encode()!==fbase);fnote(what);fmark()}
arm($('#fcp'),'Overwrite blink?',function(){return used(1)},function(){
 fr[1].set(fr[0]);
 if(cur===1)redraw();else frame(1);
 edited('Copied resting into blink. Now close the eyes.');
});
// Clear blanks the grid in front of you; Erase slot writes that to the board.
arm($('#fcl'),'Clear it?',function(){return used(cur)},function(){
 fr[cur].fill(0);redraw();edited('Cleared the '+(cur?'blink':'resting')+' frame. Not saved yet.');
});

// Read the slot, then edit. Save stays disabled until the board's drawing is in
// hand, so an empty grid cannot land on a saved face. A reply for a slot the
// user has already left is dropped.
function fload(tries){
 var s=slot;
 fok=0;fdirty=0;fbase='';fmark();fr[0].fill(0);fr[1].fill(0);redraw();
 fnote('Reading slot '+(s+1)+' from the board.');
 req(FACE+'?slot='+s+'&t='+Date.now(),null,8000).then(function(r){return r.json()}).then(function(j){
  if(s!==slot)return;
  if(j.w&&(j.w!=FW||j.h!=FH))throw new Error('the board draws a '+j.w+'x'+j.h+' grid');
  var d=String(j.data||'').toLowerCase();
  if(d&&d.length!==HEXLEN)throw new Error('expected '+HEXLEN+' hex characters, got '+d.length);
  if(j.used&&j.used.length)occ=j.used;
  fok=1;
  // Anything drawn while the reply was in flight is the user's, and stays.
  if(fdirty)fnote('Kept your drawing. Slot '+(s+1)+' has a face of its own saved.');
  else{
   if(d)decode(d);
   fbase=encode();redraw();
   fnote(used(0)||used(1)?'Loaded slot '+(s+1)+'.':'Slot '+(s+1)+' is empty. Draw a face.');
  }
  slotUI();fmark();
 },function(e){
  if(s!==slot)return;
  fok=0;fmark();
  if(tries>0){fnote('Could not read slot '+(s+1)+': '+why(e)+'. Retrying.');
   setTimeout(function(){if(s===slot)fload(tries-1)},2500)}
  else fnote('Could not read slot '+(s+1)+': '+why(e)+'. Reload before drawing -- an empty grid'
             +' must not overwrite the board.','bad');
 });
}
// Both writes come through here: on request, never per stroke, because flash
// wears out. An all-zero resting frame is how the board is told a slot is empty.
function fput(h,okmsg,after){
 if(fbusy||!fok)return;
 var s=slot;
 fbusy=1;fmark();fnote('Saving slot '+(s+1)+'...');
 post(FACE,enc({slot:s,data:h}),12000).then(function(){
  occ[s]=/[^0]/.test(h.slice(0,HEXLEN/2));
  if(s===slot){fbase=h;fdirty=0;pend=-1;if(after)after();slotUI()}
  fnote(okmsg,'ok');
 },function(e){
  fnote('Save failed: '+why(e)+'. Your drawing is kept.','bad');   // never looks like success
 }).then(function(){fbusy=0;fmark()});
}
fsv.onclick=function(){
 if(!fdirty)return;
 fput(encode(),used(0)?'Saved to slot '+(slot+1)+'.'
  :'Saved. Slot '+(slot+1)+' is empty, so it leaves the rotation.');
};
arm($('#fer'),'Erase slot?',function(){return occ[slot]||used(0)||used(1)},function(){
 fput(new Array(HEXLEN+1).join('0'),'Slot '+(slot+1)+' erased on the board.',
      function(){fr[0].fill(0);fr[1].fill(0);redraw()});
});

// The field colour, honestly: whichever orange is configured, unsaved changes
// to it included, or red while the mascot wants something. Sticky EXCITED is red
// on the board too, but the page cannot tell it from a celebration.
function fdev(d){
 if(d.custom_faces&&d.custom_faces.length){occ=d.custom_faces;slotUI()}
 var o=ctls.orange?+get(ctls.orange):0;
 var c=/^(angry|limit)$/i.test(d.mood||'')?ALERT:(ORANGE[o]||ORANGE[0]);
 if(c===fld||drawing)return;      // a poll is a no-op for the canvas under a live stroke
 fld=c;document.documentElement.style.setProperty('--fld',c);
 redraw();
}

slotUI();tools();layout();
addEventListener('resize',function(){if(!drawing)layout()});
fload(2);

$('#buzz').onclick=function(){fire('what=buzz','Buzz')};
$('#bell').onclick=function(){fire('what=strike','Bell')};
saveEl.onclick=save;
document.addEventListener('visibilitychange',function(){if(!document.hidden)poll(0)});
addEventListener('beforeunload',function(e){
 if(Object.keys(pending()).length||fdirty){e.preventDefault();e.returnValue=''}});
resetUI(0);
tick();
})();
</script>
</body></html>
)HTML";
