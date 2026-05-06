const state={status:null,temps:null,water:null,config:null,busy:false,page:'dashboard'};

function esc(s){return String(s??'').replaceAll('&','&amp;').replaceAll('<','&lt;').replaceAll('>','&gt;').replaceAll('"','&quot;');}
function fmtBool(v){return v?'Yes':'No';}
function tempF(c){return (Number(c)*9/5+32).toFixed(1);}
function ageSec(ms){if(!ms)return '0s'; return Math.floor(ms/1000)+'s';}
function setBusy(v){state.busy=v;document.querySelectorAll('.btn-action').forEach(b=>b.disabled=v);}
function postForm(url,obj){const fd=new URLSearchParams();Object.entries(obj).forEach(([k,v])=>fd.append(k,v));return fetch(url,{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:fd.toString()});}
async function postAction(url,obj={}){setBusy(true);try{await postForm(url,obj);await refreshAll();}finally{setBusy(false);}}
async function fetchJson(url){const r=await fetch(url,{cache:'no-store'});if(!r.ok)throw new Error(url);return r.json();}
async function refreshAll(){const [status,temps,water,config]=await Promise.all([fetchJson('/api/status'),fetchJson('/api/temps'),fetchJson('/api/water'),fetchJson('/api/config')]);state.status=status;state.temps=temps;state.water=water;state.config=config;renderChrome();renderPages();}

function renderChrome(){
  const s=state.status||{};
  document.getElementById('badge-id').textContent=s.id||'-';
  document.getElementById('badge-wifi').textContent=s.wifi_connected?'up':'down';
  document.getElementById('badge-mqtt').textContent=s.mqtt_connected?'up':'down';
  document.getElementById('badge-heap').textContent=(s.freeheap||0)+' B';
}

function renderDashboard(){
  const s=state.status||{};
  const w=state.water||{};
  document.getElementById('page-dashboard').innerHTML=`<div class='grid three'><div class='card'><div class='muted'>Sensors</div><div class='kpi'>${s.sensorcount??0}</div><div class='small muted'>Detected on 1-Wire bus</div></div><div class='card'><div class='muted'>Water Level</div><div class='kpi'>${esc(w.level||'-')}</div><div class='small muted'>ADC ${w.adc ?? 0}</div></div><div class='card'><div class='muted'>Uptime</div><div class='kpi'>${Math.floor((s.uptime_ms||0)/1000)}s</div><div class='small muted'>RSSI ${s.rssidbm ?? '-'} dBm</div></div></div><div class='grid two'><div class='card'><h3>Node</h3><p><b>ID:</b> <span class='mono'>${esc(s.id||'-')}</span></p><p><b>SSID:</b> ${esc(s.ssid||'-')}</p><p><b>IP:</b> <span class='mono'>${esc(s.ip||'-')}</span></p><p><b>Command topic:</b> <span class='mono'>${esc((state.config||{}).topics?.command||'-')}</span></p><p><b>Status topic:</b> <span class='mono'>${esc((state.config||{}).topics?.status||'-')}</span></p></div><div class='card'><h3>Quick Actions</h3><div class='actions'><button class='btn btn-action' onclick="postAction('/api/sensors/scan')">Scan 1-Wire Bus</button><button class='btn secondary btn-action' onclick="postAction('/api/water/sample')">Force Water Sample</button><button class='btn secondary' onclick='refreshAll()'>Refresh</button></div><p class='footer-note'>Actions are queued and executed after the HTTP response is sent to keep the ESP8266 responsive.</p></div></div>`;
}

