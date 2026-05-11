#include "web_config.h"
#include "wifi_mgr.h"
#include "gpio_mgr.h"
#include "config.h"
#include <WiFiClient.h>
#include <ESP8266HTTPUpdateServer.h>
#include <StreamString.h>

std::unique_ptr<ESP8266WebServer> WebConfigServer::server_;
std::unique_ptr<ESP8266HTTPUpdateServer> httpUpdater_;
void (*WebConfigServer::save_callback_)(const char*, const char*) = nullptr;

static const char INDEX_HTML[] PROGMEM = R"rawliteral(<!DOCTYPE html>
<html>
<head>
<meta charset='UTF-8'>
<meta name='viewport' content='width=device-width, initial-scale=1, maximum-scale=1, user-scalable=no'>
<title>智能插排</title>
<style>
*{margin:0;padding:0;box-sizing:border-box;-webkit-tap-highlight-color:transparent}
body{font-family:-apple-system,BlinkMacSystemFont,'SF Pro Text','Helvetica Neue',sans-serif;background:#f2f2f7;color:#1c1c1e;min-height:100vh}
.header{background:rgba(255,255,255,0.9);backdrop-filter:blur(20px);padding:16px 20px;border-bottom:1px solid #e5e5ea;position:sticky;top:0;z-index:100;text-align:center}
.header h1{font-size:18px;font-weight:600}
.card{background:#fff;border-radius:12px;margin:16px;padding:16px;box-shadow:0 1px 3px rgba(0,0,0,0.08)}
.card-title{font-size:12px;color:#8e8e93;text-transform:uppercase;letter-spacing:.5px;margin-bottom:12px}
.grid{display:flex;gap:10px}
.grid-item{flex:1;background:#f2f2f7;border-radius:10px;padding:14px 8px;text-align:center}
.grid-item .val{font-size:22px;font-weight:600;color:#007aff}
.grid-item .lbl{font-size:10px;color:#8e8e93;margin-top:4px}
.switch-row{display:flex;justify-content:space-between;align-items:center;padding:14px 0;border-bottom:1px solid #e5e5ea}
.switch-row:last-child{border-bottom:none}
.switch-row .lbl{font-size:15px}
.toggle{width:51px;height:31px;background:#e9e9eb;border-radius:16px;position:relative;transition:background .3s;cursor:pointer}
.toggle.on{background:#34c759}
.toggle::after{content:'';width:27px;height:27px;background:#fff;border-radius:50%;position:absolute;top:2px;left:2px;transition:transform .3s;box-shadow:0 2px 4px rgba(0,0,0,0.2)}
.toggle.on::after{transform:translateX(20px)}
.btn{display:block;width:100%;padding:14px;background:#007aff;color:#fff;border:none;border-radius:10px;font-size:15px;font-weight:500;cursor:pointer;margin:10px 0}
.btn:active{background:#0056b3}
.btn-ghost{background:#f2f2f7;color:#007aff}
.info-row{display:flex;justify-content:space-between;padding:8px 0;font-size:13px}
.info-row .lbl{color:#8e8e93}
.hidden{display:none!important}
.modal{position:fixed;top:0;left:0;right:0;bottom:0;background:rgba(0,0,0,.4);z-index:200;display:flex;align-items:center;justify-content:center}
.modal-content{background:#fff;width:92%;max-width:380px;border-radius:14px;padding:20px;max-height:85vh;overflow-y:auto}
.modal-title{font-size:17px;font-weight:600;margin-bottom:16px}
.wifi-list{max-height:280px;overflow-y:auto}
.wifi-item{display:flex;justify-content:space-between;align-items:center;padding:14px 0;border-bottom:1px solid #e5e5ea;cursor:pointer}
.wifi-item:active{background:#f2f2f7}
.wifi-item .name{font-size:15px;font-weight:500}
.wifi-item .rssi{font-size:12px;color:#8e8e93}
.wifi-item .enc{color:#8e8e93;font-size:12px}
.input-field{width:100%;padding:12px;border:1px solid #e5e5ea;border-radius:10px;font-size:15px;margin:8px 0;outline:none}
.input-field:focus{border-color:#007aff}
.progress-bar{height:4px;background:#e5e5ea;border-radius:2px;overflow:hidden;margin:12px 0}
.progress-fill{height:100%;background:#007aff;width:0;transition:width .3s}
.bar-chart{display:flex;align-items:flex-end;justify-content:space-between;height:120px;padding:0 4px}
.bar-wrap{flex:1;display:flex;flex-direction:column;align-items:center;height:100%}
.bar{width:70%;background:#007aff;border-radius:4px 4px 0 0;transition:height .3s;min-height:2px}
.bar-label{font-size:9px;color:#8e8e93;margin-top:4px;text-align:center}
.chart{height:160px;margin:12px 0;position:relative}
.canvas-wrap{position:relative;width:100%;height:100%}
canvas{width:100%;height:100%}
</style>
</head>
<body>
<div class="header"><h1>智能插排</h1></div>

<div class="card">
<div class="card-title">实时功率</div>
<div class="grid">
<div class="grid-item"><div class="val" id="v">--</div><div class="lbl">电压 V</div></div>
<div class="grid-item"><div class="val" id="i">--</div><div class="lbl">电流 A</div></div>
<div class="grid-item"><div class="val" id="p">--</div><div class="lbl">功率 W</div></div>
</div>
<div class="chart"><div class="canvas-wrap"><canvas id="pc"></canvas></div></div>
</div>

<div class="card">
<div class="card-title">继电器控制</div>
<div class="switch-row"><span class="lbl">主继电器</span><div class="toggle" id="rm"></div></div>
<div class="switch-row"><span class="lbl">从继电器</span><div class="toggle" id="rs"></div></div>
<div class="switch-row"><span class="lbl">🔒 锁定</span><div class="toggle" id="lk"></div></div>
</div>

<div class="card">
<div class="card-title">每日用电 (Wh)</div>
<div class="bar-chart" id="bc"></div>
</div>

<div class="card">
<div class="card-title">WiFi 状态</div>
<div class="info-row"><span class="lbl">状态</span><span id="ws">--</span></div>
<div class="info-row"><span class="lbl">信号</span><span id="wr">--</span></div>
<button class="btn btn-ghost" id="wifiBtn">切换 WiFi</button>
</div>

<div class="card">
<div class="card-title">设备信息</div>
<div class="info-row"><span class="lbl">版本</span><span id="ver">V</span></div>
<div class="info-row"><span class="lbl">运行时间</span><span id="up">--</span></div>
<button class="btn" id="otaBtn">固件升级</button>
<button class="btn btn-ghost" id="resetBtn">恢复出厂</button>
</div>

<div id="wifiM" style="display:none;position:fixed;top:0;left:0;right:0;bottom:0;background:rgba(0,0,0,.4);z-index:200;align-items:center;justify-content:center">
<div class="modal-content">
<div class="modal-title">选择 WiFi</div>
<input type="text" id="pwd" class="input-field" placeholder="输入密码" style="display:none">
<div id="wl" class="wifi-list"></div>
<button class="btn btn-ghost" id="scanBtn">扫描</button>
<button class="btn btn-ghost" id="closeWifiBtn">关闭</button>
</div>
</div>

<div id="otaM" style="display:none;position:fixed;top:0;left:0;right:0;bottom:0;background:rgba(0,0,0,.4);z-index:200;align-items:center;justify-content:center">
<div class="modal-content">
<div class="modal-title">固件升级</div>
<div id="opg" class="hidden">
<div class="progress-bar"><div class="progress-fill" id="opf"></div></div>
<p style="text-align:center;font-size:13px;color:#8e8e93" id="ost">上传中...</p>
</div>
<div id="ofm">
<p style="font-size:13px;color:#8e8e93;margin-bottom:12px">选择 .bin 文件进行升级</p>
<input type="file" id="of" accept=".bin" style="margin:10px 0">
<button class="btn" id="doOtaBtn">开始升级</button>
</div>
<button class="btn btn-ghost" id="closeOtaBtn">关闭</button>
</div>
</div>

<script>
var pData=[],pLabel=[];
var eData=[0,0,0,0,0,0,0];
var eLabel=[];
var selSSID='';
var pCtx;

function init(){
  for(var i=0;i<7;i++){
    var d=new Date();
    d.setDate(d.getDate()-(6-i));
    eLabel.push((d.getMonth()+1)+'/'+d.getDate());
  }
  var c=document.getElementById('pc');
  c.width=c.offsetWidth;
  c.height=c.offsetHeight;
  pCtx=c.getContext('2d');
  drawBarChart();

  document.getElementById('rm').addEventListener('click',function(){tr('m')});
  document.getElementById('rs').addEventListener('click',function(){tr('s')});
  document.getElementById('lk').addEventListener('click',function(){toggleLock()});
  document.getElementById('wifiBtn').addEventListener('click',showWifi);
  document.getElementById('scanBtn').addEventListener('click',doScan);
  document.getElementById('closeWifiBtn').addEventListener('click',closeWifi);
  document.getElementById('otaBtn').addEventListener('click',showOta);
  document.getElementById('doOtaBtn').addEventListener('click',doOta);
  document.getElementById('closeOtaBtn').addEventListener('click',closeOta);
  document.getElementById('resetBtn').addEventListener('click',doReset);
}

function drawBarChart(){
  var bc=document.getElementById('bc');
  bc.innerHTML='';
  var max=Math.max.apply(Math,eData.concat([1]));
  eData.forEach(function(v,i){
    var wrap=document.createElement('div');
    wrap.className='bar-wrap';
    var bar=document.createElement('div');
    bar.className='bar';
    bar.style.height=(v/max*100)+'%';
    var lbl=document.createElement('div');
    lbl.className='bar-label';
    lbl.textContent=eLabel[i];
    wrap.appendChild(bar);
    wrap.appendChild(lbl);
    bc.appendChild(wrap);
  });
}

function update(){
  fetch('/api/status').then(function(r){return r.json()}).then(function(d){
    document.getElementById('v').textContent=d.voltage?d.voltage.toFixed(1):'--';
    document.getElementById('i').textContent=d.current?d.current.toFixed(3):'--';
    document.getElementById('p').textContent=d.power?d.power.toFixed(2):'--';
    document.getElementById('ws').textContent=d.connected?'已连接':'未连接';
    document.getElementById('wr').textContent=d.rssi?d.rssi+' dBm':'--';
    document.getElementById('up').textContent=fmt(d.uptime);
    document.getElementById('ver').textContent='V'+d.version;
    var rm=document.getElementById('rm');
    if(d.master)rm.classList.add('on');else rm.classList.remove('on');
    var rs=document.getElementById('rs');
    if(d.slave)rs.classList.add('on');else rs.classList.remove('on');
    var lk=document.getElementById('lk');
    if(d.locked)lk.classList.add('on');else lk.classList.remove('on');
    if(d.power>0){
      var now=new Date();
      var t=(now.getHours()<10?'0':'')+now.getHours()+':'+(now.getMinutes()<10?'0':'')+now.getMinutes();
      pData.push(d.power);
      pLabel.push(t);
      if(pData.length>30){pData.shift();pLabel.shift();}
      drawPowerChart();
    }
  }).catch(function(e){console.log('Update error:',e)});
  setTimeout(update,3000);
}

function drawPowerChart(){
  var c=document.getElementById('pc');
  var ctx=pCtx;
  var w=c.width;
  var h=c.height;
  ctx.clearRect(0,0,w,h);
  ctx.strokeStyle='#e5e5ea';
  ctx.lineWidth=1;
  for(var i=0;i<=4;i++){
    var y=h-h*i/4;
    ctx.beginPath();
    ctx.moveTo(0,y);
    ctx.lineTo(w,y);
    ctx.stroke();
  }
  if(pData.length<2)return;
  var max=Math.max.apply(Math,pData.concat([100]));
  ctx.strokeStyle='#007aff';
  ctx.lineWidth=2;
  ctx.beginPath();
  var step=w/(pData.length-1);
  for(var i=0;i<pData.length;i++){
    var x=i*step;
    var y=h-(pData[i]/max*h);
    if(i==0)ctx.moveTo(x,y);else ctx.lineTo(x,y);
  }
  ctx.stroke();
  ctx.fillStyle='rgba(0,122,255,0.1)';
  ctx.beginPath();
  ctx.moveTo(0,h);
  for(var i=0;i<pData.length;i++){
    var x=i*step;
    var y=h-(pData[i]/max*h);
    ctx.lineTo(x,y);
  }
  ctx.lineTo(w,h);
  ctx.closePath();
  ctx.fill();
}

function fmt(s){
  var d=Math.floor(s/86400);
  var h=Math.floor((s%86400)/3600);
  var m=Math.floor((s%3600)/60);
  return(d>0?d+'天 ':'')+(h>0?h+'时 ':'')+m+'分';
}

function tr(w){
  fetch('/api/relay?w='+w).then(function(r){return r.json()}).then(function(d){
    var t=document.getElementById(w=='m'?'rm':'rs');
    if(d.s)t.classList.add('on');else t.classList.remove('on');
  }).catch(function(e){console.log('Relay error:',e)});
}

function toggleLock(){
  fetch('/api/lock').then(function(r){return r.json()}).then(function(d){
    var lk=document.getElementById('lk');
    if(d.locked)lk.classList.add('on');else lk.classList.remove('on');
  }).catch(function(e){console.log('Lock error:',e)});
}

function showWifi(){
  document.getElementById('wifiM').style.display='flex';
  document.getElementById('pwd').style.display='none';
  document.getElementById('wl').innerHTML='<p style="text-align:center;color:#8e8e93">点击扫描获取WiFi</p>';
}

function doScan(){
  document.getElementById('wl').innerHTML='<p style="text-align:center;color:#8e8e93">扫描中...</p>';
  fetch('/api/scan').then(function(r){return r.json()}).then(function(d){
    var wl=document.getElementById('wl');
    wl.innerHTML='';
    if(d.nets.length===0){wl.innerHTML='<p style="text-align:center;color:#8e8e93">未找到WiFi</p>';return;}
    d.nets.forEach(function(n){
      var item=document.createElement('div');
      item.className='wifi-item';
      item.innerHTML='<div><div class="name">'+n.ssid+'</div><div class="rssi">'+n.rssi+' dBm</div></div><span class="enc">'+(n.enc?'🔒':'📶')+'</span>';
      item.addEventListener('click',function(){sel(n.ssid,n.enc)});
      wl.appendChild(item);
    });
  }).catch(function(e){document.getElementById('wl').innerHTML='<p style="text-align:center;color:#ff3b30">扫描失败</p>';console.log('Scan error:',e)});
}

function sel(ssid,enc){
  selSSID=ssid;
  if(enc){
    document.getElementById('pwd').style.display='block';
  }else{
    conn(ssid,'');
  }
}

function conn(ssid,pwd){
  fetch('/api/connect',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'ssid='+encodeURIComponent(ssid)+'&password='+encodeURIComponent(pwd)})
  .then(function(r){return r.json()}).then(function(d){
    alert(d.ok?'连接成功':'连接失败');
    closeWifi();
  }).catch(function(e){alert('连接失败');console.log('Connect error:',e)});
}

function closeWifi(){
  document.getElementById('wifiM').style.display='none';
  document.getElementById('pwd').style.display='none';
}

function showOta(){
  document.getElementById('otaM').style.display='flex';
  document.getElementById('ofm').classList.remove('hidden');
  document.getElementById('opg').classList.add('hidden');
}

function doOta(){
  var f=document.getElementById('of').files[0];
  if(!f){alert('请选择固件文件');return;}
  document.getElementById('ofm').classList.add('hidden');
  document.getElementById('opg').classList.remove('hidden');
  var xhr=new XMLHttpRequest();
  xhr.upload.addEventListener('progress',function(e){
    if(e.lengthComputable){
      var pct=Math.round((e.loaded/e.total)*100);
      document.getElementById('opf').style.width=pct+'%';
      document.getElementById('ost').textContent='上传中... '+pct+'%';
    }
  });
  xhr.addEventListener('load',function(){
    document.getElementById('ost').textContent='升级成功，设备重启中...';
    setTimeout(function(){window.location.reload();},5000);
  });
  xhr.addEventListener('error',function(){
    document.getElementById('ost').textContent='上传失败';
    setTimeout(closeOta,2000);
  });
  xhr.open('POST','/update');
  xhr.send(f);
}

function closeOta(){
  document.getElementById('otaM').style.display='none';
}

function doReset(){
  if(confirm('确定恢复出厂设置?')){
    fetch('/api/reset',{method:'POST'}).then(function(){alert('设备将重启')}).catch(function(e){console.log('Reset error:',e)});
  }
}

document.addEventListener('DOMContentLoaded',function(){init();update();});
</script>
</body>
</html>)rawliteral";

void WebConfigServer::init() {
    server_ = std::make_unique<ESP8266WebServer>(WEB_SERVER_PORT);
    httpUpdater_ = std::make_unique<ESP8266HTTPUpdateServer>();

    httpUpdater_->setup(server_.get(), "/update", "admin", "admin");

    server_->on("/", HTTP_GET, []() {
        server_->send_P(200, "text/html; charset=UTF-8", INDEX_HTML);
    });

    server_->on("/api/status", HTTP_GET, []() {
        String json = "{";
        json += "\"connected\":" + String(WiFiManager::isConnected() ? "true" : "false");
        json += ",\"ip\":\"" + WiFi.localIP().toString() + "\"";
        json += ",\"ssid\":\"" + WiFiManager::getCurrentSSID() + "\"";
        json += ",\"rssi\":" + String(WiFiManager::getCurrentRSSI());
        json += ",\"version\":\"" + String(VERSION) + "\"";
        json += ",\"uptime\":" + String(millis() / 1000);
        json += ",\"voltage\":220.0,\"current\":0.500,\"power\":110.0";
        json += ",\"master\":" + String(GPIOManager::getRelayMaster() ? "true" : "false");
        json += ",\"slave\":" + String(GPIOManager::getRelaySlave() ? "true" : "false");
        json += ",\"locked\":" + String(GPIOManager::isLocked() ? "true" : "false");
        json += "}";
        server_->send(200, "application/json", json);
    });

    server_->on("/api/relay", HTTP_GET, []() {
        String w = server_->arg("w");
        bool state = false;
        if (w == "m") {
            GPIOManager::setRelayMaster(!GPIOManager::getRelayMaster());
            state = GPIOManager::getRelayMaster();
        } else if (w == "s") {
            GPIOManager::setRelaySlave(!GPIOManager::getRelaySlave());
            state = GPIOManager::getRelaySlave();
        }
        String response = "{\"s\":" + String(state ? "true" : "false") + "}";
        server_->send(200, "application/json", response);
    });

    server_->on("/api/lock", HTTP_GET, []() {
        bool current = GPIOManager::isLocked();
        GPIOManager::setLocked(!current);
        String response = "{\"locked\":" + String(GPIOManager::isLocked() ? "true" : "false") + "}";
        server_->send(200, "application/json", response);
    });

    server_->on("/api/scan", HTTP_GET, []() {
        WiFi.scanDelete();
        int n = WiFi.scanComplete();
        if (n < 0) {
            n = WiFi.scanNetworks(true, true);
            delay(2000);
            n = WiFi.scanComplete();
        }

        String json = "{\"nets\":[";
        if (n > 0) {
            for (int i = 0; i < n && i < 15; i++) {
                if (i > 0) json += ",";
                String ssid = WiFi.SSID(i);
                ssid.replace("\"", "\\\"");
                json += "{\"ssid\":\"" + ssid + "\",\"rssi\":" + WiFi.RSSI(i) + ",\"enc\":" + (WiFi.encryptionType(i) != ENC_TYPE_NONE ? "true" : "false") + "}";
            }
        }
        json += "]}";
        WiFi.scanDelete();
        server_->send(200, "application/json", json);
    });

    server_->on("/api/connect", HTTP_POST, []() {
        String ssid = server_->arg("ssid");
        String password = server_->arg("password");

        WiFiManager::saveConfig(ssid.c_str(), password.c_str());
        WiFi.begin(ssid.c_str(), password.c_str());

        String response = "{\"ok\":true,\"ssid\":\"" + ssid + "\"}";
        server_->send(200, "application/json", response);
    });

    server_->on("/api/reset", HTTP_POST, []() {
        WiFiManager::reset();
        server_->send(200, "application/json", "{\"ok\":true}");
    });

    server_->onNotFound([]() {
        server_->send(404, "text/plain", "Not Found");
    });

    server_->begin();
    Serial.printf("[Web] Server started on port %d\n", WEB_SERVER_PORT);
    Serial.printf("[OTA] Update endpoint: /update (user:admin pass:admin)\n");
}

void WebConfigServer::handle() {
    server_->handleClient();
}

void WebConfigServer::setSaveCallback(void (*callback)(const char*, const char*)) {
    save_callback_ = callback;
}
