// ---------------------------------------------------------------------------
// index_html.h  -  The control page, served from flash. Mobile-first, no build
// step, no external assets (everything inline so it works with no internet).
// ---------------------------------------------------------------------------
#pragma once
#include <Arduino.h>

static const char INDEX_HTML[] PROGMEM = R"HTML(<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover">
<title>iPixel Bridge</title>
<style>
  :root{--bg:#0f1115;--card:#1a1d24;--line:#2a2f3a;--fg:#e8eaed;--mut:#9aa0ab;--acc:#4f8cff;--ok:#3ecf8e;--bad:#ff5c5c}
  *{box-sizing:border-box}
  body{margin:0;background:var(--bg);color:var(--fg);font:15px/1.4 system-ui,sans-serif;padding:16px;max-width:560px;margin:0 auto}
  h1{font-size:19px;margin:4px 0 2px}
  .sub{color:var(--mut);font-size:13px;margin-bottom:14px}
  .card{background:var(--card);border:1px solid var(--line);border-radius:12px;padding:14px;margin-bottom:12px}
  .card h2{font-size:13px;text-transform:uppercase;letter-spacing:.04em;color:var(--mut);margin:0 0 10px}
  label{display:block;font-size:13px;color:var(--mut);margin:8px 0 4px}
  input[type=text],input[type=number],select{width:100%;padding:10px;border-radius:8px;border:1px solid var(--line);background:#0d0f13;color:var(--fg);font-size:15px}
  input[type=range]{width:100%}
  input[type=color]{width:52px;height:36px;border:none;background:none;padding:0}
  button{appearance:none;border:none;border-radius:8px;padding:10px 14px;font-size:15px;font-weight:600;color:#fff;background:var(--acc);cursor:pointer}
  button.sec{background:#30343d}
  button:disabled{opacity:.5}
  .row{display:flex;gap:8px;align-items:center;flex-wrap:wrap}
  .row>*{flex:0 0 auto}
  .grow{flex:1 1 auto;min-width:0}
  .pill{display:inline-block;width:9px;height:9px;border-radius:50%;background:var(--bad);margin-right:6px;vertical-align:middle}
  .pill.on{background:var(--ok)}
  .mono{font-family:ui-monospace,monospace;font-size:12px;color:var(--mut);word-break:break-all}
  .dev{display:flex;justify-content:space-between;gap:8px;padding:8px;border:1px solid var(--line);border-radius:8px;margin-top:6px}
  .dev small{color:var(--mut)}
  #toast{position:fixed;left:50%;bottom:18px;transform:translateX(-50%);background:#000;border:1px solid var(--line);padding:9px 14px;border-radius:9px;font-size:13px;opacity:0;transition:.2s;pointer-events:none}
  #toast.show{opacity:1}
</style>
</head>
<body>
  <h1>iPixel Bridge</h1>
  <div class="sub">ESP32 &rarr; Bluetooth control for an iPixel&nbsp;Color flexible LED sign</div>

  <div class="card">
    <h2>Connection</h2>
    <div class="row"><span><span id="pill" class="pill"></span><span id="cstate">checking&hellip;</span></span>
      <span class="grow"></span>
      <button class="sec" id="scanBtn" onclick="scan()">Scan</button>
      <button class="sec" onclick="post('/api/disconnect')">Disconnect</button>
    </div>
    <div id="devs"></div>
    <div class="mono" id="notify" style="margin-top:8px"></div>
  </div>

  <div class="card">
    <h2>Display</h2>
    <div class="row">
      <button onclick="post('/api/power',{on:true})">Power on</button>
      <button class="sec" onclick="post('/api/power',{on:false})">Off</button>
      <button class="sec" onclick="post('/api/clear')">Clear</button>
    </div>
    <label>Brightness <span id="bval">80</span></label>
    <input type="range" min="0" max="100" value="80" id="bri"
      oninput="bval.textContent=this.value" onchange="post('/api/brightness',{level:+this.value})">
    <label>Colour</label>
    <div class="row"><input type="color" id="col" value="#ff8000"></div>
  </div>

  <div class="card">
    <h2>Scrolling text</h2>
    <input type="text" id="txt" maxlength="255" placeholder="Hello world">
    <div class="row" style="margin-top:8px">
      <select id="anim">
        <option value="1">Scroll left</option>
        <option value="2">Scroll right</option>
        <option value="0">Static</option>
        <option value="5">Blink</option>
        <option value="6">Fade</option>
      </select>
      <div class="grow"><input type="range" min="0" max="100" value="80" id="spd"
        oninput="sval.textContent=this.value"></div>
      <small><span id="sval">80</span></small>
      <button onclick="sendText()">Send</button>
    </div>
  </div>

  <div class="card">
    <h2>Image / GIF</h2>
    <div class="row">
      <div><label>Panel W</label><input type="number" id="pw" value="64" style="width:80px"></div>
      <div><label>Panel H</label><input type="number" id="ph" value="16" style="width:80px"></div>
    </div>
    <input type="file" id="imgf" accept="image/*" style="margin-top:8px">
    <label style="margin-top:8px"><input type="checkbox" id="gifmode"> Send GIF animated (must already be panel size)</label>
    <div class="row" style="margin-top:8px"><button onclick="sendImage()">Send image</button></div>
    <canvas id="cv" width="64" height="16" style="display:none"></canvas>
    <div class="sub" style="margin-top:8px">Static images are cropped and resized to Panel&nbsp;W&times;H in your browser, then the sign decodes them. For a different sign, just change W/H. Animated GIFs are sent as-is, so size the GIF to W&times;H first. Max 48&nbsp;KB.</div>
  </div>

  <div class="card">
    <h2>Raw frame (debug)</h2>
    <input type="text" id="raw" placeholder="hex e.g. 05 00 07 01 01">
    <div class="row" style="margin-top:8px"><button class="sec" onclick="sendRaw()">Write</button></div>
  </div>

  <div class="card">
    <h2>Wi-Fi</h2>
    <input type="text" id="ssid" placeholder="SSID">
    <input type="text" id="pass" placeholder="password" style="margin-top:6px">
    <div class="row" style="margin-top:8px"><button class="sec" onclick="saveWifi()">Save &amp; reboot</button></div>
    <div class="sub" style="margin:8px 0 0">Joins your network in station mode. Until then the bridge runs its own hotspot.</div>
  </div>

  <div class="card">
    <h2>Remote (MQTT)</h2>
    <div class="row" style="margin-bottom:8px"><span><span id="mpill" class="pill"></span><span id="mstate">off</span></span></div>
    <label>Broker host</label>
    <input type="text" id="mqhost" placeholder="abcd1234.s1.eu.hivemq.cloud">
    <div class="row" style="margin-top:6px">
      <div><label>Port</label><input type="number" id="mqport" value="8883" style="width:90px"></div>
      <div class="grow"><label>Base topic</label><input type="text" id="mqbase" value="ipixel"></div>
    </div>
    <label>Username</label>
    <input type="text" id="mquser" placeholder="broker username">
    <label>Password</label>
    <input type="text" id="mqpass" placeholder="broker password">
    <div class="row" style="margin-top:10px">
      <label style="margin:0"><input type="checkbox" id="mqen"> Enable</label>
      <span class="grow"></span>
      <button class="sec" onclick="saveMqtt()">Save &amp; reboot</button>
    </div>
    <div class="sub" style="margin:8px 0 0">Connects outbound (TLS) to your broker and listens on <span class="mono">&lt;base&gt;/cmd</span>. Publish a message there from Home Assistant or Apple Shortcuts to drive the sign from anywhere.</div>
  </div>

  <div id="toast"></div>
<script>
const $=s=>document.querySelector(s);
function toast(m){const t=$('#toast');t.textContent=m;t.classList.add('show');clearTimeout(t._t);t._t=setTimeout(()=>t.classList.remove('show'),1800)}
function rgb(){const h=$('#col').value;return{r:parseInt(h.substr(1,2),16),g:parseInt(h.substr(3,2),16),b:parseInt(h.substr(5,2),16)}}
async function post(url,body){
  try{const r=await fetch(url,{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(body||{})});
      const j=await r.json();toast(j.ok?'OK':(j.error||'failed'));refresh();return j;}
  catch(e){toast('network error')}
}
async function refresh(){
  try{const j=await (await fetch('/api/status')).json();
    $('#pill').className='pill'+(j.ble?' on':'');
    $('#cstate').textContent=j.ble?('Connected: '+j.device):'Not connected';
    $('#notify').textContent=j.notify?('last notify: '+j.notify):'';
    const m=j.mqtt||'off';
    $('#mstate').textContent='MQTT: '+m;
    $('#mpill').className='pill'+(m==='connected'?' on':'');
  }catch(e){}
}
async function scan(){
  $('#scanBtn').disabled=true;$('#scanBtn').textContent='Scanning…';
  try{const j=await (await fetch('/api/scan')).json();
    const d=$('#devs');d.innerHTML='';
    (j.devices||[]).forEach(dev=>{
      const el=document.createElement('div');el.className='dev';
      el.innerHTML=`<span>${dev.name}<br><small>${dev.address}</small></span>`;
      const b=document.createElement('button');b.textContent='Connect';
      b.onclick=()=>post('/api/connect',{address:dev.address,type:dev.type});
      el.appendChild(b);d.appendChild(el);
    });
    if(!(j.devices||[]).length)d.innerHTML='<div class="sub">No LED_BLE_* devices found. Make sure the sign is on and the official app is closed.</div>';
  }catch(e){toast('scan failed')}
  $('#scanBtn').disabled=false;$('#scanBtn').textContent='Scan';
}
function sendText(){const c=rgb();post('/api/text',{text:$('#txt').value,r:c.r,g:c.g,b:c.b,anim:+$('#anim').value,speed:+$('#spd').value})}
function sendRaw(){post('/api/raw',{hex:$('#raw').value})}
function saveWifi(){post('/api/wifi',{ssid:$('#ssid').value,pass:$('#pass').value})}
function saveMqtt(){post('/api/mqtt',{host:$('#mqhost').value,port:+$('#mqport').value,user:$('#mquser').value,pass:$('#mqpass').value,base:$('#mqbase').value,enable:$('#mqen').checked})}
function sendImage(){
  const f=$('#imgf').files[0]; if(!f){toast('pick an image');return;}
  if($('#gifmode').checked){const fd=new FormData();fd.append('file',f,f.name||'a.gif');uploadImg(fd,1);return;}
  const W=parseInt($('#pw').value)||64,H=parseInt($('#ph').value)||16;
  const url=URL.createObjectURL(f),img=new Image();
  img.onload=()=>{const cv=$('#cv');cv.width=W;cv.height=H;const x=cv.getContext('2d');
    const s=Math.max(W/img.width,H/img.height),dw=img.width*s,dh=img.height*s;
    x.fillStyle='#000';x.fillRect(0,0,W,H);x.drawImage(img,(W-dw)/2,(H-dh)/2,dw,dh);
    URL.revokeObjectURL(url);
    cv.toBlob(b=>{const fd=new FormData();fd.append('file',b,'i.png');uploadImg(fd,0);},'image/png');};
  img.onerror=()=>{URL.revokeObjectURL(url);toast('bad image')};img.src=url;
}
async function uploadImg(fd,gif){toast('sending…');try{const r=await fetch('/api/image?gif='+gif,{method:'POST',body:fd});const j=await r.json();toast(j.ok?'image sent':(j.error||'failed'));}catch(e){toast('upload error')}}
['pw','ph'].forEach(id=>{const el=$('#'+id);try{const v=localStorage.getItem('ipx_'+id);if(v)el.value=v;}catch(e){}el.addEventListener('change',()=>{try{localStorage.setItem('ipx_'+id,el.value)}catch(e){}})});
refresh();setInterval(refresh,4000);
</script>
</body>
</html>)HTML";
