#pragma once
// Embedded web panel (served from flash). Stop search talks to Entur's geocoder straight from the browser.
static const char PAGE[] PROGMEM = R"HTML(<!doctype html><html lang="no"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>ESP32 Bus Board</title><style>
:root{--bg:#0e1240;--card:#181c56;--alt:#151a52;--line:#393d79;--tx:#fff;--dim:#aeb7e2;--acc:#5ac39a;--warn:#ffca28;--bad:#ff5959}
body{margin:0;background:var(--bg);color:var(--tx);font:14px/1.45 system-ui,sans-serif}a{color:var(--acc)}
header{padding:10px 14px;background:var(--card);border-bottom:1px solid var(--line);display:flex;gap:14px;align-items:center;flex-wrap:wrap}
header h1{font-size:18px;margin:0}nav button{background:transparent;color:var(--dim);border:1px solid var(--line);border-radius:6px;padding:6px 12px;margin-right:6px;cursor:pointer}
nav button.on{background:var(--acc);color:#000;border-color:var(--acc)}section{padding:14px;display:none;max-width:1100px}section.on{display:block}
table{border-collapse:collapse;width:100%}td,th{padding:6px 8px;border-bottom:1px solid var(--line);text-align:left;white-space:nowrap}th{color:var(--dim);font-weight:normal}
td.dest{white-space:normal;font-weight:600}.badge{display:inline-block;min-width:38px;text-align:center;border-radius:5px;padding:2px 6px;font-weight:700}
.tm{font-weight:700;font-size:16px;text-align:right}.sched{color:var(--dim)}.cx td{color:var(--bad)}.cx .tm{text-decoration:line-through}.hid td{opacity:.4}
.sit{color:var(--warn);margin:6px 0}.muted{color:var(--dim)}.row{display:flex;gap:18px;flex-wrap:wrap}.row>div{flex:1;min-width:260px}
label{display:block;margin:8px 0 2px;color:var(--dim)}input,select{width:100%;max-width:420px;background:#0b0e33;color:var(--tx);border:1px solid var(--line);border-radius:5px;padding:7px;box-sizing:border-box}
input[type=checkbox]{width:auto}button.b{background:var(--card);color:var(--tx);border:1px solid var(--line);border-radius:6px;padding:7px 12px;cursor:pointer}
button.p{background:var(--acc);color:#000;border:0;font-weight:700}img.shot{border:1px solid var(--line);width:272px;height:480px;background:#000}
.stop{background:var(--card);border:1px solid var(--line);border-radius:8px;padding:10px 12px;margin:8px 0;max-width:640px}.stop h3{margin:0 0 6px;font-size:16px}
.stop .g{display:grid;grid-template-columns:1fr 1fr 110px;gap:8px}.hits div{padding:8px 10px;border-bottom:1px solid var(--line);cursor:pointer;max-width:620px}.hits div:hover{background:var(--alt)}
pre{white-space:pre-wrap;color:var(--dim)}
</style></head><body>
<header><h1>ESP32 Bus Board</h1><nav><button data-s="board" class="on">Tavle</button><button data-s="stops">Stopp</button><button data-s="settings">Oppsett</button><button data-s="alerts">Varsler</button><button data-s="api">API</button></nav><span id="hdr" class="muted"></span></header>
<section id="board" class="on"><div class="row"><div><div class="stop" id="trk"><h3>Følg linje</h3><select id="tsel" onchange="track(this.value)" style="max-width:420px"></select><div id="tstat" style="margin-top:8px"></div></div><div id="sits"></div><table id="tbl"><thead><tr><th>Linje</th><th>Til</th><th>Stopp</th><th>Plf.</th><th style="text-align:right">Avgang</th><th>Rute</th><th>Status</th></tr></thead><tbody></tbody></table>
<p class="muted" id="fetch"></p><button class="b" onclick="act('refresh')">Oppdater nå</button> <button class="b" onclick="act('next_stop')">Neste stopp på skjermen</button> <button class="b" onclick="act('mode')">Bytt visning</button></div>
<div style="flex:0 0 290px"><img class="shot" id="shot" alt=""><br><button class="b" onclick="shot()">Vis skjermbilde</button> <span class="muted">(~260 KB)</span></div></div></section>
<section id="stops"><p class="muted">Opptil 4 stopp. Linjefilter: kommaseparert (f.eks. <code>31,37</code>), <code>-31</code> skjuler linje 31. Plattform: f.eks. <code>A,B</code> eller spor <code>2</code>. Gangtid gir «GÅ NÅ» og kan skjule avganger du ikke rekker.</p>
<div id="slist"></div><button class="b p" onclick="saveStops()">Lagre stopp</button> <span id="smsg" class="muted"></span>
<h3>Legg til stopp</h3><input id="q" placeholder="Søk holdeplass, f.eks. Hønefoss sentrum" oninput="search()" style="max-width:620px"><div class="hits" id="hits"></div></section>
<section id="settings"><form id="f" onsubmit="return save(event)"><div class="row">
<div><h3>Tavle</h3><label>Visning</label><select name="board_mode"><option value="0">Ett stopp om gangen</option><option value="1">Alle stopp samlet</option></select>
<label>Bytt stopp automatisk (s, 0 = av)</label><select name="rotate_s"><option>0</option><option>10</option><option>15</option><option>20</option><option>30</option><option>60</option></select>
<label>Oppdater hvert (s)</label><select name="update_s"><option>15</option><option>20</option><option>30</option><option>45</option><option>60</option><option>120</option></select>
<label>Vis klokkeslett i stedet for minutter etter (min, 0 = aldri)</label><select name="clock_after"><option>10</option><option>15</option><option>20</option><option>30</option><option>60</option><option>0</option></select>
<label><input type="checkbox" name="hide_unreachable"> Skjul avganger du ikke rekker (gangtid)</label><label><input type="checkbox" name="show_platform"> Vis plattform / spor</label><label><input type="checkbox" name="line_colours"> Linjefarger</label><label><input type="checkbox" name="ticker"> Avviksmeldinger</label><label><input type="checkbox" name="show_weather"> Vær i toppen</label>
<h3>Skjerm</h3><label>Tema (starter på nytt)</label><select name="theme"><option value="0">Entur</option><option value="1">Amber LED</option><option value="2">Phosphor</option><option value="3">Dag</option></select>
<label>Språk (starter på nytt)</label><select name="lang"><option value="0">Norsk</option><option value="1">English</option></select>
<label>Lysstyrke 10-255</label><input name="brightness" type="number" min="10" max="255"><label><input type="checkbox" name="night_dim"> Demp om natten</label>
<label>Natt fra / til (time)</label><div style="display:flex;gap:8px;max-width:420px"><input name="night_from" type="number" min="0" max="23"><input name="night_to" type="number" min="0" max="23"></div>
<h3>Værposisjon</h3><label>Kilde</label><select name="loc_mode"><option value="0">Første stopp (ellers IP)</option><option value="1">Manuell</option></select>
<label>Breddegrad / lengdegrad</label><div style="display:flex;gap:8px;max-width:420px"><input name="man_lat" type="number" step="0.0001"><input name="man_lon" type="number" step="0.0001"></div></div>
<div><h3>Wi-Fi</h3><label>SSID</label><input name="wifi_ssid" list="ssids"><datalist id="ssids"></datalist><button type="button" class="b" onclick="scan()">Søk etter nett</button><label>Passord (tomt = behold)</label><input name="wifi_password" type="password">
<h3>«Gå nå»-varsel</h3><label>Linjer (komma)</label><input name="alert_lines" placeholder="223,F4"><label>Aktiv fra / til (time)</label><div style="display:flex;gap:8px;max-width:420px"><input name="alert_from" type="number" min="0" max="23"><input name="alert_to" type="number" min="0" max="23"></div>
<label>ntfy.sh-emne</label><input name="ntfy_topic"><label>Webhook-URL</label><input name="webhook_url"><label>MQTT (Home Assistant)</label><input name="mqtt_uri" placeholder="mqtt://bruker:pass@192.168.1.50:1883">
<h3>Panel</h3><label>Panelpassord (bruker admin, tomt = åpent)</label><input name="panel_pass" type="password">
<h3>Fastvare</h3><p class="muted" id="otastate"></p><input type="file" id="fw" accept=".bin"><button type="button" class="b" onclick="ota()">Last opp firmware.bin</button></div>
</div><p><button class="b p">Lagre oppsett</button> <span id="msg" class="muted"></span></p></form></section>
<section id="alerts"><table id="atbl"><thead><tr><th>Tid</th><th>Type</th><th>Tittel</th><th>Melding</th></tr></thead><tbody></tbody></table></section>
<section id="api"><pre>GET  /api/state      tavla som JSON: avganger, avvik, henting, vær
GET  /api/stops      lagrede stopp
POST /api/stops      [{"id":"NSR:StopPlace:16961","name":"...","lines":"","quays":"","walk_min":0}, ...]
GET  /api/config     oppsett (uten passord)
POST /api/config     JSON-utdrag av oppsettet
GET  /api/alerts     varsellogg
GET  /api/wifi/scan  nett i nærheten
GET  /screen.bmp     skjermbilde 272x480
GET  /metrics        Prometheus
POST /ota            firmware.bin (403 hvis ikke åpnet på enheten)
POST /api/action     {"action":"refresh"|"reboot"|"locate"|"next_stop"|"mode"|"page","page":0-4}
Data: Entur Journey Planner v3 (NLOD), api.entur.io</pre></section>
<script>
const $=s=>document.querySelector(s),E=s=>String(s??'').replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
document.querySelectorAll('nav button').forEach(b=>b.onclick=()=>{document.querySelectorAll('nav button,section').forEach(x=>x.classList.remove('on'));b.classList.add('on');$('#'+b.dataset.s).classList.add('on');({alerts,settings:cfg,stops:loadStops})[b.dataset.s]?.();});
const CAT=c=>[...new Set((c||[]).map(x=>/rail/i.test(x)?'Tog':/metro/i.test(x)?'T-bane':/tram/i.test(x)?'Trikk':/ferry|harbour/i.test(x)?'Båt':/coach/i.test(x)?'Ekspressbuss':/airport/i.test(x)?'Fly':/bus/i.test(x)?'Buss':''))].filter(Boolean).join(', ');
const MC={bus:'#e60000',tram:'#0b91ef',metro:'#ec700c',rail:'#00367f',water:'#0094a8',coach:'#75a300',air:'#9d4d86',other:'#5a5f8a'};
function shot(){$('#shot').src='/screen.bmp?'+Date.now()}
async function act(a,x){await fetch('/api/action',{method:'POST',body:JSON.stringify({action:a,...x})});setTimeout(live,800);if($('#shot').src)setTimeout(shot,1200)}
async function live(){try{const d=await (await fetch('/api/state')).json();
$('#hdr').textContent=(d.stops.map(s=>s.name).join(' · ')||'ingen stopp')+(d.weather.valid?' · '+d.weather.temp.toFixed(0)+'° '+d.weather.cond:'');
$('#sits').innerHTML=d.situations.map(s=>'<div class="sit">⚠ '+E(s)+'</div>').join('');
drawTrack(d);const f=d.fetch;$('#fetch').textContent=f.ok?`Hentet for ${f.age_s} s siden · ${f.count} avganger · ${f.took_ms} ms · ${f.requests} spørringer, ${f.failures} feil`:'Entur: '+(f.error||'venter…');
const tb=$('#tbl tbody');tb.innerHTML='';d.departures.forEach(x=>{const tr=document.createElement('tr');if(x.cancelled)tr.className='cx';else if(!x.visible)tr.className='hid';
const bg=x.color?'#'+x.color:MC[x.mode]||MC.other,fg=x.color?'#'+(x.text_color||'fff'):'#fff';
tr.innerHTML=`<td><span class="badge" style="background:${bg};color:${fg}">${E(x.line)}</span></td><td class="dest">${E(x.dest)}${x.situation>=0?' <span style="color:var(--warn)">⚠</span>':''}</td><td>${E(x.stop_name)}</td><td>${E(x.quay)}</td><td class="tm ${x.realtime?'':'sched'}">${E(x.time_text)}</td><td class="muted">${E(x.aimed_hm)}</td><td>${x.cancelled?'Innstilt':x.leave_now?'<b style="color:var(--acc)">GÅ NÅ</b>':x.delay_min>=2?'+'+x.delay_min+' min':x.realtime?'sanntid':'rutetid'}</td>`;tb.appendChild(tr);});
}catch(e){}}
let TOPTS='';
function drawTrack(d){const t=d.track,seen=new Set(),o=['<option value="">Ikke følg noen linje</option>'];
d.departures.forEach(x=>{const st=d.stops.find(s=>s.id==x.stop_id);if(!st)return;const k=JSON.stringify([st.id,x.line,x.dest]);if(seen.has(k))return;seen.add(k);
o.push(`<option value='${E(k)}' ${t.active&&t.stop==st.id&&t.line==x.line&&t.dest==x.dest?'selected':''}>${E(st.name)}: ${E(x.line)} → ${E(x.dest)}</option>`)});
const h=o.join('');if(h!=TOPTS&&document.activeElement!==$('#tsel')){$('#tsel').innerHTML=h;TOPTS=h}
if(!t.active){$('#tstat').innerHTML='<span class="muted">Velg en linje for å se hvor bussen er og hvor mange stopp den har igjen til ditt stopp.</span>';return}
if(!t.trips.length){$('#tstat').innerHTML='<span class="muted">Henter / ingen avganger…</span>';return}
const a=t.trips[0];let head=!a.started?`Ikke startet – hos deg kl. ${a.arrives} (${a.minutes} min)`:a.stops_away<0?'Har passert':a.stops_away==0?`Neste stopp er ditt – ${a.minutes} min`:`<b>${a.stops_away} stopp unna</b> · ved ${E(a.at)} · hos deg kl. ${a.arrives} (${a.minutes} min)`;
if(a.estimated)head+=' <span class="muted">(beregnet)</span>';
$('#tstat').innerHTML=`<div style="font-size:16px">${head}</div><div class="muted" style="margin-top:6px">`+a.calls.map((c,i)=>`<span style="${c.passed?'opacity:.45':''}${i==a.calls.length-1?';color:var(--acc);font-weight:700':''}">${E(c.time)} ${E(c.name)}</span>`).join(' → ')+'</div>'+(t.trips[1]?`<div class="muted" style="margin-top:6px">Neste: kl. ${t.trips[1].arrives} (${t.trips[1].minutes} min)</div>`:'');}
async function track(v){const [s,l,d]=v?JSON.parse(v):['','',''];await fetch('/api/config',{method:'POST',body:JSON.stringify(v?{track_stop:s,track_line:l,track_dest:d}:{track_line:'',track_dest:''})});setTimeout(live,3000)}
let STOPS=[];
async function loadStops(){STOPS=await (await fetch('/api/stops')).json();drawStops()}
function drawStops(){$('#slist').innerHTML=STOPS.length?'':'<p class="muted">Ingen stopp ennå.</p>';STOPS.forEach((s,i)=>{const d=document.createElement('div');d.className='stop';
d.innerHTML=`<h3>${i+1}. ${E(s.name)} <span class="muted" style="font-weight:normal;font-size:12px">${E(s.id)}</span></h3><div class="g"><div><label>Linjer</label><input value="${E(s.lines)}" onchange="STOPS[${i}].lines=this.value"></div><div><label>Plattformer</label><input value="${E(s.quays)}" onchange="STOPS[${i}].quays=this.value"></div><div><label>Gangtid min</label><input type="number" min="0" max="30" value="${s.walk_min}" onchange="STOPS[${i}].walk_min=+this.value"></div></div>
<p>${i?`<button class="b" onclick="mv(${i})">▲ Flytt opp</button> `:''}<button class="b" onclick="STOPS.splice(${i},1);drawStops()">Fjern</button></p>`;$('#slist').appendChild(d);});}
function mv(i){[STOPS[i-1],STOPS[i]]=[STOPS[i],STOPS[i-1]];drawStops()}
async function saveStops(){const r=await fetch('/api/stops',{method:'POST',body:JSON.stringify(STOPS)});$('#smsg').textContent=await r.text();loadStops();setTimeout(live,1500)}
let ST=null;function search(){clearTimeout(ST);ST=setTimeout(async()=>{const q=$('#q').value.trim();if(q.length<2){$('#hits').innerHTML='';return}
const r=await (await fetch('https://api.entur.io/geocoder/v1/autocomplete?layers=venue&size=10&lang=no&text='+encodeURIComponent(q),{headers:{'ET-Client-Name':'frogswiper-esp32busboard'}})).json();
$('#hits').innerHTML='';r.features.filter(f=>f.properties.id.startsWith('NSR:StopPlace:')).forEach(f=>{const p=f.properties,d=document.createElement('div');d.innerHTML=`<b>${E(p.name)}</b> <span class="muted">${E(p.locality||p.county||'')} · ${E(CAT(p.category))}</span>`;
d.onclick=()=>{if(STOPS.length>=4){alert('Maks 4 stopp');return}if(STOPS.some(s=>s.id==p.id))return;STOPS.push({id:p.id,name:p.name,lat:f.geometry.coordinates[1],lon:f.geometry.coordinates[0],lines:'',quays:'',walk_min:0});drawStops();$('#smsg').textContent='Husk å lagre';};$('#hits').appendChild(d);});},250)}
async function alerts(){const d=await (await fetch('/api/alerts')).json();const tb=$('#atbl tbody');tb.innerHTML='';d.forEach(a=>{const tr=document.createElement('tr');tr.innerHTML=`<td>${E(a.time)}</td><td>${E(a.kind)}</td><td>${E(a.title)}</td><td>${E(a.message)}</td>`;tb.appendChild(tr);});}
async function cfg(){const d=await (await fetch('/api/config')).json();const f=$('#f');for(const k in d){const el=f.elements[k];if(!el||el.type=='file')continue;if(el.type=='checkbox')el.checked=!!d[k];else el.value=d[k];}$('#otastate').textContent=d.ota_armed?'OTA åpen':'OTA låst (åpne den på enheten: Info-siden)';}
async function save(e){e.preventDefault();const o={};for(const el of $('#f').elements){if(!el.name)continue;if(el.type=='checkbox')o[el.name]=el.checked;else if(el.value!==''){o[el.name]=(el.type=='number'||el.tagName=='SELECT')?Number(el.value):el.value;}}
const r=await fetch('/api/config',{method:'POST',body:JSON.stringify(o)});$('#msg').textContent=await r.text();}
async function scan(){$('#msg').textContent='søker…';const d=await (await fetch('/api/wifi/scan')).json();$('#ssids').innerHTML=d.map(n=>`<option value="${E(n.ssid)}">${n.rssi} dBm</option>`).join('');$('#msg').textContent=d.length+' nett funnet';}
async function ota(){const fl=$('#fw').files[0];if(!fl)return alert('velg firmware.bin');const fd=new FormData();fd.append('firmware',fl);$('#msg').textContent='laster opp '+fl.size+' byte…';const r=await fetch('/ota',{method:'POST',body:fd});$('#msg').textContent=await r.text();}
live();setInterval(live,10000);if(location.hash){const b=document.querySelector('nav button[data-s='+location.hash.slice(1)+']');if(b)setTimeout(()=>b.click(),200);}
</script></body></html>)HTML";
