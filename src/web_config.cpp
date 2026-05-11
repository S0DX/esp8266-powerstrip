#include "web_config.h"
#include "wifi_mgr.h"
#include "gpio_mgr.h"
#include "config.h"
#include "energy_mgr.h"
#include <sys/time.h>
#include "sy7t609.h"
#include "mqtt_mgr.h"
#include <EEPROM.h>
#include <WiFiClient.h>
#include <ESP8266HTTPUpdateServer.h>
#include <ESP8266httpUpdate.h>
#include <StreamString.h>
#include <DNSServer.h>

std::unique_ptr<ESP8266WebServer> WebConfigServer::server_;
std::unique_ptr<ESP8266HTTPUpdateServer> httpUpdater_;
std::unique_ptr<DNSServer> dnsServer_;
void (*WebConfigServer::save_callback_)(const char*, const char*) = nullptr;

static const char INDEX_HTML[] PROGMEM = R"rawliteral(<!DOCTYPE html>
<html>
<head>
<meta charset='UTF-8'>
<meta name='viewport' content='width=device-width, initial-scale=1, maximum-scale=1, user-scalable=no'>
<title>智能插排</title>
<style>
*{margin:0;padding:0;box-sizing:border-box;-webkit-tap-highlight-color:transparent}
body{font-family:-apple-system,BlinkMacSystemFont,'SF Pro Text','Helvetica Neue',sans-serif;background:#f2f2f7;color:#1c1c1e;min-height:100vh;overflow-x:hidden}
.header{background:rgba(255,255,255,0.9);padding:16px 20px;border-bottom:1px solid #e5e5ea;position:sticky;top:0;z-index:100;text-align:center;display:flex;justify-content:space-between;align-items:center}
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
.toggle-lg{width:74px;height:40px;background:#e9e9eb;border-radius:20px;position:relative;transition:background .3s;cursor:pointer}
.toggle-lg.on{background:#34c759}
.toggle-lg::after{content:'';width:34px;height:34px;background:#fff;border-radius:50%;position:absolute;top:3px;left:3px;transition:transform .3s;box-shadow:0 2px 4px rgba(0,0,0,0.2)}
.toggle-lg.on::after{transform:translateX(34px)}
.timer-btn{display:block;width:100%;padding:16px;background:#e9e9eb;color:#1c1c1e;border:none;border-radius:10px;font-size:16px;font-weight:600;cursor:pointer;margin:10px 0;transition:all .3s}
.timer-btn.active{background:#ff9500;color:#fff}
.btn{display:block;width:100%;padding:14px;background:#007aff;color:#fff;border:none;border-radius:10px;font-size:15px;font-weight:500;cursor:pointer;margin:10px 0}
.btn:active{background:#0056b3}
.btn-ghost{background:#f2f2f7;color:#007aff}
.info-row{display:flex;justify-content:space-between;padding:8px 0;font-size:13px}
.info-row .lbl{color:#8e8e93}
.hidden{display:none!important}
.settings-btn{cursor:pointer;-webkit-tap-highlight-color:transparent;transition:opacity .2s}
.settings-btn:active{opacity:0.6}
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
.bar-chart{display:flex;align-items:flex-end;justify-content:space-between;gap:6px;height:100px;padding:0 2px}
.bar-wrap{flex:1;display:flex;flex-direction:column;align-items:center;height:100%}
.bar{width:100%;background:linear-gradient(180deg,#30d158,#34c759);border-radius:4px 4px 0 0;min-height:2px;transition:height .6s ease}
.bar-today{background:linear-gradient(180deg,#0a84ff,#007aff)}
.bar-label{font-size:9px;color:#8e8e93;margin-top:4px;text-align:center;white-space:nowrap}
.bar-val{font-size:8px;color:#007aff;margin-bottom:2px;min-height:10px}
.loading-overlay{position:fixed;top:0;left:0;right:0;bottom:0;background:rgba(242,242,247,.85);z-index:9999;display:flex;flex-direction:column;align-items:center;justify-content:center;transition:opacity .3s;pointer-events:none}
.loading-overlay.hide{opacity:0;pointer-events:none}
.spinner{width:28px;height:28px;border:3px solid #e5e5ea;border-top-color:#007aff;border-radius:50%;animation:spin .8s linear infinite}
@keyframes spin{to{transform:rotate(360deg)}}
.loading-text{margin-top:12px;font-size:14px;color:#8e8e93}
@keyframes cardIn{from{opacity:0;transform:translateY(12px)}to{opacity:1;transform:translateY(0)}}
.drag-handle{cursor:grab;touch-action:none;-webkit-user-select:none;user-select:none;padding:8px;color:#c7c7cc;font-size:20px;line-height:1}
.drag-handle:active{cursor:grabbing}
.sort-item{display:flex;align-items:center;background:#f2f2f7;border-radius:12px;padding:12px 16px;margin-bottom:8px;transition:transform .2s,box-shadow .2s;touch-action:none}
.sort-item.dragging{transform:scale(1.03);box-shadow:0 8px 24px rgba(0,0,0,0.12);background:#fff;z-index:10}
.sort-item .sort-name{flex:1;font-size:15px;font-weight:500}
.sort-item .sort-icon{font-size:18px;color:#007aff}
.sort-toggle{width:42px;height:26px;background:#e9e9eb;border-radius:13px;position:relative;transition:background .3s;cursor:pointer;flex-shrink:0}
.sort-toggle.on{background:#34c759}
.sort-toggle::after{content:'';width:22px;height:22px;background:#fff;border-radius:50%;position:absolute;top:2px;left:2px;transition:transform .3s;box-shadow:0 1px 3px rgba(0,0,0,.15)}
.sort-toggle.on::after{transform:translateX(16px)}
</style>
</head>
<body>
<div class="loading-overlay" id="loadingOverlay">
<div class="spinner"></div>
<div class="loading-text">加载中...</div>
</div>
<div class="header">
  <h1>智能插排</h1>
  <span id="chipTime" style="font-size:13px;color:#8e8e93">--:--</span>
</div>

<div id='cardTemplates' style='display:none;'>
 
<div class="card" data-card="relay">
<div class="card-title">继电器控制</div>
<div style="display:flex;gap:16px;justify-content:center;padding:8px 0">
<div class="switch-row" style="flex-direction:column;align-items:center;border:none;padding:12px 16px;background:#f2f2f7;border-radius:14px;flex:1">
<span style="font-size:14px;font-weight:500">从继电器</span>
<span style="font-size:11px;color:#8e8e93;margin:4px 0 10px">可控端</span>
<div class="toggle-lg" id="rs"></div>
</div>
<div class="switch-row" style="flex-direction:column;align-items:center;border:none;padding:12px 16px;background:#f2f2f7;border-radius:14px;flex:1">
<span style="font-size:14px;font-weight:500">主继电器</span>
<span style="font-size:11px;color:#8e8e93;margin:4px 0 10px">常通端</span>
<div class="toggle-lg" id="rm"></div>
</div>
</div>
<div class="switch-row" style="border-top:1px solid #e5e5ea;margin-top:12px;padding-top:12px">
<span style="font-size:15px">锁定模式<span style="font-size:11px;color:#8e8e93;display:block;margin-top:2px">禁用物理按键，仅 Web 端控制</span></span>
<div class="toggle" id="lk"></div>
</div>
<div class="switch-row">
<span style="font-size:15px">拔除断电<span style="font-size:11px;color:#8e8e93;display:block;margin-top:2px">功率低于阈值自动关闭</span></span>
<div class="toggle" id="pwrOffHome" onclick="togglePowerOffHome()"></div>
</div>
</div>

<div class="card" data-card="wifidetect">
<div class="card-title">人来上电</div>
<p style="font-size:12px;color:#8e8e93;margin:0 0 12px">检测指定WiFi信号自动开启继电器</p>
<div class="info-row"><span class="lbl">启用检测</span><div class="toggle" id="wifiDetectToggle" onclick="toggleWifiDetect()"></div></div>
<div class="info-row" style="margin-top:12px"><span class="lbl">检测目标</span><span id="wifiDetectTarget" style="color:#007aff;font-weight:600">未设置</span></div>
<div id="wifiDetectMacRow" class="info-row" style="display:none"><span class="lbl">MAC 地址</span><span id="wifiDetectMacVal" style="color:#8e8e93;font-size:12px">--</span></div>
<button class="btn" style="width:100%;margin-top:8px" onclick="showWiFiSelect()">选择WiFi</button>
<div class="info-row" style="margin-top:8px"><span class="lbl">状态</span><span id="wifiDetectStatus" style="color:#34c759;font-weight:600">--</span></div>
<button class="btn btn-ghost" style="width:100%;margin-top:12px" onclick="showWifiDetectMore()">更多配置</button>
</div>

<div class="card" data-card="power">
<div class="card-title" style="display:flex;justify-content:space-between;align-items:center">实时电量<div class="toggle" id="mt"></div></div>
<div class="grid" style="margin-bottom:12px">
<div class="grid-item"><div class="val" id="mV">--</div><div class="lbl">电压(V)</div></div>
<div class="grid-item"><div class="val" id="mI">--</div><div class="lbl">电流(A)</div></div>
</div>
<div class="grid">
<div class="grid-item"><div class="val" id="mP">--</div><div class="lbl">功率(W)</div></div>
<div class="grid-item"><div class="val" id="mE">--</div><div class="lbl">用电量(kWh)</div></div>
</div>
</div>

<div class="card" data-card="history">
<div class="card-title">用电历史 <span style="font-size:10px;color:#c7c7cc;font-weight:400">过去 7 天</span></div>
<div class="bar-chart" id="histChart" style="margin:8px 0 0">
<div class="bar-wrap"><div class="bar-val" id="hv0"></div><div class="bar" id="hb0" style="height:2px"></div><div class="bar-label" id="hl0">-</div></div>
<div class="bar-wrap"><div class="bar-val" id="hv1"></div><div class="bar" id="hb1" style="height:2px"></div><div class="bar-label" id="hl1">-</div></div>
<div class="bar-wrap"><div class="bar-val" id="hv2"></div><div class="bar" id="hb2" style="height:2px"></div><div class="bar-label" id="hl2">-</div></div>
<div class="bar-wrap"><div class="bar-val" id="hv3"></div><div class="bar" id="hb3" style="height:2px"></div><div class="bar-label" id="hl3">-</div></div>
<div class="bar-wrap"><div class="bar-val" id="hv4"></div><div class="bar" id="hb4" style="height:2px"></div><div class="bar-label" id="hl4">-</div></div>
<div class="bar-wrap"><div class="bar-val" id="hv5"></div><div class="bar" id="hb5" style="height:2px"></div><div class="bar-label" id="hl5">-</div></div>
<div class="bar-wrap"><div class="bar-val" id="hv6" style="color:#007aff;font-weight:600"></div><div class="bar bar-today" id="hb6" style="height:2px"></div><div class="bar-label" id="hl6" style="color:#007aff;font-weight:600">今天</div></div>
</div>
<div style="display:flex;justify-content:space-between;margin-top:10px;font-size:11px;color:#8e8e93"><span>单位: kWh</span><span id="histTotal">合计: --</span></div>
</div>

<div class="card" data-card="wifi">
<div class="card-title">WiFi 状态</div>
<div class="info-row"><span class="lbl">连接状态</span><span id="ws">--</span></div>
<div class="info-row"><span class="lbl">信号强度</span><span id="wr">--</span></div>
<div class="info-row"><span class="lbl">已连接SSID</span><span id="wssid">--</span></div>
<div class="info-row"><span class="lbl">设备IP</span><span id="otaip">--</span></div>
<div class="info-row"><span class="lbl">WiFi断开红灯告警</span><div class="toggle" id="redLedToggle" onclick="toggleRedLed()"></div></div>
<button class="btn btn-ghost" id="wifiBtn">连接 WiFi</button>
</div>
 
</div>
 
<div id='cardsContainer'></div>

<div class="card">
<div class="card-title">系统设置</div>
<div style="margin-top:4px">
  <div class="lbl" style="margin-bottom:8px">AP 热点密码</div>
  <div style="display:flex;gap:8px">
    <input type="password" id="apPass" style="flex:1;padding:10px;border:1px solid #e5e5ea;border-radius:8px;font-size:14px" placeholder="设置八位以上密码">
    <button class="timer-btn" style="width:auto;margin:0;padding:0 16px;height:38px" onclick="saveApPass()">保存</button>
  </div>
  <div class="lbl" style="margin-top:12px;margin-bottom:8px">AP 名称后缀</div>
  <div style="display:flex;gap:8px">
    <input type="number" id="apSuffix" class="input-field" style="flex:1;padding:10px;border:1px solid #e5e5ea;border-radius:8px;font-size:14px;margin:0" step="1" min="0" max="255" value="0" placeholder="编号">
    <button class="timer-btn" style="width:auto;margin:0;padding:0 16px;height:38px" onclick="saveApSuffix()">保存</button>
  </div>
  <p style="font-size:11px;color:#8e8e93;margin:4px 0 0">AP 名称为 PowerStrip-XXX，0=无后缀</p>
  <button class="btn btn-ghost" style="margin-top:12px;width:100%" onclick="showMoreSettings()">更多设置</button>
</div>
</div>

<div class="card">
<div class="card-title">设备信息</div>
<div class="info-row"><span class="lbl">版本</span><span id="ver">V</span></div>
<div class="info-row"><span class="lbl">运行时间</span><span id="up">--</span></div>
<div style="display:flex;gap:8px">
<button class="btn" id="otaBtn">固件升级</button>
<button class="btn btn-ghost" id="restartBtn" onclick="confirmRestart()">重启设备</button>
</div>
<button class="btn btn-ghost" id="resetBtn" onclick="confirmReset()">恢复出厂</button>
</div>


<div id="wifiM" style="display:none;position:fixed;top:0;left:0;right:0;bottom:0;background:rgba(0,0,0,.4);z-index:200;align-items:center;justify-content:center">
<div class="modal-content">
<div class="modal-title">连接 WiFi</div>
<div id="wifiSucc" class="hidden" style="background:#34c759;color:#fff;padding:14px;border-radius:10px;margin-bottom:12px;text-align:center">
<div style="font-size:15px;font-weight:600">连接成功！</div>
<div style="font-size:12px;margin-top:6px">WiFi: <span id="connSSID"></span></div>
</div>
<div id="wifiSelInfo" style="background:#007aff;color:#fff;padding:12px 14px;border-radius:10px;margin-bottom:12px;display:none">
<div style="font-size:13px;font-weight:500">将要连接: <span id="selWifiName" style="font-weight:600"></span></div>
</div>
<input type="text" id="pwd" class="input-field" placeholder="输入密码" style="display:none;margin-bottom:12px">
<button class="btn" id="connBtn" style="display:none;margin-bottom:12px" onclick="conn(selSSID, document.getElementById('pwd').value)">确定连接</button>
<div id="wl" class="wifi-list"></div>
<button class="btn btn-ghost" id="scanBtn">扫描</button>
<button class="btn btn-ghost" id="closeWifiBtn">关闭</button>
</div>
</div>

<div id="wifiSelM" style="display:none;position:fixed;top:0;left:0;right:0;bottom:0;background:rgba(0,0,0,.4);z-index:200;align-items:center;justify-content:center">
<div class="modal-content">
<div class="modal-title">选择人来上电目标WiFi</div>
<div id="wifiSelSucc" class="hidden" style="background:#34c759;color:#fff;padding:14px;border-radius:10px;margin-bottom:12px;text-align:center">
<div style="font-size:15px;font-weight:600">已选择！</div>
<div style="font-size:12px;margin-top:6px">目标: <span id="selSSID"></span></div>
</div>
<div id="wsl" class="wifi-list"></div>
<button class="btn btn-ghost" id="scanSelBtn">扫描</button>
<button class="btn btn-ghost" id="closeWifiSelBtn">关闭</button>
</div>
</div>

<div id="wifiSettingsM" style="display:none;position:fixed;top:0;left:0;right:0;bottom:0;background:rgba(0,0,0,.4);z-index:200;align-items:center;justify-content:center">
<div class="modal-content">
<div class="modal-title">人来上电检测设置</div>
<div class="info-row" style="margin-top:8px">
  <span class="lbl">检测距离(RSSI阈值)</span>
  <span id="wifiSettingsRssiVal" style="color:#007aff;font-weight:600">-70 dBm</span>
</div>
<div style="display:flex;align-items:center;gap:8px;margin-top:4px;margin-bottom:16px">
  <input type="range" id="wifiSettingsRssi" min="-90" max="-30" value="-70" style="flex:1" oninput="document.getElementById('wifiSettingsRssiVal').textContent=this.value+' dBm'">
</div>
<div class="info-row">
  <span class="lbl">扫描频率</span>
  <select id="wifiSettingsScanSpeed" style="padding:6px 10px;border:1px solid #e5e5ea;border-radius:8px;font-size:13px;background:#fff;width:100%">
    <option value="0">慢 (30秒)</option>
    <option value="1" selected>中 (15秒)</option>
    <option value="2">快 (5秒)</option>
    <option value="3">自动学习</option>
  </select>
</div>
<p style="font-size:11px;color:#8e8e93;margin:4px 0 0">自动学习模式根据历史数据动态调整扫描频率</p>
<div id="wifiAutoStatus" class="info-row" style="margin-top:12px;display:none">
  <span class="lbl">学习状态</span>
  <span id="wifiAutoStatusVal" style="color:#8e8e93;font-size:12px">--</span>
</div>
<div style="display:flex;gap:8px;margin-top:16px">
  <button class="btn" style="flex:1" onclick="saveWifiDetectSettings()">保存</button>
  <button class="btn btn-ghost" style="flex:1" onclick="closeWifiDetectSettings()">取消</button>
</div>
</div>
</div>

<div id="wifiDetectMoreM" style="display:none;position:fixed;top:0;left:0;right:0;bottom:0;background:rgba(0,0,0,.4);z-index:200;align-items:center;justify-content:center">
<div class="modal-content">
<div class="modal-title">更多配置</div>
<div class="info-row" style="margin-top:12px"><span class="lbl">按键检测</span><div class="toggle" id="buttonDetectToggle" onclick="toggleButtonDetect()"></div></div>
<p style="font-size:11px;color:#8e8e93;margin:4px 0 8px">锁定模式下按物理按钮触发单次WiFi扫描</p>
<div class="info-row" style="margin-top:12px"><span class="lbl">联锁倒计时</span><div class="toggle" id="wifiDetectLinkToggle" onclick="toggleWifiDetectLink()"></div></div>
<p style="font-size:11px;color:#8e8e93;margin:4px 0 8px">WiFi消失时自动启动关闭倒计时</p>
<div class="info-row" style="margin-top:12px"><span class="lbl">仅开主继电器</span><div class="toggle" id="wdOnlyMaster" onclick="toggleWifiDetectOnlyMaster()"></div></div>
<p style="font-size:11px;color:#8e8e93;margin:4px 0 8px">开启后人来上电只打开主继电器</p>
<div class="info-row settings-btn" style="margin-top:12px" onclick="showWifiDetectSettings()">
<span class="lbl">检测设置</span><span id="wifiDetectSettingsVal" style="color:#007aff;font-weight:500;font-size:13px">--</span><span style="color:#007aff;margin-left:4px;font-size:10px">&#9654;</span></div>
<button class="btn btn-ghost" style="width:100%;margin-top:16px" onclick="closeWifiDetectMore()">关闭</button>
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
<input type="password" id="op" class="input-field" placeholder="输入升级密码">
<button class="btn" id="doOtaBtn">开始升级</button>
</div>
<button class="btn btn-ghost" id="closeOtaBtn">关闭</button>
</div>
</div>

<div id="moreSettingsM" style="display:none;position:fixed;top:0;left:0;right:0;bottom:0;background:#f2f2f7;z-index:300;overflow-y:auto">
<div style="min-height:100vh;padding:16px;padding-bottom:80px">
<div style="display:flex;justify-content:space-between;align-items:center;margin-bottom:16px">
<h1 style="font-size:22px;margin:0">更多设置</h1>
<button class="btn btn-ghost" style="width:auto;padding:8px 16px" onclick="closeMoreSettings()">关闭</button>
</div>

<div class="card">
<div class="card-title">主页卡片排序</div>
<p style="font-size:12px;color:#8e8e93;margin:0 0 12px">拖动调整卡片显示顺序</p>
<div id="sortList"></div>
</div>

<div class="card">
<div class="card-title">拔除断电设置</div>
<div style="display:flex;align-items:center;gap:12px;padding:8px 0">
  <span style="font-size:14px">功率阈值:</span>
  <input type="number" id="pwrThresh" class="input-field" style="width:80px;margin:0;padding:8px" step="0.1" min="0.1" max="5" value="0.5">
  <span style="font-size:14px">W</span>
  <button class="timer-btn" style="width:auto;margin:0;padding:0 12px;height:32px;font-size:13px" onclick="savePowerOff()">保存</button>
</div>
</div>

<div class="card">
<div class="card-title">倒计时关闭</div>
<div style="padding:8px 0">
<div class="info-row" style="margin-bottom:10px"><span class="lbl">物理按钮自动倒计时</span><div class="toggle" id="buttonAutoTimerToggle" onclick="toggleButtonAutoTimer()"></div></div>
<p style="font-size:11px;color:#8e8e93;margin:4px 0 8px">开启后按物理按钮自动启动倒计时关闭</p>
<select id="timerDuration" style="width:100%;padding:12px;border:1px solid #e5e5ea;border-radius:8px;font-size:15px;background:#fff;margin-bottom:12px" onchange="setTimerDuration(this.value)">
<option value="1">1 小时</option>
<option value="2">2 小时</option>
<option value="4">4 小时</option>
<option value="6">6 小时</option>
<option value="8">8 小时</option>
<option value="12">12 小时</option>
</select>
<button class="timer-btn" id="tm" onclick="toggleTimer()">启动定时关闭</button>
</div>
</div>

<div class="card">
<div class="card-title">24 小时时间段循环</div>
<p style="font-size:13px;color:#8e8e93;margin-bottom:12px">设定设备在此区间内自动保持开启 (依赖 NTP 联网)</p>
<div id="cyclePeriodsList"></div>
<div id="cycleAddRow" style="display:flex;gap:12px;margin-bottom:12px">
  <div style="flex:1">
    <div class="lbl" style="margin-bottom:4px">开启时间</div>
    <input type="time" id="cStartTime" class="input-field" style="margin:0;padding:8px">
  </div>
  <div style="flex:1">
    <div class="lbl" style="margin-bottom:4px">结束时间</div>
    <input type="time" id="cEndTime" class="input-field" style="margin:0;padding:8px">
  </div>
</div>
<div style="display:flex;gap:8px;margin-bottom:12px">
  <button class="timer-btn" style="flex:1" onclick="addCyclePeriod()">添加时段</button>
</div>
<div class="info-row" style="margin-bottom:12px">
  <span class="lbl">启用循环</span>
  <div class="toggle" id="cycleToggle" onclick="toggleCycle()"></div>
</div>
</div>

<div class="card">
<div class="card-title">MQTT 代理平台</div>
<div class="info-row" style="margin-bottom:12px">
  <span class="lbl">启用 MQTT</span>
  <div class="toggle" id="mqttToggle" onclick="toggleMqtt()"></div>
</div>
<input type="text" id="mqServer" class="input-field" placeholder="服务器地址 (IP 或域名)">
<input type="number" id="mqPort" class="input-field" placeholder="端口号 (默认 1883)">
<input type="text" id="mqUser" class="input-field" placeholder="用户名 (选填)">
<input type="password" id="mqPass" class="input-field" placeholder="密码 (选填)">
<button class="btn" style="margin-top:12px" onclick="saveMqtt()">保存 MQTT 设置</button>
</div>

<div class="card">
<div class="card-title">计费供电</div>
<p style="font-size:12px;color:#8e8e93;margin:0 0 12px">启用后按设定用电量自动关闭（与拔除断电互斥）</p>
<div class="info-row">
  <span class="lbl">启用计费供电</span>
  <div class="toggle" id="billingToggle" onclick="toggleBilling()"></div>
</div>
<div style="display:flex;align-items:center;gap:12px;padding:8px 0">
  <span style="font-size:14px">用电量阈值:</span>
  <input type="number" id="billingThresh" class="input-field" style="width:80px;margin:0;padding:8px" step="1" min="1" max="50" value="10">
  <span style="font-size:14px">度</span>
  <button class="timer-btn" style="width:auto;margin:0;padding:0 12px;height:32px;font-size:13px" onclick="saveBilling()">保存</button>
</div>
<div class="info-row" style="margin-top:8px">
  <span class="lbl">已用电量</span>
  <span id="billingUsed" style="color:#007aff;font-weight:600">--</span>
</div>
</div>

<div class="card">
<div class="card-title">电费计算</div>
<div style="display:flex;align-items:center;gap:12px;padding:8px 0">
  <span style="font-size:14px">电价:</span>
  <input type="number" id="energyPrice" class="input-field" style="width:80px;margin:0;padding:8px" step="0.01" min="0.1" max="2" value="0.6">
  <span style="font-size:14px">元/度</span>
  <button class="timer-btn" style="width:auto;margin:0;padding:0 12px;height:32px;font-size:13px" onclick="savePrice()">保存</button>
</div>
<div class="info-row" style="margin-top:8px">
  <span class="lbl">当前电费</span>
  <span id="currentCost" style="color:#ff9500;font-weight:600">--</span>
</div>
<div class="info-row" style="margin-top:8px">
  <span class="lbl">本月用电</span>
  <span id="monthEnergy" style="color:#34c759;font-weight:600">--</span>
</div>
<div class="info-row" style="margin-top:8px">
  <span class="lbl">上月用电</span>
  <span id="lastMonthEnergy" style="color:#8e8e93;font-weight:600">--</span>
</div>
<div style="margin-top:12px;padding-top:12px;border-top:1px solid #e5e5ea">
  <button class="timer-btn" style="width:100%;background:#ff3b30;color:#fff" onclick="resetEnergyHistory()">清零历史电量和电费记录</button>
</div>
</div>

<div class="card">
<div class="card-title">局域网域名</div>
<p style="font-size:12px;color:#8e8e93;margin:0 0 12px">设置 mDNS 域名，可在局域网通过域名访问</p>
<div style="display:flex;align-items:center;gap:6px;padding:8px 0">
  <input type="text" id="mdnsHostname" class="input-field" style="flex:1;margin:0;padding:8px" placeholder="power" maxlength="31">
  <span style="font-size:13px;color:#8e8e93;white-space:nowrap">.local</span>
  <button class="timer-btn" style="width:auto;margin:0;padding:0 12px;height:38px;font-size:13px;white-space:nowrap" onclick="saveMDNS()">保存</button>
</div>
<p style="font-size:11px;color:#8e8e93;margin:4px 0 0">1-31 个字符，默认 power，重启设备后生效</p>
</div>

</div>
</div>

<script>
var selSSID='';

function init(){
  try{
    renderCards(cardOrder);
    var tm=document.getElementById('tm');
    if(tm)tm.addEventListener('click',toggleTimer);
    
    var scanBtn=document.getElementById('scanBtn');
    if(scanBtn)scanBtn.addEventListener('click',doScan);

    var closeWifiBtn=document.getElementById('closeWifiBtn');
    if(closeWifiBtn)closeWifiBtn.addEventListener('click',closeWifi);

    var scanSelBtn=document.getElementById('scanSelBtn');
    if(scanSelBtn)scanSelBtn.addEventListener('click',doScanSelect);

    var closeWifiSelBtn=document.getElementById('closeWifiSelBtn');
    if(closeWifiSelBtn)closeWifiSelBtn.addEventListener('click',closeWifiSelect);
    
    var otaBtn=document.getElementById('otaBtn');
    if(otaBtn)otaBtn.addEventListener('click',showOta);
    
    var doOtaBtn=document.getElementById('doOtaBtn');
    if(doOtaBtn)doOtaBtn.addEventListener('click',doOta);
    
    var closeOtaBtn=document.getElementById('closeOtaBtn');
    if(closeOtaBtn)closeOtaBtn.addEventListener('click',closeOta);
    
    var resetBtn=document.getElementById('resetBtn');
    if(resetBtn)resetBtn.addEventListener('click',doReset);
    
    var moreSettingsBtn=document.querySelector('button[onclick="showMoreSettings()"]');
    if(moreSettingsBtn)moreSettingsBtn.addEventListener('click',showMoreSettings);
    
    var closeMoreBtn=document.querySelector('button[onclick="closeMoreSettings()"]');
    if(closeMoreBtn)closeMoreBtn.addEventListener('click',closeMoreSettings);
    loadCardOrder(0);
    fetch('/api/time?ts='+Math.floor(Date.now()/1000));
  }catch(e){
    console.error('Error initializing buttons:',e);
  }
}

function showMoreSettings(){
  document.getElementById('moreSettingsM').style.display='block';
  renderSortList(cardOrder);
}

function closeMoreSettings(){
  document.getElementById('moreSettingsM').style.display='none';
}

// 显示模态框
function showModal(title,content){
  var modalHtml='<div class="modal" id="tempModal" onclick="if(event.target===this){closeModal(\'tempModal\');}">';
  modalHtml+='<div class="modal-content">';
  modalHtml+='<div class="modal-title">'+title+'</div>';
  modalHtml+=content;
  modalHtml+='</div></div>';
  
  var tempDiv=document.createElement('div');
  tempDiv.innerHTML=modalHtml;
  document.body.appendChild(tempDiv.firstChild);
}

function closeModal(id){
  var m=document.getElementById(id);
  if(m)m.remove();
}

function update(){
  fetch('/api/status').then(function(r){return r.json()}).then(function(d){
    try{
    if(d.cv !== undefined && d.cv !== cardVisibility){
      cardVisibility=d.cv;
      renderCards(cardOrder);
    }
    var ws=document.getElementById('ws');
    if(ws){
      if(d.conn){
        ws.textContent='已连接';
        ws.style.color='#34c759';
      }else{
        ws.textContent='未连接';
        ws.style.color='#ff3b30';
      }
    }
    var wr=document.getElementById('wr');
    if(wr){
      if(d.conn && d.rssi > -999){
        var rssi=d.rssi;
        var color='#34c759';
        if(rssi < -70) color='#ff9500';
        if(rssi < -85) color='#ff3b30';
        wr.innerHTML='<span style="color:'+color+'">'+rssi+' dBm</span>';
      }else{
        wr.textContent='--';
      }
    }
    var wssid=document.getElementById('wssid');
    if(wssid) wssid.textContent=(d.conn && d.ssid)?d.ssid:'--';
    var otaip=document.getElementById('otaip');
    if(otaip) otaip.textContent=(d.conn && d.ip)?d.ip:'--';
    var up=document.getElementById('up'); if(up) up.textContent=fmt(d.up);
    var ver=document.getElementById('ver'); if(ver) ver.textContent='V'+d.ver;
    var rm=document.getElementById('rm'); if(rm){if(d.m)rm.classList.add('on');else rm.classList.remove('on');}
    var rs=document.getElementById('rs'); if(rs){if(d.s)rs.classList.add('on');else rs.classList.remove('on');}
    var lk=document.getElementById('lk'); if(lk){if(d.lk)lk.classList.add('on');else lk.classList.remove('on');}
    var mV=document.getElementById('mV'); if(mV) mV.textContent=d.me?(d.v!==undefined?d.v:'--'):'--';
    var mI=document.getElementById('mI'); if(mI) mI.textContent=d.me?(d.i!==undefined?d.i:'--'):'--';
    var mP=document.getElementById('mP'); if(mP) mP.textContent=d.me?(d.p!==undefined?d.p:'--'):'--';
    var mE=document.getElementById('mE'); if(mE) mE.textContent=d.me?(d.e!==undefined?(d.e/1000).toFixed(2):'--'):'--';
    var mt=document.getElementById('mt'); if(mt){if(d.me)mt.classList.add('on');else mt.classList.remove('on');}
    var tdEl=document.getElementById('timerDuration'); if(tdEl){if(d.td && d.td>0)tdEl.value=d.td;else tdEl.value='1';}
    var batEl=document.getElementById('buttonAutoTimerToggle'); if(batEl){if(d.bat)batEl.classList.add('on');else batEl.classList.remove('on');}
    
    var rlt=document.getElementById('redLedToggle');
    if(rlt){if(d.rl)rlt.classList.add('on');else rlt.classList.remove('on');}
    
    if(d.ap !== undefined && document.activeElement !== document.getElementById('apPass')) document.getElementById('apPass').value=d.ap;
    if(d.aps !== undefined && document.activeElement !== document.getElementById('apSuffix')) document.getElementById('apSuffix').value=d.aps;
    if(d.md !== undefined){
      var mdinp=document.getElementById('mdnsHostname');
      if(mdinp && document.activeElement !== mdinp) mdinp.value=d.md;
    }
    if(d.mqs !== undefined && document.activeElement !== document.getElementById('mqServer')) document.getElementById('mqServer').value=d.mqs;
    if(d.mqp !== undefined && document.activeElement !== document.getElementById('mqPort')) document.getElementById('mqPort').value=d.mqp;
    if(d.mqu !== undefined && document.activeElement !== document.getElementById('mqUser')) document.getElementById('mqUser').value=d.mqu;
    if(d.mqpw !== undefined && document.activeElement !== document.getElementById('mqPass')) document.getElementById('mqPass').value=d.mqpw;
    if(d.sh !== undefined && document.activeElement !== document.getElementById('cStartTime')) {
      document.getElementById('cStartTime').value = (d.sh<10?'0':'')+d.sh+':'+(d.sm<10?'0':'')+d.sm;
    }
    if(d.eh !== undefined && document.activeElement !== document.getElementById('cEndTime')) {
      document.getElementById('cEndTime').value = (d.eh<10?'0':'')+d.eh+':'+(d.em<10?'0':'')+d.em;
    }

    var ct=document.getElementById('cycleToggle');
    if(ct){if(d.ce)ct.classList.add('on');else ct.classList.remove('on');}
    renderCyclePeriods(d.cp);
    var addRow=document.getElementById('cycleAddRow');
    if(addRow)addRow.style.display=(d.cpc>=3)?'none':'flex';
    var mtog=document.getElementById('mqttToggle');
    if(mtog){if(d.mqe)mtog.classList.add('on');else mtog.classList.remove('on');}

    var pt=document.getElementById('pwrOffToggle');
    if(pt){if(d.pe)pt.classList.add('on');else pt.classList.remove('on');}
    syncPowerOffHome(d.pe);
    var pth=document.getElementById('pwrThresh');
    if(pth && d.pt !== undefined && document.activeElement !== pth) pth.value=d.pt;

    // 更新计费供电状态
    var bt=document.getElementById('billingToggle');
    if(bt){if(d.be)bt.classList.add('on');else bt.classList.remove('on');}
    var bth=document.getElementById('billingThresh');
    if(bth && d.bt !== undefined && document.activeElement !== bth) bth.value=d.bt;
    var bus=document.getElementById('billingUsed');
    if(bus && d.bu !== undefined) bus.textContent=d.bu.toFixed(2)+'度';
    var bcost=document.getElementById('currentCost');
    if(bcost){
      var price=d.ep||parseFloat(document.getElementById('energyPrice')?.value)||0.6;
      if(d.ep !== undefined) document.getElementById('energyPrice').value=d.ep;
      if(d.bu !== undefined){
        bcost.textContent='¥'+(d.bu*price).toFixed(2);
      }else{
        bcost.textContent='--';
      }
    }
    var me=document.getElementById('monthEnergy');
    if(me) me.textContent=d.mo!==undefined?d.mo.toFixed(2)+'度':'--';
    var lme=document.getElementById('lastMonthEnergy');
    if(lme) lme.textContent=d.lm!==undefined?d.lm.toFixed(2)+'度':'--';

    // WiFi 检测状态
    var wdt=document.getElementById('wifiDetectToggle');
    if(wdt){if(d.de)wdt.classList.add('on');else wdt.classList.remove('on');}
    var wdl=document.getElementById('wifiDetectLinkToggle');
    if(wdl){if(d.dl)wdl.classList.add('on');else wdl.classList.remove('on');}
    var wdom=document.getElementById('wdOnlyMaster');
    if(wdom){if(d.om)wdom.classList.add('on');else wdom.classList.remove('on');}
    var bdt=document.getElementById('buttonDetectToggle');
    if(bdt){if(d.bd)bdt.classList.add('on');else bdt.classList.remove('on');}
    var wdtgt=document.getElementById('wifiDetectTarget');
    if(wdtgt){
      if(d.dt && d.dt.length>0){
        wdtgt.textContent=d.dt;
      }else{
        wdtgt.textContent='未设置';
      }
    }
    var wdmr=document.getElementById('wifiDetectMacRow');
    var wdmv=document.getElementById('wifiDetectMacVal');
    if(wdmr && wdmv){
      if(d.dm && d.dm.length>0){
        wdmr.style.display='flex';
        wdmv.textContent=d.dm;
      }else{
        wdmr.style.display='none';
      }
    }
    var wdss=document.getElementById('wifiSettingsScanSpeed');
    if(wdss && d.ds !== undefined) wdss.value=d.ds;
    var wsss=document.getElementById('wifiSettingsScanSpeed');
    if(wsss && d.ds !== undefined) wsss.value=d.ds;
    var wsrs=document.getElementById('wifiSettingsRssi');
    if(wsrs && d.dr !== undefined){
      wsrs.value=d.dr;
      document.getElementById('wifiSettingsRssiVal').textContent=d.dr+' dBm';
    }
    var wdsv=document.getElementById('wifiDetectSettingsVal');
    if(wdsv){
      var rssi=d.dr||-70;
      var speeds=['慢(30秒)','中(15秒)','快(5秒)','自动学习'];
      var speedIdx=d.ds!==undefined?d.ds:1;
      wdsv.textContent='RSSI:'+rssi+'dBm | '+speeds[speedIdx];
    }
    var wauto=document.getElementById('wifiAutoStatus');
    var wav=document.getElementById('wifiAutoStatusVal');
    if(wauto && wav && d.ds===3){
      wauto.style.display='flex';
      var curInterval=d.dci||15;
      var curHour=d.dch||0;
      var prob=d.dhp||'--';
      wav.textContent='当前:'+curHour+'时 | 间隔:'+curInterval+'秒 | 出现概率:'+prob;
    }else if(wauto){
      wauto.style.display='none';
    }
    var wds=document.getElementById('wifiDetectStatus');
    if(wds){
      if(d.de){
        if(d.tr && d.tl && d.ts===1){
          var mins=Math.floor(d.tl/60);
          var secs=d.tl%60;
          wds.textContent='信号消失 | 倒计时'+mins+'分'+secs+'秒';
          wds.style.color='#ff3b30';
        }else if(d.dp){
          wds.textContent='信号在线 (继电器已开启)';
          wds.style.color='#34c759';
        }else{
          wds.textContent='等待目标信号出现...';
          wds.style.color='#8e8e93';
        }
      }else{
        wds.textContent='已关闭';
        wds.style.color='#8e8e93';
      }
    }
    var wrt=document.getElementById('wifiDetectRange');
    if(wrt && d.dr !== undefined && document.activeElement !== wrt){
      wrt.value=d.dr;
      var wrtVal=document.getElementById('wifiDetectRangeVal');
      if(wrtVal) wrtVal.textContent=d.dr+' dBm';
    }

    // 更新定时器按钮状态
    var tm=document.getElementById('tm');
    if(tm){
      if(d.te && d.tr){
        var rem=d.tl||0;
        var rh=Math.floor(rem/3600);
        var rMin=Math.floor((rem%3600)/60);
        tm.classList.add('active');
        tm.textContent='关闭倒计时 ('+(rh>0?rh+'时':'')+rMin+'分)';
      }else if(d.te){
        tm.classList.add('active');
        tm.textContent='停止倒计时';
      }else{
        tm.classList.remove('active');
        tm.textContent='启动倒计时';
      }
    }
    }catch(e){console.log('UI update error:',e);}
    var lo=document.getElementById('loadingOverlay');
    if(lo)lo.classList.add('hide');
    scheduleUpdate();
  }).catch(function(e){console.log('Status fetch error:',e);
    var lo=document.getElementById('loadingOverlay');
    if(lo)lo.classList.add('hide');
    scheduleUpdate();
  });
}

function fmt(s){
  var d=Math.floor(s/86400);
  var h=Math.floor((s%86400)/3600);
  var m=Math.floor((s%3600)/60);
  return(d>0?d+'天 ':'')+(h>0?h+'时 ':'')+m+'分';
}
function loadCardOrder(retries){
  fetch('/api/card_order').then(function(r){return r.json()}).then(function(d){
    if(d.order && d.order.length===5){
      var changed=false;
      for(var i=0;i<5;i++){if(cardOrder[i]!==d.order[i]){changed=true;break;}}
      if(changed){cardOrder=d.order;renderCards(cardOrder);}
    }
  }).catch(function(){
    if(retries < 5){
      setTimeout(function(){loadCardOrder(retries+1);}, 1500);
    }
  });
}
var cardOrder=[0,1,2,3,4];
var cardVisibility=0x1F;
var cardNames=['继电器控制','人来上电','实时电量','用电历史','WiFi 状态'];

function renderCards(order){
  cardOrder=order||[0,1,2,3,4];
  var container=document.getElementById('cardsContainer');
  if(!container)return;
  container.innerHTML='';
  var tpl=document.getElementById('cardTemplates');
  if(!tpl)return;
  var resetEls=tpl.querySelectorAll('[id]');
  for(var r=0;r<resetEls.length;r++){
    if(resetEls[r].id.indexOf('_tpl_')===0) resetEls[r].id=resetEls[r].id.substring(5);
  }
  var cards=tpl.querySelectorAll('.card');
  for(var i=0;i<cardOrder.length;i++){
    var idx=cardOrder[i];
    if(!(cardVisibility & (1<<idx))) continue;
    if(idx>=0 && idx<cards.length){
      var clone=cards[idx].cloneNode(true);
      clone.style.animation='cardIn .3s ease '+(i*0.05)+'s both';
      container.appendChild(clone);
    }
  }
  var tplEls=tpl.querySelectorAll('[id]');
  for(var i=0;i<tplEls.length;i++) tplEls[i].id='_tpl_'+tplEls[i].id;
  var rm=document.getElementById('rm');if(rm)rm.addEventListener('click',function(){tr('m')});
  var rs=document.getElementById('rs');if(rs)rs.addEventListener('click',function(){tr('s')});
  var lk=document.getElementById('lk');if(lk)lk.addEventListener('click',toggleLock);
  var mt=document.getElementById('mt');if(mt)mt.addEventListener('click',toggleMeter);
  var wb=document.getElementById('wifiBtn');if(wb)wb.addEventListener('click',showWifi);
  setTimeout(function(){loadHistory();},100);
}

function renderSortList(order){
  var list=document.getElementById('sortList');
  if(!list)return;
  list.innerHTML='';
  var items=order||cardOrder;
  for(var i=0;i<items.length;i++){
    var div=document.createElement('div');
    div.className='sort-item';
    div.setAttribute('data-index',items[i]);
    var vis=(cardVisibility & (1<<items[i]))?' on':'';
    div.innerHTML='<span class="drag-handle">&#9776;</span><span class="sort-name">'+cardNames[items[i]]+'</span><div class="sort-toggle'+vis+'" onclick="toggleCardVis('+items[i]+',event)"></div>';
    list.appendChild(div);
  }
  initDragSort();
}

var dragItem=null,dragIdx=0;
function initDragSort(){
  var list=document.getElementById('sortList');if(!list)return;
  var items=list.querySelectorAll('.sort-item');
  for(var i=0;i<items.length;i++){
    if(items[i].dataset.dragInit==='1') continue;
    items[i].dataset.dragInit='1';
    items[i].addEventListener('touchstart',function(e){
      var h=e.target.closest('.drag-handle');if(!h)return;
      e.preventDefault();dragItem=this;dragIdx=Array.prototype.indexOf.call(this.parentNode.children,this);
      this.classList.add('dragging');
    },{passive:false});
    items[i].addEventListener('touchmove',function(e){
      if(!dragItem)return;e.preventDefault();
      var y=e.touches[0].clientY;var list=dragItem.parentNode;
      var items=Array.prototype.slice.call(list.children);
      for(var j=0;j<items.length;j++){
        var r=items[j].getBoundingClientRect(),mid=r.top+r.height/2;
        if(items[j]!==dragItem){if((j>dragIdx&&y>mid)||(j<dragIdx&&y<mid)){list.insertBefore(dragItem,j>dragIdx?items[j].nextSibling:items[j]);dragIdx=j;break;}}
      }
    },{passive:false});
    items[i].addEventListener('touchend',function(){
      if(!dragItem)return;dragItem.classList.remove('dragging');
      var list=dragItem.parentNode;var items=list.querySelectorAll('.sort-item');
      var newOrder=[];for(var k=0;k<items.length;k++)newOrder.push(parseInt(items[k].getAttribute('data-index')));
      cardOrder=newOrder;
      fetch('/api/card_order',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({order:newOrder})}).then(function(r){return r.json()}).then(function(d){if(d.ok){alert('卡片顺序已保存');}}).catch(function(e){console.log('save order error',e);});
      dragItem=null;
    });
  }
}

function toggleButtonAutoTimer(){
  var wt=document.getElementById('buttonAutoTimerToggle');
  if(!wt)return;
  var en=!wt.classList.contains('on');
  wt.classList.toggle('on');
  fetch('/api/button_auto_timer?enabled='+en);
}

function saveApSuffix(){
  var v=document.getElementById('apSuffix').value||'0';
  fetch('/api/ap_suffix',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({suffix:v})}).then(function(){
    alert('AP 后缀已保存，热点将重启生效');
  });
}

function saveMDNS(){
  var v=document.getElementById('mdnsHostname').value.trim();
  if(!v || v.length<1){alert('域名至少需要 1 个字符');return;}
  fetch('/api/mdns_hostname?name='+encodeURIComponent(v)).then(function(r){return r.json()}).then(function(d){
    if(d.ok)alert('域名已保存: '+d.name+'.local（重启设备后生效）');
    else alert('保存失败');
  }).catch(function(e){alert('保存失败');console.log('mDNS save error:',e)});
}

function toggleCardVis(idx,ev){
  if(ev) ev.stopPropagation();
  cardVisibility ^= (1<<idx);
  var list=document.getElementById('sortList');
  if(list){
    var item=list.querySelector('[data-index="'+idx+'"]');
    if(item){
      var tog=item.querySelector('.sort-toggle');
      if(tog){
        if(cardVisibility & (1<<idx)) tog.classList.add('on');
        else tog.classList.remove('on');
      }
    }
  }
  fetch('/api/card_visibility',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({v:cardVisibility})}).then(function(){
    renderCards(cardOrder);
  });
}

var _relayBusy=false;
function tr(w){
  if(_relayBusy) return;
  _relayBusy=true;
  var t=document.getElementById(w=='m'?'rm':'rs');
  var other=document.getElementById(w=='m'?'rs':'rm');
  if(t) t.classList.toggle('on');
  if(w=='s' && t && t.classList.contains('on')){
    if(other && !other.classList.contains('on')) other.classList.add('on');
  }
  if(w=='m' && t && !t.classList.contains('on')){
    if(other) other.classList.remove('on');
  }
  fetch('/api/relay?w='+w).then(function(r){return r.json()}).then(function(d){
    var rm=document.getElementById('rm');
    var rs=document.getElementById('rs');
    if(rm){if(d.m)rm.classList.add('on');else rm.classList.remove('on');}
    if(rs){if(d.s)rs.classList.add('on');else rs.classList.remove('on');}
  }).catch(function(e){console.log('Relay error:',e);})
  .finally(function(){setTimeout(function(){_relayBusy=false;},300);});
}

function toggleMeter(){
  var mt = document.getElementById('mt');
  if(!mt) return;
  var enabled = !mt.classList.contains('on');
  fetch('/api/meter?a='+(enabled?'on':'off')).then(function(r){return r.json()}).then(function(d){
    update();
  }).catch(function(e){console.log('Meter error:',e)});
}

function toggleTimer(){
  var tm = document.getElementById('tm');
  if(!tm) return;
  if(tm.classList.contains('active')){
    fetch('/api/timer?enabled=false').then(function(r){return r.json()}).then(function(){
      update();
    }).catch(function(e){console.log('Timer error:',e)});
  }else{
    var dur=document.getElementById('timerDuration').value;
    if(!dur || dur==0){alert('请先选择倒计时时长');return;}
    fetch('/api/timer?duration='+dur+'&enabled=true').then(function(r){return r.json()}).then(function(){
      update();
    }).catch(function(e){console.log('Timer error:',e)});
  }
}

function setTimerDuration(h){
  fetch('/api/timer?duration='+h).then(function(r){return r.json()}).then(function(d){
    console.log('Timer duration set to:',d.duration);
  }).catch(function(e){console.log('Timer duration error:',e)});
}

function toggleLock(){
  fetch('/api/lock').then(function(r){return r.json()}).then(function(d){
    var lk=document.getElementById('lk');
    if(d.locked)lk.classList.add('on');else lk.classList.remove('on');
  }).catch(function(e){console.log('Lock error:',e)});
}

function toggleCycle(){
  var ct=document.getElementById('cycleToggle');
  if(!ct)return;
  var en=!ct.classList.contains('on');
  if(en){
    var st=document.getElementById('cStartTime').value;
    var et=document.getElementById('cEndTime').value;
    if(!st || !et || st=='' || et==''){
      alert('请先设置循环时间段');
      return;
    }
  }
  fetch('/api/cycle?enabled='+en).then(function(r){return r.json()}).then(function(d){
    if(d.enabled)ct.classList.add('on');else ct.classList.remove('on');
  });
}
function addCyclePeriod(){
  var s=document.getElementById('cStartTime').value;
  var e=document.getElementById('cEndTime').value;
  if(!s||!e){alert('请选择时间');return;}
  var st=s.split(':'),et=e.split(':');
  fetch('/api/cycle?add=1&sh='+st[0]+'&sm='+st[1]+'&eh='+et[0]+'&em='+et[1]).then(function(r){return r.json()}).then(function(d){
    if(d.ok){update();}else{alert('最多3个时段');}
  });
}

function removeCyclePeriod(idx){
  fetch('/api/cycle?remove='+idx).then(function(r){return r.json()}).then(function(d){
    if(d.ok)update();
  });
}

function renderCyclePeriods(cp){
  var list=document.getElementById('cyclePeriodsList');
  if(!list)return;
  list.innerHTML='';
  if(!cp||cp.length===0)return;
  for(var i=0;i<cp.length;i++){
    var p=cp[i];
    var div=document.createElement('div');
    div.style.cssText='display:flex;align-items:center;gap:8px;padding:8px 0;border-bottom:1px solid #e5e5ea';
    div.innerHTML='<span style="flex:1;font-size:14px">'+String(p.sh).padStart(2,'0')+':'+String(p.sm).padStart(2,'0')+' - '+String(p.eh).padStart(2,'0')+':'+String(p.em).padStart(2,'0')+'</span>'
      +'<button class="timer-btn" style="width:auto;margin:0;padding:0 12px;height:28px;font-size:12px;background:#ff3b30" onclick="removeCyclePeriod('+i+')">删除</button>';
    list.appendChild(div);
  }
}

var _wifiDetectBusy=false;
function toggleWifiDetect(){
  if(_wifiDetectBusy)return;_wifiDetectBusy=true;
  var wt=document.getElementById('wifiDetectToggle');
  if(!wt){_wifiDetectBusy=false;return;}
  var en=!wt.classList.contains('on');
  wt.classList.toggle('on');
  fetch('/api/wifidetect?enabled='+en).then(function(r){return r.json()}).then(function(d){
    if(d.enabled){wt.classList.add('on');}else{wt.classList.remove('on');}
  }).catch(function(e){console.log('WifiDetect error:',e);})
  .finally(function(){setTimeout(function(){_wifiDetectBusy=false;},300);});
}

function toggleWifiDetectLink(){
  if(_wifiDetectBusy)return;_wifiDetectBusy=true;
  var wt=document.getElementById('wifiDetectLinkToggle');
  if(!wt){_wifiDetectBusy=false;return;}
  var en=!wt.classList.contains('on');
  wt.classList.toggle('on');
  fetch('/api/wifidetect?linkTimer='+en).then(function(r){return r.json()}).then(function(d){
    if(d.linkTimer){wt.classList.add('on');}else{wt.classList.remove('on');}
  }).catch(function(e){console.log('WifiDetectLink error:',e);})
  .finally(function(){setTimeout(function(){_wifiDetectBusy=false;},300);});
}

function toggleWifiDetectOnlyMaster(){
  if(_wifiDetectBusy)return;_wifiDetectBusy=true;
  var wt=document.getElementById('wdOnlyMaster');
  if(!wt){_wifiDetectBusy=false;return;}
  var en=!wt.classList.contains('on');
  wt.classList.toggle('on');
  fetch('/api/wifidetect?onlyMaster='+en).catch(function(e){console.log('OnlyMaster error:',e);})
  .finally(function(){setTimeout(function(){_wifiDetectBusy=false;},300);});
}

function saveWifiRange(){
  var range=document.getElementById('wifiSettingsRssi').value;
  fetch('/api/wifidetect?rssi='+range).then(function(){alert('检测距离已保存')});
}

function saveScanSpeed(){
  var speed=document.getElementById('wifiSettingsScanSpeed').value;
  fetch('/api/wifidetect?scanSpeed='+speed);
}

function showWifiDetectSettings(){
  document.getElementById('wifiSettingsM').style.display='flex';
  var wsrs=document.getElementById('wifiSettingsRssi');
  var wsss=document.getElementById('wifiSettingsScanSpeed');
  if(wsrs && wsrs.value) document.getElementById('wifiSettingsRssiVal').textContent=wsrs.value+' dBm';
}

function closeWifiDetectSettings(){
  document.getElementById('wifiSettingsM').style.display='none';
}

function showWifiDetectMore(){
  document.getElementById('wifiDetectMoreM').style.display='flex';
}

function closeWifiDetectMore(){
  document.getElementById('wifiDetectMoreM').style.display='none';
}

function toggleButtonDetect(){
  if(_wifiDetectBusy)return;_wifiDetectBusy=true;
  var bt=document.getElementById('buttonDetectToggle');
  if(!bt){_wifiDetectBusy=false;return;}
  var en=!bt.classList.contains('on');
  bt.classList.toggle('on');
  fetch('/api/button_detect?enabled='+en).catch(function(e){console.log('ButtonDetect error:',e);})
  .finally(function(){setTimeout(function(){_wifiDetectBusy=false;},300);});
}

function saveWifiDetectSettings(){
  var rssi=document.getElementById('wifiSettingsRssi').value;
  var speed=document.getElementById('wifiSettingsScanSpeed').value;
  fetch('/api/wifidetect?rssi='+rssi+'&scanSpeed='+speed).then(function(){
    closeWifiDetectSettings();
    update();
  });
}


function toggleMqtt(){
  var mt=document.getElementById('mqttToggle');
  if(!mt)return;
  var en=!mt.classList.contains('on');
  fetch('/api/mqtt?enabled='+en,{method:'POST'}).then(function(r){return r.json()}).then(function(d){
    if(d.enabled)mt.classList.add('on');else mt.classList.remove('on');
  });
}

function togglePowerOff(){
  var pt=document.getElementById('pwrOffToggle');
  if(!pt)return;
  var en=!pt.classList.contains('on');
  fetch('/api/poweroff?enabled='+en).then(function(r){return r.json()}).then(function(d){
    if(d.enabled)pt.classList.add('on');else pt.classList.remove('on');
    syncPowerOffHome(d.enabled);
  });
}

function togglePowerOffHome(){
  var pt=document.getElementById('pwrOffHome');
  if(!pt)return;
  var en=!pt.classList.contains('on');
  fetch('/api/poweroff?enabled='+en).then(function(r){return r.json()}).then(function(d){
    syncPowerOffHome(d.enabled);
  });
}

function syncPowerOffHome(enabled){
  var pt=document.getElementById('pwrOffHome');
  if(pt){if(enabled)pt.classList.add('on');else pt.classList.remove('on');}
}

function savePowerOff(){
  var th=document.getElementById('pwrThresh').value;
  fetch('/api/poweroff?threshold='+th).then(function(){alert('保存成功')});
}

function toggleBilling(){
  var bt=document.getElementById('billingToggle');
  if(!bt)return;
  var en=!bt.classList.contains('on');
  fetch('/api/billing?enabled='+en).then(function(r){return r.json()}).then(function(d){
    if(d.enabled)bt.classList.add('on');else bt.classList.remove('on');
    if(d.used !== undefined) document.getElementById('billingUsed').textContent=d.used.toFixed(2)+'度';
  });
}

function saveBilling(){
  var th=document.getElementById('billingThresh').value;
  fetch('/api/billing?threshold='+th).then(function(){alert('保存成功')});
}

function savePrice(){
  var price=document.getElementById('energyPrice').value;
  var data=new URLSearchParams();
  data.append('price',price);
  fetch('/api/billing',{method:'POST',body:data}).then(function(){alert('电价已保存')});
}

function resetEnergyHistory(){
  if(!confirm('确定要清零所有历史电量和电费记录吗？此操作不可恢复。')) return;
  fetch('/api/reset_energy',{method:'POST'}).then(function(r){return r.json()}).then(function(d){
    if(d.ok){
      alert('历史记录已清零');
      update();
    }else{
      alert('清零失败');
    }
  }).catch(function(e){alert('请求失败');});
}

function saveMqtt(){
  var data=new URLSearchParams();
  data.append('server',document.getElementById('mqServer').value);
  data.append('port',document.getElementById('mqPort').value);
  data.append('user',document.getElementById('mqUser').value);
  data.append('pass',document.getElementById('mqPass').value);
  fetch('/api/mqtt',{method:'POST',body:data}).then(function(r){return r.json()}).then(function(d){
    if(d.ok)alert('MQTT设置已保存！将尝试连接。');
  });
}

function showWifi(){
  document.getElementById('wifiM').style.display='flex';
  document.getElementById('pwd').style.display='none';
  document.getElementById('connBtn').style.display='none';
  document.getElementById('wifiSucc').classList.add('hidden');
  var selInfo=document.getElementById('wifiSelInfo');
  if(selInfo) selInfo.style.display='none';
  doScan();
}

function showWiFiSelect(){
  document.getElementById('wifiSelM').style.display='flex';
  document.getElementById('wifiSelSucc').classList.add('hidden');
  doScanSelect();
}

function doScan(){
  document.getElementById('wl').innerHTML='<p style="text-align:center;color:#8e8e93">扫描中...</p>';
  fetch('/api/scan').then(function(r){return r.json()}).then(function(d){
    var wl=document.getElementById('wl');
    wl.innerHTML='';
    if(!d || d.length===0){wl.innerHTML='<p style="text-align:center;color:#8e8e93">未找到WiFi</p>';return;}
    d.filter(function(n){return n.ssid.length>0 && !n.ssid.includes('\x00');})
     .sort(function(a,b){return b.rssi-a.rssi;})
     .forEach(function(n){
       var item=document.createElement('div');
       item.className='wifi-item';
       item.innerHTML='<div><div class="name">'+n.ssid+'</div><div class="rssi">'+n.rssi+' dBm</div></div><span class="enc">'+(n.enc?'🔒':'📶')+'</span>';
       item.addEventListener('click',function(){sel(n.ssid,n.enc)});
       wl.appendChild(item);
     });
  }).catch(function(e){document.getElementById('wl').innerHTML='<p style="text-align:center;color:#ff3b30">扫描失败</p>';console.log('Scan error:',e)});
}

function doScanSelect(){
  document.getElementById('wsl').innerHTML='<p style="text-align:center;color:#8e8e93">扫描中...</p>';
  fetch('/api/scan').then(function(r){return r.json()}).then(function(d){
    var wl=document.getElementById('wsl');
    wl.innerHTML='';
    if(!d || d.length===0){wl.innerHTML='<p style="text-align:center;color:#8e8e93">未找到WiFi</p>';return;}
    d.filter(function(n){return n.ssid.length>0 && !n.ssid.includes('\x00');})
     .sort(function(a,b){return b.rssi-a.rssi;})
     .forEach(function(n){
       var item=document.createElement('div');
       item.className='wifi-item';
       item.innerHTML='<div><div class="name">'+n.ssid+'</div><div class="rssi">'+n.rssi+' dBm</div></div><span class="enc">'+(n.enc?'🔒':'📶')+'</span>';
       item.addEventListener('click',function(){selTarget(n.ssid)});
       wl.appendChild(item);
     });
  }).catch(function(e){document.getElementById('wsl').innerHTML='<p style="text-align:center;color:#ff3b30">扫描失败</p>';console.log('Scan error:',e)});
}

function sel(ssid,enc){
  selSSID=ssid;
  var selInfo=document.getElementById('wifiSelInfo');
  var selName=document.getElementById('selWifiName');
  if(selInfo && selName){
    selName.textContent=ssid;
    selInfo.style.display='block';
  }
  if(enc){
    var pwdInput = document.getElementById('pwd');
    pwdInput.style.display='block';
    pwdInput.value='';
    document.getElementById('connBtn').style.display='block';
    pwdInput.focus();
  }else{
    conn(ssid,'');
  }
}

function selTarget(ssid){
  document.getElementById('wifiSelM').style.display='none';
  fetch('/api/wifidetect?target='+encodeURIComponent(ssid)).then(function(){alert('已选择: '+ssid)});
  var tgt=document.getElementById('wifiDetectTarget');
  if(tgt)tgt.textContent=ssid;
}

function conn(ssid,pwd){
  if(!ssid){alert('请选择WiFi');return;}
  var body='ssid='+encodeURIComponent(ssid)+'&password='+encodeURIComponent(pwd||'');
  document.getElementById('wl').innerHTML='<p style="text-align:center;color:#007aff">正在连接...</p>';
  document.getElementById('connBtn').style.display='none';
  fetch('/api/connect',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:body})
  .then(function(r){return r.json()}).then(function(d){
    if(d.ok && d.status==='connecting'){
      pollConnectStatus();
    }else if(d.ok && d.status==='connected'){
      document.getElementById('wifiSucc').classList.remove('hidden');
      document.getElementById('connSSID').textContent=d.ssid;
      document.getElementById('wl').innerHTML='<p style="text-align:center;color:#34c759">WiFi 连接成功！IP: '+d.ip+'</p>';
      document.getElementById('pwd').style.display='none';
      setTimeout(function(){closeWifi();update();},2000);
    }else{
      document.getElementById('wl').innerHTML='<p style="text-align:center;color:#ff3b30">'+(d.error||'连接失败')+'</p>';
      document.getElementById('connBtn').style.display='block';
    }
  }).catch(function(e){
    document.getElementById('wl').innerHTML='<p style="text-align:center;color:#ff3b30">连接请求失败</p>';
    document.getElementById('connBtn').style.display='block';
    console.log('Connect error:',e);
  });
}

function pollConnectStatus(){
  fetch('/api/connect_status').then(function(r){return r.json()}).then(function(d){
    if(d.status==='connected'){
      document.getElementById('wifiSucc').classList.remove('hidden');
      document.getElementById('connSSID').textContent=d.ssid;
      document.getElementById('wl').innerHTML='<p style="text-align:center;color:#34c759">WiFi 连接成功！IP: '+d.ip+'</p>';
      document.getElementById('pwd').style.display='none';
      setTimeout(function(){closeWifi();update();},2000);
    }else if(d.status==='failed'){
      document.getElementById('wl').innerHTML='<p style="text-align:center;color:#ff3b30">'+d.error+'</p>';
      document.getElementById('connBtn').style.display='block';
    }else if(d.status==='connecting'){
      setTimeout(pollConnectStatus, 1000);
    }else{
      document.getElementById('wl').innerHTML='<p style="text-align:center;color:#ff3b30">连接超时</p>';
      document.getElementById('connBtn').style.display='block';
    }
  }).catch(function(){
    setTimeout(pollConnectStatus, 1000);
  });
}

function closeWifi(){
  document.getElementById('wifiM').style.display='none';
  document.getElementById('pwd').style.display='none';
  document.getElementById('connBtn').style.display='none';
  document.getElementById('wifiSucc').classList.add('hidden');
}

function closeWifiSelect(){
  document.getElementById('wifiSelM').style.display='none';
  document.getElementById('wifiSelSucc').classList.add('hidden');
}

function showOta(){
  document.getElementById('otaM').style.display='flex';
  document.getElementById('ofm').classList.remove('hidden');
  document.getElementById('opg').classList.add('hidden');
}

function doOta(){
  var f=document.getElementById('of').files[0];
  if(!f){alert('请选择固件文件');return;}
  var p=document.getElementById('op').value;
  if(p!=='admin'){alert('密码错误');return;}

  // 核心改进：升级前先关闭电能检测，释放资源
  document.getElementById('ost').textContent='准备升级 (正在关闭计量逻辑)...';
  fetch('/api/meter?a=off').then(function(){
    startActualOta(f);
  }).catch(function(){
    startActualOta(f); // 即使失败也尝试升级
  });
}

function startActualOta(f){
  document.getElementById('ofm').classList.add('hidden');
  document.getElementById('opg').classList.remove('hidden');

  var fd=new FormData();
  fd.append('update',f);  // 使用标准 /update 接口

  var xhr=new XMLHttpRequest();
  xhr.open('POST','/update');
  xhr.setRequestHeader('Authorization', 'Basic ' + btoa('admin:admin'));
  xhr.upload.addEventListener('progress',function(e){
    if(e.lengthComputable){
      var pct=Math.round((e.loaded/e.total)*100);
      document.getElementById('opf').style.width=pct+'%';
      document.getElementById('ost').textContent='正在上传... '+pct+'%';
    }
  });

  xhr.addEventListener('load',function(){
    if(xhr.status === 200 || xhr.status === 302 || xhr.status === 304){
      document.getElementById('ost').textContent='升级成功！设备正在重启...';
      // 5秒后刷新页面
      setTimeout(function(){window.location.reload();},5000);
    } else if(xhr.status === 401){
      document.getElementById('ost').textContent='权限错误 (401)，正在重试...';
      setTimeout(function(){
        // 如果是 401，尝试带上默认的 admin:admin Auth 再次请求一次
        var xhr2 = new XMLHttpRequest();
        xhr2.open('POST', '/update');
        xhr2.setRequestHeader('Authorization', 'Basic ' + btoa('admin:admin'));
        // ... 此处简化处理 ...
        window.location.href = '/update'; // 或者直接跳转到标准更新页
      }, 1000);
    } else {
      document.getElementById('ost').textContent='状态异常: '+xhr.status+' '+xhr.responseText;
      setTimeout(closeOta,5000);
    }
  });

  xhr.addEventListener('error',function(){
    document.getElementById('ost').textContent='通信中断：请检查网络或固件大小 (瞬间100%通常是连接被拒绝)';
    setTimeout(closeOta,5000);
  });

  xhr.send(fd);
}

function closeOta(){
  document.getElementById('otaM').style.display='none';
}

function doReset(){
  if(confirm('确定恢复出厂设置?')){
    fetch('/api/reset',{method:'POST'}).then(function(){alert('设备将重启')}).catch(function(e){console.log('Reset error:',e)});
  }
}

function toggleRedLed(){
  fetch('/api/settings?redLed=toggle').then(function(r){return r.json()}).then(function(d){
    var rlt=document.getElementById('redLedToggle');
    if(d.redLed)rlt.classList.add('on');else rlt.classList.remove('on');
  }).catch(function(e){console.log('Red LED toggle error:',e)});
}

function saveApPass(){
  var p=document.getElementById('apPass').value;
  if(p && p.length>0 && p.length<8){alert('密码长度至少8位');return;}
  fetch('/api/ap_pass?pass='+encodeURIComponent(p), {method:'POST'}).then(function(r){return r.json()}).then(function(d){
    if(d.ok)alert('已保存，请手动重启设备以生效');
  }).catch(function(e){alert('保存失败');console.log('AP pass error:',e)});
}

function confirmRestart(){
  if(confirm('确定要重启设备吗？')){
    fetch('/api/restart', {method:'POST'});
    document.body.innerHTML='<div style="padding:40px;text-align:center;font-size:18px">设备正在重启...</div>';
  }
}

function confirmReset(){
  if(confirm('确定要恢复出厂设置吗？所有配置将被清除！')){
    fetch('/api/reset', {method:'POST'});
    document.body.innerHTML='<div style="padding:40px;text-align:center;font-size:18px">正在恢复出厂设置...</div>';
  }
}

function loadHistory(){
  fetch('/api/history').then(function(r){return r.json()}).then(function(d){
    if(!d.data) return;
    var vals=d.data;
    var max=0;
    for(var i=0;i<vals.length;i++){if(vals[i]>max)max=vals[i];}
    if(max<1)max=1;
    var total=0;
    var now=new Date();
    for(var i=0;i<7;i++){
      var label=document.getElementById('hl'+i);
      var bar=document.getElementById('hb'+i);
      var valEl=document.getElementById('hv'+i);
      if(!bar) continue;
      total+=vals[i];
      var pct=Math.round((vals[i]/max)*88);
      bar.style.height=(pct<2?2:pct)+'px';
      var kwh=vals[i]/1000;
      if(valEl) valEl.textContent=kwh>0?kwh.toFixed(2):'';
      if(label && i<6){
        var d2=new Date(now);
        d2.setDate(now.getDate()-(6-i));
        label.textContent=(d2.getMonth()+1)+'/'+(d2.getDate());
      }
    }
    var ht=document.getElementById('histTotal');
    if(ht) ht.textContent='合计: '+(total/1000).toFixed(2)+' kWh';
    scheduleHistory();
  }).catch(function(e){console.log('History error:',e);scheduleHistory();});
}

document.addEventListener('DOMContentLoaded',function(){
  init();
  update();
  setTimeout(function(){updateChipTime();},3000);
  setTimeout(function(){
    var lo=document.getElementById('loadingOverlay');
    if(lo && !lo.classList.contains('hide')) lo.classList.add('hide');
  },3000);
});
var _updateTimer=null;
function scheduleUpdate(){
  if(_updateTimer)clearTimeout(_updateTimer);
  _updateTimer=setTimeout(function(){update()},10000);
}
var _histTimer=null;
function scheduleHistory(){
  if(_histTimer)clearTimeout(_histTimer);
  _histTimer=setTimeout(function(){loadHistory()},60000);
}
var _timeTimer=null;
function scheduleChipTime(){
  if(_timeTimer)clearTimeout(_timeTimer);
  _timeTimer=setTimeout(function(){updateChipTime()},30000);
}

function updateChipTime(){
  fetch('/api/now').then(function(r){return r.json()}).then(function(d){
    var el=document.getElementById('chipTime');
    if(el) el.textContent=d.str||'--:--';
    scheduleChipTime();
  }).catch(function(){scheduleChipTime();});
}
</script>
</body>
</html>)rawliteral";

void WebConfigServer::init() {
    server_ = std::make_unique<ESP8266WebServer>(WEB_SERVER_PORT);
    httpUpdater_ = std::make_unique<ESP8266HTTPUpdateServer>();
    dnsServer_ = std::make_unique<DNSServer>();

    httpUpdater_->setup(server_.get(), "/update", "admin", "admin");

    // 启动 DNS 服务器，实现 Captive Portal (强制门户) 拦截所有域名解析请求重定向到 ESP
    dnsServer_->start(53, "*", WiFi.softAPIP());

    server_->on("/", HTTP_GET, []() {
        server_->sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
        server_->sendHeader("Pragma", "no-cache");
        server_->sendHeader("Expires", "-1");
        server_->setContentLength(sizeof(INDEX_HTML) - 1);
        server_->send(200, "text/html; charset=UTF-8", "");
        server_->sendContent_P(INDEX_HTML, sizeof(INDEX_HTML) - 1);
    });

    server_->on("/api/status", HTTP_GET, []() {
        char buf[1600];
        String ip = (WiFi.status() == WL_CONNECTED) ? WiFi.localIP().toString() : WiFi.softAPIP().toString();
        uint8_t sh, sm, eh, em;
        GPIOManager::getCycleTime(sh, sm, eh, em);
        MQTTConfig mq = MQTTManager::getConfig();
        String apPass = WiFiManager::getAPPassword();

        snprintf(buf, sizeof(buf),
            "{\"conn\":%s,\"ip\":\"%s\",\"ssid\":\"%s\",\"rssi\":%d,"
            "\"ver\":\"%s\",\"up\":%lu,\"m\":%s,\"s\":%s,"
            "\"lk\":%s,\"te\":%s,\"tr\":%s,\"td\":%d,\"tl\":%lu,"
            "\"v\":%.1f,\"i\":%.3f,\"p\":%.2f,\"e\":%.2f,\"me\":%s,\"rl\":%s,"
            "\"ce\":%s,\"sh\":%d,\"sm\":%d,\"eh\":%d,\"em\":%d,\"cpc\":%d,"
            "\"pe\":%s,\"pt\":%.2f,"
            "\"be\":%s,\"bt\":%.2f,\"bu\":%.2f,\"ep\":%.2f,\"mo\":%.2f,\"lm\":%.2f,"
            "\"de\":%s,\"dt\":\"%s\",\"dl\":%s,\"dr\":%d,\"dm\":\"%s\",\"dp\":%s,\"pc\":%s,\"ds\":%d,\"ts\":%d,\"om\":%s,\"bd\":%s,"
            "\"mqe\":%s,\"mqs\":\"%s\",\"mqp\":%d,\"mqu\":\"%s\",\"mqpw\":\"%s\","
            "\"ap\":\"%s\",\"aps\":\"%s\",\"cv\":%d,\"bat\":%s,\"md\":\"%s\"}",
            WiFiManager::isConnected() ? "true" : "false",
            ip.c_str(),
            WiFiManager::getCurrentSSID().c_str(),
            WiFiManager::getCurrentRSSI(),
            VERSION,
            millis() / 1000,
            GPIOManager::getRelayMaster() ? "true" : "false",
            GPIOManager::getRelaySlave() ? "true" : "false",
            GPIOManager::isLocked() ? "true" : "false",
            GPIOManager::isTimerEnabled() ? "true" : "false",
            GPIOManager::isTimerRunning() ? "true" : "false",
            GPIOManager::getTimerDuration(),
            GPIOManager::getTimerRemaining(),
            SY7T609::getVoltage(),
            SY7T609::getCurrent(),
            SY7T609::getPower(),
            EnergyManager::getTotalEnergy(),
            SY7T609::isEnabled() ? "true" : "false",
            GPIOManager::isRedLedEnabled() ? "true" : "false",
            GPIOManager::isCycleEnabled() ? "true" : "false",
            sh, sm, eh, em, GPIOManager::getCyclePeriodCount(),
            GPIOManager::isPowerOffEnabled() ? "true" : "false",
            GPIOManager::getPowerOffThreshold(),
            GPIOManager::isBillingEnabled() ? "true" : "false",
            GPIOManager::getBillingThreshold(),
            (EnergyManager::getTotalEnergy() - GPIOManager::getBillingStartEnergy()) / 1000.0f,
            GPIOManager::getEnergyPrice(),
            EnergyManager::getMonthlyEnergy() / 1000.0f,
            EnergyManager::getLastMonthEnergy() / 1000.0f,
            GPIOManager::isWiFiDetectEnabled() ? "true" : "false",
            GPIOManager::getWiFiDetectTarget().c_str(),
            GPIOManager::isWiFiDetectLinkTimer() ? "true" : "false",
            GPIOManager::getWiFiDetectRssiThreshold(),
            GPIOManager::getWiFiDetectMac().c_str(),
            GPIOManager::isWiFiDetectPresent() ? "true" : "false",
            GPIOManager::isWiFiDetectPowerChecking() ? "true" : "false",
            GPIOManager::getWiFiDetectScanSpeed(),
            GPIOManager::getTimerSource(),
            GPIOManager::getWiFiDetectRelayOnlyMaster() ? "true" : "false",
            GPIOManager::isButtonDetectEnabled() ? "true" : "false",
            mq.enabled ? "true" : "false",
            mq.server,
            mq.port,
            mq.username,
            mq.password,
            apPass.c_str(),
            WiFiManager::getAPSuffix().c_str(),
            EEPROM.read(EEPROM_CARD_VISIBILITY_ADDR),
            GPIOManager::isButtonAutoTimerEnabled() ? "true" : "false",
            GPIOManager::getMDNSHostname().c_str()
        );
        int len = strlen(buf);
        uint8_t cpCount = GPIOManager::getCyclePeriodCount();
        if (len > 0 && buf[len-1] == '}') {
            buf[--len] = '\0';
            len += snprintf(buf + len, sizeof(buf) - len, ",\"cp\":[");
            for (uint8_t i = 0; i < cpCount && len < (int)sizeof(buf) - 40; i++) {
                uint8_t psh,psm,peh,pem;
                if (GPIOManager::getCyclePeriod(i, psh, psm, peh, pem)) {
                    if (i > 0) buf[len++] = ',';
                    len += snprintf(buf + len, sizeof(buf) - len,
                        "{\"sh\":%d,\"sm\":%d,\"eh\":%d,\"em\":%d}", psh, psm, peh, pem);
                }
            }
            len += snprintf(buf + len, sizeof(buf) - len, "],\"card_order\":[");
            for (uint8_t i = 0; i < 5; i++) {
                if (i > 0) buf[len++] = ',';
                uint8_t v = EEPROM.read(504 + i);
                if (v > 4) v = i;
                len += snprintf(buf + len, sizeof(buf) - len, "%d", v);
            }
            len += snprintf(buf + len, sizeof(buf) - len, "]}");
        }
        server_->send(200, "application/json", buf);
    });


    // Debug API for SY7T609
    server_->on("/api/debug", HTTP_GET, []() {
        String json = "{\"log\":";
        json += "\"" + SY7T609::getDebugLog() + "\"";
        json += ",\"meterEnabled\":" + String(SY7T609::isEnabled() ? "true" : "false");
        json += ",\"meterReady\":" + String(SY7T609::isReady() ? "true" : "false");
        json += ",\"flashMode\":" + String(SY7T609::isFlashMode() ? "true" : "false");
        json += "}";
        server_->send(200, "application/json", json);
    });

    server_->on("/api/cleardebug", HTTP_POST, []() {
        SY7T609::clearDebugLog();
        server_->send(200, "application/json", "{\"ok\":true}");
    });

    server_->on("/api/relay", HTTP_GET, []() {
        String w = server_->arg("w");
        if (w == "m") {
            GPIOManager::setRelayMaster(!GPIOManager::getRelayMaster());
        } else if (w == "s") {
            GPIOManager::setRelaySlave(!GPIOManager::getRelaySlave());
        }
        String response = "{\"m\":" + String(GPIOManager::getRelayMaster() ? "true" : "false") +
                          ",\"s\":" + String(GPIOManager::getRelaySlave() ? "true" : "false") + "}";
        server_->send(200, "application/json", response);
    });

    server_->on("/api/cycle", HTTP_GET, []() {
        if (server_->hasArg("enabled")) {
            bool on = (server_->arg("enabled") == "true");
            GPIOManager::setCycleEnabled(on);
            server_->send(200, "application/json", "{\"enabled\":" + String(on ? "true" : "false") + "}");
            return;
        }
        if (server_->hasArg("add")) {
            uint8_t sh = server_->arg("sh").toInt();
            uint8_t sm = server_->arg("sm").toInt();
            uint8_t eh = server_->arg("eh").toInt();
            uint8_t em = server_->arg("em").toInt();
            bool ok = GPIOManager::addCyclePeriod(sh, sm, eh, em);
            server_->send(200, "application/json", ok ? "{\"ok\":true}" : "{\"ok\":false,\"error\":\"max periods\"}");
            return;
        }
        if (server_->hasArg("remove")) {
            uint8_t idx = server_->arg("remove").toInt();
            bool ok = GPIOManager::removeCyclePeriod(idx);
            server_->send(200, "application/json", ok ? "{\"ok\":true}" : "{\"ok\":false,\"error\":\"invalid index\"}");
            return;
        }
        if (server_->hasArg("sh") && server_->hasArg("eh")) {
            uint8_t sh = server_->arg("sh").toInt();
            uint8_t sm = server_->arg("sm").toInt();
            uint8_t eh = server_->arg("eh").toInt();
            uint8_t em = server_->arg("em").toInt();
            uint8_t idx = server_->hasArg("idx") ? server_->arg("idx").toInt() : 0;
            GPIOManager::setCyclePeriod(idx, sh, sm, eh, em);
            server_->send(200, "application/json", "{\"ok\":true}");
            return;
        }
        server_->send(400, "application/json", "{\"error\":\"bad request\"}");
    });

    server_->on("/api/mqtt", HTTP_POST, []() {
        MQTTConfig conf = MQTTManager::getConfig();
        if (server_->hasArg("enabled")) {
            conf.enabled = (server_->arg("enabled") == "true");
            MQTTManager::saveConfig(conf);
            server_->send(200, "application/json", "{\"enabled\":" + String(conf.enabled ? "true" : "false") + "}");
            return;
        }
        if (server_->hasArg("server")) {
            strncpy(conf.server, server_->arg("server").c_str(), sizeof(conf.server)-1);
            conf.port = (uint16_t)server_->arg("port").toInt();
            strncpy(conf.username, server_->arg("user").c_str(), sizeof(conf.username)-1);
            strncpy(conf.password, server_->arg("pass").c_str(), sizeof(conf.password)-1);
            MQTTManager::saveConfig(conf);
            server_->send(200, "application/json", "{\"ok\":true}");
            return;
        }
        server_->send(400, "application/json", "{\"error\":\"bad request\"}");
    });


    server_->on("/api/lock", HTTP_GET, []() {
        bool current = GPIOManager::isLocked();
        GPIOManager::setLocked(!current);
        String response = "{\"locked\":" + String(GPIOManager::isLocked() ? "true" : "false") + "}";
        server_->send(200, "application/json", response);
    });

    // 定时器 API
    server_->on("/api/timer", HTTP_GET, []() {
        String enabled = server_->arg("enabled");
        String duration = server_->arg("duration");

        if (duration.length() > 0) {
            uint8_t hrs = duration.toInt();
            GPIOManager::setTimerDuration(hrs);
        }

        if (enabled.length() > 0) {
            bool en = (enabled == "true");
            GPIOManager::setTimerEnabled(en);
            if (en) {
                GPIOManager::startTimer();
            } else {
                GPIOManager::stopTimer();
            }
            String response = "{\"enabled\":" + String(en ? "true" : "false") + ",\"duration\":" + String(GPIOManager::getTimerDuration()) + "}";
            server_->send(200, "application/json", response);
        } else if (duration.length() > 0) {
            String response = "{\"duration\":" + String(GPIOManager::getTimerDuration()) + "}";
            server_->send(200, "application/json", response);
        } else {
            server_->send(400, "application/json", "{\"error\":\"Invalid parameters\"}");
        }
    });

    server_->on("/api/card_order", HTTP_GET, []() {
        uint8_t order[5] = {0,1,2,3,4};
        for(int i=0;i<5;i++) order[i] = EEPROM.read(504+i);
        bool dup = false;
        for(int i=0;i<5 && !dup;i++) for(int j=i+1;j<5;j++) if(order[i]==order[j]) dup=true;
        if(order[0]>4||order[1]>4||order[2]>4||order[3]>4||order[4]>4||dup){
            for(int i=0;i<5;i++) order[i]=i;
        }
        String json = "{\"order\":[";
        for(int i=0;i<5;i++){if(i>0)json+=',';json+=String(order[i]);}
        json += "]}";
        server_->send(200, "application/json", json);
    });

    server_->on("/api/card_order", HTTP_POST, []() {
        String body = server_->arg("plain");
        int s=body.indexOf('['), e=body.indexOf(']');
        if(s>=0 && e>s){
            String arr = body.substring(s+1, e);
            for(int idx=0; idx<5; idx++){
                int c=arr.indexOf(',');
                String num = (c>=0)?arr.substring(0,c):arr;
                int v = num.toInt();
                if(v<0||v>4) v=idx;
                EEPROM.write(504+idx, (uint8_t)v);
                if(c<0) break;
                arr = arr.substring(c+1);
            }
            EEPROM.write(509, 0xCD);
            EEPROM.commit();
        }
        server_->send(200, "application/json", "{\"ok\":true}");
    });

    server_->on("/api/card_visibility", HTTP_POST, []() {
        String body = server_->arg("plain");
        int s = body.indexOf(':'), e = body.indexOf('}');
        if (s >= 0 && e > s) {
            String val = body.substring(s + 1, e);
            val.trim();
            uint8_t v = (uint8_t)val.toInt();
            EEPROM.write(EEPROM_CARD_VISIBILITY_ADDR, v);
            EEPROM.commit();
        }
        server_->send(200, "application/json", "{\"ok\":true}");
    });

    server_->on("/api/button_auto_timer", HTTP_GET, []() {
        String enabled = server_->arg("enabled");
        GPIOManager::setButtonAutoTimerEnabled(enabled == "true");
        server_->send(200, "application/json", "{\"ok\":true}");
    });

    server_->on("/api/ap_suffix", HTTP_GET, []() {
        uint8_t v = EEPROM.read(EEPROM_AP_SUFFIX_ADDR);
        server_->send(200, "application/json", "{\"suffix\":" + String(v) + "}");
    });

    server_->on("/api/ap_suffix", HTTP_POST, []() {
        String body = server_->arg("plain");
        int s=body.indexOf(':'), e=body.indexOf('}');
        if(s>=0 && e>s){
            String val = body.substring(s+1, e);
            val.trim();
            int v = val.toInt();
            if (v < 0) v = 0;
            if (v > 255) v = 255;
            EEPROM.write(EEPROM_AP_SUFFIX_ADDR, v);
            EEPROM.commit();
            WiFiManager::setAPSuffix(String(v).c_str());
        }
        server_->send(200, "application/json", "{\"ok\":true}");
    });

    static bool connect_pending_ = false;
    static String connect_ssid_;
    static bool connect_was_wifi_detect_ = false;
    static unsigned long connect_start_ = 0;

    server_->on("/api/connect", HTTP_POST, []() {
        String ssid = server_->arg("ssid");
        String password = server_->arg("password");

        if (ssid.length() == 0) {
            server_->send(400, "application/json", "{\"ok\":false,\"error\":\"SSID不能为空\"}");
            return;
        }

        connect_pending_ = true;
        connect_ssid_ = ssid;
        connect_was_wifi_detect_ = GPIOManager::isWiFiDetectEnabled();
        connect_start_ = millis();

        if (connect_was_wifi_detect_) {
            GPIOManager::setWiFiDetectEnabled(false);
        }

        WiFiManager::saveConfig(ssid.c_str(), password.c_str());
        server_->send(200, "application/json", "{\"ok\":true,\"status\":\"connecting\"}");
    });

    server_->on("/api/connect_status", HTTP_GET, []() {
        if (!connect_pending_) {
            server_->send(200, "application/json", "{\"status\":\"idle\"}");
            return;
        }

        if (WiFi.status() == WL_CONNECTED) {
            connect_pending_ = false;
            if (connect_was_wifi_detect_) {
                GPIOManager::setWiFiDetectEnabled(true);
            }
            server_->send(200, "application/json", "{\"ok\":true,\"status\":\"connected\",\"ssid\":\"" + connect_ssid_ + "\",\"ip\":\"" + WiFi.localIP().toString() + "\"}");
        } else if (millis() - connect_start_ > 15000) {
            connect_pending_ = false;
            if (connect_was_wifi_detect_) {
                GPIOManager::setWiFiDetectEnabled(true);
            }
            WiFi.disconnect();
            server_->send(200, "application/json", "{\"ok\":false,\"status\":\"failed\",\"error\":\"连接失败，请检查密码\"}");
        } else {
            server_->send(200, "application/json", "{\"status\":\"connecting\",\"elapsed\":" + String(millis() - connect_start_) + "}");
        }
    });

    server_->on("/api/settings", HTTP_GET, []() {
        String redLed = server_->arg("redLed");
        if (redLed == "toggle") {
            GPIOManager::setRedLedEnabled(!GPIOManager::isRedLedEnabled());
        }
        String response = "{\"redLed\":" + String(GPIOManager::isRedLedEnabled() ? "true" : "false") + "}";
        server_->send(200, "application/json", response);
    });

    server_->on("/api/ap_pass", HTTP_POST, []() {
        String pass = server_->arg("pass");
        WiFiManager::saveAPPassword(pass.c_str());
        server_->send(200, "application/json", "{\"ok\":true}");
    });

    server_->on("/api/mdns_hostname", HTTP_GET, []() {
        if (server_->hasArg("name")) {
            String name = server_->arg("name");
            name.trim();
            if (name.length() >= 1 && name.length() <= 31) {
                GPIOManager::setMDNSHostname(name);
                char buf[96];
                snprintf(buf, sizeof(buf), "{\"ok\":true,\"name\":\"%s\"}", name.c_str());
                server_->send(200, "application/json", buf);
                return;
            }
        }
        String hn = GPIOManager::getMDNSHostname();
        char buf[64];
        snprintf(buf, sizeof(buf), "{\"name\":\"%s\"}", hn.c_str());
        server_->send(200, "application/json", buf);
    });

    server_->on("/api/restart", HTTP_POST, []() {
        server_->send(200, "application/json", "{\"ok\":true}");
        delay(100);
        ESP.restart();
    });

    server_->on("/api/reset", HTTP_POST, []() {
        for (int i = 0; i < 512; i++) {
            EEPROM.write(i, 0);
        }
        EEPROM.commit();
        server_->send(200, "application/json", "{\"ok\":true}");
        delay(100);
        ESP.restart();
    });

    server_->on("/api/meter", HTTP_GET, []() {
        String action = server_->arg("a");
        if (action == "on") {
            SY7T609::setEnabled(true);
        } else if (action == "off") {
            SY7T609::setEnabled(false);
        }
        String response = "{\"enabled\":" + String(SY7T609::isEnabled() ? "true" : "false") + "}";
        server_->send(200, "application/json", response);
    });

    server_->on("/api/history", HTTP_GET, []() {
        String json = "{\"data\":[";
        for (int i = 0; i < 7; i++) {
            if (i > 0) json += ",";
            json += String(EnergyManager::getHistory(i), 2);
        }
        json += "]}";
        server_->send(200, "application/json", json);
    });

    server_->on("/api/poweroff", HTTP_GET, []() {
        if (server_->hasArg("enabled")) {
            bool en = (server_->arg("enabled") == "true");
            GPIOManager::setPowerOffEnabled(en);
        }
        if (server_->hasArg("threshold")) {
            float th = server_->arg("threshold").toFloat();
            GPIOManager::setPowerOffThreshold(th);
        }
        char buf[128];
        snprintf(buf, sizeof(buf), "{\"enabled\":%s,\"threshold\":%.2f}",
            GPIOManager::isPowerOffEnabled() ? "true" : "false",
            GPIOManager::getPowerOffThreshold());
        server_->send(200, "application/json", buf);
    });

    server_->on("/api/billing", HTTP_GET, []() {
        if (server_->hasArg("enabled")) {
            bool en = (server_->arg("enabled") == "true");
            GPIOManager::setBillingEnabled(en);
        }
        if (server_->hasArg("threshold")) {
            float th = server_->arg("threshold").toFloat();
            GPIOManager::setBillingThreshold(th);
        }
        char buf[128];
        snprintf(buf, sizeof(buf), "{\"enabled\":%s,\"threshold\":%.2f,\"used\":%.2f,\"startEnergy\":%.2f}",
            GPIOManager::isBillingEnabled() ? "true" : "false",
            GPIOManager::getBillingThreshold(),
            (EnergyManager::getTotalEnergy() - GPIOManager::getBillingStartEnergy()) / 1000.0f,
            GPIOManager::getBillingStartEnergy() / 1000.0f);
        server_->send(200, "application/json", buf);
    });

    server_->on("/api/billing", HTTP_POST, []() {
        float price = server_->arg("price").toFloat();
        if (price > 0 && price < 10) {
            GPIOManager::setEnergyPrice(price);
        }
        server_->send(200, "application/json", "{\"ok\":true}");
    });

    server_->on("/api/time", HTTP_GET, []() {
        if (server_->hasArg("ts")) {
            time_t ts = server_->arg("ts").toInt();
            struct timeval tv = { .tv_sec = ts, .tv_usec = 0 };
            settimeofday(&tv, NULL);
            configTime(8 * 3600, 0, "pool.ntp.org", "time.nist.gov");
        }
        server_->send(200, "text/plain", "OK");
    });

    server_->on("/api/now", HTTP_GET, []() {
        time_t now = time(nullptr);
        struct tm* timeinfo = localtime(&now);
        char buf[64];
        if (timeinfo && timeinfo->tm_year >= 100) {
            snprintf(buf, sizeof(buf), "{\"ok\":true,\"hour\":%d,\"min\":%02d,\"str\":\"%02d:%02d\"}",
                timeinfo->tm_hour, timeinfo->tm_min,
                timeinfo->tm_hour, timeinfo->tm_min);
        } else {
            snprintf(buf, sizeof(buf), "{\"ok\":false,\"str\":\"--:--\"}");
        }
        server_->send(200, "application/json", buf);
    });

    server_->on("/api/wifidetect", HTTP_GET, []() {
        if (server_->hasArg("enabled")) {
            GPIOManager::setWiFiDetectEnabled(server_->arg("enabled") == "true");
        }
        if (server_->hasArg("target")) {
            GPIOManager::setWiFiDetectTarget(server_->arg("target").c_str());
        }
        if (server_->hasArg("timeout")) {
            GPIOManager::setWiFiDetectTimeout(server_->arg("timeout").toInt());
        }
        if (server_->hasArg("linkTimer")) {
            GPIOManager::setWiFiDetectLinkTimer(server_->arg("linkTimer") == "true");
        }
        if (server_->hasArg("rssi")) {
            GPIOManager::setWiFiDetectRssiThreshold(server_->arg("rssi").toInt());
        }
        if (server_->hasArg("scanSpeed")) {
            GPIOManager::setWiFiDetectScanSpeed((uint8_t)server_->arg("scanSpeed").toInt());
        }
        if (server_->hasArg("onlyMaster")) {
            GPIOManager::setWiFiDetectRelayOnlyMaster(server_->arg("onlyMaster") == "true");
        }
        char buf[256];
        snprintf(buf, sizeof(buf), "{\"enabled\":%s,\"target\":\"%s\",\"timeout\":%d,\"linkTimer\":%s,\"rssi\":%d,\"scanSpeed\":%d,\"mac\":\"%s\",\"present\":%s,\"powerCheck\":%s,\"onlyMaster\":%s}",
            GPIOManager::isWiFiDetectEnabled() ? "true" : "false",
            GPIOManager::getWiFiDetectTarget().c_str(),
            GPIOManager::getWiFiDetectTimeout(),
            GPIOManager::isWiFiDetectLinkTimer() ? "true" : "false",
            GPIOManager::getWiFiDetectRssiThreshold(),
            GPIOManager::getWiFiDetectScanSpeed(),
            GPIOManager::getWiFiDetectMac().c_str(),
            GPIOManager::isWiFiDetectPresent() ? "true" : "false",
            GPIOManager::isWiFiDetectPowerChecking() ? "true" : "false",
            GPIOManager::getWiFiDetectRelayOnlyMaster() ? "true" : "false");
        server_->send(200, "application/json", buf);
    });

    server_->on("/api/button_detect", HTTP_GET, []() {
        if (server_->hasArg("enabled")) {
            GPIOManager::setButtonDetectEnabled(server_->arg("enabled") == "true");
        }
        char buf[64];
        snprintf(buf, sizeof(buf), "{\"enabled\":%s}",
            GPIOManager::isButtonDetectEnabled() ? "true" : "false");
        server_->send(200, "application/json", buf);
    });

    // 扫描 WiFi 网络
    server_->on("/api/scan", HTTP_GET, []() {
        int n = WiFi.scanNetworks(false);
        struct WiFiAP { String ssid; int rssi; bool enc; };
        WiFiAP aps[32];
        int apCount = 0;
        for (int i = 0; i < n && apCount < 32; i++) {
            String ssid = WiFi.SSID(i);
            int rssi = WiFi.RSSI(i);
            bool enc = (WiFi.encryptionType(i) != ENC_TYPE_NONE);
            bool found = false;
            for (int j = 0; j < apCount; j++) {
                if (aps[j].ssid == ssid) {
                    found = true;
                    if (rssi > aps[j].rssi) {
                        aps[j].rssi = rssi;
                        aps[j].enc = enc;
                    }
                    break;
                }
            }
            if (!found) {
                aps[apCount++] = {ssid, rssi, enc};
            }
        }
        String json = "[";
        for (int i = 0; i < apCount; i++) {
            if (i > 0) json += ",";
            json += "{\"ssid\":\"" + aps[i].ssid + "\",\"rssi\":" + aps[i].rssi + ",\"enc\":" + (aps[i].enc ? "true" : "false") + "}";
        }
        json += "]";
        WiFi.scanDelete();
        server_->send(200, "application/json", json);
    });

    server_->on("/api/reset_energy", HTTP_POST, []() {
        EnergyManager::reset();
        GPIOManager::setBillingStartEnergy(0);
        server_->send(200, "application/json", "{\"ok\":true}");
    });

    server_->onNotFound([]() {
        // 重定向未找到的页面到主页 (用于 Captive Portal)
        server_->sendHeader("Location", String("http://") + WiFi.softAPIP().toString(), true);
        server_->send(302, "text/plain", "");
    });

    server_->begin();
    DBG_PRINTF("[Web] Server started on port %d\n", WEB_SERVER_PORT);
    DBG_PRINTF("[OTA] Update endpoint: /update (user:admin pass:admin)\n");
}

void WebConfigServer::handle() {
    if (dnsServer_) {
        dnsServer_->processNextRequest();
    }
    server_->handleClient();
}

void WebConfigServer::setSaveCallback(void (*callback)(const char*, const char*)) {
    save_callback_ = callback;
}