function renderTemps(){
  const t=state.temps||{};
  const sensors=t.sensors||[];
  document.getElementById('page-temps').innerHTML=`<div class='grid two'><div class='card'><h3>Temperature Network</h3><p><b>Detected:</b> ${t.sensorcount ?? 0}</p><p><b>Physical network seen:</b> ${fmtBool(!!t.networkdetected)}</p><p><b>Simulated sensors:</b> ${fmtBool(!!t.simulated)}</p><div class='actions'><button class='btn btn-action' onclick="postAction('/api/sensors/scan')">Scan / Rediscover</button><button class='btn secondary' onclick='refreshAll()'>Refresh</button></div></div><div class='card'><h3>Sampling</h3><p><b>Last sample age:</b> ${ageSec(t.last_sample_ms_age)}</p><p><b>Last rescan age:</b> ${ageSec(t.last_rescan_ms_age)}</p><p class='footer-note'>Use this page to watch the current 1-Wire roster and temperatures without changing MQTT behavior.</p></div></div><div class='card'><h3>Live Sensors</h3><table class='table'><thead><tr><th>#</th><th>Name</th><th>Address</th><th>State</th><th>C</th><th>F</th></tr></thead><tbody>${sensors.length ? sensors.map(s=>`<tr><td>${s.index}</td><td>${esc(s.name)}</td><td class='mono'>${esc(s.address)}</td><td>${s.connected?`<span class='pill good'>connected</span>`:`<span class='pill bad'>disconnected</span>`}</td><td>${s.tempc===null?'-':Number(s.tempc).toFixed(2)}</td><td>${s.tempc===null?'-':tempF(s.tempc)}</td></tr>`).join('') : `<tr><td colspan='6' class='muted'>No sensors available.</td></tr>`}</tbody></table></div>`;
}

function renderWater(){
  const w=state.water||{};
  const cfg=state.config||{};
  const th=(cfg.water||{}).thresholds||[];
  document.getElementById('page-water').innerHTML=`<div class='grid two'><div class='card'><h3>Live Water Probe</h3><p><b>Level:</b> ${esc(w.level||'-')}</p><p><b>Probe present:</b> ${fmtBool(!!w.probepresent)}</p><p><b>Reading valid:</b> ${fmtBool(!!w.valid)}</p><p><b>ADC:</b> ${w.adc ?? 0}</p><p><b>Last sample age:</b> ${ageSec(w.sampleagems)}</p><div class='actions'><button class='btn btn-action' onclick="postAction('/api/water/sample')">Force Probe</button><button class='btn secondary' onclick='refreshAll()'>Refresh</button></div></div><div class='card'><h3>Thresholds</h3><p class='mono small'>noprobe: ${th[0]??'-'} | 40gal: ${th[1]??'-'} | 15-40gal: ${th[2]??'-'} | 5-15gal: ${th[3]??'-'} | 5gal: ${th[4]??'-'}</p><form class='form' onsubmit='saveWater(event)'><div class='field'><label>Heartbeat interval (ms)</label><input name='intervalms' type='number' min='1000' max='86400000' value='${(cfg.water||{}).intervalms ?? 60000}'></div><div class='grid two'><div class='field'><label>No probe ADC</label><input name='t0' type='number' min='0' max='1023' value='${th[0]??20}'></div><div class='field'><label>40 gal ADC</label><input name='t1' type='number' min='0' max='1023' value='${th[1]??44}'></div><div class='field'><label>15-40 gal ADC</label><input name='t2' type='number' min='0' max='1023' value='${th[2]??268}'></div><div class='field'><label>5-15 gal ADC</label><input name='t3' type='number' min='0' max='1023' value='${th[3]??485}'></div><div class='field'><label>5 gal ADC</label><input name='t4' type='number' min='0' max='1023' value='${th[4]??1023}'></div></div><div class='actions'><button class='btn btn-action' type='submit'>Save Water Settings</button></div></form></div></div>`;
}

function renderNames(){
  const t=state.temps||{};
  const sensors=t.sensors||[];
  document.getElementById('page-names').innerHTML=`<div class='card'><h3>Rename Sensors</h3><p class='muted'>Names are persisted by 64-bit ROM address, so rediscovery keeps the mapping.</p><table class='table'><thead><tr><th>#</th><th>Address</th><th>Current Name</th><th>Rename</th></tr></thead><tbody>${sensors.length ? sensors.map(s=>`<tr><td>${s.index}</td><td class='mono'>${esc(s.address)}</td><td>${esc(s.name)}</td><td><form class='row' onsubmit='renameSensor(event,${s.index})'><input name='name' value='${esc(s.name)}' maxlength='31' style='max-width:220px;background:var(--panel2);color:var(--text);border:1px solid var(--line);border-radius:10px;padding:9px 10px'><button class='btn btn-action' type='submit'>Save</button></form></td></tr>`).join('') : `<tr><td colspan='4' class='muted'>No active sensors to rename.</td></tr>`}</tbody></table></div>`;
}

