#include "web_config.h"
#include "wifi_mgr.h"
#include "gpio_mgr.h"
#include "config.h"
#include "energy_mgr.h"
#include "log_buffer.h"
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

// P0: Web 请求优先级机制
volatile bool WebConfigServer::web_request_active_ = false;
unsigned long WebConfigServer::web_request_start_ = 0;

// /api/status 使用静态缓冲区，避免 1600 字节大数组压在栈上导致栈溢出
static char status_buf[2048];

// P0: Web 请求优先级机制实现
bool WebConfigServer::isWebRequestActive() {
    // 超时保护：50ms 后自动清除（防止死锁导致主循环永久跳过 SY7T609 读取）
    if (web_request_active_ && millis() - web_request_start_ > 50) {
        web_request_active_ = false;
    }
    return web_request_active_;
}

void WebConfigServer::markWebRequestStart() {
    web_request_active_ = true;
    web_request_start_ = millis();
}

void WebConfigServer::markWebRequestEnd() {
    web_request_active_ = false;
}

// P0: 关键 API 列表（WiFi 扫描期间仍需响应）
bool WebConfigServer::isCriticalApi(const String& uri) {
    return uri == "/" || uri == "/api/status" || uri == "/api/debug" ||
           uri == "/api/meter" || uri == "/api/relay" ||
           uri.startsWith("/api/connect") || uri == "/api/now";
}

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
.modal{position:fixed;top:0;left:0;right:0;bottom:0;background:rgba(0,0,0,.4);display:flex;align-items:center;justify-content:center}
.modal-layer-1{z-index:200}
.modal-layer-2{z-index:210}
.settings-page{position:fixed;top:0;left:0;right:0;bottom:0;background:#f2f2f7;z-index:150;overflow-y:auto}
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
.time-picker{display:flex;align-items:center;justify-content:center;height:180px;position:relative;background:#fff;border-radius:12px;margin-bottom:12px;overflow:hidden}
.time-picker-column{flex:1;height:100%;position:relative;overflow-y:scroll;-webkit-overflow-scrolling:touch;scroll-snap-type:y mandatory;text-align:center}
.time-picker-column::-webkit-scrollbar{display:none}
.time-picker-items{padding:72px 0}
.time-picker-item{height:36px;line-height:36px;font-size:20px;color:#1c1c1e;scroll-snap-align:center;transition:opacity .15s,color .15s;opacity:.2}
.time-picker-item.active{opacity:1;font-weight:600}
.time-picker-separator{font-size:17px;color:#8e8e93;padding:0 8px;position:relative;z-index:2}
.cycle-track{position:relative;height:48px;background:#e9e9eb;border-radius:24px;overflow:hidden;user-select:none;-webkit-user-select:none;-webkit-touch-callout:none;touch-action:pan-y;transition:opacity .3s}
.cycle-seg{position:absolute;top:6px;bottom:6px;border-radius:9px;min-width:6px}
.seg-handle{position:absolute;top:0;bottom:0;width:28px;display:flex;align-items:center;justify-content:center;touch-action:none;cursor:ew-resize;z-index:3}
.seg-handle::after{content:'';width:4px;height:20px;border-radius:2px;background:rgba(255,255,255,.95);box-shadow:0 1px 4px rgba(0,0,0,.3)}
.seg-hl{left:0}
.seg-hr{right:0}
.cycle-now{position:absolute;top:6px;bottom:6px;width:1.5px;background:#8e8e93;z-index:5;border-radius:1px;pointer-events:none;opacity:.85}
.cycle-scale{display:flex;justify-content:space-between;padding:3px 4px 0;font-size:9px;color:#c7c7cc}
.cycle-tip{position:absolute;top:-26px;transform:translateX(-50%);background:#1c1c1e;color:#fff;font-size:11px;font-weight:600;padding:3px 8px;border-radius:6px;z-index:30;white-space:nowrap;pointer-events:none}
.cycle-add-pill{position:absolute;left:50%;top:50%;transform:translate(-50%,-50%);background:#fff;color:#007aff;font-size:13px;font-weight:600;padding:5px 14px;border-radius:99px;box-shadow:0 1px 3px rgba(0,0,0,.15);pointer-events:none;white-space:nowrap}
.cycle-draft{position:absolute;top:6px;bottom:6px;border-radius:9px;background:rgba(0,122,255,.2);border:1.5px solid #007aff;box-sizing:border-box;z-index:2;pointer-events:none}
.cycle-row{display:flex;align-items:center;gap:10px;padding:12px 0}
.cycle-row+.cycle-row{border-top:.5px solid #e5e5ea}
.period-dot{width:10px;height:10px;border-radius:50%;flex-shrink:0}
.cycle-badge{font-size:11px;color:#8e8e93;background:#f2f2f7;border-radius:99px;padding:2px 8px;font-weight:500;flex-shrink:0}
.cycle-badge-tap{cursor:pointer;color:#007aff;background:rgba(0,122,255,.1);-webkit-tap-highlight-color:transparent;transition:background .15s}
.cycle-txt-btn{background:none;border:none;font-size:15px;font-family:inherit;cursor:pointer;padding:8px 2px;font-weight:400;-webkit-tap-highlight-color:transparent}
.cycle-empty{font-size:13px;color:#c7c7cc;text-align:center;line-height:36px}
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
<div class="info-row" style="margin-top:12px"><span class="lbl">检测目标</span><span id="wifiDetectTarget" style="color:#007aff;font-weight:600">等待记录</span></div>
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
<div style="display:flex;justify-content:space-between;margin-top:12px;padding-top:12px;border-top:1px solid #e5e5ea;font-size:13px">
  <span style="flex:1;text-align:left"><span style="color:#8e8e93;display:block;font-size:11px">当前电量</span><span id="histCurrentEnergy" style="color:#007aff;font-weight:600">--</span></span>
  <span style="flex:1;text-align:center"><span style="color:#8e8e93;display:block;font-size:11px">本月用电</span><span id="histMonthEnergy" style="color:#34c759;font-weight:600">--</span></span>
  <span style="flex:1;text-align:right"><span style="color:#8e8e93;display:block;font-size:11px">上月用电</span><span id="histLastMonth" style="color:#8e8e93;font-weight:600">--</span></span>
</div>
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

<div class="card" data-card="quicktimer">
<div class="card-title">快速倒计时</div>
<div style="padding:8px 0">
<div class="time-picker" id="quickTimerPicker">
  <div class="time-picker-column" id="hourCol"><div class="time-picker-items" id="hourItems"></div></div>
  <div class="time-picker-separator">时</div>
  <div class="time-picker-column" id="minuteCol"><div class="time-picker-items" id="minuteItems"></div></div>
  <div class="time-picker-separator">分</div>
</div>
<button class="timer-btn" id="qtm" onclick="toggleQuickTimer()">启动倒计时</button>
</div>
</div>
 
</div>

<div id='cardsContainer'></div>

<div class="card">
<div class="card-title">设备信息</div>
<div class="info-row"><span class="lbl">版本</span><span id="ver">V</span></div>
<div class="info-row"><span class="lbl">运行时间</span><span id="up">--</span></div>
<div class="info-row"><span class="lbl">上次复位</span><span id="rst" style="font-size:12px;color:#8e8e93">--</span></div>
<div class="info-row" style="margin-bottom:10px">
  <span class="lbl">系统日志</span>
  <div class="toggle" id="sysLogToggle" onclick="toggleSysLog()"></div>
</div>
<div id="sysLogContainer" class="info-row" style="align-items:flex-start;flex-direction:column;margin-top:4px"><span class="lbl" style="margin-bottom:4px">系统日志</span><pre id="sysLog" style="width:100%;background:#f2f2f7;border-radius:8px;padding:8px;font-size:11px;max-height:180px;overflow-y:auto;white-space:pre-wrap;word-break:break-all;color:#333;line-height:1.4;margin:0">--</pre></div>
<div id="crashLogContainer" class="info-row" style="align-items:flex-start;flex-direction:column;margin-top:4px"><span class="lbl" style="margin-bottom:4px">崩溃前日志</span><pre id="crashLog" style="width:100%;background:#fff2f0;border:1px solid #ffccc7;border-radius:10px;padding:12px;font-size:12px;max-height:360px;min-height:120px;overflow-y:auto;white-space:pre-wrap;word-break:break-all;color:#c00;line-height:1.5;margin:0">--</pre></div>
<button class="btn btn-ghost" style="margin-top:12px;width:100%" onclick="showMoreSettings()">更多设置</button>
</div>


<div id="wifiM" class="modal modal-layer-1" style="display:none">
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

<div id="wifiSelM" class="modal modal-layer-1" style="display:none">
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

<div id="wifiSettingsM" class="modal modal-layer-2" style="display:none">
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

<div id="wifiDetectMoreM" class="modal modal-layer-1" style="display:none">
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

<div id="otaM" class="modal modal-layer-1" style="display:none">
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

<div id="cycleEditM" class="modal modal-layer-1" style="display:none">
<div class="modal-content">
<div class="modal-title" id="cycleEditTitle" style="text-align:center">添加时段</div>
<div style="display:flex;gap:12px">
  <div style="flex:1">
    <div class="lbl" style="margin-bottom:6px">开始时间</div>
    <input type="time" id="ceStart" class="input-field" style="margin:0;padding:10px">
  </div>
  <div style="flex:1">
    <div class="lbl" style="margin-bottom:6px">结束时间</div>
    <input type="time" id="ceEnd" class="input-field" style="margin:0;padding:10px">
  </div>
</div>
<p style="font-size:11px;color:#c7c7cc;margin:10px 0 0;text-align:center">结束时间早于开始时间时，时段将跨午夜生效</p>
<p id="cycleEditErr" style="display:none;font-size:12px;color:#ff3b30;margin:10px 0 0;text-align:center"></p>
<button class="btn" style="margin-top:16px" onclick="cycleEditSave()">保存</button>
<button id="cycleEditDelBtn" class="cycle-txt-btn" style="width:100%;padding:12px 0;margin:0;color:#ff3b30" onclick="cycleEditDelete()">删除此时段</button>
<button class="cycle-txt-btn" style="width:100%;padding:12px 0;margin:0;color:#007aff" onclick="cycleEditCancel()">取消</button>
</div>
</div>

<div id="moreSettingsM" class="settings-page" style="display:none">
<div style="min-height:100vh;padding:16px;padding-bottom:80px">
<div style="display:flex;justify-content:space-between;align-items:center;margin-bottom:16px">
<h1 style="font-size:22px;margin:0">更多设置</h1>
<button class="btn btn-ghost" style="width:auto;padding:8px 16px" onclick="closeMoreSettings()">关闭</button>
</div>

<div class="card">
<div class="card-title">24 小时时间段循环</div>
<div style="position:relative;margin-top:24px">
<div id="cycleTip" class="cycle-tip" style="display:none">--:--</div>
<div class="cycle-track" id="cycleTrack"></div>
<div class="cycle-scale"><span>0点</span><span>6点</span><span>12点</span><span>18点</span><span>24点</span></div>
</div>
<p id="cycleMsg" style="display:none;font-size:12px;color:#ff3b30;margin:8px 0 0"></p>
<div class="info-row" style="margin:12px 0 0">
  <span class="lbl">运行状态</span>
  <span id="cycleStatus" style="color:#8e8e93;font-weight:600">--</span>
</div>
<p style="font-size:12px;color:#8e8e93;margin:8px 0 6px">在空白处按住并拖动即可添加时段 · 拖动色块边缘调整 · 轻点色块编辑</p>
<p style="font-size:11px;color:#c7c7cc;margin:0 0 10px">结束早于开始表示跨午夜 · 需联网同步时间 · 最多 6 个时段</p>
<div id="cyclePeriodsList"></div>
<button id="cyclePowerBtn" class="btn" style="margin:16px 0 0" onclick="toggleCycle()">启用循环</button>
</div>

<div class="card">
<div class="card-title">倒计时关闭</div>
<div style="padding:8px 0">
<div class="info-row" style="margin-bottom:10px"><span class="lbl">物理按钮自动倒计时</span><div class="toggle" id="buttonAutoTimerToggle" onclick="toggleButtonAutoTimer()"></div></div>
<p style="font-size:11px;color:#8e8e93;margin:4px 0 0">开启后按物理按钮自动启动倒计时关闭</p>
</div>
</div>

<div class="card">
<div class="card-title">计费供电</div>
<p style="font-size:12px;color:#8e8e93;margin:0 0 12px">启用后立即供电并按设定条件自动关闭（与拔除断电互斥）</p>
<div class="info-row">
  <span class="lbl">启用计费供电</span>
  <div class="toggle" id="billingToggle" onclick="toggleBilling()"></div>
</div>
<div style="display:flex;align-items:center;gap:12px;padding:8px 0">
  <span style="font-size:14px">阈值类型:</span>
  <select id="billingMode" class="input-field" style="width:100px;margin:0;padding:8px" onchange="onBillingModeChange()">
    <option value="0">用电量</option>
    <option value="1">金额</option>
    <option value="2">时长</option>
  </select>
</div>
<div id="billingEnergyRow" style="display:flex;align-items:center;gap:12px;padding:8px 0">
  <span style="font-size:14px">用电量阈值:</span>
  <input type="number" id="billingThresh" class="input-field" style="width:80px;margin:0;padding:8px" step="0.01" min="0.01" max="50" value="10">
  <span style="font-size:14px">度</span>
  <button class="timer-btn" style="width:auto;margin:0 0 0 auto;padding:0 12px;height:32px;font-size:13px" onclick="saveBilling()">保存</button>
</div>
<div id="billingMoneyRow" style="display:none;align-items:center;gap:12px;padding:8px 0">
  <span style="font-size:14px">金额阈值:</span>
  <input type="number" id="billingMoney" class="input-field" style="width:80px;margin:0;padding:8px" step="0.01" min="0.01" max="655" value="5">
  <span style="font-size:14px">元</span>
  <button class="timer-btn" style="width:auto;margin:0 0 0 auto;padding:0 12px;height:32px;font-size:13px" onclick="saveBilling()">保存</button>
</div>
<div id="billingTimeRow" style="display:none;align-items:center;gap:12px;padding:8px 0">
  <span style="font-size:14px">时长阈值:</span>
  <input type="number" id="billingTime" class="input-field" style="width:80px;margin:0;padding:8px" step="1" min="1" max="65535" value="60">
  <span style="font-size:14px">分钟</span>
  <button class="timer-btn" style="width:auto;margin:0 0 0 auto;padding:0 12px;height:32px;font-size:13px" onclick="saveBilling()">保存</button>
</div>
<div class="info-row" style="margin-top:8px">
  <span class="lbl">已用电量</span>
  <span id="billingUsed" style="color:#007aff;font-weight:600">--</span>
</div>
<div class="info-row" style="margin-top:8px">
  <span class="lbl">已用金额</span>
  <span id="billingUsedMoney" style="color:#ff9500;font-weight:600">--</span>
</div>
<div class="info-row" style="margin-top:8px">
  <span class="lbl">已用时长</span>
  <span id="billingUsedTime" style="color:#34c759;font-weight:600">--</span>
</div>
<div style="display:flex;align-items:center;gap:12px;padding:12px 0 0;border-top:1px solid #e5e5ea;margin-top:12px">
  <span style="font-size:14px">电价:</span>
  <input type="number" id="energyPrice" class="input-field" style="width:80px;margin:0;padding:8px" step="0.01" min="0.1" max="2" value="0.6">
  <span style="font-size:14px">元/度</span>
  <button class="timer-btn" style="width:auto;margin:0 0 0 auto;padding:0 12px;height:32px;font-size:13px" onclick="savePrice()">保存</button>
</div>
</div>

<div class="card">
<div class="card-title">功耗优化</div>
<div style="display:flex;align-items:center;gap:12px;padding:8px 0;border-bottom:1px solid #e5e5ea">
  <span style="font-size:14px">拔断阈值:</span>
  <input type="number" id="pwrThresh" class="input-field" style="width:80px;margin:0;padding:8px" step="0.1" min="0.1" max="1" value="0.5">
  <span style="font-size:14px">W</span>
  <button class="timer-btn" style="width:auto;margin:0 0 0 auto;padding:0 12px;height:32px;font-size:13px" onclick="savePowerOff()">保存</button>
</div>
<div class="info-row" style="margin-top:12px;margin-bottom:10px">
  <span class="lbl">STA 连接后自动关闭 AP</span>
  <div class="toggle" id="autoCloseAPToggle" onclick="toggleAutoCloseAP()"></div>
</div>
<p style="font-size:11px;color:#8e8e93;margin:4px 0 0">开启后 STA 连接 5 分钟自动关闭 AP，STA 断开时自动恢复。默认关闭，关闭前可通过 AP 访问此开关。</p>
<div class="info-row" style="margin-top:12px;margin-bottom:10px">
  <span class="lbl">限制 WiFi 发射功率</span>
  <div class="toggle" id="wifiTxPowerToggle" onclick="toggleWiFiTxPower()"></div>
</div>
<p style="font-size:11px;color:#8e8e93;margin:4px 0 0">开启后将弱信号下的最大发射功率从 14dBm 降到 10dBm，可降低峰值电流与功耗，可能改善继电器异响。</p>
</div>

<div class="card">
<div class="card-title">MQTT 代理平台</div>
<div class="info-row" style="margin-bottom:12px">
  <span class="lbl">启用 MQTT</span>
  <div class="toggle" id="mqttToggle" onclick="toggleMqtt()"></div>
</div>
<input type="text" id="mqServer" class="input-field" placeholder="服务器地址 (IP 或域名)" maxlength="32">
<input type="number" id="mqPort" class="input-field" placeholder="端口号 (默认 1883)">
<input type="text" id="mqUser" class="input-field" placeholder="用户名 (选填)" maxlength="20">
<input type="password" id="mqPass" class="input-field" placeholder="密码 (选填)" maxlength="20">
<button class="btn" style="margin-top:12px" onclick="saveMqtt()">保存 MQTT 设置</button>
</div>

<div class="card">
<div class="card-title">电量检测卡片</div>
<p style="font-size:11px;color:#8e8e93;margin:4px 0 12px">校准前请确保万用表已就位，校准期间设备会阻塞 1-2 秒</p>
<div style="margin-bottom:12px">
<div style="font-size:13px;color:#8e8e93;margin-bottom:6px">电压校准（实测 V）</div>
<div style="display:flex;gap:8px;align-items:center">
<input type="number" id="calibV" class="input-field" placeholder="220.0" step="0.1" min="1" max="300" style="flex:1;width:auto;margin:0">
<button class="btn" style="width:auto;flex:0 0 80px;margin:0" onclick="doCalibV()">校准</button>
</div>
</div>
<div style="margin-bottom:12px">
<div style="font-size:13px;color:#8e8e93;margin-bottom:6px">电流校准（实测 A）</div>
<div style="display:flex;gap:8px;align-items:center">
<input type="number" id="calibI" class="input-field" placeholder="4.545" step="0.001" min="0.001" max="100" style="flex:1;width:auto;margin:0">
<button class="btn" style="width:auto;flex:0 0 80px;margin:0" onclick="doCalibI()">校准</button>
</div>
</div>
<div style="border-top:1px solid #e5e5ea;margin:12px 0;padding-top:12px">
<button class="btn btn-ghost" style="width:100%;margin:0 0 8px 0" onclick="doResetCalib()">恢复出厂校准</button>
<button class="btn btn-ghost" style="width:100%;margin:0 0 8px 0" onclick="doSaveCalib()">保存校准到 Flash</button>
<button class="btn btn-ghost" style="width:100%;margin:0" onclick="doClearEnergy()">清零电能计数器</button>
</div>
</div>

<div class="card">
<div class="card-title">主页卡片排序</div>
<p style="font-size:12px;color:#8e8e93;margin:0 0 12px">拖动调整卡片显示顺序</p>
<div id="sortList"></div>
</div>

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
  <div class="lbl" style="margin-top:12px;margin-bottom:8px">局域网域名</div>
  <div style="display:flex;gap:8px">
    <input type="text" id="hostname" style="flex:1;padding:10px;border:1px solid #e5e5ea;border-radius:8px;font-size:14px" maxlength="10" placeholder="power" oninput="validateHostname(this)">
    <button class="timer-btn" style="width:auto;margin:0;padding:0 16px;height:38px" onclick="saveHostname()">保存</button>
  </div>
  <p id="hostnameHint" style="font-size:11px;color:#8e8e93;margin:4px 0 0">访问地址为 <span id="hostnamePreview">power.local</span>，1-10 位小写字母/数字/连字符</p>
  <div style="display:flex;gap:8px;margin-top:12px">
    <button class="btn" id="otaBtn">固件升级</button>
    <button class="btn btn-ghost" id="restartBtn" onclick="confirmRestart()">重启设备</button>
  </div>
  <button class="timer-btn" style="width:100%;background:#ff3b30;color:#fff;margin-top:10px" onclick="resetEnergyHistory()">清零历史电量和电费记录</button>
  <button class="btn btn-ghost" id="resetBtn" onclick="confirmReset()" style="margin-top:10px;width:100%">恢复出厂</button>
</div>
</div>

</div>
</div>

<script>
var selSSID='';

function init(){
  try{
    renderCards(cardOrder);
    
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
    cycleInitTrack();
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
  var _fetched=false;
  var _to=setTimeout(function(){
    if(_fetched)return;
    console.log('[update] fetch /api/status timeout (5s), retrying');
    var lo=document.getElementById('loadingOverlay');
    if(lo)lo.classList.add('hide');
    scheduleUpdate();
  },5000);
  fetch('/api/status').then(function(r){return r.json()}).then(function(d){
    _fetched=true;clearTimeout(_to);
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
    var rst=document.getElementById('rst'); if(rst) rst.textContent=d.rst||'--';
    var ver=document.getElementById('ver'); if(ver) ver.textContent='V'+d.ver;
    var rm=document.getElementById('rm'); if(rm){if(d.m)rm.classList.add('on');else rm.classList.remove('on');}
    var rs=document.getElementById('rs'); if(rs){if(d.s)rs.classList.add('on');else rs.classList.remove('on');}
    var lk=document.getElementById('lk'); if(lk){if(d.lk)lk.classList.add('on');else lk.classList.remove('on');}
    var mV=document.getElementById('mV'); if(mV) mV.textContent=d.me?(d.v!==undefined?d.v:'--'):'--';
    var mI=document.getElementById('mI'); if(mI) mI.textContent=d.me?(d.i!==undefined?d.i:'--'):'--';
    var mP=document.getElementById('mP'); if(mP) mP.textContent=d.me?(d.p!==undefined?d.p:'--'):'--';
    var mE=document.getElementById('mE'); if(mE) mE.textContent=d.me?(d.e!==undefined?(d.e/1000).toFixed(2):'--'):'--';
    var mt=document.getElementById('mt'); if(mt){if(d.me)mt.classList.add('on');else mt.classList.remove('on');}
    // 快速倒计时卡片：iOS 风格滚动选择器（小时 + 分钟）
    initQuickTimerPicker();
    // 定时器运行中时强制同步到当前设定值；非运行状态不覆盖用户正在滑动的选择
    var timerRunning=(d.te && d.tr>0);
    if(timerRunning){
      setQuickTimerPickerValue(d.td,true);
      qPickerUserSet=false;
    }else if(!qPickerUserSet && d.td && d.td>0 && d.td<=1440){
      setQuickTimerPickerValue(d.td,false);
    }
    var batEl=document.getElementById('buttonAutoTimerToggle'); if(batEl){if(d.bat)batEl.classList.add('on');else batEl.classList.remove('on');}
    
    var rlt=document.getElementById('redLedToggle');
    if(rlt){if(d.rl)rlt.classList.add('on');else rlt.classList.remove('on');}
    
    if(d.ap !== undefined && document.activeElement !== document.getElementById('apPass')) document.getElementById('apPass').value=d.ap;
    if(d.aps !== undefined && document.activeElement !== document.getElementById('apSuffix')) document.getElementById('apSuffix').value=d.aps;
    if(d.host !== undefined && document.activeElement !== document.getElementById('hostname')) {
      document.getElementById('hostname').value=d.host;
      document.getElementById('hostnamePreview').textContent=d.host+'.local';
    }
    if(d.mqs !== undefined && document.activeElement !== document.getElementById('mqServer')) document.getElementById('mqServer').value=d.mqs;
    if(d.mqp !== undefined && document.activeElement !== document.getElementById('mqPort')) document.getElementById('mqPort').value=d.mqp;
    if(d.mqu !== undefined && document.activeElement !== document.getElementById('mqUser')) document.getElementById('mqUser').value=d.mqu;
    if(d.mqpw !== undefined && document.activeElement !== document.getElementById('mqPass')) document.getElementById('mqPass').value=d.mqpw;
    // 24h 循环：拖动期间冻结本地渲染，避免 3 秒轮询重建 DOM 打断拖拽
    _cycleEnabled=!!d.ce;
    _cycleNowMins=(d.cm!==undefined)?d.cm:-1;
    if(!_cycleDrag){
      _cyclePeriods=d.cp||[];
      renderCycleTimeline();
      renderCyclePeriods();
    }
    cycleUpdateNow();
    updateCycleStatus(d);

    var ct=document.getElementById('cyclePowerBtn');
    if(ct)cyclePowerBtnRender(!!d.ce);
    var mtog=document.getElementById('mqttToggle');
    if(mtog){if(d.mqe)mtog.classList.add('on');else mtog.classList.remove('on');}

    var acap=document.getElementById('autoCloseAPToggle');
    if(acap){if(d.acap)acap.classList.add('on');else acap.classList.remove('on');}
    var wtpl=document.getElementById('wifiTxPowerToggle');
    if(wtpl){if(d.wtpl)wtpl.classList.add('on');else wtpl.classList.remove('on');}

    var slog=document.getElementById('sysLogToggle');
    if(slog){if(d.syslog)slog.classList.add('on');else slog.classList.remove('on');}
    var slc=document.getElementById('sysLogContainer');
    var clc=document.getElementById('crashLogContainer');
    if(slc)slc.style.display=d.syslog?'flex':'none';
    if(clc)clc.style.display=d.syslog?'flex':'none';

    var pt=document.getElementById('pwrOffToggle');
    if(pt){if(d.pe)pt.classList.add('on');else pt.classList.remove('on');}
    syncPowerOffHome(d.pe);
    var pth=document.getElementById('pwrThresh');
    if(pth && d.pt !== undefined && document.activeElement !== pth) pth.value=d.pt;

    // 更新计费供电状态
    var bt=document.getElementById('billingToggle');
    if(bt){if(d.be)bt.classList.add('on');else bt.classList.remove('on');}
    var bmo=document.getElementById('billingMode');
    if(bmo && d.bmo !== undefined && document.activeElement !== bmo){
      bmo.value=d.bmo;
      onBillingModeChange();
    }
    var bth=document.getElementById('billingThresh');
    if(bth && d.bt !== undefined && document.activeElement !== bth) bth.value=d.bt;
    var bmn=document.getElementById('billingMoney');
    if(bmn && d.bm !== undefined && document.activeElement !== bmn) bmn.value=d.bm;
    var btt=document.getElementById('billingTime');
    if(btt && d.btt !== undefined && document.activeElement !== btt) btt.value=d.btt;
    var bus=document.getElementById('billingUsed');
    if(bus && d.bu !== undefined) bus.textContent=d.bu.toFixed(2)+'度';
    var bum=document.getElementById('billingUsedMoney');
    if(bum && d.bum !== undefined) bum.textContent='¥'+d.bum.toFixed(2);
    var but=document.getElementById('billingUsedTime');
    if(but && d.but !== undefined) but.textContent=d.but+'分钟';
    if(d.ep !== undefined){var epEl=document.getElementById('energyPrice'); if(epEl) epEl.value=d.ep;}
    // 用电历史卡片底部三数据项
    var hce=document.getElementById('histCurrentEnergy');
    if(hce) hce.textContent=d.e!==undefined?(d.e/1000).toFixed(2)+'度':'--';
    var hme=document.getElementById('histMonthEnergy');
    if(hme) hme.textContent=d.mo!==undefined?d.mo.toFixed(2)+'度':'--';
    var hlm=document.getElementById('histLastMonth');
    if(hlm) hlm.textContent=d.lm!==undefined?d.lm.toFixed(2)+'度':'--';

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
        wdtgt.textContent='等待记录';
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

    // 快速倒计时卡片状态同步
    var qtm=document.getElementById('qtm');
    if(qtm){
      if(d.te && d.tr){
        var qrem=d.tl||0;
        // 同步本地计时器（偏差>3秒才校准，避免重复启动）
        if(qTimerRemain===0 || Math.abs(qTimerRemain-qrem)>3){
          startQuickTimerDisplay(qrem);
        }
      }else{
        qTimerRemain=0;
        if(qTimerInterval){clearInterval(qTimerInterval);qTimerInterval=null;}
        qtm.classList.remove('active');
        qtm.textContent='启动倒计时';
      }
    }
    }catch(e){console.log('UI update error:',e);}
    var lo=document.getElementById('loadingOverlay');
    if(lo)lo.classList.add('hide');
    updateLog();
    updateCrashLog();
    scheduleUpdate();
  }).catch(function(e){console.log('Status fetch error:',e);
    _fetched=true;clearTimeout(_to);
    var lo=document.getElementById('loadingOverlay');
    if(lo)lo.classList.add('hide');
    updateLog();
    updateCrashLog();
    scheduleUpdate();
  });
}

function fmt(s){
  var d=Math.floor(s/86400);
  var h=Math.floor((s%86400)/3600);
  var m=Math.floor((s%3600)/60);
  return(d>0?d+'天 ':'')+(h>0?h+'时 ':'')+m+'分';
}
function updateLog(){
  var slog=document.getElementById('sysLogToggle');
  if(slog && !slog.classList.contains('on')){
    var el=document.getElementById('sysLog');
    if(el) el.textContent='--';
    return;
  }
  fetch('/api/log').then(function(r){return r.json()}).then(function(lines){
    var el=document.getElementById('sysLog');
    if(!el) return;
    if(!lines || lines.length===0){el.textContent='--';return;}
    el.textContent=lines.join('\n');
    el.scrollTop=el.scrollHeight;
  }).catch(function(e){console.log('Log fetch error:',e);});
}
function toggleSysLog(){
  var el=document.getElementById('sysLogToggle');
  var enabled=el?!el.classList.contains('on'):false;
  fetch('/api/sys_log',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({enabled:enabled})})
    .then(function(r){return r.json();}).then(function(d){
      if(el){if(d.enabled)el.classList.add('on');else el.classList.remove('on');}
      var slc=document.getElementById('sysLogContainer');
      var clc=document.getElementById('crashLogContainer');
      if(slc)slc.style.display=d.enabled?'flex':'none';
      if(clc)clc.style.display=d.enabled?'flex':'none';
    }).catch(function(e){console.log('Sys log toggle error:',e);});
}
function updateCrashLog(){
  fetch('/api/crash_log').then(function(r){return r.json()}).then(function(d){
    var el=document.getElementById('crashLog');
    if(!el) return;
    if(!d || !d.logs || d.logs.length===0){el.textContent='--';return;}
    el.textContent='上次复位: '+(d.reason||'unknown')+'\n'+d.logs.join('\n');
    el.scrollTop=el.scrollHeight;
  }).catch(function(e){console.log('Crash log fetch error:',e);});
}
function toggleAutoCloseAP(){
  var el=document.getElementById('autoCloseAPToggle');
  var enabled=el?!el.classList.contains('on'):false;
  fetch('/api/auto_close_ap',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({enabled:enabled})})
    .then(function(r){return r.json();}).then(function(d){
      if(el){if(d.enabled)el.classList.add('on');else el.classList.remove('on');}
    }).catch(function(e){console.log('Auto close AP error:',e);});
}
function toggleWiFiTxPower(){
  var el=document.getElementById('wifiTxPowerToggle');
  var enabled=el?!el.classList.contains('on'):false;
  fetch('/api/wifi_tx_power',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({enabled:enabled})})
    .then(function(r){return r.json();}).then(function(d){
      if(el){if(d.enabled)el.classList.add('on');else el.classList.remove('on');}
    }).catch(function(e){console.log('WiFi TX power error:',e);});
}
function loadCardOrder(retries){
  fetch('/api/card_order').then(function(r){return r.json()}).then(function(d){
    if(d.order && d.order.length===6){
      var changed=false;
      for(var i=0;i<6;i++){if(cardOrder[i]!==d.order[i]){changed=true;break;}}
      if(changed){cardOrder=d.order;renderCards(cardOrder);}
    }
  }).catch(function(){
    if(retries < 5){
      setTimeout(function(){loadCardOrder(retries+1);}, 1500);
    }
  });
}
var cardOrder=[0,1,2,3,4,5];
var cardVisibility=0x3F;
var cardNames=['继电器控制','人来上电','实时电量','用电历史','WiFi 状态','快速倒计时'];

function renderCards(order){
  cardOrder=order||[0,1,2,3,4,5];
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

function validateHostname(el){
  var v=el.value.toLowerCase().replace(/[^a-z0-9-]/g,'');
  if(v!==el.value) el.value=v;
  var hint=document.getElementById('hostnameHint');
  var preview=document.getElementById('hostnamePreview');
  if(v.length>=1 && v.length<=10){
    preview.textContent=v+'.local';
    hint.style.color='#8e8e93';
  }else{
    hint.style.color='#ff3b30';
  }
}

function saveHostname(){
  var el=document.getElementById('hostname');
  var v=el.value.trim().toLowerCase();
  if(v.length===0){ v='power'; }
  if(v.length<1 || v.length>10 || !/^[a-z0-9]([a-z0-9-]*[a-z0-9])?$/.test(v)){
    alert('域名格式错误：1-10 位，仅小写字母、数字、连字符，且不能以连字符开头或结尾');
    return;
  }
  fetch('/api/hostname',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({hostname:v})}).then(function(r){return r.json();}).then(function(d){
    if(d.ok){
      alert('局域网域名已保存为 '+v+'.local，mDNS 已更新');
      document.getElementById('hostnamePreview').textContent=v+'.local';
    }else{
      alert('保存失败：'+(d.error||'格式错误'));
    }
  }).catch(function(e){console.log('save hostname error',e);});
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

function setTimerDuration(mins){
  fetch('/api/timer?duration='+mins).then(function(r){return r.json()}).then(function(d){
    console.log('Timer duration set to:',d.duration);
  }).catch(function(e){console.log('Timer duration error:',e)});
}

// iOS 风格快速倒计时选择器
var qPickerInited=false,qPickerUserSet=false,qPickerSelectedMins=60;
function initQuickTimerPicker(){
  var hItems=document.getElementById('hourItems');
  var mItems=document.getElementById('minuteItems');
  if(!hItems || !mItems) return;
  // 如果卡片被重新渲染导致子元素丢失，需要重新初始化
  if(qPickerInited && hItems.children.length>0 && mItems.children.length>0) return;
  hItems.innerHTML='';
  mItems.innerHTML='';
  for(var i=0;i<=23;i++){
    var d=document.createElement('div');d.className='time-picker-item';d.textContent=i;
    d.dataset.value=i;hItems.appendChild(d);
  }
  for(var i=0;i<=59;i++){
    var d=document.createElement('div');d.className='time-picker-item';d.textContent=i;
    d.dataset.value=i;mItems.appendChild(d);
  }
  function setItemOpacity(items,idx){
    for(var i=0;i<items.children.length;i++){
      var dist=Math.abs(i-idx);
      var op;
      if(dist===0) op=1;
      else if(dist===1) op=0.45;
      else if(dist===2) op=0.22;
      else op=0.1;
      items.children[i].style.opacity=op;
      items.children[i].classList.toggle('active',i===idx);
    }
  }
  function onScroll(col,items,maxVal){
    return function(){
      var itemHeight=36;
      var idx=Math.round(col.scrollTop/itemHeight);
      if(idx<0) idx=0;
      if(idx>=items.children.length) idx=items.children.length-1;
      if(idx>maxVal) idx=maxVal;
      setItemOpacity(items,idx);
      qPickerUserSet=true;
      qPickerSelectedMins=getQuickTimerPickerValue();
    };
  }
  function onClick(col,maxVal){
    return function(e){
      var rect=col.getBoundingClientRect();
      var y=e.clientY-rect.top;
      var idx=Math.round(col.scrollTop/36);
      if(y<rect.height/2) idx=Math.max(0,idx-1); else idx=Math.min(maxVal,idx+1);
      col.scrollTo({top:idx*36,behavior:'smooth'});
      qPickerUserSet=true;
    };
  }
  var hCol=document.getElementById('hourCol');
  var mCol=document.getElementById('minuteCol');
  hCol.addEventListener('scroll',onScroll(hCol,hItems,23));
  mCol.addEventListener('scroll',onScroll(mCol,mItems,59));
  hCol.addEventListener('click',onClick(hCol,23));
  mCol.addEventListener('click',onClick(mCol,59));
  qPickerInited=true;
}
function setQuickTimerPickerValue(totalMins,force){
  if(qPickerUserSet && !force) return;
  var hCol=document.getElementById('hourCol');
  var mCol=document.getElementById('minuteCol');
  if(!hCol || !mCol) return;
  var hrs=Math.floor(totalMins/60);
  var mins=totalMins%60;
  if(hrs<0) hrs=0; if(hrs>23) hrs=23;
  if(mins<0) mins=0; if(mins>59) mins=59;
  hCol.scrollTop=hrs*36;
  mCol.scrollTop=mins*36;
  qPickerSelectedMins=hrs*60+mins;
  updateQuickTimerPickerActive();
}
function updateQuickTimerPickerActive(){
  var hItems=document.getElementById('hourItems');
  var mItems=document.getElementById('minuteItems');
  if(!hItems || !mItems) return;
  var hCol=document.getElementById('hourCol');
  var mCol=document.getElementById('minuteCol');
  var hIdx=Math.round(hCol.scrollTop/36);
  var mIdx=Math.round(mCol.scrollTop/36);
  if(hIdx<0) hIdx=0; if(hIdx>23) hIdx=23;
  if(mIdx<0) mIdx=0; if(mIdx>59) mIdx=59;
  function setOp(items,idx){
    for(var i=0;i<items.children.length;i++){
      var dist=Math.abs(i-idx);
      var op;
      if(dist===0) op=1;
      else if(dist===1) op=0.45;
      else if(dist===2) op=0.22;
      else op=0.1;
      items.children[i].style.opacity=op;
      items.children[i].classList.toggle('active',i===idx);
    }
  }
  setOp(hItems,hIdx);
  setOp(mItems,mIdx);
}
function getQuickTimerPickerValue(){
  var hCol=document.getElementById('hourCol');
  var mCol=document.getElementById('minuteCol');
  if(!hCol || !mCol) return qPickerSelectedMins;
  var hrs=Math.round(hCol.scrollTop/36);
  var mins=Math.round(mCol.scrollTop/36);
  if(hrs<0) hrs=0; if(hrs>23) hrs=23;
  if(mins<0) mins=0; if(mins>59) mins=59;
  return hrs*60+mins;
}

// 快速倒计时卡片：一键启动 + 动态显示剩余时间
var qTimerRemain=0, qTimerInterval=null;
function toggleQuickTimer(){
  var qtm=document.getElementById('qtm');
  if(!qtm) return;
  if(qtm.classList.contains('active')){
    fetch('/api/timer?enabled=false').then(function(r){return r.json()}).then(function(){
      qTimerRemain=0;
      if(qTimerInterval){clearInterval(qTimerInterval);qTimerInterval=null;}
      qPickerUserSet=false;
      update();
    }).catch(function(e){console.log('QuickTimer error:',e)});
  }else{
    var dur=qPickerSelectedMins;
    if(dur<=0){alert('请先选择倒计时时长');return;}
    fetch('/api/timer?duration='+dur+'&enabled=true').then(function(r){return r.json()}).then(function(){
      qPickerUserSet=false;
      update();
    }).catch(function(e){console.log('QuickTimer error:',e)});
  }
}
function startQuickTimerDisplay(remain){
  qTimerRemain=remain;
  if(qTimerInterval) clearInterval(qTimerInterval);
  qTimerInterval=setInterval(function(){
    if(qTimerRemain>0){
      qTimerRemain--;
      updateQuickTimerBtn();
    }else{
      clearInterval(qTimerInterval);
      qTimerInterval=null;
      update();
    }
  },1000);
  updateQuickTimerBtn();
}
function updateQuickTimerBtn(){
  var qtm=document.getElementById('qtm');
  if(!qtm) return;
  if(qTimerRemain>0){
    var rh=Math.floor(qTimerRemain/3600);
    var rMin=Math.floor((qTimerRemain%3600)/60);
    var rSec=Math.floor(qTimerRemain%60);
    qtm.classList.add('active');
    qtm.textContent='关闭 '+(rh>0?rh+':':'')+String(rMin).padStart(2,'0')+':'+String(rSec).padStart(2,'0');
  }else{
    qtm.classList.remove('active');
    qtm.textContent='启动倒计时';
  }
}

function toggleLock(){
  fetch('/api/lock').then(function(r){return r.json()}).then(function(d){
    var lk=document.getElementById('lk');
    if(d.locked)lk.classList.add('on');else lk.classList.remove('on');
  }).catch(function(e){console.log('Lock error:',e)});
}

// ============= 24h 循环时间段（可视化时间轴） =============
var _cyclePeriods=[];
var _cycleCount=0;
var _cycleEnabled=false;
var _cycleNowMins=-1;
var _cycleEditing=-1;
var _cycleDrag=null;
var _cycleDraft=null;
var _cycleTrackInited=false;
var _cycleMsgT=null;
var CYCLE_COLORS=['#34c759','#007aff','#ff9500','#af52de','#30b0c7','#ff2d55'];
var CYCLE_MAX=6;
function pad2(n){return (n<10?'0':'')+n;}
function cyclePeriodForMinute(cp,cur){
  for(var i=0;i<cp.length;i++){
    var p=cp[i];
    var s=p.sh*60+p.sm,e=p.eh*60+p.em;
    if(s<=e){if(cur>=s&&cur<e)return p;}
    else{if(cur>=s||cur<e)return p;}
  }
  return null;
}
function cycleIntervals(p){
  var s=p.sh*60+p.sm,e=p.eh*60+p.em;
  if(s<e)return [[s,e]];
  if(s>e)return [[s,1440],[0,e]];
  return [];
}
function cycleOverlapIdx(p,skip){
  var a=cycleIntervals(p);
  if(!a.length)return -1;
  for(var i=0;i<_cyclePeriods.length;i++){
    if(i===skip)continue;
    var b=cycleIntervals(_cyclePeriods[i]);
    for(var x=0;x<a.length;x++)for(var y=0;y<b.length;y++){
      if(a[x][0]<b[y][1]&&b[y][0]<a[x][1])return i;
    }
  }
  return -1;
}
function cycleSegs(){
  var out=[];
  for(var i=0;i<_cyclePeriods.length;i++){
    var p=_cyclePeriods[i];
    var s=p.sh*60+p.sm,e=p.eh*60+p.em;
    if(s<e)out.push({i:i,s:s,e:e,hl:1,hr:1});
    else if(s>e){out.push({i:i,s:s,e:1440,hl:1,hr:0});out.push({i:i,s:0,e:e,hl:0,hr:1});}
  }
  return out;
}
function cycleLiveRender(){
  var track=document.getElementById('cycleTrack');
  if(!track)return;
  var html='';
  if(!_cyclePeriods.length && !_cycleDraft){
    html+='<div class="cycle-add-pill">＋ 添加时段</div>';
  }
  var segs=cycleSegs();
  for(var k=0;k<segs.length;k++){
    var g=segs[k];
    var col=CYCLE_COLORS[g.i%CYCLE_COLORS.length];
    html+='<div class="cycle-seg" data-i="'+g.i+'" style="left:'+(g.s/14.4)+'%;width:'+((g.e-g.s)/14.4)+'%;background:'+col+'">';
    if(g.hl)html+='<div class="seg-handle seg-hl" data-i="'+g.i+'" data-k="s"></div>';
    if(g.hr)html+='<div class="seg-handle seg-hr" data-i="'+g.i+'" data-k="e"></div>';
    html+='</div>';
  }
  if(_cycleDraft&&_cycleDraft.e>_cycleDraft.s){
    html+='<div class="cycle-draft" style="left:'+(_cycleDraft.s/14.4)+'%;width:'+((_cycleDraft.e-_cycleDraft.s)/14.4)+'%"></div>';
  }else if(_cycleDraft){
    html+='<div class="cycle-draft" style="left:'+(_cycleDraft.s/14.4)+'%;width:3px"></div>';
  }
  html+='<div class="cycle-now" id="cycleNow" style="display:none"></div>';
  track.innerHTML=html;
  track.style.opacity=_cycleEnabled?'1':'.45';
  cycleUpdateNow();
}
function renderCycleTimeline(){
  if(_cycleDrag)return;
  cycleLiveRender();
}
function cycleUpdateNow(){
  var el=document.getElementById('cycleNow');
  if(!el)return;
  if(_cycleNowMins<0){el.style.display='none';return;}
  el.style.display='block';
  el.style.left=(_cycleNowMins/14.4)+'%';
}
function cycleMinutesFromEvent(ev){
  var t=document.getElementById('cycleTrack');
  if(!t)return 0;
  var r=t.getBoundingClientRect();
  var m=Math.round((ev.clientX-r.left)/r.width*1440);
  return Math.max(0,Math.min(1439,m));
}
function cycleSnap(m){
  m=Math.round(m/5)*5;
  return Math.max(0,Math.min(1439,m));
}
function cycleShowTip(m){
  cycleShowTipRange(m,null);
}
function cycleShowTipRange(a,b){
  var tip=document.getElementById('cycleTip');
  var t=document.getElementById('cycleTrack');
  if(!tip||!t)return;
  tip.style.display='block';
  tip.textContent=(b===null)?(pad2(Math.floor(a/60))+':'+pad2(a%60))
    :(pad2(Math.floor(Math.min(a,b)/60))+':'+pad2(Math.min(a,b)%60)+' – '+pad2(Math.floor(Math.max(a,b)/60))+':'+pad2(Math.max(a,b)%60));
  var r=t.getBoundingClientRect();
  var pos=(b===null)?a:Math.max(a,b);
  var x=Math.max(20,Math.min(r.width-20,r.width*pos/1440));
  tip.style.left=x+'px';
}
function cycleHideTip(){
  var tip=document.getElementById('cycleTip');
  if(tip)tip.style.display='none';
}
function cycleMsg(t){
  var el=document.getElementById('cycleMsg');
  if(!el)return;
  el.textContent=t;
  el.style.display='block';
  if(_cycleMsgT)clearTimeout(_cycleMsgT);
  _cycleMsgT=setTimeout(function(){el.style.display='none';},2600);
}
function cycleInitTrack(){
  if(_cycleTrackInited)return;
  var t=document.getElementById('cycleTrack');
  if(!t)return;
  _cycleTrackInited=true;
  if(!window.PointerEvent){
    // 不支持 Pointer Events 的浏览器回退为点击弹窗设置
    t.addEventListener('click',function(ev){
      var seg=(ev.target&&ev.target.closest)?ev.target.closest('.cycle-seg'):null;
      if(seg){cycleEditIdx(parseInt(seg.getAttribute('data-i'),10));return;}
      if(_cyclePeriods.length>=CYCLE_MAX){cycleMsg('最多 '+CYCLE_MAX+' 个时段');return;}
      var m=cycleSnap(cycleMinutesFromEvent(ev));
      cycleOpenEdit(-1,m,(m+60)%1440);
    });
    return;
  }
  t.addEventListener('pointerdown',function(ev){
    var h=(ev.target&&ev.target.closest)?ev.target.closest('.seg-handle'):null;
    var seg=(!h&&ev.target&&ev.target.closest)?ev.target.closest('.cycle-seg'):null;
    if(h){
      ev.preventDefault();
      var i=parseInt(h.getAttribute('data-i'),10);
      var p=_cyclePeriods[i];
      if(!p)return;
      _cycleDrag={i:i,k:h.getAttribute('data-k'),moved:false,sx:ev.clientX,
        os:p.sh*60+p.sm,oe:p.eh*60+p.em};
      try{t.setPointerCapture(ev.pointerId);}catch(e){}
      cycleShowTip(_cycleDrag.k==='s'?_cycleDrag.os:_cycleDrag.oe);
      return;
    }
    if(seg){
      _cycleDrag={tapEdit:parseInt(seg.getAttribute('data-i'),10),moved:false,sx:ev.clientX};
      try{t.setPointerCapture(ev.pointerId);}catch(e){}
      return;
    }
    // 空白处按下：直接进入拖动创建
    if(_cyclePeriods.length>=CYCLE_MAX){
      _cycleDrag={full:true};
      try{t.setPointerCapture(ev.pointerId);}catch(e){}
      cycleMsg('最多 '+CYCLE_MAX+' 个时段');
      return;
    }
    var m=cycleSnap(cycleMinutesFromEvent(ev));
    _cycleDrag={create:true,anchor:m,cur:m,moved:false,sx:ev.clientX};
    _cycleDraft={s:m,e:m};
    cycleLiveRender();
    cycleShowTipRange(m,m);
    try{t.setPointerCapture(ev.pointerId);}catch(e){}
  });
  t.addEventListener('pointermove',function(ev){
    if(!_cycleDrag)return;
    if(_cycleDrag.k==='s'||_cycleDrag.k==='e'){
      ev.preventDefault();
      if(Math.abs(ev.clientX-_cycleDrag.sx)>6)_cycleDrag.moved=true;
      var m=cycleSnap(cycleMinutesFromEvent(ev));
      var p=_cyclePeriods[_cycleDrag.i];
      if(!p)return;
      if(_cycleDrag.k==='s'){
        if(m!==p.eh*60+p.em){p.sh=Math.floor(m/60);p.sm=m%60;}
      }else{
        if(m!==p.sh*60+p.sm){p.eh=Math.floor(m/60);p.em=m%60;}
      }
      cycleLiveRender();
      cycleShowTip(m);
    }else if(_cycleDrag.create){
      ev.preventDefault();
      if(Math.abs(ev.clientX-_cycleDrag.sx)>6)_cycleDrag.moved=true;
      var mc=cycleSnap(cycleMinutesFromEvent(ev));
      _cycleDrag.cur=mc;
      _cycleDraft={s:Math.min(_cycleDrag.anchor,mc),e:Math.max(_cycleDrag.anchor,mc)};
      cycleLiveRender();
      cycleShowTipRange(_cycleDrag.anchor,mc);
    }
  });
  t.addEventListener('pointerup',function(ev){
    if(!_cycleDrag)return;
    var d=_cycleDrag;
    _cycleDrag=null;
    cycleHideTip();
    if(d.full)return;
    if(d.moved){
      if(d.k==='s'||d.k==='e')cycleCommitDrag(d);
      else if(d.create)cycleCreateSave(Math.min(d.anchor,d.cur),Math.max(d.anchor,d.cur));
      return;
    }
    if(d.tapEdit!==undefined&&d.tapEdit!==null){cycleEditIdx(d.tapEdit);return;}
    if(d.k==='s'||d.k==='e'){cycleEditIdx(d.i);return;}
    if(d.create)cycleCreateSave(d.anchor,(d.anchor+60)%1440);
  });
  t.addEventListener('pointercancel',function(){
    if(!_cycleDrag)return;
    var d=_cycleDrag;
    _cycleDrag=null;
    cycleHideTip();
    if(d.create){_cycleDraft=null;cycleLiveRender();return;}
    if(d.k==='s'||d.k==='e')cycleRevertDrag(d);
  });
}
function cycleCreateSave(s,e){
  _cycleDraft=null;
  if(_cyclePeriods.length>=CYCLE_MAX){cycleMsg('最多 '+CYCLE_MAX+' 个时段');cycleLiveRender();return;}
  if(e>=s&&e-s<10){e=s+60;if(e>1440)e-=1440;}
  var p={sh:Math.floor(s/60),sm:s%60,eh:Math.floor(e/60),em:e%60};
  if(cycleOverlapIdx(p,-1)>=0){cycleMsg('与其他时段重叠');cycleLiveRender();return;}
  fetch('/api/cycle?add=1&sh='+p.sh+'&sm='+p.sm+'&eh='+p.eh+'&em='+p.em)
    .then(function(r){return r.json()})
    .then(function(d2){
      if(!d2.ok)cycleMsg('保存失败');
      update();
    })
    .catch(function(){cycleMsg('网络错误');update();});
}
function cycleRevertDrag(d){
  var p=_cyclePeriods[d.i];
  if(p){p.sh=Math.floor(d.os/60);p.sm=d.os%60;p.eh=Math.floor(d.oe/60);p.em=d.oe%60;}
  cycleLiveRender();
}
function cycleCommitDrag(d){
  var p=_cyclePeriods[d.i];
  if(!p)return;
  var s=p.sh*60+p.sm,e=p.eh*60+p.em;
  if(s===e){cycleRevertDrag(d);cycleMsg('起止时间不能相同');return;}
  if(cycleOverlapIdx(p,d.i)>=0){cycleRevertDrag(d);cycleMsg('与其他时段重叠');return;}
  fetch('/api/cycle?idx='+d.i+'&sh='+p.sh+'&sm='+p.sm+'&eh='+p.eh+'&em='+p.em)
    .then(function(r){return r.json()})
    .then(function(d2){if(!d2.ok)cycleMsg('保存失败');update();})
    .catch(function(){cycleMsg('网络错误');update();});
}
function cycleEditIdx(i){
  var p=_cyclePeriods[i];
  if(!p)return;
  cycleOpenEdit(i,p.sh*60+p.sm,p.eh*60+p.em);
}
function cycleReverse(i){
  var p=_cyclePeriods[i];
  if(!p)return;
  var np={sh:p.eh,sm:p.em,eh:p.sh,em:p.sm};
  if(cycleOverlapIdx(np,i)>=0){cycleMsg('反向后与其他时段重叠');return;}
  fetch('/api/cycle?idx='+i+'&sh='+np.sh+'&sm='+np.sm+'&eh='+np.eh+'&em='+np.em)
    .then(function(r){return r.json()})
    .then(function(d){
      if(d.ok)update();
      else cycleMsg('保存失败');
    })
    .catch(function(){cycleMsg('网络错误');update();});
}
function cycleOpenEdit(i,s,e){
  _cycleEditing=i;
  document.getElementById('cycleEditTitle').textContent=(i<0)?'添加时段':'编辑时段 '+(i+1);
  document.getElementById('ceStart').value=pad2(Math.floor(s/60))+':'+pad2(s%60);
  document.getElementById('ceEnd').value=pad2(Math.floor(e/60))+':'+pad2(e%60);
  document.getElementById('cycleEditDelBtn').style.display=(i<0)?'none':'block';
  var err=document.getElementById('cycleEditErr');
  err.style.display='none';
  err.textContent='';
  document.getElementById('cycleEditM').style.display='flex';
}
function cycleEditErr(t){
  var err=document.getElementById('cycleEditErr');
  if(!err)return;
  err.textContent=t;
  err.style.display='block';
}
function cycleEditCancel(){
  document.getElementById('cycleEditM').style.display='none';
}
function cycleEditSave(){
  var sV=document.getElementById('ceStart').value;
  var eV=document.getElementById('ceEnd').value;
  if(!sV||!eV){cycleEditErr('请选择开始和结束时间');return;}
  var st=sV.split(':'),et=eV.split(':');
  var p={sh:parseInt(st[0],10),sm:parseInt(st[1],10),eh:parseInt(et[0],10),em:parseInt(et[1],10)};
  if(isNaN(p.sh)||isNaN(p.sm)||isNaN(p.eh)||isNaN(p.em)){cycleEditErr('时间格式无效');return;}
  if(p.sh*60+p.sm===p.eh*60+p.em){cycleEditErr('开始与结束时间不能相同');return;}
  var ov=cycleOverlapIdx(p,_cycleEditing);
  if(ov>=0){cycleEditErr('与时段 '+(ov+1)+' 重叠，请调整');return;}
  var url;
  if(_cycleEditing<0){
    if(_cyclePeriods.length>=CYCLE_MAX){cycleEditErr('最多 '+CYCLE_MAX+' 个时段');return;}
    url='/api/cycle?add=1&sh='+p.sh+'&sm='+p.sm+'&eh='+p.eh+'&em='+p.em;
  }else{
    url='/api/cycle?idx='+_cycleEditing+'&sh='+p.sh+'&sm='+p.sm+'&eh='+p.eh+'&em='+p.em;
  }
  fetch(url).then(function(r){return r.json()}).then(function(d){
    if(d.ok){document.getElementById('cycleEditM').style.display='none';update();}
    else{cycleEditErr('保存失败'+(d.error?(':'+d.error):''));}
  }).catch(function(){cycleEditErr('网络错误，请重试');});
}
function cycleEditDelete(){
  if(_cycleEditing<0)return;
  fetch('/api/cycle?remove='+_cycleEditing).then(function(r){return r.json()}).then(function(d){
    document.getElementById('cycleEditM').style.display='none';
    if(d.ok)update();
  }).catch(function(){document.getElementById('cycleEditM').style.display='none';});
}
function removeCyclePeriod(idx){
  fetch('/api/cycle?remove='+idx).then(function(r){return r.json()}).then(function(d){
    if(d.ok)update();
  });
}
function cyclePowerBtnRender(en){
  var pb=document.getElementById('cyclePowerBtn');
  if(!pb)return;
  if(en){
    pb.textContent='关闭循环';
    pb.style.background='#34c759';
    pb.style.color='#fff';
  }else{
    pb.textContent='启用循环';
    pb.style.background='#007aff';
    pb.style.color='#fff';
  }
}
function toggleCycle(){
  var en=!_cycleEnabled;
  if(en && _cycleCount===0){
    cycleMsg('请先添加循环时段');
    return;
  }
  fetch('/api/cycle?enabled='+en).then(function(r){return r.json()}).then(function(d){
    cyclePowerBtnRender(!!d.enabled);
    update();
  });
}
function renderCyclePeriods(){
  var list=document.getElementById('cyclePeriodsList');
  if(!list)return;
  _cycleCount=_cyclePeriods.length;
  list.innerHTML='';
  if(!_cyclePeriods.length){
    var em=document.createElement('div');
    em.className='cycle-empty';
    em.textContent='暂无时段';
    list.appendChild(em);
    return;
  }
  for(var i=0;i<_cyclePeriods.length;i++){
    var p=_cyclePeriods[i];
    var col=CYCLE_COLORS[i%CYCLE_COLORS.length];
    var cross=(p.sh*60+p.sm>p.eh*60+p.em);
    var div=document.createElement('div');
    div.className='cycle-row';
    div.innerHTML='<span class="period-dot" style="background:'+col+'"></span>'
      +'<span style="flex:1;font-size:15px;font-weight:500;letter-spacing:.2px">'+pad2(p.sh)+':'+pad2(p.sm)+' – '+pad2(p.eh)+':'+pad2(p.em)+'</span>'
      +(cross?'<span class="cycle-badge cycle-badge-tap" onclick="cycleReverse('+i+')" title="点击反向选择时段">↕ 跨天</span>':'')
      +'<button class="cycle-txt-btn" style="color:#007aff" onclick="cycleEditIdx('+i+')">编辑</button>'
      +'<button class="cycle-txt-btn" style="color:#ff3b30" onclick="removeCyclePeriod('+i+')">删除</button>';
    list.appendChild(div);
  }
}
function updateCycleStatus(d){
  var cs=document.getElementById('cycleStatus');
  if(!cs)return;
  if(!d.ce){
    cs.textContent='未启用';
    cs.style.color='#8e8e93';
    return;
  }
  if(_cycleCount===0){
    cs.textContent='已启用 · 未配置时段';
    cs.style.color='#ff9500';
    return;
  }
  if(!d.tv){
    cs.textContent='已启用 · 等待NTP时间同步';
    cs.style.color='#ff9500';
    return;
  }
  var p=cyclePeriodForMinute(_cyclePeriods,d.cm);
  if(p){
    cs.textContent='运行中 · '+pad2(p.sh)+':'+pad2(p.sm)+'-'+pad2(p.eh)+':'+pad2(p.em);
    cs.style.color='#34c759';
  }else{
    cs.textContent='已启用 · 当前为关闭时段';
    cs.style.color='#007aff';
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

// ============= 电表校准 =============
function _calibBusy(btn){
  if(btn){btn.disabled=true;btn.textContent='处理中...';}
}
function _calibDone(btn,text){
  if(btn){btn.disabled=false;btn.textContent=text;}
}
function doCalibV(){
  var v=parseFloat(document.getElementById('calibV').value);
  if(!v||v<=0||v>300){alert('请输入有效电压（1-300V）');return;}
  var btn=event.target;_calibBusy(btn);
  fetch('/api/meter_calib?a=v&v='+v).then(function(r){return r.json()})
  .then(function(d){
    alert(d.ok?'电压校准成功':'电压校准失败：'+(d.error||'未知错误'));
    _calibDone(btn,'校准');
  }).catch(function(e){alert('请求失败:'+e);_calibDone(btn,'校准');});
}
function doCalibI(){
  var i=parseFloat(document.getElementById('calibI').value);
  if(!i||i<=0||i>100){alert('请输入有效电流（0.001-100A）');return;}
  var btn=event.target;_calibBusy(btn);
  fetch('/api/meter_calib?a=i&v='+i).then(function(r){return r.json()})
  .then(function(d){
    alert(d.ok?'电流校准成功':'电流校准失败：'+(d.error||'未知错误'));
    _calibDone(btn,'校准');
  }).catch(function(e){alert('请求失败:'+e);_calibDone(btn,'校准');});
}
function doResetCalib(){
  if(!confirm('确认恢复出厂校准？这将覆盖当前校准值。'))return;
  var btn=event.target;_calibBusy(btn);
  fetch('/api/meter_calib?a=reset').then(function(r){return r.json()})
  .then(function(d){
    alert(d.ok?'已恢复出厂校准':'恢复失败：'+(d.error||'未知错误'));
    _calibDone(btn,'恢复出厂校准');
  }).catch(function(e){alert('请求失败:'+e);_calibDone(btn,'恢复出厂校准');});
}
function doSaveCalib(){
  var btn=event.target;_calibBusy(btn);
  fetch('/api/meter_calib?a=save').then(function(r){return r.json()})
  .then(function(d){
    alert(d.ok?'校准已保存到 Flash':'保存失败：'+(d.error||'未知错误'));
    _calibDone(btn,'保存校准到 Flash');
  }).catch(function(e){alert('请求失败:'+e);_calibDone(btn,'保存校准到 Flash');});
}
function doClearEnergy(){
  if(!confirm('确认清零电能计数器？此操作不可恢复。'))return;
  var btn=event.target;_calibBusy(btn);
  fetch('/api/meter_calib?a=clear').then(function(r){return r.json()})
  .then(function(d){
    alert(d.ok?'电能计数器已清零':'清零失败：'+(d.error||'未知错误'));
    _calibDone(btn,'清零电能计数器');
  }).catch(function(e){alert('请求失败:'+e);_calibDone(btn,'清零电能计数器');});
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

function onBillingModeChange(){
  var mode=document.getElementById('billingMode').value;
  document.getElementById('billingEnergyRow').style.display=mode=='0'?'flex':'none';
  document.getElementById('billingMoneyRow').style.display=mode=='1'?'flex':'none';
  document.getElementById('billingTimeRow').style.display=mode=='2'?'flex':'none';
}

function toggleBilling(){
  var bt=document.getElementById('billingToggle');
  if(!bt)return;
  var en=!bt.classList.contains('on');
  fetch('/api/billing?enabled='+en).then(function(r){return r.json()}).then(function(d){
    if(d.enabled)bt.classList.add('on');else bt.classList.remove('on');
    if(d.used !== undefined) document.getElementById('billingUsed').textContent=d.used.toFixed(2)+'度';
    if(d.usedMoney !== undefined) document.getElementById('billingUsedMoney').textContent='¥'+d.usedMoney.toFixed(2);
    if(d.usedTime !== undefined) document.getElementById('billingUsedTime').textContent=d.usedTime+'分钟';
  });
}

function saveBilling(){
  var mode=document.getElementById('billingMode').value;
  var qs='mode='+mode;
  if(mode=='0'){
    qs+='&threshold='+document.getElementById('billingThresh').value;
  }else if(mode=='1'){
    qs+='&money='+document.getElementById('billingMoney').value;
  }else if(mode=='2'){
    qs+='&time='+document.getElementById('billingTime').value;
  }
  fetch('/api/billing?'+qs).then(function(r){return r.json()}).then(function(d){
    alert('保存成功');
  });
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
    if(lo && !lo.classList.contains('hide')){
      console.log('[boot] loading overlay force-hidden after 3s, kick-starting update loop');
      lo.classList.add('hide');
      scheduleUpdate();
    }
  },3000);
});
var _updateTimer=null;
function scheduleUpdate(){
  if(_updateTimer)clearTimeout(_updateTimer);
  _updateTimer=setTimeout(function(){update()},3000);
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
        WiFiManager::boostTxPower();  // P1: 页面访问时临时提升功率
        server_->sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
        server_->sendHeader("Pragma", "no-cache");
        server_->sendHeader("Expires", "-1");
        server_->setContentLength(sizeof(INDEX_HTML) - 1);
        server_->send(200, "text/html; charset=UTF-8", "");
        server_->sendContent_P(INDEX_HTML, sizeof(INDEX_HTML) - 1);
        // 发送完成后让出 1ms，让 lwIP/TCP 栈有机会把数据真正推出去，
        // 避免在弱信号或慢客户端场景下因缓冲区未排空导致页面空白/截断。
        delay(1);
    });

    // iOS Captive Portal 检测端点：返回 200 HTML 页面并自动跳转到配置页。
    // 若返回 "Success" 或 302 重定向，iOS 某些版本会判定已联网而不弹出门户。
    server_->on("/hotspot-detect.html", HTTP_GET, []() {
        server_->sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
        server_->sendHeader("Pragma", "no-cache");
        server_->sendHeader("Expires", "-1");
        String portalUrl = String("http://") + WiFi.softAPIP().toString() + "/";
        String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'>";
        html += "<meta http-equiv='refresh' content='0; url=" + portalUrl + "'>";
        html += "<title>WiFi 配置</title></head><body>";
        html += "<p style='font-family:-apple-system,sans-serif;text-align:center;padding-top:40px'>";
        html += "请打开 <a href='" + portalUrl + "'>配置页面</a>";
        html += "</p></body></html>";
        server_->send(200, "text/html; charset=UTF-8", html);
    });

    // Android Captive Portal 检测：期望 204，返回 302 让 Android 判定为门户并弹窗
    // 覆盖 clients3.google.com / connectivitycheck.gstatic.com 等变体的探测路径
    server_->on("/generate_204", HTTP_GET, []() {
        String portalUrl = String("http://") + WiFi.softAPIP().toString() + "/";
        server_->sendHeader("Location", portalUrl, true);
        server_->send(302, "text/plain", "");
    });
    server_->on("/gen_204", HTTP_GET, []() {
        String portalUrl = String("http://") + WiFi.softAPIP().toString() + "/";
        server_->sendHeader("Location", portalUrl, true);
        server_->send(302, "text/plain", "");
    });

    // Windows Captive Portal 检测：msftconnecttest.com/connecttest.txt 与 /redirect
    server_->on("/connecttest.txt", HTTP_GET, []() {
        String portalUrl = String("http://") + WiFi.softAPIP().toString() + "/";
        server_->sendHeader("Location", portalUrl, true);
        server_->send(302, "text/plain", "");
    });
    server_->on("/redirect", HTTP_GET, []() {
        String portalUrl = String("http://") + WiFi.softAPIP().toString() + "/";
        server_->sendHeader("Location", portalUrl, true);
        server_->send(302, "text/plain", "");
    });

    server_->on("/api/status", HTTP_GET, []() {
        WiFiManager::boostTxPower();  // P1: 状态查询时临时提升功率
        String ip = (WiFi.status() == WL_CONNECTED) ? WiFi.localIP().toString() : WiFi.softAPIP().toString();
        uint8_t sh, sm, eh, em;
        GPIOManager::getCycleTime(sh, sm, eh, em);
        MQTTConfig mq = MQTTManager::getConfig();
        String apPass = WiFiManager::getAPPassword();

        time_t nowTs = time(nullptr);
        struct tm* tinfo = localtime(&nowTs);
        bool timeValid = (tinfo && tinfo->tm_year > 100);
        int curMins = timeValid ? (tinfo->tm_hour * 60 + tinfo->tm_min) : -1;

        snprintf(status_buf, sizeof(status_buf),
            "{\"conn\":%s,\"ip\":\"%s\",\"ssid\":\"%s\",\"rssi\":%d,"
            "\"ver\":\"%s\",\"up\":%lu,\"m\":%s,\"s\":%s,"
            "\"lk\":%s,\"te\":%s,\"tr\":%s,\"td\":%d,\"tl\":%lu,"
            "\"v\":%.1f,\"i\":%.3f,\"p\":%.2f,\"e\":%.2f,\"me\":%s,\"rl\":%s,"
            "\"ce\":%s,\"sh\":%d,\"sm\":%d,\"eh\":%d,\"em\":%d,\"cpc\":%d,\"ca\":%s,\"tv\":%s,\"cm\":%d,"
            "\"pe\":%s,\"pt\":%.2f,"
            "\"be\":%s,\"bt\":%.2f,\"bm\":%.2f,\"btt\":%d,\"bmo\":%d,\"bu\":%.2f,\"bum\":%.2f,\"but\":%u,\"bsr\":%d,\"ep\":%.2f,\"mo\":%.2f,\"lm\":%.2f,"
            "\"de\":%s,\"dt\":\"%s\",\"dl\":%s,\"dr\":%d,\"dm\":\"%s\",\"dp\":%s,\"pc\":%s,\"ds\":%d,\"ts\":%d,\"om\":%s,\"bd\":%s,"
            "\"mqe\":%s,\"mqs\":\"%s\",\"mqp\":%d,\"mqu\":\"%s\",\"mqpw\":\"%s\","
            "\"ap\":\"%s\",\"aps\":\"%s\",\"host\":\"%s\",\"cv\":%d,\"bat\":%s,\"acap\":%s,\"wtpl\":%s,\"syslog\":%s,\"rst\":\"%s\"}",
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
            GPIOManager::isCycleActive() ? "true" : "false",
            timeValid ? "true" : "false",
            curMins,
            GPIOManager::isPowerOffEnabled() ? "true" : "false",
            GPIOManager::getPowerOffThreshold(),
            GPIOManager::isBillingEnabled() ? "true" : "false",
            GPIOManager::getBillingThreshold(),
            GPIOManager::getBillingThresholdMoney(),
            GPIOManager::getBillingThresholdTime(),
            (int)GPIOManager::getBillingMode(),
            GPIOManager::getBillingUsedEnergy() / 1000.0f,
            GPIOManager::getBillingUsedMoney(),
            GPIOManager::getBillingUsedMinutes(),
            (int)GPIOManager::getBillingStopReason(),
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
            WiFiManager::getHostname().c_str(),
            EEPROM.read(EEPROM_CARD_VISIBILITY_ADDR),
            GPIOManager::isButtonAutoTimerEnabled() ? "true" : "false",
            WiFiManager::isAutoCloseAPEnabled() ? "true" : "false",
            WiFiManager::isWiFiTxPowerLimited() ? "true" : "false",
            log_buffer_is_enabled() ? "true" : "false",
            ESP.getResetReason().c_str()
        );
        int len = strlen(status_buf);
        uint8_t cpCount = GPIOManager::getCyclePeriodCount();
        if (len > 0 && status_buf[len-1] == '}') {
            status_buf[--len] = '\0';
            len += snprintf(status_buf + len, sizeof(status_buf) - len, ",\"cp\":[");
            for (uint8_t i = 0; i < cpCount && len < (int)sizeof(status_buf) - 40; i++) {
                uint8_t psh,psm,peh,pem;
                if (GPIOManager::getCyclePeriod(i, psh, psm, peh, pem)) {
                    if (i > 0 && len < (int)sizeof(status_buf) - 1) status_buf[len++] = ',';
                    len += snprintf(status_buf + len, sizeof(status_buf) - len,
                        "{\"sh\":%d,\"sm\":%d,\"eh\":%d,\"em\":%d}", psh, psm, peh, pem);
                }
            }
            if (len < (int)sizeof(status_buf) - 20) {
                len += snprintf(status_buf + len, sizeof(status_buf) - len, "],\"card_order\":[");
                for (uint8_t i = 0; i < 6 && len < (int)sizeof(status_buf) - 10; i++) {
                    if (i > 0 && len < (int)sizeof(status_buf) - 1) status_buf[len++] = ',';
                    uint8_t v = EEPROM.read(EEPROM_CARD_ORDER_ADDR + i);
                    if (v > 5) v = i;
                    len += snprintf(status_buf + len, sizeof(status_buf) - len, "%d", v);
                }
                if (len < (int)sizeof(status_buf) - 2) {
                    len += snprintf(status_buf + len, sizeof(status_buf) - len, "]}");
                }
            }
        }
        server_->send(200, "application/json", status_buf);
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
            bool newState = !GPIOManager::getRelayMaster();
            GPIOManager::setRelayMaster(newState);
            // 关闭主继电器时，同时关闭从继电器
            if (!newState) {
                GPIOManager::setRelaySlave(false);
            }
        } else if (w == "s") {
            bool newState = !GPIOManager::getRelaySlave();
            GPIOManager::setRelaySlave(newState);
            // 开启从继电器时，同时开启主继电器
            if (newState) {
                GPIOManager::setRelayMaster(true);
            }
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
            if (sh > 23 || sm > 59 || eh > 23 || em > 59 || (sh == eh && sm == em)) {
                server_->send(200, "application/json", "{\"ok\":false,\"error\":\"invalid period\"}");
                return;
            }
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
            if (sh > 23 || sm > 59 || eh > 23 || em > 59 || (sh == eh && sm == em)) {
                server_->send(200, "application/json", "{\"ok\":false,\"error\":\"invalid period\"}");
                return;
            }
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
            conf.server[sizeof(conf.server)-1] = '\0';
            conf.port = (uint16_t)server_->arg("port").toInt();
            strncpy(conf.username, server_->arg("user").c_str(), sizeof(conf.username)-1);
            conf.username[sizeof(conf.username)-1] = '\0';
            strncpy(conf.password, server_->arg("pass").c_str(), sizeof(conf.password)-1);
            conf.password[sizeof(conf.password)-1] = '\0';
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
            uint16_t mins = duration.toInt();
            GPIOManager::setTimerDuration(mins);
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
        uint8_t order[6] = {0,1,2,3,4,5};
        for(int i=0;i<6;i++) order[i] = EEPROM.read(EEPROM_CARD_ORDER_ADDR+i);
        bool dup = false;
        for(int i=0;i<6 && !dup;i++) for(int j=i+1;j<6;j++) if(order[i]==order[j]) dup=true;
        if(order[0]>5||order[1]>5||order[2]>5||order[3]>5||order[4]>5||order[5]>5||dup){
            for(int i=0;i<6;i++) order[i]=i;
        }
        String json = "{\"order\":[";
        for(int i=0;i<6;i++){if(i>0)json+=',';json+=String(order[i]);}
        json += "]}";
        server_->send(200, "application/json", json);
    });

    server_->on("/api/card_order", HTTP_POST, []() {
        String body = server_->arg("plain");
        int s=body.indexOf('['), e=body.indexOf(']');
        if(s>=0 && e>s){
            String arr = body.substring(s+1, e);
            for(int idx=0; idx<6; idx++){
                int c=arr.indexOf(',');
                String num = (c>=0)?arr.substring(0,c):arr;
                int v = num.toInt();
                if(v<0||v>5) v=idx;
                EEPROM.write(EEPROM_CARD_ORDER_ADDR+idx, (uint8_t)v);
                if(c<0) break;
                arr = arr.substring(c+1);
            }
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

    server_->on("/api/hostname", HTTP_GET, []() {
        String json = "{\"hostname\":\"" + WiFiManager::getHostname() + "\"}";
        server_->send(200, "application/json", json);
    });

    server_->on("/api/hostname", HTTP_POST, []() {
        String body = server_->arg("plain");
        String hostname;
        int idx = body.indexOf("\"hostname\"");
        if (idx >= 0) {
            int colon = body.indexOf(':', idx);
            int q1 = body.indexOf('\"', colon);
            int q2 = body.indexOf('\"', q1 + 1);
            if (q1 >= 0 && q2 > q1) {
                hostname = body.substring(q1 + 1, q2);
            }
        }
        hostname.trim();
        hostname.toLowerCase();
        if (WiFiManager::setHostname(hostname.c_str())) {
            server_->send(200, "application/json", "{\"ok\":true}");
        } else {
            server_->send(400, "application/json", "{\"ok\":false,\"error\":\"invalid hostname\"}");
        }
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

    // STA 连接后自动关闭 AP 开关
    server_->on("/api/auto_close_ap", HTTP_GET, []() {
        server_->send(200, "application/json",
            String("{\"enabled\":") + (WiFiManager::isAutoCloseAPEnabled() ? "true" : "false") + "}");
    });

    server_->on("/api/auto_close_ap", HTTP_POST, []() {
        if (!server_->hasArg("plain")) {
            server_->send(400, "application/json", "{\"ok\":false}");
            return;
        }
        String body = server_->arg("plain");
        bool enabled = (body.indexOf("\"enabled\":true") >= 0) || (body.indexOf("\"enabled\":1") >= 0);
        WiFiManager::setAutoCloseAP(enabled);
        server_->send(200, "application/json",
            String("{\"ok\":true,\"enabled\":") + (enabled ? "true" : "false") + "}");
    });

    // WiFi 发射功率限制开关
    server_->on("/api/wifi_tx_power", HTTP_GET, []() {
        server_->send(200, "application/json",
            String("{\"enabled\":") + (WiFiManager::isWiFiTxPowerLimited() ? "true" : "false") + "}");
    });

    server_->on("/api/wifi_tx_power", HTTP_POST, []() {
        if (!server_->hasArg("plain")) {
            server_->send(400, "application/json", "{\"ok\":false}");
            return;
        }
        String body = server_->arg("plain");
        bool enabled = (body.indexOf("\"enabled\":true") >= 0) || (body.indexOf("\"enabled\":1") >= 0);
        WiFiManager::setWiFiTxPowerLimited(enabled);
        server_->send(200, "application/json",
            String("{\"ok\":true,\"enabled\":") + (enabled ? "true" : "false") + "}");
    });

    // 系统日志开关
    server_->on("/api/sys_log", HTTP_GET, []() {
        server_->send(200, "application/json",
            String("{\"enabled\":") + (log_buffer_is_enabled() ? "true" : "false") + "}");
    });

    server_->on("/api/sys_log", HTTP_POST, []() {
        if (!server_->hasArg("plain")) {
            server_->send(400, "application/json", "{\"ok\":false}");
            return;
        }
        String body = server_->arg("plain");
        bool enabled = (body.indexOf("\"enabled\":true") >= 0) || (body.indexOf("\"enabled\":1") >= 0);
        log_buffer_set_enabled(enabled);
        server_->send(200, "application/json",
            String("{\"ok\":true,\"enabled\":") + (enabled ? "true" : "false") + "}");
    });

    server_->on("/api/restart", HTTP_POST, []() {
        server_->send(200, "application/json", "{\"ok\":true}");
        delay(100);
        ESP.restart();
    });

    server_->on("/api/reset", HTTP_POST, []() {
        for (int i = 0; i < 1024; i++) {
            if (i == EEPROM_SY7T609_ENABLED_ADDR) {
                EEPROM.write(i, 1); // 电量检测默认开启
            } else {
                EEPROM.write(i, 0);
            }
        }
        EEPROM.commit();
        // 重新初始化默认配置，确保各模块状态一致
        GPIOManager::reset();
        WiFiManager::reset();
        EnergyManager::reset();
        server_->send(200, "application/json", "{\"ok\":true}");
        delay(100);
        ESP.restart();
    });

    server_->on("/api/log", HTTP_GET, []() {
        String json = "[";
        int count = log_buffer_count();
        for (int i = 0; i < count; i++) {
            if (i > 0) json += ",";
            json += "\"";
            const char* line = log_buffer_get_line(i);
            for (int j = 0; line[j]; j++) {
                char c = line[j];
                if (c == '"' || c == '\\') json += '\\';
                if (c == '\n') {
                    json += "\\n";
                } else if (c == '\r') {
                    json += "\\r";
                } else if ((unsigned char)c >= 32 && (unsigned char)c <= 126) {
                    json += c;
                } else {
                    json += '?';
                }
            }
            json += "\"";
        }
        json += "]";
        server_->send(200, "application/json", json);
    });

    server_->on("/api/crash_log", HTTP_GET, []() {
        char crash_logs[CRASH_LOG_MAX_LINES][CRASH_LOG_LINE_LEN];
        int crash_count = 0;
        bool has_crash = log_buffer_load_crash_logs(crash_logs, CRASH_LOG_MAX_LINES, &crash_count);
        String json = "{";
        json += "\"reason\":\"";
        String reason = ESP.getResetReason();
        for (size_t i = 0; i < reason.length(); i++) {
            char c = reason[i];
            if (c == '"' || c == '\\') json += '\\';
            if (c >= 32 && c <= 126) json += c; else json += '?';
        }
        json += "\",\"logs\":[";
        if (has_crash) {
            for (int i = 0; i < crash_count; i++) {
                if (i > 0) json += ",";
                json += "\"";
                for (int j = 0; crash_logs[i][j]; j++) {
                    char c = crash_logs[i][j];
                    if (c == '"' || c == '\\') json += '\\';
                    if (c == '\n') {
                        json += "\\n";
                    } else if (c == '\r') {
                        json += "\\r";
                    } else if ((unsigned char)c >= 32 && (unsigned char)c <= 126) {
                        json += c;
                    } else {
                        json += '?';
                    }
                }
                json += "\"";
            }
        }
        json += "]}";
        server_->send(200, "application/json", json);
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

    // 电表校准 API（P1）
    // a=v&v=220.0   电压自动校准
    // a=i&v=4.545   电流自动校准
    // a=reset       恢复出厂校准
    // a=save        保存当前寄存器到芯片 flash
    // a=clear       清零电能计数器
    server_->on("/api/meter_calib", HTTP_GET, []() {
        String action = server_->arg("a");
        String response;
        if (SY7T609::isFlashMode()) {
            response = "{\"ok\":false,\"error\":\"flash mode\"}";
        } else if (!SY7T609::isReady()) {
            response = "{\"ok\":false,\"error\":\"meter not ready\"}";
        } else if (action == "v") {
            float v = server_->arg("v").toFloat();
            bool ok = SY7T609::calibrateVoltage(v);
            response = "{\"ok\":" + String(ok ? "true" : "false") + ",\"action\":\"voltage\"}";
        } else if (action == "i") {
            float i = server_->arg("v").toFloat();
            bool ok = SY7T609::calibrateCurrent(i);
            response = "{\"ok\":" + String(ok ? "true" : "false") + ",\"action\":\"current\"}";
        } else if (action == "reset") {
            bool ok = SY7T609::resetCalibration();
            response = "{\"ok\":" + String(ok ? "true" : "false") + ",\"action\":\"reset\"}";
        } else if (action == "save") {
            bool ok = SY7T609::saveCalibration();
            response = "{\"ok\":" + String(ok ? "true" : "false") + ",\"action\":\"save\"}";
        } else if (action == "clear") {
            bool ok = SY7T609::clearEnergyCounter();
            response = "{\"ok\":" + String(ok ? "true" : "false") + ",\"action\":\"clear\"}";
        } else {
            response = "{\"ok\":false,\"error\":\"invalid action\"}";
        }
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
        if (server_->hasArg("money")) {
            float money = server_->arg("money").toFloat();
            GPIOManager::setBillingThresholdMoney(money);
        }
        if (server_->hasArg("time")) {
            int minutes = server_->arg("time").toInt();
            GPIOManager::setBillingThresholdTime((uint16_t)minutes);
        }
        if (server_->hasArg("mode")) {
            int mode = server_->arg("mode").toInt();
            if (mode >= 0 && mode <= 2) {
                GPIOManager::setBillingMode((GPIOManager::BillingMode)mode);
            }
        }
        char buf[256];
        snprintf(buf, sizeof(buf),
            "{\"enabled\":%s,\"mode\":%d,\"threshold\":%.2f,\"money\":%.2f,\"time\":%d,\"used\":%.2f,\"usedMoney\":%.2f,\"usedTime\":%u,\"stopReason\":%d,\"startEnergy\":%.2f,\"price\":%.2f}",
            GPIOManager::isBillingEnabled() ? "true" : "false",
            (int)GPIOManager::getBillingMode(),
            GPIOManager::getBillingThreshold(),
            GPIOManager::getBillingThresholdMoney(),
            GPIOManager::getBillingThresholdTime(),
            GPIOManager::getBillingUsedEnergy() / 1000.0f,
            GPIOManager::getBillingUsedMoney(),
            GPIOManager::getBillingUsedMinutes(),
            (int)GPIOManager::getBillingStopReason(),
            GPIOManager::getBillingStartEnergy() / 1000.0f,
            GPIOManager::getEnergyPrice());
        server_->send(200, "application/json", buf);
    });

    server_->on("/api/billing", HTTP_POST, []() {
        if (server_->hasArg("price")) {
            float price = server_->arg("price").toFloat();
            if (price > 0 && price < 10) {
                GPIOManager::setEnergyPrice(price);
            }
        }
        if (server_->hasArg("mode")) {
            int mode = server_->arg("mode").toInt();
            if (mode >= 0 && mode <= 2) {
                GPIOManager::setBillingMode((GPIOManager::BillingMode)mode);
            }
        }
        if (server_->hasArg("money")) {
            float money = server_->arg("money").toFloat();
            GPIOManager::setBillingThresholdMoney(money);
        }
        if (server_->hasArg("time")) {
            int minutes = server_->arg("time").toInt();
            GPIOManager::setBillingThresholdTime((uint16_t)minutes);
        }
        if (server_->hasArg("threshold")) {
            float th = server_->arg("threshold").toFloat();
            GPIOManager::setBillingThreshold(th);
        }
        server_->send(200, "application/json", "{\"ok\":true}");
    });

    server_->on("/api/billing/history", HTTP_GET, []() {
        String json = "{\"ok\":true,\"history\":[";
        uint8_t count = GPIOManager::getBillingHistoryCount();
        for (uint8_t i = 0; i < count; i++) {
            GPIOManager::BillingRecord rec = GPIOManager::getBillingHistory(i);
            char item[128];
            snprintf(item, sizeof(item),
                "{\"startTime\":%u,\"usedEnergy\":%.2f,\"cost\":%.2f}%s",
                rec.startTime, rec.usedEnergyWh, rec.costCents / 100.0f,
                (i < count - 1) ? "," : "");
            json += item;
        }
        json += "]}";
        server_->send(200, "application/json", json);
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

    // 扫描 WiFi 网络（改为异步扫描并在等待期间喂狗，避免同步阻塞触发 Hardware Watchdog）
    server_->on("/api/scan", HTTP_GET, []() {
        int8_t scanStatus = WiFi.scanComplete();
        if (scanStatus == WIFI_SCAN_RUNNING) {
            // 已有扫描在运行，等待其完成
            unsigned long scanStart = millis();
            while (WiFi.scanComplete() == WIFI_SCAN_RUNNING) {
                yield();
                ESP.wdtFeed();
                if (millis() - scanStart > 10000) {
                    server_->send(200, "application/json", "[]");
                    return;
                }
            }
            scanStatus = WiFi.scanComplete();
        } else if (scanStatus < 0) {
            // 启动新异步扫描
            WiFi.scanNetworks(true, true);
            unsigned long scanStart = millis();
            while (WiFi.scanComplete() == WIFI_SCAN_RUNNING) {
                yield();
                ESP.wdtFeed();
                if (millis() - scanStart > 10000) {
                    WiFi.scanDelete();
                    server_->send(200, "application/json", "[]");
                    return;
                }
            }
            scanStatus = WiFi.scanComplete();
        }

        if (scanStatus < 0) {
            WiFi.scanDelete();
            server_->send(200, "application/json", "[]");
            return;
        }

        int n = scanStatus;
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
        // 计费会话和历史记录同时清零
        GPIOManager::setBillingStartEnergy(0);
        EEPROM.write(EEPROM_BILLING_HISTORY_COUNT_ADDR, 0);
        for (int i = 0; i < EEPROM_BILLING_HISTORY_MAX * EEPROM_BILLING_RECORD_SIZE; i++) {
            EEPROM.write(EEPROM_BILLING_HISTORY_ADDR + i, 0);
        }
        EEPROM.commit();
        server_->send(200, "application/json", "{\"ok\":true}");
    });

    server_->onNotFound([]() {
        // Captive Portal：AP 模式下所有未匹配请求重定向到配置页。
        // 使用 AP IP 而非 .local 主机名，避免 iOS 某些版本因 mDNS 解析未完成而无法弹出门户。
        String host = WiFiManager::getMDNSHostname();
        if (host.length() == 0) host = "power";
        IPAddress apIP = WiFi.softAPIP();
        String portalUrl;
        if (apIP[0] != 0) {
            portalUrl = String("http://") + apIP.toString() + "/";
        } else {
            portalUrl = String("http://") + host + ".local/";
        }
        server_->sendHeader("Location", portalUrl, true);
        server_->send(302, "text/plain", "");
    });

    server_->begin();
    DBG_PRINTF("[Web] Server started on port %d\n", WEB_SERVER_PORT);
    DBG_PRINTF("[OTA] Update endpoint: /update (user:admin pass:admin)\n");
}

void WebConfigServer::handle() {
    // 每次循环处理多个 DNS/Web 请求，降低高并发或慢客户端场景下的超时概率
    if (dnsServer_) {
        for (int i = 0; i < 4; i++) {
            dnsServer_->processNextRequest();
        }
    }
    WiFiManager::updateMDNS();  // Web 请求前让 mDNS 有机会响应查询
    // P0: 标记 Web 请求处理中，主循环检测到此标志时跳过 SY7T609 读取等阻塞操作
    // 50ms 超时保护（在 isWebRequestActive 中实现）防止死锁
    markWebRequestStart();
    for (int i = 0; i < 2; i++) {
        server_->handleClient();
    }
    markWebRequestEnd();
    WiFiManager::updateMDNS();  // Web 请求后再次响应，避免长请求期间 mDNS 查询超时
}

void WebConfigServer::setSaveCallback(void (*callback)(const char*, const char*)) {
    save_callback_ = callback;
}