function renderWifi(){
  const s=state.status||{};
  document.getElementById('page-wifi').innerHTML=`<div class='grid two'><div class='card'><h3>WiFi Status</h3><p><b>Connected:</b> ${fmtBool(!!s.wifi_connected)}</p><p><b>SSID:</b> ${esc(s.ssid||'-')}</p><p><b>IP:</b> <span class='mono'>${esc(s.ip||'-')}</span></p><p><b>RSSI:</b> ${s.rssidbm ?? '-'} dBm</p></div><div class='card'><h3>WiFi Setup</h3><div class='note'>WiFi credentials are still handled by the startup portal in the current firmware layout. This page is reserved for the future always-on WiFi credential editor.</div><p class='footer-note'>That keeps this web UI expansion aligned with your current modules, since WiFi credentials are not yet stored in app_config.</p></div></div>`;
}

function renderServices(){
  const c=state.config||{};
  document.getElementById('page-services').innerHTML=`<div class='grid two'><div class='card'><h3>MQTT & Prometheus</h3><form class='form' onsubmit='saveServices(event)'><div class='field'><label>MQTT host</label><input name='mqtthost' value='${esc(c.mqtthost||'')}'></div><div class='grid two'><div class='field'><label>MQTT port</label><input name='mqttport' type='number' min='1' max='65535' value='${c.mqttport ?? 1883}'></div><div class='field'><label>Prometheus port</label><input name='prometheusport' type='number' min='1' max='65535' value='${c.prometheusport ?? 9111}'></div></div><div class='field'><label>Base topic</label><input name='basetopic' value='${esc(c.basetopic||'')}'></div><div class='field'><label>Device ID</label><input name='deviceid' value='${esc(c.deviceid||'')}'></div><div class='actions'><button class='btn btn-action' type='submit'>Save Services</button></div></form></div><div class='card'><h3>Resolved Topics</h3><p><b>Command:</b><br><span class='mono small'>${esc((c.topics||{}).command||'-')}</span></p><p><b>Status:</b><br><span class='mono small'>${esc((c.topics||{}).status||'-')}</span></p><p><b>Results:</b><br><span class='mono small'>${esc((c.topics||{}).results||'-')}</span></p><p><b>Water:</b><br><span class='mono small'>${esc((c.topics||{}).water||'-')}</span></p><p><b>Metrics URL:</b><br><span class='mono small'>http://${esc((state.status||{}).ip||'0.0.0.0')}:${c.prometheusport ?? 9111}/metrics</span></p></div></div>`;
}

function renderPages(){renderDashboard();renderTemps();renderWater();renderNames();renderWifi();renderServices();showPage(state.page);}
function showPage(name){state.page=name;document.querySelectorAll('.page').forEach(p=>p.classList.add('hidden'));const page=document.getElementById('page-'+name);if(page)page.classList.remove('hidden');document.querySelectorAll('.nav button').forEach(b=>b.classList.toggle('active',b.dataset.page===name));const titles={dashboard:['Dashboard','Live node overview'],temps:['Temperature Network','Monitor and rescan the 1-Wire sensor bus'],water:['Water Probe','Live level status and threshold configuration'],names:['Sensor Names','Persistent address-based sensor naming'],wifi:['WiFi','Current connection state'],services:['MQTT & Prometheus','Broker, topics, identity, and metrics']};document.getElementById('page-title').textContent=titles[name][0];document.getElementById('page-subtitle').textContent=titles[name][1];}
async function saveServices(ev){ev.preventDefault();const fd=new FormData(ev.target);await postAction('/api/config/services',Object.fromEntries(fd.entries()));}
async function saveWater(ev){ev.preventDefault();const fd=new FormData(ev.target);await postAction('/api/config/water',Object.fromEntries(fd.entries()));}
async function renameSensor(ev,index){ev.preventDefault();const fd=new FormData(ev.target);await postAction('/api/sensors/rename',{index,name:fd.get('name')});}

document.querySelectorAll('.nav button').forEach(b=>b.addEventListener('click',()=>showPage(b.dataset.page)));
refreshAll();
setInterval(refreshAll,5000);
