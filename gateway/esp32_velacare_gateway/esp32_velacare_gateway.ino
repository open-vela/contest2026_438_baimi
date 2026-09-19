#include <Arduino.h>
#include <ESP_I2S.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <WebServer.h>
#include <WiFi.h>
#include <Wire.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <math.h>
#include <time.h>
#include <stdlib.h>

#include "voice_prompts.h"

namespace {

constexpr uint32_t kBaudRate = 115200;
constexpr int kGatewayRxPin = 18;
constexpr int kGatewayTxPin = 17;
constexpr int kWaterSensorPin = 4;
constexpr int kWaterAlarmLevel = LOW;
constexpr int kSmokeSensorPin = 5;
constexpr int kDoorSensorPin = 6;
constexpr int kAudioDataPin = 7;
constexpr int kImuSdaPin = 8;
constexpr int kImuSclPin = 9;
constexpr int kImuIntPin = 10;
constexpr int kAudioBclkPin = 15;
constexpr int kAudioLrcPin = 16;
constexpr uint32_t kAudioSampleRate = 16000;
constexpr uint8_t kImuAddress = 0x68;
constexpr uint8_t kImuWhoAmIRegister = 0x75;
constexpr uint8_t kImuPowerManagementRegister = 0x6B;
constexpr uint8_t kImuConfigRegister = 0x1A;
constexpr uint8_t kImuSampleRateDividerRegister = 0x19;
constexpr uint8_t kImuGyroConfigRegister = 0x1B;
constexpr uint8_t kImuAccelConfigRegister = 0x1C;
constexpr uint8_t kImuAccelConfig2Register = 0x1D;
constexpr uint8_t kImuAccelDataRegister = 0x3B;
constexpr uint8_t kMpu6500WhoAmI = 0x70;
constexpr uint8_t kMpu9250WhoAmI = 0x71;
constexpr int kSmokeAlarmLevel = LOW;
constexpr uint32_t kHeartbeatMs = 1000;
constexpr uint32_t kWaterAlarmConfirmMs = 800;
constexpr uint32_t kWaterRecoveryConfirmMs = 2500;
constexpr uint32_t kSmokeWarmupMs = 60000;
constexpr uint32_t kSmokeAlarmConfirmMs = 1000;
constexpr uint32_t kSmokeRecoveryConfirmMs = 5000;
constexpr uint32_t kDoorDebounceMs = 80;
constexpr uint32_t kImuSamplePeriodMs = 20;
constexpr uint32_t kFallFreeFallTimeoutMs = 700;
constexpr uint32_t kFallPostImpactWindowMs = 1800;
constexpr uint32_t kFallStillnessConfirmMs = 900;
constexpr uint32_t kFallPersistentEscalationMs = 15000;
constexpr uint32_t kGenericPersistentEscalationMs = 30000;
constexpr float kFallFreeFallThresholdG = 0.55f;
constexpr float kFallImpactThresholdG = 2.20f;
constexpr float kFallStableMinG = 0.72f;
constexpr float kFallStableMaxG = 1.28f;
constexpr float kFallStillGyroDps = 45.0f;
constexpr float kFallImpactGyroDps = 90.0f;
constexpr float kFallPostureChangeDeg = 45.0f;
constexpr uint32_t kFallRecoveryConfirmMs = 2500;
constexpr float kFallSafeEnterDeg = 25.0f;
constexpr float kFallSafeExitDeg = 35.0f;
constexpr float kFallKnownPoseDeg = 25.0f;
constexpr float kFallGenericPoseDeg = 50.0f;
constexpr float kFallRecoveryMinG = 0.80f;
constexpr float kFallRecoveryMaxG = 1.20f;
constexpr float kFallRecoveryGyroDps = 20.0f;
constexpr uint32_t kFallRecoveryBadSampleToleranceMs = 250;
constexpr uint32_t kFallKnownPoseConfirmMs = 1800;
// Fixed installation anchors captured from this device on 2026-09-18.
// They are normalized gravity directions, so they survive restart and can no
// longer drift when the module remains in a fallen pose.
constexpr float kSafeAnchorAx = 0.149537f;
constexpr float kSafeAnchorAy = 0.059815f;
constexpr float kSafeAnchorAz = 0.986945f;
constexpr float kFallAnchorAx = -0.957276f;
constexpr float kFallAnchorAy = -0.199432f;
constexpr float kFallAnchorAz = 0.209404f;
constexpr uint32_t kWifiConnectTimeoutMs = 15000;
constexpr char kConfigApPassword[] = "VelaCare2026";
constexpr uint32_t kEventStoreMagic = 0x56434531;
constexpr size_t kEventCapacity = 12;
constexpr time_t kMinimumValidEpoch = 1704067200;
constexpr uint32_t kConfigSyncMs = 5000;
constexpr uint32_t kEventUploadRetryMs = 10000;
constexpr uint32_t kAlarmMuteMs = 10UL * 60UL * 1000UL;
constexpr uint32_t kBuzzerControlSyncMs = 1000;
constexpr size_t kAudioQueueLength = 6;
constexpr size_t kAudioFramesPerBlock = 128;
constexpr uint32_t kAudioTaskStackSize = 6144;
constexpr UBaseType_t kAudioTaskPriority = 2;
constexpr uint32_t kCareEventStoreMagic = 0x56434331;
constexpr size_t kCareEventCapacity = 12;
constexpr uint32_t kCareRequestDedupMs = 5UL * 60UL * 1000UL;
constexpr uint32_t kFamilyNotificationRetryMs = 1500;
constexpr uint32_t kFamilyNotificationStoreMagic = 0x56434631;
constexpr size_t kFamilyNotificationCapacity = 12;
constexpr char kFirmwareVersion[] = "0.15.1";

const char kDashboardPage[] PROGMEM = R"HTML(
<!doctype html><html lang="zh-CN"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>VelaCare 家庭守护</title>
<style>
:root{color-scheme:light;font-family:Inter,system-ui,-apple-system,"Segoe UI","Microsoft YaHei",sans-serif;--ink:#18332b;--muted:#70837c;--green:#0a8f61;--green2:#20b77d;--greenSoft:#e8f7f1;--red:#db4742;--amber:#d99321;--line:#e8efec;--shadow:0 14px 34px rgba(34,78,62,.10)}
*{box-sizing:border-box}html{scroll-behavior:smooth}body{margin:0;min-height:100vh;color:var(--ink);background:radial-gradient(circle at 8% 0,#dff6ed 0,transparent 31%),linear-gradient(180deg,#f8fbfa,#edf5f2);padding-bottom:80px}.app{width:min(1080px,100%);margin:auto;padding:24px}.top{display:flex;align-items:center;justify-content:space-between;margin-bottom:22px}.brandBox{display:flex;align-items:center;gap:12px}.logo{display:grid;place-items:center;width:44px;height:44px;border-radius:14px;color:#fff;background:linear-gradient(145deg,var(--green),var(--green2));box-shadow:0 8px 20px rgba(10,143,97,.25)}.brand{font-size:24px;font-weight:800;letter-spacing:-.5px}.tagline{font-size:13px;color:var(--muted);margin-top:2px}.live{display:flex;align-items:center;gap:8px;padding:9px 13px;border:1px solid #caeade;border-radius:999px;background:rgba(255,255,255,.78);color:#087c55;font-size:14px;font-weight:700;backdrop-filter:blur(8px)}.dot{width:8px;height:8px;border-radius:50%;background:#17b778;box-shadow:0 0 0 5px rgba(23,183,120,.13);animation:pulse 2s infinite}.hero{position:relative;overflow:hidden;display:grid;grid-template-columns:1.25fr .75fr;min-height:246px;border-radius:28px;padding:34px;color:#fff;background:linear-gradient(125deg,#086e50 0,#0c9867 56%,#2ac58c 100%);box-shadow:0 20px 45px rgba(7,111,79,.22)}.hero:before,.hero:after{content:"";position:absolute;border-radius:50%;border:1px solid rgba(255,255,255,.16)}.hero:before{width:260px;height:260px;right:-80px;top:-100px}.hero:after{width:170px;height:170px;right:80px;bottom:-130px}.eyebrow{display:flex;align-items:center;gap:8px;font-size:13px;font-weight:700;letter-spacing:1.5px;opacity:.8}.hero h1{font-size:38px;line-height:1.12;margin:18px 0 10px;letter-spacing:-1px}.hero p{margin:0;max-width:520px;line-height:1.65;color:rgba(255,255,255,.83)}.heroFoot{display:flex;gap:20px;margin-top:25px;font-size:13px;color:rgba(255,255,255,.75)}.heroArt{display:grid;place-items:center;z-index:1}.shield{display:grid;place-items:center;width:152px;height:152px;border-radius:50%;background:rgba(255,255,255,.14);border:1px solid rgba(255,255,255,.24);box-shadow:inset 0 0 35px rgba(255,255,255,.08)}.shield svg{width:76px;height:76px}.sectionHead{display:flex;align-items:end;justify-content:space-between;margin:30px 2px 15px}.sectionHead h2{font-size:20px;margin:0}.sectionHead span{font-size:13px;color:var(--muted)}.metrics{display:grid;grid-template-columns:repeat(3,1fr);gap:14px}.metric{display:flex;align-items:center;gap:14px;padding:18px;background:rgba(255,255,255,.86);border:1px solid rgba(255,255,255,.9);border-radius:19px;box-shadow:0 8px 22px rgba(36,73,60,.07)}.metricIcon{display:grid;place-items:center;flex:0 0 42px;height:42px;border-radius:13px;background:var(--greenSoft);color:var(--green)}.metricIcon svg{width:22px;height:22px}.metricLabel{font-size:12px;color:var(--muted)}.metricValue{margin-top:3px;font-size:20px;font-weight:800}.devices{display:grid;grid-template-columns:repeat(4,1fr);gap:14px}.device{position:relative;overflow:hidden;background:#fff;border:1px solid var(--line);border-radius:22px;padding:20px;box-shadow:var(--shadow);transition:.25s transform,.25s box-shadow}.device:hover{transform:translateY(-3px);box-shadow:0 18px 38px rgba(34,78,62,.15)}.deviceTop{display:flex;align-items:center;justify-content:space-between}.deviceIcon{display:grid;place-items:center;width:46px;height:46px;border-radius:15px;background:#edf8f4;color:var(--green)}.deviceIcon svg{width:25px;height:25px}.stateDot{width:10px;height:10px;border-radius:50%;background:#aab8b3}.deviceName{font-size:14px;color:var(--muted);margin-top:22px}.deviceState{font-size:24px;font-weight:800;margin-top:5px}.deviceHint{font-size:12px;color:#8b9b95;margin-top:5px}.device.normal .stateDot{background:#17b778;box-shadow:0 0 0 5px rgba(23,183,120,.11)}.device.normal .deviceState{color:#087e56}.device.alarm{border-color:#f2c2bf}.device.alarm .deviceIcon{background:#feeeee;color:var(--red)}.device.alarm .stateDot{background:var(--red);box-shadow:0 0 0 5px rgba(219,71,66,.11)}.device.alarm .deviceState{color:var(--red)}.device.offline{border-color:#f1d9ac}.device.offline .deviceIcon{background:#fff6e5;color:var(--amber)}.device.offline .stateDot{background:var(--amber)}.device.offline .deviceState{color:#aa6a08}.lower{display:grid;grid-template-columns:1.25fr .75fr;gap:14px;margin-top:14px}.panel{background:#fff;border:1px solid var(--line);border-radius:22px;padding:21px;box-shadow:var(--shadow)}.panelTitle{display:flex;align-items:center;justify-content:space-between;font-size:16px;font-weight:800;margin-bottom:17px}.panelTitle small{font-size:12px;color:var(--muted);font-weight:500}.event{display:flex;align-items:center;gap:13px;padding:12px 0;border-top:1px solid #eef3f1}.event:first-of-type{border-top:0}.eventMark{display:grid;place-items:center;width:34px;height:34px;border-radius:11px;background:#e8f7f1;color:var(--green);font-weight:900}.eventText{font-size:14px;font-weight:700}.eventSub{font-size:12px;color:var(--muted);margin-top:2px}.connection{display:flex;align-items:center;gap:16px}.signal{position:relative;display:grid;place-items:center;width:74px;height:74px;border-radius:50%;background:conic-gradient(var(--green) 0 85%,#e5eeea 85%)}.signal:after{content:"";position:absolute;inset:7px;border-radius:50%;background:#fff}.signal strong{z-index:1;font-size:12px}.connState{font-size:20px;font-weight:800}.connIp{font-size:13px;color:var(--muted);margin-top:4px}.configure{display:inline-block;margin-top:17px;color:#087e56;text-decoration:none;font-weight:700;font-size:13px}.foot{display:flex;justify-content:space-between;color:#85958f;font-size:12px;margin:20px 3px}.bottomNav{display:none}.dangerHero{background:linear-gradient(125deg,#942f2c,#d84d47)}.warnHero{background:linear-gradient(125deg,#956010,#d99a31)}.errorLive{color:#a72e2b;border-color:#f0cbc9;background:#fff5f4}.errorLive .dot{background:var(--red);box-shadow:0 0 0 5px rgba(219,71,66,.12)}
.historyMeta{font-size:11px;color:var(--muted);font-weight:500}.eventList{max-height:274px;overflow:auto}.eventRow{display:grid;grid-template-columns:38px 1fr auto;align-items:center;gap:12px;padding:12px 0;border-top:1px solid #eef3f1}.eventRow:first-child{border-top:0}.eventBadge{display:grid;place-items:center;width:36px;height:36px;border-radius:12px;background:#e8f7f1;color:var(--green);font-weight:900}.eventRow.active .eventBadge{background:#feeceb;color:var(--red)}.eventRow.interrupted .eventBadge{background:#fff4e0;color:var(--amber)}.eventName{font-size:14px;font-weight:800}.eventInfo{font-size:12px;color:var(--muted);margin-top:3px}.eventOutcome{text-align:right;font-size:12px;font-weight:800;color:#17845c}.eventRow.active .eventOutcome{color:var(--red)}.eventRow.interrupted .eventOutcome{color:var(--amber)}.emptyEvents{padding:28px 8px;text-align:center;color:var(--muted);font-size:13px}.method{margin-top:10px;padding-top:10px;border-top:1px dashed #e1ebe7;color:#87968f;font-size:11px;line-height:1.55}
.careGrid{display:grid;grid-template-columns:repeat(3,1fr);gap:12px}.careRequest{padding:16px;border:1px solid var(--line);border-radius:16px;background:#fbfdfc}.careRequest.active{border-color:#f0c0bc;background:#fff8f7}.careRequestTitle{display:flex;align-items:center;justify-content:space-between;gap:8px;font-size:15px;font-weight:800}.careRequestId{font-size:12px;color:var(--muted);font-weight:700}.careRequestState{margin-top:9px;font-size:18px;font-weight:800}.careRequestHint{margin-top:4px;font-size:12px;color:var(--muted);line-height:1.55}.careReplies{display:none;flex-wrap:wrap;gap:7px;margin-top:12px}.careRequest.active .careReplies{display:flex}.careReply{padding:8px 10px;border:0;border-radius:9px;background:#e8f7f1;color:#087e56;font-size:12px;font-weight:800;cursor:pointer}.careReply.primary{background:var(--green);color:#fff}.careReply:disabled{opacity:.55;cursor:wait}.careEventBadge{font-size:11px}.careNotice{margin-top:12px;color:var(--muted);font-size:12px}.careNotice.error{color:var(--red)}
@keyframes pulse{50%{box-shadow:0 0 0 8px rgba(23,183,120,0)}}@media(prefers-reduced-motion:reduce){*{animation:none!important;transition:none!important}}@media(max-width:820px){.devices{grid-template-columns:repeat(2,1fr)}.hero{grid-template-columns:1fr}.heroArt{display:none}.lower{grid-template-columns:1fr}.metrics{grid-template-columns:repeat(3,1fr)}.careGrid{grid-template-columns:1fr}}@media(max-width:560px){body{padding-bottom:88px}.app{padding:16px 14px 6px}.top{margin-bottom:16px}.logo{width:40px;height:40px}.brand{font-size:21px}.tagline{display:none}.live{font-size:12px;padding:8px 10px}.hero{min-height:226px;padding:27px 23px;border-radius:24px}.hero h1{font-size:32px;margin-top:20px}.heroFoot{gap:12px;flex-wrap:wrap}.metrics{grid-template-columns:1fr}.metric{padding:14px 16px}.devices{grid-template-columns:repeat(2,1fr);gap:10px}.device{padding:16px;border-radius:19px}.deviceIcon{width:41px;height:41px}.deviceName{margin-top:17px}.deviceState{font-size:22px}.sectionHead{margin-top:25px}.foot{padding-bottom:8px}.eventRow{grid-template-columns:36px 1fr}.eventOutcome{grid-column:2;text-align:left}.bottomNav{position:fixed;display:flex;z-index:20;left:12px;right:12px;bottom:12px;justify-content:space-around;padding:9px;border:1px solid rgba(230,239,235,.9);border-radius:19px;background:rgba(255,255,255,.93);box-shadow:0 12px 32px rgba(27,64,51,.20);backdrop-filter:blur(14px)}.bottomNav a{display:flex;flex-direction:column;align-items:center;gap:3px;color:#73857e;text-decoration:none;font-size:10px}.bottomNav a:first-child{color:var(--green);font-weight:800}.bottomNav svg{width:20px;height:20px}}
</style></head><body><main class="app">
<header class="top"><div class="brandBox"><div class="logo"><svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M3 11.2 12 4l9 7.2v8.3a.5.5 0 0 1-.5.5h-17a.5.5 0 0 1-.5-.5z"/><path d="M8.2 12.4c1.8-2.2 5.8-1.8 7.6.2-1.2 2.6-2.7 4.2-3.8 4.9-1.2-.8-2.8-2.5-3.8-5.1Z"/></svg></div><div><div class="brand">VelaCare</div><div class="tagline">家庭健康守护中心</div></div></div><div id="live" class="live"><i class="dot"></i><span id="liveText">正在连接</span></div></header>
<section id="hero" class="hero"><div><div class="eyebrow"><span>●</span> 实时家庭状态</div><h1 id="risk">正在读取状态</h1><p id="summaryText">正在连接 ESP32 传感器网关，请稍候。</p><div class="heroFoot"><span id="clock">--:--</span><span>最近更新 <b id="lastUpdate">--</b></span><span>心跳 #<b id="seq">--</b></span></div></div><div class="heroArt"><div class="shield"><svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.7"><path d="M12 3 5 6v5c0 4.7 2.8 8.4 7 10 4.2-1.6 7-5.3 7-10V6z"/><path d="m8.5 12 2.3 2.3 4.8-5"/></svg></div></div></section>
<div class="sectionHead"><h2>守护概览</h2><span>数据每秒自动更新</span></div>
<section class="metrics"><article class="metric"><div class="metricIcon"><svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><circle cx="12" cy="12" r="8"/><path d="m8.5 12 2.2 2.2 4.8-4.8"/></svg></div><div><div class="metricLabel">在线设备</div><div id="onlineCount" class="metricValue">--/4</div></div></article><article class="metric"><div class="metricIcon"><svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M5 10a10 10 0 0 1 14 0M8 13a6 6 0 0 1 8 0m-5 4a2 2 0 0 1 2 0"/></svg></div><div><div class="metricLabel">网络质量</div><div id="quality" class="metricValue">--</div></div></article><article class="metric"><div class="metricIcon"><svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><circle cx="12" cy="12" r="9"/><path d="M12 7v5l3 2"/></svg></div><div><div class="metricLabel">稳定运行</div><div id="uptime" class="metricValue">--</div></div></article></section>
<div id="devices" class="sectionHead"><h2>监测设备</h2><span id="deviceSummary">等待数据</span></div>
<section class="devices">
<article id="card-smoke" class="device"><div class="deviceTop"><div class="deviceIcon"><svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.8"><path d="M8 18h8M9 14c-3-3 2-4 0-8m6 8c3-3-2-4 0-8m-3 8c-2-2 2-3 0-6"/><path d="M5 21h14"/></svg></div><i class="stateDot"></i></div><div class="deviceName">烟雾监测</div><div id="smoke" class="deviceState">--</div><div id="hint-smoke" class="deviceHint">等待网关数据</div></article>
<article id="card-water" class="device"><div class="deviceTop"><div class="deviceIcon"><svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.8"><path d="M12 3S6 10 6 15a6 6 0 0 0 12 0c0-5-6-12-6-12Z"/><path d="M9.5 16.5c.7 1.1 1.6 1.6 2.8 1.6"/></svg></div><i class="stateDot"></i></div><div class="deviceName">漏水监测</div><div id="water" class="deviceState">--</div><div id="hint-water" class="deviceHint">等待网关数据</div></article>
<article id="card-door" class="device"><div class="deviceTop"><div class="deviceIcon"><svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.8"><path d="M5 21h14M7 21V4h10v17"/><path d="M13.5 13h.01"/></svg></div><i class="stateDot"></i></div><div class="deviceName">门窗监测</div><div id="door" class="deviceState">--</div><div id="hint-door" class="deviceHint">等待网关数据</div></article>
<article id="card-fall" class="device"><div class="deviceTop"><div class="deviceIcon"><svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.8"><circle cx="14" cy="5" r="2"/><path d="m12 9-3 4 4 2 2 5m-3-11 4 4 3-1M4 20h6"/></svg></div><i class="stateDot"></i></div><div class="deviceName">跌倒监测</div><div id="fall" class="deviceState">--</div><div id="hint-fall" class="deviceHint">等待网关数据</div></article>
</section>
<section class="lower"><article class="panel"><div class="panelTitle"><span>告警历史</span><small><span id="eventCount">0</span>/12 条 · ESP32 本地</small></div><div id="eventList" class="eventList"><div class="emptyEvents">正在读取本地事件记录</div></div><div class="method">持续时长按网关单调运行时间计算；实时时间由联网后的 NTP 校准。记录写入独立 NVS，断电后保留。</div></article><aside class="panel"><div class="panelTitle">网关连接 <small>ESP32-S3</small></div><div class="connection"><div id="signal" class="signal"><strong id="rssi">--</strong></div><div><div id="wifi" class="connState">读取中</div><div class="connIp">IP <span id="ip">--</span></div></div></div><a class="configure" href="/configure">管理 Wi-Fi 与照护人 →</a></aside></section>
<section id="alarmPanel" class="panel" style="margin-top:14px"><div class="panelTitle"><span>报警控制</span><small id="alarmControlState">读取中</small></div><div id="alarmControlText" class="eventSub">没有待处理报警。</div><button style="margin-top:12px;padding:10px 16px;border:0;border-radius:10px;background:#0a8f61;color:white;font-weight:700" onclick="alarmAction('/api/alarm/ack')">确认并停止蜂鸣</button><button style="margin:12px 0 0 8px;padding:10px 16px;border:0;border-radius:10px;background:#d99321;color:white;font-weight:700" onclick="alarmAction('/api/alarm/mute')">静音 10 分钟</button><button style="margin:12px 0 0 8px;padding:10px 16px;border:0;border-radius:10px;background:#71847c;color:white;font-weight:700" onclick="alarmAction('/api/alarm/unmute')">恢复蜂鸣</button><div style="margin-top:10px"><a class="configure" href="/selftest">打开传感器自检页 →</a></div></section>
<section id="carePanel" class="panel" style="margin-top:14px"><div class="panelTitle"><span>照护与求助</span><small id="caregiverState">正在读取配置</small></div><div class="careGrid"><article id="care-sos" class="careRequest"><div class="careRequestTitle"><span>SOS 紧急求助</span><span id="care-sos-id" class="careRequestId">请求 ID #--</span></div><div id="care-sos-state" class="careRequestState">读取中</div><div class="careRequestHint">D12X 长按 SOS 后生成独立请求。</div><div class="careReplies"><button class="careReply primary" onclick="careReply('sos',1)">已看到</button><button class="careReply" onclick="careReply('sos',2)">马上联系</button><button class="careReply" onclick="careReply('sos',3)">正在赶来</button><button class="careReply" onclick="careReply('sos',4)">请安心休息</button></div></article><article id="care-contact" class="careRequest"><div class="careRequestTitle"><span>请家人联系</span><span id="care-contact-id" class="careRequestId">请求 ID #--</span></div><div id="care-contact-state" class="careRequestState">读取中</div><div class="careRequestHint">老人请求家人通过电话或其他方式联系。</div><div class="careReplies"><button class="careReply primary" onclick="careReply('contact',1)">已看到</button><button class="careReply" onclick="careReply('contact',2)">马上联系</button><button class="careReply" onclick="careReply('contact',3)">正在赶来</button><button class="careReply" onclick="careReply('contact',4)">请安心休息</button></div></article><article id="care-wellbeing" class="careRequest"><div class="careRequestTitle"><span>今天很好</span><span id="care-wellbeing-id" class="careRequestId">请求 ID #--</span></div><div id="care-wellbeing-state" class="careRequestState">读取中</div><div class="careRequestHint">老人主动报平安，家属可发送明确回复。</div><div class="careReplies"><button class="careReply primary" onclick="careReply('wellbeing',1)">已看到</button><button class="careReply" onclick="careReply('wellbeing',2)">马上联系</button><button class="careReply" onclick="careReply('wellbeing',3)">正在赶来</button><button class="careReply" onclick="careReply('wellbeing',4)">请安心休息</button></div></article></div><div id="elderSafeState" class="eventText">当前没有跌倒确认</div><div id="careActionMessage" class="careNotice">家属回复会携带当前请求的类型与 ID。</div></section>
<section class="panel" style="margin-top:14px"><div class="panelTitle"><span>照护事件</span><small><span id="careEventCount">0</span>/12 条 · ESP32 本地</small></div><div id="careEventList" class="eventList"><div class="emptyEvents">正在读取照护事件记录</div></div></section>
<footer class="foot"><span>VelaCare 本地守护 · 配置保存在网关</span><span>Gateway v0.15.1</span></footer></main>
<nav class="bottomNav"><a href="#"><svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M3 11 12 4l9 7v9H3z"/><path d="M9 20v-6h6v6"/></svg>总览</a><a href="#devices"><svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><rect x="4" y="4" width="6" height="6" rx="1"/><rect x="14" y="4" width="6" height="6" rx="1"/><rect x="4" y="14" width="6" height="6" rx="1"/><rect x="14" y="14" width="6" height="6" rx="1"/></svg>设备</a><a href="/configure"><svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><circle cx="12" cy="12" r="3"/><path d="M19 12a7 7 0 0 0-.1-1l2-1.5-2-3.4-2.4 1A7 7 0 0 0 14.8 6L14.5 3h-4L10 6a7 7 0 0 0-1.7 1.1l-2.4-1-2 3.4L6 11a7 7 0 0 0 0 2l-2.1 1.5 2 3.4 2.4-1A7 7 0 0 0 10 18l.5 3h4l.4-3a7 7 0 0 0 1.7-1.1l2.4 1 2-3.4L19 13a7 7 0 0 0 0-1Z"/></svg>设置</a></nav>
<script>
const ids=['smoke','water','door','fall'],names=['烟雾','漏水','门窗','跌倒'],labels=['离线','正常','告警'],classes=['offline','normal','alarm'],severityNames=['普通提醒','严重告警','持续未恢复'];
const activeCare={sos:0,contact:0,wellbeing:0},careTypeNames={sos:'SOS 紧急求助',contact:'请家人联系',wellbeing:'今天很好'},careOutcomeNames={pending:'待家属回复',seen:'家属已回复',cancelled:'老人已取消',superseded:'被新请求替代'},careReplyNames=['未回复','已看到','马上联系','正在赶来','请安心休息'];
function duration(s){if(s<60)return s+'秒';const m=Math.floor(s/60);if(m<60)return m+'分钟';const h=Math.floor(m/60),r=m%60;return h+'小时'+(r?r+'分':'');}
function render(d){let alarms=[],offline=[],online=0;ids.forEach((id,i)=>{const s=d.sensors[i],level=(d.alarmSeverity||[])[i]||0,card=document.getElementById('card-'+id);card.className='device '+(classes[s]||'offline')+(s===2?' severity-'+level:'');document.getElementById(id).textContent=s===2?(severityNames[level]||'告警'):labels[s]||'未知';document.getElementById('hint-'+id).textContent=s===1?'持续监测中':s===2?((level===2?'已持续未恢复 · 请立即处理':level===1?'已确认严重告警 · 请立即确认':'请尽快确认')):'暂未收到数据';if(s!==0)online++;if(s===2)alarms.push(names[i]+'：'+(severityNames[level]||'告警'));if(s===0)offline.push(names[i]);});
 const hero=document.getElementById('hero'),risk=document.getElementById('risk'),text=document.getElementById('summaryText');hero.className='hero'+(alarms.length?' dangerHero':offline.length?' warnHero':'');if(alarms.length){risk.textContent=alarms.join('、')+'出现告警';text.textContent='请立即前往现场确认，同时保留本地声光报警。';}else if(offline.length){risk.textContent='部分设备暂时离线';text.textContent=offline.join('、')+'没有有效数据，请检查传感器或连线。';}else{risk.textContent='家中状态一切正常';text.textContent='四项安全监测持续在线，VelaCare 正在安静守护。';}
 document.getElementById('onlineCount').textContent=online+'/4';const q=!d.wifi?'离线':d.rssi>=-55?'优秀':d.rssi>=-67?'良好':'一般';document.getElementById('quality').textContent=q;document.getElementById('uptime').textContent=duration(d.uptime);document.getElementById('seq').textContent=d.sequence;document.getElementById('deviceSummary').textContent=alarms.length?alarms.length+' 项告警':offline.length?offline.length+' 项离线':'全部正常';
 const live=document.getElementById('live');live.className='live'+(d.wifi?'':' errorLive');document.getElementById('liveText').textContent=d.wifi?'网关在线':'网关离线';document.getElementById('wifi').textContent=d.wifi?'连接稳定':'未连接';document.getElementById('rssi').textContent=d.wifi?d.rssi:'--';document.getElementById('ip').textContent=d.ip;const pct=!d.wifi?0:d.rssi>=-55?92:d.rssi>=-67?70:45;document.getElementById('signal').style.background='conic-gradient(var(--green) 0 '+pct+'%,#e5eeea '+pct+'%)';const alarmNames=alarms.length?alarms.join('、'):'没有待处理报警';document.getElementById('alarmControlState').textContent=d.alarmMuted?'静音中':d.alarmAcknowledged?'已确认':'监听中';document.getElementById('alarmControlText').textContent=alarms.length?alarmNames+(d.alarmMuted?'（蜂鸣器已静音）':d.alarmAcknowledged?'（已确认，蜂鸣器停止）':'（蜂鸣器已启用）'):'没有待处理报警。';
 document.getElementById('caregiverState').textContent=d.caregiverConfigured?'照护人 '+d.caregiverMasked:'照护人未配置';renderCareRequest('sos',d.sosActive,d.sosId,'等待家属处理','当前无 SOS 请求');renderCareRequest('contact',d.contactActive,d.contactId,'等待家属联系','当前无联系请求');renderCareRequest('wellbeing',d.wellbeingActive,d.wellbeingId,'等待家属回应','当前无报平安请求');document.getElementById('carePanel').style.borderColor=(d.sosActive||d.contactActive||d.wellbeingActive)?'#efb2af':'var(--line)';const elder=document.getElementById('elderSafeState');if(d.elderConfirmed){elder.textContent='老人已在 D12X 点击确认安全';elder.style.color='var(--green)';elder.style.fontSize='22px';elder.style.fontWeight='800';}else if(d.sensors&&d.sensors[3]===2){elder.textContent='跌倒告警中，等待老人在 D12X 点确认安全';elder.style.color='var(--red)';elder.style.fontSize='20px';elder.style.fontWeight='800';}else{elder.textContent='当前没有跌倒确认';elder.style.color='var(--muted)';elder.style.fontSize='';elder.style.fontWeight='';}if(d.elderConfirmed&&d.sensors&&d.sensors[3]===2){document.getElementById('hint-fall').textContent='老人已点确认安全，跌倒仍以传感器为准';}const now=new Date(),ts=now.toLocaleTimeString('zh-CN',{hour:'2-digit',minute:'2-digit',second:'2-digit'});document.getElementById('lastUpdate').textContent=ts;}
function renderCareRequest(type,active,id,activeText,inactiveText){activeCare[type]=active&&id?id:0;const card=document.getElementById('care-'+type);card.className='careRequest'+(active?' active':'');document.getElementById('care-'+type+'-state').textContent=active?activeText:inactiveText;document.getElementById('care-'+type+'-id').textContent='请求 ID #'+(id||'--');}
function renderEvents(d){const box=document.getElementById('eventList'),events=d.events||[];document.getElementById('eventCount').textContent=events.length;const outcomes=['进行中','已恢复','重启中断'],icons=['!','✓','↻'];box.innerHTML=events.length?events.slice(0,8).map(e=>{const when=e.startEpoch?new Date(e.startEpoch*1000).toLocaleString('zh-CN',{month:'2-digit',day:'2-digit',hour:'2-digit',minute:'2-digit',second:'2-digit'}):'启动后 '+duration(e.startUptime);const dur=e.outcome===2?'时长未完整记录':('持续 '+duration(e.duration));const cls=e.outcome===0?' active':e.outcome===2?' interrupted':'';const level=severityNames[e.severity||0]||'普通提醒';const sync=e.syncPending&&d.eventUploadConfigured?' · 待网络补传':'';return '<div class="eventRow'+cls+'"><div class="eventBadge">'+icons[e.outcome]+'</div><div><div class="eventName">'+names[ids.indexOf(e.sensor)]+' · '+level+'</div><div class="eventInfo">'+when+' · '+dur+sync+'</div></div><div class="eventOutcome">'+outcomes[e.outcome]+'</div></div>';}).join(''):'<div class="emptyEvents">暂无告警记录，当前运行平稳</div>';renderCareEvents(d.careEvents||[]);}
function renderCareEvents(events){const box=document.getElementById('careEventList');document.getElementById('careEventCount').textContent=events.length;box.innerHTML=events.length?events.map(e=>{const when=e.startEpoch?new Date(e.startEpoch*1000).toLocaleString('zh-CN',{month:'2-digit',day:'2-digit',hour:'2-digit',minute:'2-digit',second:'2-digit'}):'启动后 '+duration(e.startUptime);const cls=e.outcome==='pending'?' active':e.outcome==='superseded'?' interrupted':'';const sync=e.syncPending?' · 待同步':'';const reply=careReplyNames[e.reply]||'未知回复';return '<div class="eventRow'+cls+'"><div class="eventBadge careEventBadge">'+(e.type==='sos'?'SOS':e.type==='contact'?'☎':'✓')+'</div><div><div class="eventName">'+(careTypeNames[e.type]||e.type)+' · 请求 ID #'+e.requestId+'</div><div class="eventInfo">'+when+' · 持续 '+duration(e.duration)+' · '+reply+sync+'</div></div><div class="eventOutcome">'+(careOutcomeNames[e.outcome]||e.outcome)+'</div></div>';}).join(''):'<div class="emptyEvents">暂无照护事件记录</div>';}
function tick(){document.getElementById('clock').textContent=new Date().toLocaleTimeString('zh-CN',{hour:'2-digit',minute:'2-digit'});}async function careReply(type,reply){const id=activeCare[type],message=document.getElementById('careActionMessage');if(!id)return;message.className='careNotice';message.textContent='正在发送家属回复…';try{const body='type='+encodeURIComponent(type)+'&id='+encodeURIComponent(id)+'&reply='+encodeURIComponent(reply),response=await fetch('/api/care/reply',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body});if(!response.ok)throw Error('reply failed');message.textContent='回复已发送：'+careReplyNames[reply]+'（'+careTypeNames[type]+' #'+id+'）';await update();}catch(e){message.className='careNotice error';message.textContent='回复失败，请刷新后确认请求仍处于待处理状态。';}}async function alarmAction(url){await fetch(url,{method:'POST'});update();}async function update(){try{const rs=await Promise.all([fetch('/api/status',{cache:'no-store'}),fetch('/api/events',{cache:'no-store'})]);if(!rs[0].ok||!rs[1].ok)throw Error();render(await rs[0].json());renderEvents(await rs[1].json());}catch(e){const live=document.getElementById('live');live.className='live errorLive';document.getElementById('liveText').textContent='读取失败';}}
tick();update();setInterval(tick,1000);setInterval(update,1000);
</script></body></html>
)HTML";

const char kSelfTestPage[] PROGMEM = R"HTML(
<!doctype html><html lang="zh-CN"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>VelaCare 传感器自检</title>
<style>body{font-family:system-ui,"Microsoft YaHei",sans-serif;max-width:760px;margin:24px auto;padding:0 16px;color:#18332b;background:#f4faf7}h1{margin-bottom:6px}.muted{color:#6d8178;font-size:13px}.card{background:#fff;border-radius:16px;padding:18px;margin:14px 0;box-shadow:0 6px 20px #174d3212}table{width:100%;border-collapse:collapse}td{padding:9px;border-bottom:1px solid #edf2ef}td:first-child{color:#6d8178;width:42%}.ok{color:#087e56;font-weight:700}.bad{color:#b43b35;font-weight:700}button{padding:10px 14px;border:0;border-radius:9px;background:#0a8f61;color:#fff;font-weight:700;margin:4px;cursor:pointer}.danger{background:#b43b35}</style></head><body>
<h1>VelaCare 传感器自检</h1><div class="muted">实时读取 ESP32 网关，不会修改传感器状态。</div>
<div class="card"><h2>总体状态</h2><div id="overall">读取中...</div><button onclick="alarm('/api/alarm/ack')">确认并停止蜂鸣</button><button onclick="alarm('/api/alarm/mute')">静音 10 分钟</button><button class="danger" onclick="alarm('/api/alarm/unmute')">恢复蜂鸣</button></div>
<div class="card"><h2>MPU6500 / MPU9250</h2><table id="imu"><tr><td>读取中</td><td>--</td></tr></table></div>
<div class="card"><h2>数字输入</h2><table id="inputs"><tr><td>读取中</td><td>--</td></tr></table></div>
<div class="card"><h2>报警状态</h2><table id="alarm"><tr><td>读取中</td><td>--</td></tr></table></div>
<p><a href="/dashboard">返回守护页面</a> · <a href="/configure">配置页面</a></p>
<script>
const yes=v=>v?'<span class="ok">正常</span>':'<span class="bad">异常</span>';
function row(k,v){return '<tr><td>'+k+'</td><td>'+v+'</td></tr>';}
function render(d){const i=d.imu;document.getElementById('overall').innerHTML='IMU '+yes(i.available)+'，D12X 蜂鸣 '+(d.alarm.d12xAlarm?'应报警':'正常');document.getElementById('imu').innerHTML=row('芯片识别','0x'+i.whoAmI.toString(16).padStart(2,'0'))+row('I²C 引脚','SDA GPIO'+i.sda+' / SCL GPIO'+i.scl)+row('最近读取',i.lastReadMs+' ms 前')+row('读取错误',i.readErrors)+row('加速度',i.accelG.toFixed(3)+' g')+row('角速度',i.gyroDps.toFixed(1)+' dps')+row('距安全位置',i.safeAngleDeg.toFixed(1)+'°')+row('距跌倒位置',i.fallAngleDeg.toFixed(1)+'°')+row('判定阶段',i.phase);const p=d.inputs;document.getElementById('inputs').innerHTML=row('漏水 GPIO4','电平 '+p.waterLevel+' / '+p.waterState)+row('烟雾 GPIO5','电平 '+p.smokeLevel+' / '+p.smokeState)+row('门磁 GPIO6','电平 '+p.doorLevel+' / '+p.doorState)+row('D12X 蜂鸣器','由 VC1/VCB1 串口控制');document.getElementById('alarm').innerHTML=row('最高等级',d.alarm.severity)+row('确认状态',d.alarm.acknowledged?'已确认':'待确认')+row('静音状态',d.alarm.muted?'静音中':'未静音')+row('静音剩余',d.alarm.muteRemainingMs+' ms');}
async function alarm(url){await fetch(url,{method:'POST'});update();}async function update(){try{render(await (await fetch('/api/diagnostics',{cache:'no-store'})).json());}catch(e){document.getElementById('overall').textContent='读取失败';}}update();setInterval(update,1000);
</script></body></html>)HTML";

enum SensorState : uint8_t {
  kOffline = 0,
  kNormal = 1,
  kAlarm = 2,
};

enum EventOutcome : uint8_t {
  kEventActive = 0,
  kEventRecovered = 1,
  kEventInterrupted = 2,
};

enum AlarmSeverity : uint8_t {
  kSeverityNotice = 0,
  kSeveritySerious = 1,
  kSeverityPersistent = 2,
};

struct EventRecord {
  uint32_t id;
  uint32_t startEpoch;
  uint32_t startUptime;
  uint32_t endEpoch;
  uint32_t endUptime;
  uint8_t sensorIndex;
  uint8_t outcome;
  // Reuse the original padding bytes so existing NVS records keep their size.
  uint8_t severity;
  uint8_t syncState;
};

struct EventStore {
  uint32_t magic;
  uint32_t nextId;
  uint8_t count;
  uint8_t reserved[3];
  EventRecord records[kEventCapacity];
};

enum CareEventType : uint8_t {
  kCareEmergency = 0,
  kCareContact = 1,
  kCareWellbeing = 2,
};

enum CareEventOutcome : uint8_t {
  kCarePending = 0,
  kCareSeen = 1,
  kCareCancelled = 2,
  kCareSuperseded = 3,
};

enum FamilyReply : uint8_t {
  kFamilyReplyNone = 0,
  kFamilyReplySeen = 1,
  kFamilyReplyCalling = 2,
  kFamilyReplyComing = 3,
  kFamilyReplyRest = 4,
};

struct CareEventRecord {
  uint32_t id;
  uint32_t requestId;
  uint32_t startEpoch;
  uint32_t startUptime;
  uint32_t endEpoch;
  uint32_t endUptime;
  uint8_t type;
  uint8_t outcome;
  uint8_t reply;
  uint8_t syncState;
};

struct CareEventStore {
  uint32_t magic;
  uint32_t nextId;
  uint8_t count;
  uint8_t reserved[3];
  CareEventRecord records[kCareEventCapacity];
};

struct FamilyNotificationRecord {
  uint32_t requestId;
  uint32_t queuedEpoch;
  uint8_t type;
  uint8_t reply;
  uint8_t reserved[2];
};

struct FamilyNotificationStore {
  uint32_t magic;
  uint8_t count;
  uint8_t reserved[3];
  FamilyNotificationRecord records[kFamilyNotificationCapacity];
};

struct CurrentBootHandledCareEvent {
  uint32_t eventId;
  uint32_t handledAtMs;
};

enum class VoicePromptId : uint8_t {
  kNone = 0,
  kFall = 1,
  kPersistent = 2,
  kFamily = 3,
  kElder = 4,
  kTest = 5,
};

struct AudioRequest {
  VoicePromptId prompt;
  uint32_t generation;
  uint32_t cancellationEpoch;
  bool alarmBound;
};

struct AudioStatistics {
  uint32_t enqueued;
  uint32_t played;
  uint32_t droppedQueueFull;
  uint32_t deduplicated;
  uint32_t cancelled;
  uint32_t writeErrors;
};

HardwareSerial d12xSerial(1);
I2SClass audioI2s;
WebServer configServer(80);
SensorState sensorStates[4] = {kOffline, kOffline, kOffline, kOffline};
QueueHandle_t audioQueue = nullptr;
TaskHandle_t audioTaskHandle = nullptr;
portMUX_TYPE audioStateMux = portMUX_INITIALIZER_UNLOCKED;
bool audioReady = false;
bool audioTaskRunning = false;
VoicePromptId audioCurrentPrompt = VoicePromptId::kNone;
uint32_t audioCancellationEpoch = 1;
uint32_t fallVoiceGeneration = 0;
uint32_t fallPromptHandledGeneration = 0;
uint32_t persistentPromptHandledGeneration = 0;
uint32_t elderPromptHandledGeneration = 0;
AudioStatistics audioStatistics = {};
uint32_t audioSamplesWritten = 0;
bool wifiOnline = false;
bool wifiConnecting = false;
bool configPortalActive = false;
bool webRoutesReady = false;
bool timeSyncStarted = false;
uint32_t sequenceNumber = 1;
uint32_t lastHeartbeatMs = 0;
uint32_t wifiConnectStartedMs = 0;
char commandBuffer[64] = {};
size_t commandLength = 0;
char d12xReceiveBuffer[128] = {};
size_t d12xReceiveLength = 0;
String configApSsid;
EventStore eventStore = {};
CareEventStore careEventStore = {};
FamilyNotificationStore familyNotificationStore = {};
CurrentBootHandledCareEvent currentBootHandledCareEvents[kCareEventCapacity] = {};
String caregiverName;
String caregiverPhone;
String eventUploadUrl;
uint32_t caregiverRevision = 1;
bool sosActive = false;
uint32_t sosRequestId = 0;
uint32_t sosEpoch = 0;
uint32_t sosUptime = 0;
bool contactActive = false;
uint32_t contactRequestId = 0;
bool wellbeingActive = false;
uint32_t wellbeingRequestId = 0;
uint32_t familyNotificationLastSentMs = 0;
size_t familyNotificationNextIndex = 0;
bool legacyFamilyNotificationPending = false;
uint32_t legacyFamilyNotificationRequestId = 0;
uint8_t legacyFamilyNotificationType = kCareEmergency;
uint8_t legacyFamilyNotificationReply = kFamilyReplyNone;
bool elderConfirmed = false;
uint32_t lastConfigSyncMs = 0;
uint32_t lastEventUploadMs = 0;
uint32_t alarmMutedUntilMs = 0;
bool alarmAcknowledged = false;
bool d12xAlarmExpected = false;
bool d12xControlInitialized = false;
bool lastD12xMute = false;
bool lastD12xAck = false;
uint32_t lastBuzzerUpdateMs = 0;
uint32_t alarmSinceMs[4] = {};
AlarmSeverity alarmSeverities[4] = {kSeverityNotice, kSeverityNotice,
                                     kSeverityNotice, kSeverityNotice};
int waterCandidateLevel = HIGH;
uint32_t waterCandidateSinceMs = 0;
bool waterInputInitialized = false;
int smokeCandidateLevel = HIGH;
uint32_t smokeCandidateSinceMs = 0;
bool smokeInputInitialized = false;
int doorCandidateLevel = HIGH;
uint32_t doorCandidateSinceMs = 0;
bool doorInputInitialized = false;

enum FallPhase : uint8_t {
  kFallMonitoring = 0,
  kFallFreeFall = 1,
  kFallPostImpact = 2,
  kFallAlarmed = 3,
};

bool imuAvailable = false;
uint8_t imuWhoAmI = 0;
uint32_t lastImuSampleMs = 0;
uint32_t imuReadErrorCount = 0;
bool imuReferenceReady = true;
float imuReferenceAx = kSafeAnchorAx;
float imuReferenceAy = kSafeAnchorAy;
float imuReferenceAz = kSafeAnchorAz;
float lastImuAccelerationG = 0.0f;
float lastImuGyroDps = 0.0f;
float lastImuPostureDeg = 0.0f;
float lastImuFallPoseDeg = 180.0f;
uint32_t lastImuGoodReadMs = 0;
FallPhase fallPhase = kFallMonitoring;
uint32_t fallPhaseSinceMs = 0;
uint32_t fallStillSinceMs = 0;
uint32_t fallKnownPoseSinceMs = 0;
uint32_t fallRecoverSinceMs = 0;
uint32_t fallRecoverBadSinceMs = 0;
uint32_t lastFallRecoverHintMs = 0;
bool fallRecoveryInSafeZone = false;
bool fallPostureChanged = false;
bool heartbeatDirty = true;

void printStatus();
const char *stateName(SensorState state);
void startConfigPortal();
void beginSavedWifiConnection();
void persistEventHistory();
void persistCareEventHistory();
bool persistFamilyNotificationQueue();
void sendCaregiverFrame();
void updateEventUpload();
bool eventUploadConfigured();
void updateBuzzer();
void printImuStatus();
void sendBuzzerControlFrame();
bool anyAlarmActive();
bool alarmMuted();
bool beginAudioSubsystem();
bool queueAudioPrompt(VoicePromptId prompt, uint32_t generation,
                      bool alarmBound, bool highPriority = false);
void stopAudioPlayback(bool printMessage = false);
uint32_t advanceFallVoiceGeneration();
uint32_t currentFallVoiceGeneration();
void noteAudioDeduplicated();
bool sendFamilyNotificationFor(uint32_t requestId, uint8_t type);
void updateFamilyNotification();
bool acknowledgeCareRequest(uint8_t type, uint32_t requestId, uint8_t reply);

uint32_t currentEpoch() {
  const time_t now = time(nullptr);
  return now >= kMinimumValidEpoch ? static_cast<uint32_t>(now) : 0;
}

bool clockIsSynchronized() {
  return currentEpoch() != 0;
}

String buildStatusJson() {
  String json;
  json.reserve(640);
  json += F("{\"version\":\"");
  json += kFirmwareVersion;
  json += F("\",\"sequence\":");
  json += sequenceNumber;
  json += F(",\"uptime\":");
  json += millis() / 1000;
  json += F(",\"wifi\":");
  json += wifiOnline ? F("true") : F("false");
  json += F(",\"ip\":\"");
  json += wifiOnline ? WiFi.localIP().toString() : String("0.0.0.0");
  json += F("\",\"rssi\":");
  json += wifiOnline ? WiFi.RSSI() : 0;
  json += F(",\"sensors\":[");
  for (size_t i = 0; i < 4; ++i) {
    if (i > 0) {
      json += ',';
    }
    json += static_cast<unsigned int>(sensorStates[i]);
  }
  json += F("],\"alarmSeverity\":[");
  for (size_t i = 0; i < 4; ++i) {
    if (i > 0) {
      json += ',';
    }
    json += static_cast<unsigned int>(alarmSeverities[i]);
  }
  json += F("],\"pendingEvents\":");
  size_t pendingEvents = 0;
  for (size_t i = 0; i < eventStore.count; ++i) {
    if (eventStore.records[i].syncState == 0) {
      ++pendingEvents;
    }
  }
  json += static_cast<unsigned int>(pendingEvents);
  json += F(",\"eventUploadConfigured\":");
  json += eventUploadConfigured() ? F("true") : F("false");
  json += F(",\"alarmAcknowledged\":");
  json += alarmAcknowledged ? F("true") : F("false");
  json += F(",\"alarmMuted\":");
  json += alarmMuted() ? F("true") : F("false");
  json += F(",\"alarmMuteRemainingMs\":");
  json += alarmMuted() ? alarmMutedUntilMs - millis() : 0;
  json += F(",\"d12xAlarm\":");
  json += d12xAlarmExpected ? F("true") : F("false");
  json += F(",\"elderConfirmed\":");
  json += elderConfirmed ? F("true") : F("false");
  json += F(",\"caregiverConfigured\":");
  json += caregiverPhone.isEmpty() ? F("false") : F("true");
  json += F(",\"caregiverMasked\":\"");
  if (!caregiverPhone.isEmpty()) {
    json += F("****");
    json += caregiverPhone.substring(caregiverPhone.length() - 4);
  }
  json += F("\",\"sosActive\":");
  json += sosActive ? F("true") : F("false");
  json += F(",\"sosId\":");
  json += sosRequestId;
  json += F(",\"sosEpoch\":");
  json += sosEpoch;
  json += F(",\"contactActive\":");
  json += contactActive ? F("true") : F("false");
  json += F(",\"contactId\":");
  json += contactRequestId;
  json += F(",\"wellbeingActive\":");
  json += wellbeingActive ? F("true") : F("false");
  json += F(",\"wellbeingId\":");
  json += wellbeingRequestId;
  json += F(",\"familyNotificationPending\":");
  json += familyNotificationStore.count > 0 ? F("true") : F("false");
  json += F(",\"familyNotificationPendingCount\":");
  json += static_cast<unsigned int>(familyNotificationStore.count);
  json += '}';
  return json;
}

const char *eventSensorName(uint8_t sensorIndex) {
  static const char *const names[] = {"smoke", "water", "door", "fall"};
  return sensorIndex < 4 ? names[sensorIndex] : "unknown";
}

const char *severityName(AlarmSeverity severity) {
  switch (severity) {
    case kSeverityNotice:
      return "notice";
    case kSeveritySerious:
      return "serious";
    case kSeverityPersistent:
      return "persistent";
  }
  return "notice";
}

const char *careEventTypeName(uint8_t type) {
  switch (type) {
    case kCareEmergency:
      return "sos";
    case kCareContact:
      return "contact";
    case kCareWellbeing:
      return "wellbeing";
  }
  return "unknown";
}

const char *careEventOutcomeName(uint8_t outcome) {
  switch (outcome) {
    case kCarePending:
      return "pending";
    case kCareSeen:
      return "seen";
    case kCareCancelled:
      return "cancelled";
    case kCareSuperseded:
      return "superseded";
  }
  return "unknown";
}

String buildEventsJson() {
  String json;
  json.reserve(3200);
  json += F("{\"version\":\"");
  json += kFirmwareVersion;
  json += F("\",\"source\":\"esp32-nvs\",\"capacity\":");
  json += static_cast<unsigned int>(kEventCapacity);
  json += F(",\"eventUploadConfigured\":");
  json += eventUploadConfigured() ? F("true") : F("false");
  json += F(",\"timeSynced\":");
  json += clockIsSynchronized() ? F("true") : F("false");
  json += F(",\"events\":[");
  const uint32_t nowUptime = millis() / 1000;
  for (size_t i = 0; i < eventStore.count; ++i) {
    if (i > 0) {
      json += ',';
    }
    const EventRecord &event = eventStore.records[i];
    uint32_t duration = 0;
    if (event.outcome == kEventRecovered && event.endUptime >= event.startUptime) {
      duration = event.endUptime - event.startUptime;
    } else if (event.outcome == kEventActive && nowUptime >= event.startUptime) {
      duration = nowUptime - event.startUptime;
    }
    json += F("{\"id\":");
    json += event.id;
    json += F(",\"sensor\":\"");
    json += eventSensorName(event.sensorIndex);
    json += F("\",\"startEpoch\":");
    json += event.startEpoch;
    json += F(",\"startUptime\":");
    json += event.startUptime;
    json += F(",\"duration\":");
    json += duration;
    json += F(",\"outcome\":");
    json += static_cast<unsigned int>(event.outcome);
    json += F(",\"severity\":");
    json += static_cast<unsigned int>(event.severity);
    json += F(",\"severityName\":\"");
    json += severityName(static_cast<AlarmSeverity>(event.severity));
    json += F("\",\"syncPending\":");
    json += event.syncState == 0 ? F("true") : F("false");
    json += '}';
  }
  json += F("],\"careEvents\":[");
  for (size_t i = 0; i < careEventStore.count; ++i) {
    if (i > 0) {
      json += ',';
    }
    const CareEventRecord &event = careEventStore.records[i];
    uint32_t duration = 0;
    if (event.outcome != kCarePending && event.endUptime >= event.startUptime) {
      duration = event.endUptime - event.startUptime;
    } else if (event.outcome == kCarePending && nowUptime >= event.startUptime) {
      duration = nowUptime - event.startUptime;
    }
    json += F("{\"id\":");
    json += event.id;
    json += F(",\"requestId\":");
    json += event.requestId;
    json += F(",\"type\":\"");
    json += careEventTypeName(event.type);
    json += F("\",\"startEpoch\":");
    json += event.startEpoch;
    json += F(",\"startUptime\":");
    json += event.startUptime;
    json += F(",\"duration\":");
    json += duration;
    json += F(",\"outcome\":\"");
    json += careEventOutcomeName(event.outcome);
    json += F("\",\"reply\":");
    json += static_cast<unsigned int>(event.reply);
    json += F(",\"syncPending\":");
    json += event.syncState == 0 ? F("true") : F("false");
    json += '}';
  }
  json += F("]}");
  return json;
}

void resetEventStore() {
  memset(&eventStore, 0, sizeof(eventStore));
  eventStore.magic = kEventStoreMagic;
  eventStore.nextId = 1;
}

void persistEventHistory() {
  Preferences preferences;
  if (!preferences.begin("velaevents", false)) {
    Serial.println("ERR unable to open event history preferences");
    return;
  }
  const size_t written =
      preferences.putBytes("history", &eventStore, sizeof(eventStore));
  preferences.end();
  if (written != sizeof(eventStore)) {
    Serial.println("ERR unable to persist event history");
  }
}

void loadEventHistory() {
  resetEventStore();
  Preferences preferences;
  if (preferences.begin("velaevents", true)) {
    if (preferences.getBytesLength("history") == sizeof(eventStore)) {
      EventStore stored = {};
      if (preferences.getBytes("history", &stored, sizeof(stored)) ==
              sizeof(stored) &&
          stored.magic == kEventStoreMagic && stored.count <= kEventCapacity &&
          stored.nextId != 0) {
        eventStore = stored;
      }
    }
    preferences.end();
  }

  bool changed = false;
  for (size_t i = 0; i < eventStore.count; ++i) {
    if (eventStore.records[i].outcome == kEventActive) {
      eventStore.records[i].outcome = kEventInterrupted;
      changed = true;
    }
  }
  if (changed) {
    persistEventHistory();
  }
  Serial.printf("EVENT history loaded count=%u\n",
                static_cast<unsigned int>(eventStore.count));
}

void resetCareEventStore() {
  memset(&careEventStore, 0, sizeof(careEventStore));
  careEventStore.magic = kCareEventStoreMagic;
  careEventStore.nextId = 1;
}

void persistCareEventHistory() {
  Preferences preferences;
  if (!preferences.begin("velacarecare", false)) {
    Serial.println("ERR unable to open care event preferences");
    return;
  }
  const size_t written = preferences.putBytes(
      "history", &careEventStore, sizeof(careEventStore));
  preferences.end();
  if (written != sizeof(careEventStore)) {
    Serial.println("ERR unable to persist care event history");
  }
}

void resetFamilyNotificationQueue() {
  memset(&familyNotificationStore, 0, sizeof(familyNotificationStore));
  familyNotificationStore.magic = kFamilyNotificationStoreMagic;
  familyNotificationLastSentMs = 0;
  familyNotificationNextIndex = 0;
}

bool familyNotificationRecordIsValid(
    const FamilyNotificationRecord &notification) {
  return notification.requestId != 0 &&
         notification.type <= kCareWellbeing &&
         notification.reply >= kFamilyReplySeen &&
         notification.reply <= kFamilyReplyRest;
}

int findFamilyNotificationIndex(uint32_t requestId, uint8_t type) {
  for (size_t i = 0; i < familyNotificationStore.count; ++i) {
    const FamilyNotificationRecord &notification =
        familyNotificationStore.records[i];
    if (notification.requestId == requestId && notification.type == type) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

bool persistFamilyNotificationQueue() {
  Preferences preferences;
  if (!preferences.begin("velafamily", false)) {
    Serial.println("ERR unable to open family notification preferences");
    return false;
  }
  const size_t written = preferences.putBytes(
      "queue", &familyNotificationStore, sizeof(familyNotificationStore));
  preferences.end();
  if (written != sizeof(familyNotificationStore)) {
    Serial.println("ERR unable to persist family notification queue");
    return false;
  }
  return true;
}

void loadFamilyNotificationQueue() {
  resetFamilyNotificationQueue();
  bool loaded = false;
  Preferences preferences;
  if (preferences.begin("velafamily", true)) {
    if (preferences.getBytesLength("queue") ==
        sizeof(familyNotificationStore)) {
      FamilyNotificationStore stored = {};
      if (preferences.getBytes("queue", &stored, sizeof(stored)) ==
              sizeof(stored) &&
          stored.magic == kFamilyNotificationStoreMagic &&
          stored.count <= kFamilyNotificationCapacity) {
        familyNotificationStore = stored;
        loaded = true;
      }
    }
    preferences.end();
  }

  FamilyNotificationStore repaired = {};
  repaired.magic = kFamilyNotificationStoreMagic;
  for (size_t i = 0; i < familyNotificationStore.count; ++i) {
    const FamilyNotificationRecord &candidate =
        familyNotificationStore.records[i];
    if (!familyNotificationRecordIsValid(candidate)) {
      continue;
    }
    bool duplicate = false;
    for (size_t j = 0; j < repaired.count; ++j) {
      if (repaired.records[j].requestId == candidate.requestId &&
          repaired.records[j].type == candidate.type) {
        repaired.records[j] = candidate;
        duplicate = true;
        break;
      }
    }
    if (!duplicate && repaired.count < kFamilyNotificationCapacity) {
      repaired.records[repaired.count++] = candidate;
    }
  }
  const bool repairedQueue =
      memcmp(&repaired, &familyNotificationStore,
             sizeof(familyNotificationStore)) != 0;
  familyNotificationStore = repaired;
  if (!loaded || repairedQueue) {
    persistFamilyNotificationQueue();
  }
  Serial.printf("FAMILY queue loaded count=%u repaired=%u\n",
                static_cast<unsigned int>(familyNotificationStore.count),
                repairedQueue ? 1u : 0u);
}

bool enqueueFamilyNotification(uint32_t requestId, uint8_t type,
                               uint8_t reply) {
  FamilyNotificationRecord candidate = {};
  candidate.requestId = requestId;
  candidate.queuedEpoch = currentEpoch();
  candidate.type = type;
  candidate.reply = reply;
  if (!familyNotificationRecordIsValid(candidate)) {
    return false;
  }

  const FamilyNotificationStore previous = familyNotificationStore;
  const int existingIndex = findFamilyNotificationIndex(requestId, type);
  if (existingIndex >= 0) {
    FamilyNotificationRecord &existing =
        familyNotificationStore.records[existingIndex];
    existing.reply = reply;
    if (existing.queuedEpoch == 0) {
      existing.queuedEpoch = candidate.queuedEpoch;
    }
  } else {
    if (familyNotificationStore.count >= kFamilyNotificationCapacity) {
      Serial.println("ERR family notification queue full");
      return false;
    }
    familyNotificationStore.records[familyNotificationStore.count++] =
        candidate;
  }

  if (!persistFamilyNotificationQueue()) {
    familyNotificationStore = previous;
    return false;
  }
  Serial.printf("FAMILY queued id=%lu type=%s reply=%u depth=%u\n",
                static_cast<unsigned long>(requestId),
                careEventTypeName(type), reply,
                static_cast<unsigned int>(familyNotificationStore.count));
  return true;
}

bool removeFamilyNotification(uint32_t requestId, uint8_t type) {
  const int found = findFamilyNotificationIndex(requestId, type);
  if (found < 0) {
    return false;
  }

  const FamilyNotificationStore previous = familyNotificationStore;
  const size_t previousNextIndex = familyNotificationNextIndex;
  const size_t index = static_cast<size_t>(found);
  for (size_t i = index + 1; i < familyNotificationStore.count; ++i) {
    familyNotificationStore.records[i - 1] =
        familyNotificationStore.records[i];
  }
  --familyNotificationStore.count;
  memset(&familyNotificationStore.records[familyNotificationStore.count], 0,
         sizeof(FamilyNotificationRecord));
  if (index < familyNotificationNextIndex && familyNotificationNextIndex > 0) {
    --familyNotificationNextIndex;
  }
  if (familyNotificationStore.count == 0 ||
      familyNotificationNextIndex >= familyNotificationStore.count) {
    familyNotificationNextIndex = 0;
  }

  if (!persistFamilyNotificationQueue()) {
    familyNotificationStore = previous;
    familyNotificationNextIndex = previousNextIndex;
    return false;
  }
  familyNotificationLastSentMs = 0;
  return true;
}

void loadCareEventHistory() {
  resetCareEventStore();
  Preferences preferences;
  if (preferences.begin("velacarecare", true)) {
    if (preferences.getBytesLength("history") == sizeof(careEventStore)) {
      CareEventStore stored = {};
      if (preferences.getBytes("history", &stored, sizeof(stored)) ==
              sizeof(stored) &&
          stored.magic == kCareEventStoreMagic &&
          stored.count <= kCareEventCapacity && stored.nextId != 0) {
        careEventStore = stored;
      }
    }
    preferences.end();
  }

  Serial.printf("CARE history loaded count=%u\n",
                static_cast<unsigned int>(careEventStore.count));
}

CareEventRecord *findCareEvent(uint8_t type, uint32_t requestId) {
  for (size_t i = 0; i < careEventStore.count; ++i) {
    CareEventRecord &event = careEventStore.records[i];
    if (event.type == type && event.requestId == requestId) {
      return &event;
    }
  }
  return nullptr;
}

void rememberCurrentBootHandledCareEvent(uint32_t eventId) {
  if (eventId == 0) {
    return;
  }
  const uint32_t now = millis();
  size_t replacement = 0;
  uint32_t oldestAge = 0;
  for (size_t i = 0; i < kCareEventCapacity; ++i) {
    CurrentBootHandledCareEvent &entry = currentBootHandledCareEvents[i];
    if (entry.eventId == eventId || entry.eventId == 0) {
      entry.eventId = eventId;
      entry.handledAtMs = now;
      return;
    }
    const uint32_t age = static_cast<uint32_t>(now - entry.handledAtMs);
    if (age >= oldestAge) {
      oldestAge = age;
      replacement = i;
    }
  }
  currentBootHandledCareEvents[replacement].eventId = eventId;
  currentBootHandledCareEvents[replacement].handledAtMs = now;
}

bool currentBootHandledCareEventIsRecent(uint32_t eventId) {
  const uint32_t now = millis();
  for (size_t i = 0; i < kCareEventCapacity; ++i) {
    const CurrentBootHandledCareEvent &entry = currentBootHandledCareEvents[i];
    if (entry.eventId == eventId) {
      return static_cast<uint32_t>(now - entry.handledAtMs) <=
             kCareRequestDedupMs;
    }
  }
  return false;
}

bool careEventWasRecentlyHandled(uint8_t type, uint32_t requestId) {
  CareEventRecord *event = findCareEvent(type, requestId);
  if (event == nullptr || event->outcome == kCarePending) {
    return false;
  }

  const uint32_t dedupSeconds = kCareRequestDedupMs / 1000;
  const uint32_t nowEpoch = currentEpoch();
  const uint32_t handledEpoch =
      event->endEpoch != 0 ? event->endEpoch : event->startEpoch;
  if (nowEpoch != 0 && handledEpoch != 0) {
    if (nowEpoch < handledEpoch) {
      return true;
    }
    return nowEpoch - handledEpoch <= dedupSeconds;
  }
  if (currentBootHandledCareEventIsRecent(event->id)) {
    return true;
  }

  // A persisted event without a trustworthy wall clock belongs to an earlier
  // boot. Its uptime must never be compared with this boot's millis(). Fail
  // closed while the record remains in history so an old request ID cannot be
  // mistaken for a new request.
  return true;
}

void finishCareEvent(uint8_t type, uint32_t requestId, uint8_t outcome,
                     uint8_t reply) {
  CareEventRecord *event = findCareEvent(type, requestId);
  if (event == nullptr || event->outcome != kCarePending) {
    return;
  }
  event->endEpoch = currentEpoch();
  event->endUptime = millis() / 1000;
  event->outcome = outcome;
  event->reply = reply;
  event->syncState = 0;
  rememberCurrentBootHandledCareEvent(event->id);
  persistCareEventHistory();
  Serial.printf("CARE resolved event=%lu request=%lu type=%s outcome=%s reply=%u\n",
                static_cast<unsigned long>(event->id),
                static_cast<unsigned long>(requestId),
                careEventTypeName(type), careEventOutcomeName(outcome), reply);
}

bool beginCareEvent(uint8_t type, uint32_t requestId) {
  if (type > kCareWellbeing || requestId == 0) {
    return false;
  }
  CareEventRecord *existing = findCareEvent(type, requestId);
  if (existing != nullptr && existing->outcome == kCarePending) {
    return false;
  }

  size_t newCount = careEventStore.count;
  size_t insertionTail = newCount;
  if (newCount < kCareEventCapacity) {
    ++newCount;
    insertionTail = newCount - 1;
  } else {
    bool foundCompletedVictim = false;
    for (size_t i = careEventStore.count; i > 0; --i) {
      const size_t candidate = i - 1;
      if (careEventStore.records[candidate].outcome != kCarePending) {
        insertionTail = candidate;
        foundCompletedVictim = true;
        break;
      }
    }
    if (!foundCompletedVictim) {
      Serial.println("ERR care event history full of active requests");
      return false;
    }
  }
  for (size_t i = insertionTail; i > 0; --i) {
    careEventStore.records[i] = careEventStore.records[i - 1];
  }
  careEventStore.count = static_cast<uint8_t>(newCount);
  CareEventRecord &event = careEventStore.records[0];
  memset(&event, 0, sizeof(event));
  event.id = careEventStore.nextId++;
  if (careEventStore.nextId == 0) {
    careEventStore.nextId = 1;
  }
  event.requestId = requestId;
  event.startEpoch = currentEpoch();
  event.startUptime = millis() / 1000;
  event.type = type;
  event.outcome = kCarePending;
  event.syncState = 0;
  persistCareEventHistory();
  Serial.printf("CARE start event=%lu request=%lu type=%s\n",
                static_cast<unsigned long>(event.id),
                static_cast<unsigned long>(requestId),
                careEventTypeName(type));
  return true;
}

void beginAlarmEvent(uint8_t sensorIndex, AlarmSeverity severity) {
  const size_t newCount =
      eventStore.count < kEventCapacity ? eventStore.count + 1 : kEventCapacity;
  for (size_t i = newCount - 1; i > 0; --i) {
    eventStore.records[i] = eventStore.records[i - 1];
  }
  eventStore.count = static_cast<uint8_t>(newCount);
  EventRecord &event = eventStore.records[0];
  memset(&event, 0, sizeof(event));
  event.id = eventStore.nextId++;
  if (eventStore.nextId == 0) {
    eventStore.nextId = 1;
  }
  event.startEpoch = currentEpoch();
  event.startUptime = millis() / 1000;
  event.sensorIndex = sensorIndex;
  event.outcome = kEventActive;
  event.severity = static_cast<uint8_t>(severity);
  event.syncState = 0;
  persistEventHistory();
  Serial.printf("EVENT start id=%lu sensor=%s\n",
                static_cast<unsigned long>(event.id),
                eventSensorName(sensorIndex));
}

void setEventSeverity(uint8_t sensorIndex, AlarmSeverity severity) {
  if (sensorIndex >= 4 || alarmSeverities[sensorIndex] == severity) {
    return;
  }
  alarmSeverities[sensorIndex] = severity;
  for (size_t i = 0; i < eventStore.count; ++i) {
    EventRecord &event = eventStore.records[i];
    if (event.sensorIndex == sensorIndex && event.outcome == kEventActive) {
      event.severity = static_cast<uint8_t>(severity);
      event.syncState = 0;
      persistEventHistory();
      Serial.printf("ALARM severity sensor=%s level=%s\n",
                    eventSensorName(sensorIndex), severityName(severity));
      return;
    }
  }
}

void markEventSyncPending(uint8_t sensorIndex) {
  for (size_t i = 0; i < eventStore.count; ++i) {
    EventRecord &event = eventStore.records[i];
    if (event.sensorIndex == sensorIndex && event.outcome == kEventActive) {
      event.syncState = 0;
    }
  }
}

void resolveAlarmEvent(uint8_t sensorIndex) {
  for (size_t i = 0; i < eventStore.count; ++i) {
    EventRecord &event = eventStore.records[i];
    if (event.sensorIndex == sensorIndex && event.outcome == kEventActive) {
      event.endEpoch = currentEpoch();
      event.endUptime = millis() / 1000;
      event.outcome = kEventRecovered;
      event.syncState = 0;
      persistEventHistory();
      Serial.printf("EVENT resolved id=%lu sensor=%s duration=%lu\n",
                    static_cast<unsigned long>(event.id),
                    eventSensorName(sensorIndex),
                    static_cast<unsigned long>(event.endUptime -
                                               event.startUptime));
      return;
    }
  }
}

bool setSensorState(uint8_t sensorIndex, SensorState newState) {
  if (sensorIndex >= 4 || sensorStates[sensorIndex] == newState) {
    return false;
  }
  const SensorState oldState = sensorStates[sensorIndex];
  sensorStates[sensorIndex] = newState;
  if (newState == kAlarm && oldState != kAlarm) {
    alarmAcknowledged = false;
    alarmMutedUntilMs = 0;
    if (sensorIndex == 3) {
      elderConfirmed = false;
      const uint32_t generation = advanceFallVoiceGeneration();
      fallPromptHandledGeneration = generation;
      persistentPromptHandledGeneration = 0;
      elderPromptHandledGeneration = 0;
      queueAudioPrompt(VoicePromptId::kFall, generation, true);
    }
    alarmSinceMs[sensorIndex] = millis();
    alarmSeverities[sensorIndex] = kSeverityNotice;
    beginAlarmEvent(sensorIndex, alarmSeverities[sensorIndex]);
  } else if (oldState == kAlarm && newState != kAlarm) {
    alarmSinceMs[sensorIndex] = 0;
    alarmSeverities[sensorIndex] = kSeverityNotice;
    resolveAlarmEvent(sensorIndex);
    if (sensorIndex == 3) {
      elderConfirmed = false;
      // A generation change invalidates only pending/playing fall warnings.
      // Family/elder acknowledgement prompts remain independent.
      advanceFallVoiceGeneration();
      fallPromptHandledGeneration = 0;
      persistentPromptHandledGeneration = 0;
      elderPromptHandledGeneration = 0;
    }
    if (!anyAlarmActive()) {
      alarmAcknowledged = false;
      alarmMutedUntilMs = 0;
    }
  }
  updateBuzzer();
  heartbeatDirty = true;
  return true;
}

bool anyAlarmActive() {
  for (uint8_t sensorIndex = 0; sensorIndex < 4; ++sensorIndex) {
    if (sensorStates[sensorIndex] == kAlarm) {
      return true;
    }
  }
  return false;
}

AlarmSeverity highestAlarmSeverity() {
  AlarmSeverity result = kSeverityNotice;
  for (uint8_t sensorIndex = 0; sensorIndex < 4; ++sensorIndex) {
    if (sensorStates[sensorIndex] == kAlarm &&
        alarmSeverities[sensorIndex] > result) {
      result = alarmSeverities[sensorIndex];
    }
  }
  return result;
}

bool alarmMuted() {
  return alarmMutedUntilMs != 0 &&
         static_cast<int32_t>(millis() - alarmMutedUntilMs) < 0;
}

void updateBuzzer() {
  // The physical buzzer is on D12X. ESP32 only computes the desired state;
  // VC1 carries sensor alarms and VCB1 carries acknowledgement/mute intent.
  const uint32_t now = millis();
  const bool mutedNow = alarmMuted();
  const bool previousExpected = d12xAlarmExpected;
  d12xAlarmExpected = anyAlarmActive() && !alarmAcknowledged && !mutedNow;
  const bool stateChanged =
      !d12xControlInitialized || previousExpected != d12xAlarmExpected ||
      lastD12xMute != mutedNow || lastD12xAck != alarmAcknowledged;
  const bool periodicSyncDue =
      d12xControlInitialized &&
      static_cast<uint32_t>(now - lastBuzzerUpdateMs) >=
          kBuzzerControlSyncMs;
  if (stateChanged || periodicSyncDue) {
    d12xControlInitialized = true;
    lastD12xMute = mutedNow;
    lastD12xAck = alarmAcknowledged;
    sendBuzzerControlFrame();
    lastBuzzerUpdateMs = now;
  }
}

void acknowledgeAlarms() {
  const bool firstAcknowledgement = anyAlarmActive() && !alarmAcknowledged;
  const bool fallActiveAtAcknowledgement = sensorStates[3] == kAlarm;
  if (firstAcknowledgement) {
    alarmAcknowledged = true;
    stopAudioPlayback();
    if (fallActiveAtAcknowledgement && !alarmMuted()) {
      queueAudioPrompt(VoicePromptId::kFamily, 0, true);
    }
  }
  updateBuzzer();
  if (!firstAcknowledgement) {
    Serial.println("ALARM acknowledgement unchanged");
  } else if (fallActiveAtAcknowledgement && !alarmMuted()) {
    Serial.println("ALARM acknowledged; D12X buzzer stopped; family voice queued");
  } else {
    Serial.println("ALARM acknowledged; D12X buzzer stopped; no fall voice needed");
  }
}

void muteAlarms() {
  alarmMutedUntilMs = millis() + kAlarmMuteMs;
  stopAudioPlayback();
  updateBuzzer();
  Serial.println("ALARM muted for 10 minutes; all queued audio stopped");
}

void unmuteAlarms() {
  alarmMutedUntilMs = 0;
  updateBuzzer();
  Serial.println("ALARM mute cleared; previous voice prompts will not replay");
}

void updateAlarmEscalation() {
  const uint32_t now = millis();
  for (uint8_t sensorIndex = 0; sensorIndex < 4; ++sensorIndex) {
    if (sensorStates[sensorIndex] != kAlarm || alarmSinceMs[sensorIndex] == 0) {
      continue;
    }
    const uint32_t threshold = sensorIndex == 3
        ? kFallPersistentEscalationMs
        : kGenericPersistentEscalationMs;
    if (alarmSeverities[sensorIndex] != kSeverityPersistent &&
        static_cast<uint32_t>(now - alarmSinceMs[sensorIndex]) >= threshold) {
      setEventSeverity(sensorIndex, kSeverityPersistent);
      if (sensorIndex == 3) {
        const uint32_t generation = currentFallVoiceGeneration();
        if (persistentPromptHandledGeneration == generation) {
          noteAudioDeduplicated();
        } else {
          // Mark this generation handled even when muted/acknowledged so an old
          // escalation is never replayed later merely because mute was lifted.
          persistentPromptHandledGeneration = generation;
          if (!elderConfirmed && !alarmAcknowledged && !alarmMuted()) {
            queueAudioPrompt(VoicePromptId::kPersistent, generation, true);
          }
        }
      }
    }
  }
}

bool eventUploadConfigured() {
  return eventUploadUrl.startsWith("http://") ||
         eventUploadUrl.startsWith("https://");
}

int findPendingEventIndex() {
  // The records are newest-first; replay the oldest pending event first.
  for (int index = static_cast<int>(eventStore.count) - 1; index >= 0; --index) {
    if (eventStore.records[index].syncState == 0) {
      return index;
    }
  }
  return -1;
}

int findPendingCareEventIndex() {
  for (int index = static_cast<int>(careEventStore.count) - 1;
       index >= 0; --index) {
    if (careEventStore.records[index].syncState == 0) {
      return index;
    }
  }
  return -1;
}

void updateEventUpload() {
  if (!wifiOnline || !eventUploadConfigured()) {
    return;
  }
  const uint32_t now = millis();
  if (static_cast<uint32_t>(now - lastEventUploadMs) < kEventUploadRetryMs) {
    return;
  }
  const int index = findPendingEventIndex();
  const int careIndex = findPendingCareEventIndex();
  if (index < 0 && careIndex < 0) {
    return;
  }
  lastEventUploadMs = now;
  const uint32_t nowUptime = millis() / 1000;
  String payload;
  payload.reserve(320);
  bool uploadingCare = index < 0;
  if (!uploadingCare) {
    const EventRecord &event = eventStore.records[index];
    uint32_t duration = 0;
    if (event.outcome == kEventRecovered && event.endUptime >= event.startUptime) {
      duration = event.endUptime - event.startUptime;
    } else if (nowUptime >= event.startUptime) {
      duration = nowUptime - event.startUptime;
    }
    payload += F("{\"device\":\"velacare-esp32\",\"category\":\"sensor\",\"id\":");
    payload += event.id;
    payload += F(",\"sensor\":\"");
    payload += eventSensorName(event.sensorIndex);
    payload += F("\",\"severity\":\"");
    payload += severityName(static_cast<AlarmSeverity>(event.severity));
    payload += F("\",\"startEpoch\":");
    payload += event.startEpoch;
    payload += F(",\"duration\":");
    payload += duration;
    payload += F(",\"outcome\":");
    payload += event.outcome;
    payload += '}';
  } else {
    const CareEventRecord &event = careEventStore.records[careIndex];
    uint32_t duration = 0;
    if (event.outcome != kCarePending && event.endUptime >= event.startUptime) {
      duration = event.endUptime - event.startUptime;
    } else if (nowUptime >= event.startUptime) {
      duration = nowUptime - event.startUptime;
    }
    payload += F("{\"device\":\"velacare-esp32\",\"category\":\"care\",\"id\":");
    payload += event.id;
    payload += F(",\"requestId\":");
    payload += event.requestId;
    payload += F(",\"type\":\"");
    payload += careEventTypeName(event.type);
    payload += F("\",\"startEpoch\":");
    payload += event.startEpoch;
    payload += F(",\"duration\":");
    payload += duration;
    payload += F(",\"outcome\":\"");
    payload += careEventOutcomeName(event.outcome);
    payload += F("\",\"reply\":");
    payload += static_cast<unsigned int>(event.reply);
    payload += '}';
  }

  WiFiClient client;
  HTTPClient http;
  if (!http.begin(client, eventUploadUrl)) {
    Serial.println("EVENT upload begin failed; keeping pending");
    return;
  }
  http.setTimeout(2500);
  http.addHeader("Content-Type", "application/json");
  const int code = http.POST(payload);
  http.end();
  if (code >= 200 && code < 300) {
    if (uploadingCare) {
      const uint32_t uploadedId = careEventStore.records[careIndex].id;
      careEventStore.records[careIndex].syncState = 1;
      persistCareEventHistory();
      Serial.printf("CARE uploaded id=%lu code=%d\n",
                    static_cast<unsigned long>(uploadedId), code);
    } else {
      const uint32_t uploadedId = eventStore.records[index].id;
      eventStore.records[index].syncState = 1;
      persistEventHistory();
      Serial.printf("EVENT uploaded id=%lu code=%d\n",
                    static_cast<unsigned long>(uploadedId), code);
    }
  } else {
    const uint32_t pendingId = uploadingCare
        ? careEventStore.records[careIndex].id
        : eventStore.records[index].id;
    Serial.printf("%s upload pending id=%lu code=%d\n",
                  uploadingCare ? "CARE" : "EVENT",
                  static_cast<unsigned long>(pendingId), code);
  }
}

bool writeImuRegister(uint8_t reg, uint8_t value) {
  Wire.beginTransmission(kImuAddress);
  Wire.write(reg);
  Wire.write(value);
  return Wire.endTransmission() == 0;
}

bool readImuRegister(uint8_t reg, uint8_t &value) {
  Wire.beginTransmission(kImuAddress);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0 ||
      Wire.requestFrom(static_cast<int>(kImuAddress), 1, true) != 1) {
    return false;
  }
  value = Wire.read();
  return true;
}

bool beginImu() {
  Wire.begin(kImuSdaPin, kImuSclPin, 100000);
  delay(10);

  if (!readImuRegister(kImuWhoAmIRegister, imuWhoAmI) ||
      (imuWhoAmI != kMpu6500WhoAmI && imuWhoAmI != kMpu9250WhoAmI)) {
    Serial.printf("IMU not detected on 0x%02X WHO_AM_I=0x%02X\n",
                  kImuAddress, imuWhoAmI);
    return false;
  }

  // Wake the device, configure 50 Hz sampling, ±500 dps gyro and ±8 g accel.
  const bool configured =
      writeImuRegister(kImuPowerManagementRegister, 0x00) &&
      writeImuRegister(kImuConfigRegister, 0x03) &&
      writeImuRegister(kImuSampleRateDividerRegister, 19) &&
      writeImuRegister(kImuGyroConfigRegister, 0x08) &&
      writeImuRegister(kImuAccelConfigRegister, 0x10) &&
      writeImuRegister(kImuAccelConfig2Register, 0x03);
  if (!configured) {
    Serial.println("IMU configuration failed");
    return false;
  }

  imuAvailable = true;
  imuReadErrorCount = 0;
  fallPhase = kFallMonitoring;
  fallPhaseSinceMs = millis();
  Serial.printf("IMU detected MPU-%s address=0x%02X SDA=GPIO%d SCL=GPIO%d\n",
                imuWhoAmI == kMpu6500WhoAmI ? "6500" : "9250", kImuAddress,
                kImuSdaPin, kImuSclPin);
  Serial.printf(
      "IMU fixed anchors safe=(%.3f,%.3f,%.3f) fall=(%.3f,%.3f,%.3f) separation=87.0deg\n",
      imuReferenceAx, imuReferenceAy, imuReferenceAz, kFallAnchorAx,
      kFallAnchorAy, kFallAnchorAz);
  return true;
}

void resetFallDetector() {
  fallPhase = kFallMonitoring;
  fallPhaseSinceMs = millis();
  fallStillSinceMs = 0;
  fallKnownPoseSinceMs = 0;
  fallRecoverSinceMs = 0;
  fallRecoverBadSinceMs = 0;
  lastFallRecoverHintMs = 0;
  fallRecoveryInSafeZone = false;
  fallPostureChanged = false;
}

void updateImuSensor() {
  if (!imuAvailable) {
    return;
  }
  const uint32_t now = millis();
  if (static_cast<uint32_t>(now - lastImuSampleMs) < kImuSamplePeriodMs) {
    return;
  }
  lastImuSampleMs = now;

  uint8_t raw[14] = {};
  Wire.beginTransmission(kImuAddress);
  Wire.write(kImuAccelDataRegister);
  const bool addressOk = Wire.endTransmission(false) == 0;
  const int received = addressOk
                           ? Wire.requestFrom(static_cast<int>(kImuAddress),
                                              static_cast<int>(sizeof(raw)),
                                              true)
                           : 0;
  if (!addressOk || received != static_cast<int>(sizeof(raw))) {
    ++imuReadErrorCount;
    if (imuReadErrorCount == 1 || imuReadErrorCount == 10) {
      Serial.printf("IMU read error count=%lu\n",
                    static_cast<unsigned long>(imuReadErrorCount));
    }
    if (imuReadErrorCount >= 10 && sensorStates[3] != kAlarm) {
      setSensorState(3, kOffline);
    }
    return;
  }
  for (uint8_t i = 0; i < sizeof(raw); ++i) {
    raw[i] = Wire.read();
  }
  imuReadErrorCount = 0;
  if (sensorStates[3] == kOffline) {
    setSensorState(3, kNormal);
  }

  const int16_t axRaw = static_cast<int16_t>((raw[0] << 8) | raw[1]);
  const int16_t ayRaw = static_cast<int16_t>((raw[2] << 8) | raw[3]);
  const int16_t azRaw = static_cast<int16_t>((raw[4] << 8) | raw[5]);
  const int16_t gxRaw = static_cast<int16_t>((raw[8] << 8) | raw[9]);
  const int16_t gyRaw = static_cast<int16_t>((raw[10] << 8) | raw[11]);
  const int16_t gzRaw = static_cast<int16_t>((raw[12] << 8) | raw[13]);
  const float ax = static_cast<float>(axRaw) / 4096.0f;
  const float ay = static_cast<float>(ayRaw) / 4096.0f;
  const float az = static_cast<float>(azRaw) / 4096.0f;
  const float gx = static_cast<float>(gxRaw) / 65.5f;
  const float gy = static_cast<float>(gyRaw) / 65.5f;
  const float gz = static_cast<float>(gzRaw) / 65.5f;
  const float accelerationG = sqrtf(ax * ax + ay * ay + az * az);
  const float gyroDps = sqrtf(gx * gx + gy * gy + gz * gz);
  lastImuAccelerationG = accelerationG;
  lastImuGyroDps = gyroDps;
  lastImuGoodReadMs = now;
  if (accelerationG < 0.10f) {
    return;
  }

  const float currentAx = ax / accelerationG;
  const float currentAy = ay / accelerationG;
  const float currentAz = az / accelerationG;
  float dot = imuReferenceAx * currentAx + imuReferenceAy * currentAy +
              imuReferenceAz * currentAz;
  dot = constrain(dot, -1.0f, 1.0f);
  const float postureAngle = acosf(dot) * 57.29578f;
  float fallDot = kFallAnchorAx * currentAx + kFallAnchorAy * currentAy +
                  kFallAnchorAz * currentAz;
  fallDot = constrain(fallDot, -1.0f, 1.0f);
  const float fallPoseAngle = acosf(fallDot) * 57.29578f;
  lastImuPostureDeg = postureAngle;
  lastImuFallPoseDeg = fallPoseAngle;

  const bool stable = accelerationG >= kFallStableMinG &&
                      accelerationG <= kFallStableMaxG &&
                      gyroDps < kFallStillGyroDps;

  if (fallPhase == kFallAlarmed) {
    if (!fallRecoveryInSafeZone && postureAngle <= kFallSafeEnterDeg) {
      fallRecoveryInSafeZone = true;
    } else if (fallRecoveryInSafeZone && postureAngle >= kFallSafeExitDeg) {
      fallRecoveryInSafeZone = false;
    }
    const bool recoveredPose =
        fallRecoveryInSafeZone && accelerationG >= kFallRecoveryMinG &&
        accelerationG <= kFallRecoveryMaxG &&
        gyroDps < kFallRecoveryGyroDps;
    if (recoveredPose) {
      fallRecoverBadSinceMs = 0;
      if (fallRecoverSinceMs == 0) {
        fallRecoverSinceMs = now;
      }
      if (static_cast<uint32_t>(now - fallRecoverSinceMs) >=
          kFallRecoveryConfirmMs) {
        Serial.printf("FALL recovered posture=%.1f accel=%.2fg\n",
                      postureAngle, accelerationG);
        setSensorState(3, kNormal);
        resetFallDetector();
      }
    } else if (fallRecoverSinceMs != 0) {
      if (fallRecoverBadSinceMs == 0) {
        fallRecoverBadSinceMs = now;
      } else if (static_cast<uint32_t>(now - fallRecoverBadSinceMs) >
                 kFallRecoveryBadSampleToleranceMs) {
        fallRecoverSinceMs = 0;
        fallRecoverBadSinceMs = 0;
      }
    }
    if (static_cast<uint32_t>(now - lastFallRecoverHintMs) >= 1000) {
      lastFallRecoverHintMs = now;
      const uint32_t holdMs = fallRecoverSinceMs != 0
                                  ? static_cast<uint32_t>(now - fallRecoverSinceMs)
                                  : 0;
      Serial.printf(
          "FALL wait-recover safe=%.1f need<=%.0f/%.0f fall=%.1f stable=%s hold=%lums/%lums accel=%.2fg gyro=%.1f\n",
          postureAngle, kFallSafeEnterDeg, kFallSafeExitDeg, fallPoseAngle,
          recoveredPose ? "yes" : "no",
          static_cast<unsigned long>(holdMs),
          static_cast<unsigned long>(kFallRecoveryConfirmMs),
          accelerationG, gyroDps);
    }
    return;
  }

  if (fallPhase == kFallMonitoring) {
    const bool knownFallPose =
        stable && postureAngle >= kFallGenericPoseDeg &&
        fallPoseAngle <= kFallKnownPoseDeg;
    if (knownFallPose) {
      if (fallKnownPoseSinceMs == 0) {
        fallKnownPoseSinceMs = now;
        Serial.printf("FALL candidate=known-pose safe=%.1f fall=%.1f\n",
                      postureAngle, fallPoseAngle);
      } else if (static_cast<uint32_t>(now - fallKnownPoseSinceMs) >=
                 kFallKnownPoseConfirmMs) {
        setSensorState(3, kAlarm);
        setEventSeverity(3, kSeveritySerious);
        fallPhase = kFallAlarmed;
        fallRecoverSinceMs = 0;
        fallRecoverBadSinceMs = 0;
        fallRecoveryInSafeZone = false;
        Serial.printf("FALL confirmed source=known-pose safe=%.1f fall=%.1f\n",
                      postureAngle, fallPoseAngle);
        return;
      }
    } else {
      fallKnownPoseSinceMs = 0;
    }
    if (accelerationG < kFallFreeFallThresholdG) {
      fallPhase = kFallFreeFall;
      fallPhaseSinceMs = now;
      Serial.println("FALL candidate=free-fall");
    } else if (accelerationG > kFallImpactThresholdG &&
               gyroDps > kFallImpactGyroDps) {
      fallPhase = kFallPostImpact;
      fallPhaseSinceMs = now;
      fallStillSinceMs = 0;
      fallPostureChanged = postureAngle >= kFallGenericPoseDeg;
      Serial.println("FALL candidate=impact");
    }
    return;
  }

  if (fallPhase == kFallFreeFall) {
    if (accelerationG > kFallImpactThresholdG &&
        gyroDps > kFallImpactGyroDps) {
      fallPhase = kFallPostImpact;
      fallPhaseSinceMs = now;
      fallStillSinceMs = 0;
      fallPostureChanged = postureAngle >= kFallGenericPoseDeg;
      Serial.println("FALL candidate=impact-after-free-fall");
    } else if (static_cast<uint32_t>(now - fallPhaseSinceMs) >
               kFallFreeFallTimeoutMs) {
      resetFallDetector();
    }
    return;
  }

  if (fallPhase == kFallPostImpact) {
    if (postureAngle >= kFallGenericPoseDeg) {
      fallPostureChanged = true;
    }
    if (stable) {
      if (fallStillSinceMs == 0) {
        fallStillSinceMs = now;
      }
      if (fallPostureChanged &&
          static_cast<uint32_t>(now - fallStillSinceMs) >=
              kFallStillnessConfirmMs) {
        setSensorState(3, kAlarm);
        // The combined free-fall + impact + posture + stillness evidence is
        // already stronger than a simple digital sensor trigger.
        setEventSeverity(3, kSeveritySerious);
        fallPhase = kFallAlarmed;
        fallRecoverSinceMs = 0;
        Serial.printf("FALL confirmed alarm posture_change=%.1f accel=%.2fg\n",
                      postureAngle, accelerationG);
        // The phase changed to kFallAlarmed. Stop processing this sample so
        // the post-impact timeout below cannot reset the freshly confirmed
        // alarm back to monitoring before recovery handling gets a chance to
        // run on the next IMU sample.
        return;
      }
    } else {
      fallStillSinceMs = 0;
    }
    if (static_cast<uint32_t>(now - fallPhaseSinceMs) >
        kFallPostImpactWindowMs) {
      resetFallDetector();
    }
  }
}

const char *fallPhaseName(FallPhase phase) {
  switch (phase) {
    case kFallMonitoring:
      return "monitoring";
    case kFallFreeFall:
      return "free-fall";
    case kFallPostImpact:
      return "post-impact";
    case kFallAlarmed:
      return "alarmed";
  }
  return "unknown";
}

void printImuStatus() {
  const uint32_t age = lastImuGoodReadMs == 0
      ? 0
      : static_cast<uint32_t>(millis() - lastImuGoodReadMs);
  Serial.printf(
      "IMU status available=%s who=0x%02X sda=%d scl=%d age_ms=%lu errors=%lu accel=%.3fg gyro=%.1fdps safe=%.1fdeg fall=%.1fdeg recover<=%.0f/%.0f anchors=fixed phase=%s\n",
      imuAvailable ? "yes" : "no", imuWhoAmI, kImuSdaPin, kImuSclPin,
      static_cast<unsigned long>(age),
      static_cast<unsigned long>(imuReadErrorCount), lastImuAccelerationG,
      lastImuGyroDps, lastImuPostureDeg, lastImuFallPoseDeg,
      kFallSafeEnterDeg, kFallSafeExitDeg, fallPhaseName(fallPhase));
}

String buildDiagnosticsJson() {
  const uint32_t age = lastImuGoodReadMs == 0
      ? 0
      : static_cast<uint32_t>(millis() - lastImuGoodReadMs);
  const uint32_t remaining = alarmMuted() ? alarmMutedUntilMs - millis() : 0;
  String json;
  json.reserve(1500);
  json += F("{\"version\":\"");
  json += kFirmwareVersion;
  json += F("\",\"imu\":{\"available\":");
  json += imuAvailable ? F("true") : F("false");
  json += F(",\"whoAmI\":");
  json += imuWhoAmI;
  json += F(",\"sda\":");
  json += kImuSdaPin;
  json += F(",\"scl\":");
  json += kImuSclPin;
  json += F(",\"lastReadMs\":");
  json += age;
  json += F(",\"readErrors\":");
  json += imuReadErrorCount;
  json += F(",\"accelG\":");
  json += String(lastImuAccelerationG, 3);
  json += F(",\"gyroDps\":");
  json += String(lastImuGyroDps, 1);
  json += F(",\"postureDeg\":");
  json += String(lastImuPostureDeg, 1);
  json += F(",\"safeAngleDeg\":");
  json += String(lastImuPostureDeg, 1);
  json += F(",\"fallAngleDeg\":");
  json += String(lastImuFallPoseDeg, 1);
  json += F(",\"anchorsFixed\":true");
  json += F(",\"phase\":\"");
  json += fallPhaseName(fallPhase);
  json += F("\"},\"inputs\":{\"waterLevel\":");
  json += digitalRead(kWaterSensorPin);
  json += F(",\"waterState\":\"");
  json += stateName(sensorStates[1]);
  json += F("\",\"smokeLevel\":");
  json += digitalRead(kSmokeSensorPin);
  json += F(",\"smokeState\":\"");
  json += stateName(sensorStates[0]);
  json += F("\",\"doorLevel\":");
  json += digitalRead(kDoorSensorPin);
  json += F(",\"doorState\":\"");
  json += stateName(sensorStates[2]);
  json += F("\"},\"alarm\":{\"severity\":\"");
  json += severityName(highestAlarmSeverity());
  json += F("\",\"acknowledged\":");
  json += alarmAcknowledged ? F("true") : F("false");
  json += F(",\"muted\":");
  json += alarmMuted() ? F("true") : F("false");
  json += F(",\"muteRemainingMs\":");
  json += remaining;
  json += F(",\"d12xAlarm\":");
  json += d12xAlarmExpected ? F("true") : F("false");
  json += F("}}");
  return json;
}

void updateWaterSensor() {
  const uint32_t now = millis();
  const int level = digitalRead(kWaterSensorPin);

  if (!waterInputInitialized) {
    waterCandidateLevel = level;
    waterCandidateSinceMs = now;
    waterInputInitialized = true;
    Serial.printf("WATER input GPIO%d initial=%s\n", kWaterSensorPin,
                  level == kWaterAlarmLevel ? "wet" : "dry");
    return;
  }

  if (level != waterCandidateLevel) {
    waterCandidateLevel = level;
    waterCandidateSinceMs = now;
    Serial.printf("WATER input GPIO%d candidate=%s\n", kWaterSensorPin,
                  level == kWaterAlarmLevel ? "wet" : "dry");
    return;
  }

  const bool wet = level == kWaterAlarmLevel;
  const uint32_t confirmMs =
      wet ? kWaterAlarmConfirmMs : kWaterRecoveryConfirmMs;
  if (static_cast<uint32_t>(now - waterCandidateSinceMs) < confirmMs) {
    return;
  }

  const SensorState desiredState = wet ? kAlarm : kNormal;
  if (setSensorState(1, desiredState)) {
    Serial.printf("WATER confirmed=%s state=%s\n", wet ? "wet" : "dry",
                  stateName(desiredState));
  }
}

void updateSmokeSensor() {
  const uint32_t now = millis();
  if (now < kSmokeWarmupMs) {
    return;
  }

  const int level = digitalRead(kSmokeSensorPin);
  if (!smokeInputInitialized) {
    smokeCandidateLevel = level;
    smokeCandidateSinceMs = now;
    smokeInputInitialized = true;
    Serial.printf("SMOKE input GPIO%d warmup complete, initial=%s\n",
                  kSmokeSensorPin,
                  level == kSmokeAlarmLevel ? "alarm" : "clear");
    return;
  }

  if (level != smokeCandidateLevel) {
    smokeCandidateLevel = level;
    smokeCandidateSinceMs = now;
    Serial.printf("SMOKE input GPIO%d candidate=%s\n", kSmokeSensorPin,
                  level == kSmokeAlarmLevel ? "alarm" : "clear");
    return;
  }

  const bool smokeDetected = level == kSmokeAlarmLevel;
  const uint32_t confirmMs =
      smokeDetected ? kSmokeAlarmConfirmMs : kSmokeRecoveryConfirmMs;
  if (static_cast<uint32_t>(now - smokeCandidateSinceMs) < confirmMs) {
    return;
  }

  const SensorState desiredState = smokeDetected ? kAlarm : kNormal;
  if (setSensorState(0, desiredState)) {
    Serial.printf("SMOKE confirmed=%s state=%s\n",
                  smokeDetected ? "alarm" : "clear",
                  stateName(desiredState));
  }
}

void updateDoorSensor() {
  const uint32_t now = millis();
  const int level = digitalRead(kDoorSensorPin);
  if (!doorInputInitialized) {
    doorCandidateLevel = level;
    doorCandidateSinceMs = now;
    doorInputInitialized = true;
    Serial.printf("DOOR input GPIO%d initial=%s\n", kDoorSensorPin,
                  level == LOW ? "closed" : "open");
    return;
  }
  if (level != doorCandidateLevel) {
    doorCandidateLevel = level;
    doorCandidateSinceMs = now;
    Serial.printf("DOOR input GPIO%d candidate=%s\n", kDoorSensorPin,
                  level == LOW ? "closed" : "open");
    return;
  }
  if (static_cast<uint32_t>(now - doorCandidateSinceMs) < kDoorDebounceMs) {
    return;
  }
  const bool open = level == HIGH;
  const SensorState desiredState = open ? kAlarm : kNormal;
  if (setSensorState(2, desiredState)) {
    Serial.printf("DOOR confirmed=%s state=%s\n", open ? "open" : "closed",
                  stateName(desiredState));
  }
}
String htmlEscape(String text) {
  text.replace("&", "&amp;");
  text.replace("<", "&lt;");
  text.replace(">", "&gt;");
  text.replace("\"", "&quot;");
  text.replace("'", "&#39;");
  return text;
}

bool loadWifiCredentials(String &ssid, String &password) {
  Preferences preferences;
  if (!preferences.begin("velacare", true)) {
    Serial.println("ERR unable to open Wi-Fi preferences");
    return false;
  }
  ssid = preferences.getString("ssid", "");
  password = preferences.getString("password", "");
  preferences.end();
  return !ssid.isEmpty();
}

bool saveWifiCredentials(const String &ssid, const String &password) {
  Preferences preferences;
  if (!preferences.begin("velacare", false)) {
    return false;
  }
  const bool ok = preferences.putString("ssid", ssid) == ssid.length() &&
                  preferences.putString("password", password) == password.length();
  preferences.end();
  return ok;
}

void clearWifiCredentials() {
  Preferences preferences;
  if (preferences.begin("velacare", false)) {
    preferences.clear();
    preferences.end();
  }
}

String normalizeCaregiverPhone(const String &input) {
  String result;
  result.reserve(input.length());
  for (size_t i = 0; i < input.length(); ++i) {
    const char ch = input[i];
    if (ch >= '0' && ch <= '9') {
      result += ch;
    } else if (ch != ' ' && ch != '-' && ch != '(' && ch != ')') {
      return String();
    }
  }
  return result;
}

bool caregiverPhoneIsValid(const String &phone) {
  return phone.length() >= 6 && phone.length() <= 20;
}

void loadCaregiverSettings() {
  Preferences preferences;
  if (preferences.begin("velaset", true)) {
    caregiverName = preferences.getString("name", "");
    caregiverPhone = preferences.getString("phone", "");
    caregiverRevision = preferences.getUInt("revision", 1);
    preferences.end();
  }
  if (!caregiverPhoneIsValid(caregiverPhone)) {
    caregiverPhone = "";
  }
  if (caregiverRevision == 0) {
    caregiverRevision = 1;
  }
  Serial.printf("CAREGIVER loaded configured=%s revision=%lu\n",
                caregiverPhone.isEmpty() ? "no" : "yes",
                static_cast<unsigned long>(caregiverRevision));
}

bool saveCaregiverSettings(const String &name, const String &phone) {
  Preferences preferences;
  uint32_t nextRevision = caregiverRevision + 1;

  if (!preferences.begin("velaset", false)) {
    return false;
  }

  if (nextRevision == 0) nextRevision = 1;
  const bool ok = preferences.putString("name", name) == name.length() &&
                  preferences.putString("phone", phone) == phone.length() &&
                  preferences.putUInt("revision", nextRevision) ==
                      sizeof(nextRevision);
  preferences.end();
  if (ok) {
    caregiverRevision = nextRevision;
    caregiverName = name;
    caregiverPhone = phone;
  }
  return ok;
}

void loadEventUploadSettings() {
  Preferences preferences;
  if (preferences.begin("velaupload", true)) {
    eventUploadUrl = preferences.getString("url", "");
    preferences.end();
  }
  eventUploadUrl.trim();
  if (!eventUploadConfigured()) {
    eventUploadUrl = "";
  }
  Serial.printf("EVENT upload configured=%s\n",
                eventUploadUrl.isEmpty() ? "no" : "yes");
}

bool saveEventUploadUrl(const String &url) {
  Preferences preferences;
  if (!preferences.begin("velaupload", false)) {
    return false;
  }
  const bool ok = preferences.putString("url", url) == url.length();
  preferences.end();
  if (ok) {
    eventUploadUrl = url;
    eventUploadUrl.trim();
    if (!eventUploadConfigured()) {
      eventUploadUrl = "";
    }
  }
  return ok;
}

void loadSosState() {
  Preferences preferences;
  if (preferences.begin("velasos", true)) {
    sosActive = preferences.getBool("active", false);
    sosRequestId = preferences.getUInt("id", 0);
    sosEpoch = preferences.getUInt("epoch", 0);
    sosUptime = preferences.getUInt("uptime", 0);
    legacyFamilyNotificationPending =
        preferences.getBool("famPending", false);
    legacyFamilyNotificationRequestId = preferences.getUInt("famId", 0);
    legacyFamilyNotificationType =
        preferences.getUChar("famType", kCareEmergency);
    legacyFamilyNotificationReply =
        preferences.getUChar("famReply", kFamilyReplyNone);
    preferences.end();
  }
  if (sosRequestId == 0) {
    sosActive = false;
    sosEpoch = 0;
    sosUptime = 0;
  }
}

void persistSosState() {
  Preferences preferences;
  if (!preferences.begin("velasos", false)) return;
  preferences.putBool("active", sosActive);
  preferences.putUInt("id", sosRequestId);
  preferences.putUInt("epoch", sosEpoch);
  preferences.putUInt("uptime", sosUptime);
  preferences.end();
}

void clearLegacyFamilyNotificationState() {
  Preferences preferences;
  if (preferences.begin("velasos", false)) {
    preferences.remove("famPending");
    preferences.remove("famId");
    preferences.remove("famType");
    preferences.remove("famReply");
    preferences.end();
  }
  legacyFamilyNotificationPending = false;
  legacyFamilyNotificationRequestId = 0;
  legacyFamilyNotificationType = kCareEmergency;
  legacyFamilyNotificationReply = kFamilyReplyNone;
}

void migrateLegacyFamilyNotificationState() {
  if (!legacyFamilyNotificationPending) {
    if (legacyFamilyNotificationRequestId != 0 ||
        legacyFamilyNotificationType != kCareEmergency ||
        legacyFamilyNotificationReply != kFamilyReplyNone) {
      clearLegacyFamilyNotificationState();
    }
    return;
  }
  if (legacyFamilyNotificationRequestId == 0 ||
      legacyFamilyNotificationType > kCareWellbeing ||
      legacyFamilyNotificationReply < kFamilyReplySeen ||
      legacyFamilyNotificationReply > kFamilyReplyRest) {
    Serial.println("FAMILY discarded invalid legacy notification");
    clearLegacyFamilyNotificationState();
    return;
  }
  if (enqueueFamilyNotification(legacyFamilyNotificationRequestId,
                                legacyFamilyNotificationType,
                                legacyFamilyNotificationReply)) {
    Serial.println("FAMILY migrated legacy notification into queue");
    clearLegacyFamilyNotificationState();
  } else {
    Serial.println("ERR unable to migrate legacy family notification");
  }
}

bool careEventRecordIsValid(const CareEventRecord &event) {
  return event.id != 0 && event.requestId != 0 &&
         event.type <= kCareWellbeing && event.outcome <= kCareSuperseded &&
         event.reply <= kFamilyReplyRest;
}

void reconcileCareStateAfterLoad() {
  bool historyChanged = false;
  const uint32_t bootUptime = millis() / 1000;

  size_t writeIndex = 0;
  for (size_t readIndex = 0; readIndex < careEventStore.count; ++readIndex) {
    const CareEventRecord &candidate = careEventStore.records[readIndex];
    if (!careEventRecordIsValid(candidate)) {
      historyChanged = true;
      continue;
    }
    if (writeIndex != readIndex) {
      careEventStore.records[writeIndex] = candidate;
      historyChanged = true;
    }
    ++writeIndex;
  }
  for (size_t i = writeIndex; i < careEventStore.count; ++i) {
    memset(&careEventStore.records[i], 0, sizeof(CareEventRecord));
  }
  careEventStore.count = static_cast<uint8_t>(writeIndex);

  // Collapse impossible duplicate active rows before attempting legacy SOS
  // recovery. This guarantees a completed victim exists if a previously
  // partial transaction needs one more history slot.
  bool initialPendingTypeSeen[3] = {false, false, false};
  for (size_t i = 0; i < careEventStore.count; ++i) {
    CareEventRecord &event = careEventStore.records[i];
    if (event.outcome != kCarePending) {
      continue;
    }
    if (!initialPendingTypeSeen[event.type]) {
      initialPendingTypeSeen[event.type] = true;
      continue;
    }
    event.outcome = kCareSuperseded;
    event.reply = kFamilyReplyNone;
    event.endEpoch = currentEpoch();
    event.endUptime = bootUptime;
    event.syncState = 0;
    rememberCurrentBootHandledCareEvent(event.id);
    historyChanged = true;
  }

  // Older firmware persisted SOS activity before appending its history row.
  // Recover that one-way partial transaction instead of silently losing SOS.
  if (sosActive && sosRequestId != 0 &&
      findCareEvent(kCareEmergency, sosRequestId) == nullptr) {
    if (beginCareEvent(kCareEmergency, sosRequestId)) {
      CareEventRecord *recovered =
          findCareEvent(kCareEmergency, sosRequestId);
      if (recovered != nullptr) {
        recovered->startEpoch = sosEpoch;
        recovered->startUptime = bootUptime;
        historyChanged = true;
      }
      Serial.printf("CARE recovered missing SOS history request=%lu\n",
                    static_cast<unsigned long>(sosRequestId));
    }
  }

  bool pendingTypeSeen[3] = {false, false, false};
  for (size_t i = 0; i < careEventStore.count; ++i) {
    CareEventRecord &event = careEventStore.records[i];
    const int notificationIndex =
        findFamilyNotificationIndex(event.requestId, event.type);
    if (event.outcome == kCarePending && notificationIndex >= 0) {
      const FamilyNotificationRecord &notification =
          familyNotificationStore.records[notificationIndex];
      event.outcome = kCareSeen;
      event.reply = notification.reply;
      event.endEpoch = notification.queuedEpoch != 0
                           ? notification.queuedEpoch
                           : currentEpoch();
      event.endUptime = bootUptime;
      event.syncState = 0;
      rememberCurrentBootHandledCareEvent(event.id);
      historyChanged = true;
      continue;
    }
    if (event.outcome != kCarePending) {
      continue;
    }
    if (pendingTypeSeen[event.type]) {
      event.outcome = kCareSuperseded;
      event.reply = kFamilyReplyNone;
      event.endEpoch = currentEpoch();
      event.endUptime = bootUptime;
      event.syncState = 0;
      rememberCurrentBootHandledCareEvent(event.id);
      historyChanged = true;
      continue;
    }
    pendingTypeSeen[event.type] = true;
    // startUptime came from another boot and is not comparable with millis().
    event.startUptime = bootUptime;
    historyChanged = true;
  }

  sosActive = false;
  sosRequestId = 0;
  sosEpoch = 0;
  sosUptime = 0;
  contactActive = false;
  contactRequestId = 0;
  wellbeingActive = false;
  wellbeingRequestId = 0;
  for (size_t i = 0; i < careEventStore.count; ++i) {
    const CareEventRecord &event = careEventStore.records[i];
    if (event.outcome != kCarePending) {
      continue;
    }
    if (event.type == kCareEmergency && !sosActive) {
      sosActive = true;
      sosRequestId = event.requestId;
      sosEpoch = event.startEpoch;
      sosUptime = event.startUptime;
    } else if (event.type == kCareContact && !contactActive) {
      contactActive = true;
      contactRequestId = event.requestId;
    } else if (event.type == kCareWellbeing && !wellbeingActive) {
      wellbeingActive = true;
      wellbeingRequestId = event.requestId;
    }
  }

  if (historyChanged) {
    persistCareEventHistory();
  }
  persistSosState();
  Serial.printf(
      "CARE reconcile sos=%u contact=%u wellbeing=%u notifications=%u repaired=%u\n",
      sosActive ? 1u : 0u, contactActive ? 1u : 0u,
      wellbeingActive ? 1u : 0u,
      static_cast<unsigned int>(familyNotificationStore.count),
      historyChanged ? 1u : 0u);
}

void printWifiStatus() {
  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("WIFI connected ssid=%s ip=%s rssi=%d\n", WiFi.SSID().c_str(),
                  WiFi.localIP().toString().c_str(), WiFi.RSSI());
  } else if (configPortalActive) {
    Serial.printf("WIFI setup ssid=%s url=http://%s\n", configApSsid.c_str(),
                  WiFi.softAPIP().toString().c_str());
  } else if (wifiConnecting) {
    Serial.println("WIFI connecting");
  } else {
    Serial.println("WIFI offline");
  }
}

void restartWebServer(const char *networkInterface) {
  // WebServer::begin() can run before the STA has an IP, leaving port 80
  // unopened. Recreate the listener only after an AP or STA interface is up.
  configServer.stop();
  delay(10);
  configServer.begin();
  Serial.printf("HTTP server listening interface=%s port=80\n",
                networkInterface);
}

void onWifiDisconnected(WiFiEvent_t event, WiFiEventInfo_t info) {
  (void)event;
  const auto reason = static_cast<wifi_err_reason_t>(
      info.wifi_sta_disconnected.reason);
  Serial.printf("WIFI disconnected reason=%u name=%s\n",
                static_cast<unsigned int>(reason),
                WiFi.disconnectReasonName(reason));
}

void beginWifiConnection(const String &ssid, const String &password) {
  WiFi.persistent(false);
  WiFi.setAutoReconnect(false);

  // A previous failed connection can leave the ESP-IDF STA state machine busy.
  // In that state WiFi.begin() cannot apply a different SSID and reports
  // "sta is connecting, cannot set config". Disable only the STA interface
  // first (preserving the setup AP when it is active), then start a clean STA.
  WiFi.mode(configPortalActive ? WIFI_AP : WIFI_OFF);
  delay(100);
  WiFi.mode(configPortalActive ? WIFI_AP_STA : WIFI_STA);
  WiFi.setAutoReconnect(true);
  const wl_status_t beginStatus = WiFi.begin(ssid.c_str(), password.c_str());
  wifiConnecting = beginStatus != WL_CONNECT_FAILED;
  wifiConnectStartedMs = millis();
  Serial.printf("WIFI connecting ssid=%s\n", ssid.c_str());
  if (!wifiConnecting) {
    Serial.println("ERR unable to start Wi-Fi station connection");
    startConfigPortal();
  }
}

String buildConfigPage() {
  String connectionText;
  if (WiFi.status() == WL_CONNECTED) {
    connectionText = "已连接：" + htmlEscape(WiFi.SSID()) + "，IP：" +
                     WiFi.localIP().toString();
  } else {
    connectionText = "尚未连接家庭 Wi-Fi";
  }

  String page;
  String caregiverText = caregiverPhone.isEmpty()
      ? String("尚未配置照护人")
      : String("已配置：****") + caregiverPhone.substring(caregiverPhone.length() - 4);
  const String uploadText = eventUploadUrl.isEmpty()
      ? String("未配置，事件仅保存在 ESP32 NVS")
      : String("已配置：") + eventUploadUrl;

  page.reserve(4200);
  page += F("<!doctype html><html lang='zh-CN'><head><meta charset='utf-8'>");
  page += F("<meta name='viewport' content='width=device-width,initial-scale=1'>");
  page += F("<title>VelaCare 配置</title><style>body{font-family:sans-serif;max-width:520px;margin:32px auto;padding:0 18px;color:#173b2d}input,button{box-sizing:border-box;width:100%;padding:12px;margin:7px 0;font-size:16px}button{background:#129b66;color:white;border:0;border-radius:8px}.danger{background:#b83c38}section{background:#f3faf7;padding:18px;border-radius:12px;margin-bottom:16px}.muted{color:#60756c;font-size:14px}</style></head><body>");
  page += F("<h1>VelaCare 配置</h1><section><h2>家庭 Wi-Fi</h2><p>");
  page += connectionText;
  page += F("</p><form method='post' action='/save'><label>家庭 Wi-Fi 名称</label><input name='ssid' maxlength='32' required autocomplete='off'><label>Wi-Fi 密码</label><input name='password' type='password' maxlength='63' autocomplete='new-password'><button type='submit'>保存并连接</button></form><form method='post' action='/forget'><button class='danger' type='submit'>清除已保存网络</button></form><p class='muted'>密码只提交给当前 ESP32，并保存到本机 NVS；串口日志不会打印密码。</p></section>");
  page += F("<section><h2>照护人</h2><p>");
  page += htmlEscape(caregiverText);
  page += F("</p><form method='post' action='/caregiver/save'><label>照护人姓名（选填）</label><input name='name' maxlength='48' autocomplete='off'><label>联系电话</label><input name='phone' inputmode='tel' maxlength='24' required autocomplete='tel'><button type='submit'>保存照护人</button></form><form method='post' action='/caregiver/clear'><button class='danger' type='submit'>清除照护人配置</button></form><p class='muted'>号码保存在当前 ESP32 的 NVS 中，D12X 仅接收脱敏后的末四位。当前版本提供局域网求助，不会自动拨号或发送短信。</p></section>");
  page += F("<section><h2>事件补传</h2><p>");
  page += htmlEscape(uploadText);
  page += F("</p><form method='post' action='/upload/save'><label>事件接收地址（可选，HTTP POST）</label><input name='url' maxlength='160' placeholder='http://电脑IP:端口/api/events'><button type='submit'>保存补传地址</button></form><p class='muted'>断网时事件先写入 NVS；网络恢复后按时间顺序 POST，成功后标记为已补传。留空则仅保存在本地。</p></section><p><a href='/dashboard'>返回守护页面</a></p></body></html>");
  return page;
}

void configureWebRoutes() {
  if (webRoutesReady) {
    return;
  }
  configServer.on("/", HTTP_GET, []() {
    configServer.sendHeader("Cache-Control", "no-store");
    if (WiFi.status() == WL_CONNECTED) {
      configServer.send_P(200, "text/html; charset=utf-8", kDashboardPage);
    } else {
      configServer.send(200, "text/html; charset=utf-8", buildConfigPage());
    }
  });
  configServer.on("/dashboard", HTTP_GET, []() {
    configServer.sendHeader("Cache-Control", "no-store");
    configServer.send_P(200, "text/html; charset=utf-8", kDashboardPage);
  });
  configServer.on("/api/status", HTTP_GET, []() {
    configServer.sendHeader("Cache-Control", "no-store");
    configServer.send(200, "application/json; charset=utf-8", buildStatusJson());
  });
  configServer.on("/api/events", HTTP_GET, []() {
    configServer.sendHeader("Cache-Control", "no-store");
    configServer.send(200, "application/json; charset=utf-8", buildEventsJson());
  });
  configServer.on("/api/diagnostics", HTTP_GET, []() {
    configServer.sendHeader("Cache-Control", "no-store");
    configServer.send(200, "application/json; charset=utf-8",
                      buildDiagnosticsJson());
  });
  configServer.on("/api/alarm/ack", HTTP_POST, []() {
    acknowledgeAlarms();
    configServer.send(200, "application/json; charset=utf-8", "{\"ok\":true}");
  });
  configServer.on("/api/alarm/mute", HTTP_POST, []() {
    muteAlarms();
    configServer.send(200, "application/json; charset=utf-8", "{\"ok\":true}");
  });
  configServer.on("/api/alarm/unmute", HTTP_POST, []() {
    unmuteAlarms();
    configServer.send(200, "application/json; charset=utf-8", "{\"ok\":true}");
  });
  configServer.on("/api/sos/ack", HTTP_POST, []() {
    const String idText = configServer.arg("id");
    char *idEnd = nullptr;
    const unsigned long parsedRequestId = strtoul(idText.c_str(), &idEnd, 10);
    if (idText.isEmpty() || idEnd == idText.c_str() || *idEnd != '\0' ||
        parsedRequestId == 0) {
      configServer.send(400, "application/json; charset=utf-8",
                        "{\"ok\":false,\"error\":\"id-required\"}");
      return;
    }
    const uint32_t requestId = static_cast<uint32_t>(parsedRequestId);
    if (!sosActive || sosRequestId == 0 || requestId != sosRequestId) {
      configServer.send(409, "application/json; charset=utf-8",
                        "{\"ok\":false,\"error\":\"stale-sos-id\"}");
      return;
    }
    if (!acknowledgeCareRequest(kCareEmergency, requestId,
                                 kFamilyReplySeen)) {
      configServer.send(409, "application/json; charset=utf-8",
                        "{\"ok\":false,\"error\":\"reply-not-queued\"}");
      return;
    }
    String response = String("{\"ok\":true,\"requestId\":") +
                      requestId + '}';
    configServer.send(200, "application/json; charset=utf-8", response);
  });
  configServer.on("/api/care/reply", HTTP_POST, []() {
    const String typeText = configServer.arg("type");
    const String idText = configServer.arg("id");
    const String replyText = configServer.arg("reply");
    uint8_t type = 255;
    if (typeText == "sos" || typeText == "0") {
      type = kCareEmergency;
    } else if (typeText == "contact" || typeText == "1") {
      type = kCareContact;
    } else if (typeText == "wellbeing" || typeText == "2") {
      type = kCareWellbeing;
    }
    char *idEnd = nullptr;
    char *replyEnd = nullptr;
    const unsigned long requestId = strtoul(idText.c_str(), &idEnd, 10);
    const unsigned long reply = strtoul(replyText.c_str(), &replyEnd, 10);
    if (type > kCareWellbeing || idText.isEmpty() || replyText.isEmpty() ||
        idEnd == idText.c_str() || *idEnd != '\0' || requestId == 0 ||
        replyEnd == replyText.c_str() || *replyEnd != '\0' ||
        reply < kFamilyReplySeen || reply > kFamilyReplyRest) {
      configServer.send(400, "application/json; charset=utf-8",
                        "{\"ok\":false,\"error\":\"invalid-request\"}");
      return;
    }
    if (!acknowledgeCareRequest(type, static_cast<uint32_t>(requestId),
                                static_cast<uint8_t>(reply))) {
      configServer.send(409, "application/json; charset=utf-8",
                        "{\"ok\":false,\"error\":\"stale-or-closed\"}");
      return;
    }
    String response = String("{\"ok\":true,\"requestId\":") +
                      requestId + F(",\"type\":") + type +
                      F(",\"reply\":") + reply + '}';
    configServer.send(200, "application/json; charset=utf-8", response);
  });
  configServer.on("/configure", HTTP_GET, []() {
    configServer.sendHeader("Cache-Control", "no-store");
    configServer.send(200, "text/html; charset=utf-8", buildConfigPage());
  });
  configServer.on("/selftest", HTTP_GET, []() {
    configServer.sendHeader("Cache-Control", "no-store");
    configServer.send_P(200, "text/html; charset=utf-8", kSelfTestPage);
  });
  configServer.on("/save", HTTP_POST, []() {
    String ssid = configServer.arg("ssid");
    const String password = configServer.arg("password");
    ssid.trim();
    if (ssid.isEmpty() || ssid.length() > 32 || password.length() > 63 ||
        (!password.isEmpty() && password.length() < 8)) {
      configServer.send(400, "text/plain; charset=utf-8",
                        "配置无效：SSID 必填，密码需为空或 8 到 63 位。");
      return;
    }
    if (!saveWifiCredentials(ssid, password)) {
      configServer.send(500, "text/plain; charset=utf-8", "保存失败，请重试。");
      return;
    }
    configServer.send(200, "text/html; charset=utf-8",
                      "<meta charset='utf-8'><h2>已保存，正在连接。</h2><p>约 15 秒后返回 <a href='/'>配置页</a> 查看状态。</p>");
    beginWifiConnection(ssid, password);
  });
  configServer.on("/forget", HTTP_POST, []() {
    clearWifiCredentials();
    WiFi.disconnect(false, true);
    wifiOnline = false;
    wifiConnecting = false;
    configServer.send(200, "text/html; charset=utf-8",
                      "<meta charset='utf-8'><h2>已清除家庭 Wi-Fi 配置。</h2><p><a href='/'>返回配置页</a></p>");
  });
  configServer.on("/caregiver/save", HTTP_POST, []() {
    String name = configServer.arg("name");
    const String phone = normalizeCaregiverPhone(configServer.arg("phone"));
    name.trim();
    if (name.length() > 48 || !caregiverPhoneIsValid(phone)) {
      configServer.send(400, "text/plain; charset=utf-8",
                        "配置无效：电话需包含 6 到 20 位数字。");
      return;
    }
    if (!saveCaregiverSettings(name, phone)) {
      configServer.send(500, "text/plain; charset=utf-8",
                        "照护人配置保存失败，请重试。");
      return;
    }
    sendCaregiverFrame();
    configServer.send(200, "text/html; charset=utf-8",
                      "<meta charset='utf-8'><h2>照护人已保存并同步到 D12X。</h2><p><a href='/configure'>返回配置</a> · <a href='/dashboard'>查看守护页面</a></p>");
  });
  configServer.on("/caregiver/clear", HTTP_POST, []() {
    if (!saveCaregiverSettings("", "")) {
      configServer.send(500, "text/plain; charset=utf-8",
                        "清除失败，请重试。");
      return;
    }
    sendCaregiverFrame();
    configServer.send(200, "text/html; charset=utf-8",
                      "<meta charset='utf-8'><h2>照护人配置已清除。</h2><p><a href='/configure'>返回配置</a></p>");
  });
  configServer.on("/upload/save", HTTP_POST, []() {
    String url = configServer.arg("url");
    url.trim();
    if (url.length() > 160 ||
        (!url.isEmpty() &&
         !(url.startsWith("http://") || url.startsWith("https://")))) {
      configServer.send(400, "text/plain; charset=utf-8",
                        "地址无效：请填写 http:// 或 https:// 地址，也可以留空关闭补传。");
      return;
    }
    if (!saveEventUploadUrl(url)) {
      configServer.send(500, "text/plain; charset=utf-8", "补传地址保存失败，请重试。");
      return;
    }
    configServer.send(200, "text/html; charset=utf-8",
                      "<meta charset='utf-8'><h2>事件补传配置已保存。</h2><p><a href='/configure'>返回配置</a> · <a href='/dashboard'>查看守护页面</a></p>");
  });
  configServer.onNotFound([]() {
    configServer.sendHeader("Location", "/", true);
    configServer.send(302, "text/plain", "");
  });
  webRoutesReady = true;
}

void startConfigPortal() {
  if (configApSsid.isEmpty()) {
    char suffix[5] = {};
    snprintf(suffix, sizeof(suffix), "%04X",
             static_cast<unsigned int>(ESP.getEfuseMac() & 0xFFFF));
    configApSsid = String("VelaCare-Setup-") + suffix;
  }
  if (configPortalActive) {
    printWifiStatus();
    return;
  }
  WiFi.mode(WIFI_AP_STA);
  if (!WiFi.softAP(configApSsid.c_str(), kConfigApPassword)) {
    Serial.println("ERR unable to start Wi-Fi setup access point");
    return;
  }
  configPortalActive = true;
  restartWebServer("setup-ap");
  Serial.printf("WIFI setup ssid=%s password=%s url=http://%s\n",
                configApSsid.c_str(), kConfigApPassword,
                WiFi.softAPIP().toString().c_str());
}

void beginSavedWifiConnection() {
  String ssid;
  String password;
  if (loadWifiCredentials(ssid, password)) {
    beginWifiConnection(ssid, password);
  } else {
    Serial.println("WIFI no saved network");
    startConfigPortal();
  }
}

void updateWifiState() {
  const bool nowOnline = WiFi.status() == WL_CONNECTED;
  if (nowOnline != wifiOnline) {
    wifiOnline = nowOnline;
    if (wifiOnline) {
      wifiConnecting = false;
      if (configPortalActive) {
        WiFi.softAPdisconnect(true);
        WiFi.mode(WIFI_STA);
        configPortalActive = false;
        Serial.println("WIFI setup access point stopped");
      }
      restartWebServer("station");
      if (!timeSyncStarted) {
        configTzTime("CST-8", "ntp.aliyun.com", "pool.ntp.org",
                     "time.cloudflare.com");
        timeSyncStarted = true;
        Serial.println("TIME synchronization started");
      }
      Serial.printf("WIFI connected ssid=%s ip=%s rssi=%d\n",
                    WiFi.SSID().c_str(), WiFi.localIP().toString().c_str(),
                    WiFi.RSSI());
    } else {
      Serial.println("WIFI disconnected");
      wifiConnecting = true;
      wifiConnectStartedMs = millis();
    }
    printStatus();
  }
  if (wifiConnecting && !nowOnline &&
      static_cast<uint32_t>(millis() - wifiConnectStartedMs) >=
          kWifiConnectTimeoutMs) {
    wifiConnecting = false;
    Serial.println("WIFI connection timeout; starting setup access point");
    startConfigPortal();
  }
  configServer.handleClient();
}

uint16_t crc16CcittFalse(const uint8_t *data, size_t length) {
  uint16_t crc = 0xFFFF;
  for (size_t i = 0; i < length; ++i) {
    crc ^= static_cast<uint16_t>(data[i]) << 8;
    for (uint8_t bit = 0; bit < 8; ++bit) {
      crc = (crc & 0x8000) ? static_cast<uint16_t>((crc << 1) ^ 0x1021)
                           : static_cast<uint16_t>(crc << 1);
    }
  }
  return crc;
}

void sendD12xFrame(const char *payload) {
  const size_t length = strlen(payload);
  const uint16_t crc = crc16CcittFalse(
      reinterpret_cast<const uint8_t *>(payload), length);
  char frame[128] = {};
  const int frameLength = snprintf(frame, sizeof(frame), "%s*%04X\n",
                                   payload, crc);
  if (frameLength <= 0 || static_cast<size_t>(frameLength) >= sizeof(frame)) {
    Serial.println("ERR D12x frame overflow");
    return;
  }
  d12xSerial.print(frame);
}

void sendBuzzerControlFrame() {
  char payload[64] = {};
  snprintf(payload, sizeof(payload), "VCB1,%u,%u,%u",
           alarmMuted() ? 1u : 0u, alarmAcknowledged ? 1u : 0u,
           static_cast<unsigned int>(highestAlarmSeverity()));
  sendD12xFrame(payload);
}

void sendCaregiverFrame() {
  char payload[64] = {};
  char last4[5] = "0000";
  if (caregiverPhone.length() >= 4) {
    caregiverPhone.substring(caregiverPhone.length() - 4).toCharArray(
        last4, sizeof(last4));
  }
  snprintf(payload, sizeof(payload), "CFG1,%lu,%u,%s",
           static_cast<unsigned long>(caregiverRevision),
           caregiverPhone.isEmpty() ? 0u : 1u, last4);
  sendD12xFrame(payload);
}

void sendSosAck(uint32_t requestId) {
  char payload[48] = {};
  const unsigned int status = caregiverPhone.isEmpty() ? 1u : 2u;
  snprintf(payload, sizeof(payload), "SACK1,%lu,%u",
           static_cast<unsigned long>(requestId), status);
  sendD12xFrame(payload);
}

void sendSosCancelAck(uint32_t requestId, uint8_t status) {
  char payload[48] = {};
  snprintf(payload, sizeof(payload), "SXACK1,%lu,%u",
           static_cast<unsigned long>(requestId),
           static_cast<unsigned int>(status));
  sendD12xFrame(payload);
}

void sendCareAck(uint32_t requestId, uint8_t type) {
  char payload[56] = {};
  const unsigned int status = caregiverPhone.isEmpty() ? 1u : 2u;
  snprintf(payload, sizeof(payload), "CACK1,%lu,%u,%u",
           static_cast<unsigned long>(requestId),
           static_cast<unsigned int>(type), status);
  sendD12xFrame(payload);
}

void sendCareCancelAck(uint32_t requestId, uint8_t type, uint8_t status) {
  char payload[64] = {};
  snprintf(payload, sizeof(payload), "CXACK1,%lu,%u,%u",
           static_cast<unsigned long>(requestId),
           static_cast<unsigned int>(type),
           static_cast<unsigned int>(status));
  sendD12xFrame(payload);
}

bool sendFamilyNotificationAt(size_t index) {
  if (index >= familyNotificationStore.count) {
    return false;
  }
  const FamilyNotificationRecord &notification =
      familyNotificationStore.records[index];
  if (!familyNotificationRecordIsValid(notification)) {
    return false;
  }
  char payload[64] = {};
  snprintf(payload, sizeof(payload), "FAM1,%lu,%u,%u",
           static_cast<unsigned long>(notification.requestId),
           static_cast<unsigned int>(notification.type),
           static_cast<unsigned int>(notification.reply));
  sendD12xFrame(payload);
  familyNotificationLastSentMs = millis();
  Serial.printf("FAMILY notify id=%lu type=%s reply=%u slot=%u/%u\n",
                static_cast<unsigned long>(notification.requestId),
                careEventTypeName(notification.type), notification.reply,
                static_cast<unsigned int>(index + 1),
                static_cast<unsigned int>(familyNotificationStore.count));
  return true;
}

bool sendFamilyNotificationFor(uint32_t requestId, uint8_t type) {
  const int index = findFamilyNotificationIndex(requestId, type);
  return index >= 0 && sendFamilyNotificationAt(static_cast<size_t>(index));
}

void updateFamilyNotification() {
  if (familyNotificationStore.count == 0) {
    return;
  }
  const uint32_t now = millis();
  if (familyNotificationLastSentMs == 0 ||
      static_cast<uint32_t>(now - familyNotificationLastSentMs) >=
          kFamilyNotificationRetryMs) {
    if (familyNotificationNextIndex >= familyNotificationStore.count) {
      familyNotificationNextIndex = 0;
    }
    const size_t sendingIndex = familyNotificationNextIndex;
    familyNotificationNextIndex =
        (familyNotificationNextIndex + 1) % familyNotificationStore.count;
    sendFamilyNotificationAt(sendingIndex);
  }
}

bool queueFamilyNotification(uint32_t requestId, uint8_t type,
                             uint8_t reply) {
  if (!enqueueFamilyNotification(requestId, type, reply)) {
    return false;
  }
  sendFamilyNotificationFor(requestId, type);
  return true;
}

void setCareRequestInactive(uint8_t type) {
  if (type == kCareEmergency) {
    sosActive = false;
    sosRequestId = 0;
    sosEpoch = 0;
    sosUptime = 0;
    persistSosState();
  } else if (type == kCareContact) {
    contactActive = false;
    contactRequestId = 0;
  } else if (type == kCareWellbeing) {
    wellbeingActive = false;
    wellbeingRequestId = 0;
  }
}

bool acknowledgeCareRequest(uint8_t type, uint32_t requestId, uint8_t reply) {
  if (type > kCareWellbeing || requestId == 0 ||
      reply == kFamilyReplyNone || reply > kFamilyReplyRest) {
    return false;
  }
  const bool exactActive =
      (type == kCareEmergency && sosActive && sosRequestId == requestId) ||
      (type == kCareContact && contactActive &&
       contactRequestId == requestId) ||
      (type == kCareWellbeing && wellbeingActive &&
       wellbeingRequestId == requestId);
  if (!exactActive) {
    return false;
  }

  // Persist the outbound family reply first. If power is lost during the
  // following cross-namespace updates, startup reconciliation can complete
  // the event from this durable queue entry without losing the reply.
  if (!queueFamilyNotification(requestId, type, reply)) {
    return false;
  }
  finishCareEvent(type, requestId, kCareSeen, reply);
  setCareRequestInactive(type);
  queueAudioPrompt(VoicePromptId::kFamily, 0, false, true);
  Serial.printf("CARE family reply request=%lu type=%s reply=%u\n",
                static_cast<unsigned long>(requestId),
                careEventTypeName(type), reply);
  return true;
}

bool activateCareRequest(uint8_t type, uint32_t requestId) {
  if (!beginCareEvent(type, requestId)) {
    return false;
  }
  if (type == kCareEmergency) {
    sosActive = true;
    sosRequestId = requestId;
    sosEpoch = currentEpoch();
    sosUptime = millis() / 1000;
    persistSosState();
  } else if (type == kCareContact) {
    contactActive = true;
    contactRequestId = requestId;
  } else if (type == kCareWellbeing) {
    wellbeingActive = true;
    wellbeingRequestId = requestId;
  }
  return true;
}

void supersedeActiveCareRequest(uint8_t type) {
  uint32_t requestId = 0;
  if (type == kCareEmergency && sosActive) {
    requestId = sosRequestId;
  } else if (type == kCareContact && contactActive) {
    requestId = contactRequestId;
  } else if (type == kCareWellbeing && wellbeingActive) {
    requestId = wellbeingRequestId;
  }
  if (requestId == 0) {
    return;
  }
  finishCareEvent(type, requestId, kCareSuperseded, kFamilyReplyNone);
  setCareRequestInactive(type);
}

uint8_t cancelCareRequest(uint8_t type, uint32_t requestId) {
  const bool exactActive =
      (type == kCareEmergency && sosActive && sosRequestId == requestId) ||
      (type == kCareContact && contactActive &&
       contactRequestId == requestId) ||
      (type == kCareWellbeing && wellbeingActive &&
       wellbeingRequestId == requestId);
  if (exactActive) {
    finishCareEvent(type, requestId, kCareCancelled, kFamilyReplyNone);
    setCareRequestInactive(type);
    Serial.printf("CARE elder cancelled request=%lu type=%s\n",
                  static_cast<unsigned long>(requestId),
                  careEventTypeName(type));
    return 1;
  }

  CareEventRecord *event = findCareEvent(type, requestId);
  if (event != nullptr && (event->outcome == kCareCancelled ||
                           event->outcome == kCareSuperseded)) {
    return 1;
  }
  if (event != nullptr && event->outcome == kCareSeen) {
    return 2;
  }
  return 0;
}

void handleD12xLine(char *line) {
  if (strstr(line, "OK1") != nullptr || strstr(line, "SOS1") != nullptr ||
      strstr(line, "SOSX1") != nullptr || strstr(line, "CARE1") != nullptr ||
      strstr(line, "CAREX1") != nullptr || strstr(line, "FACK1") != nullptr) {
    Serial.printf("RX D12x %s\n", line);
  }
  char *crcText = strchr(line, '*');
  if (crcText == nullptr || strlen(crcText + 1) != 4) {
    return;
  }
  *crcText = '\0';
  char *end = nullptr;
  const unsigned long receivedCrc = strtoul(crcText + 1, &end, 16);
  if (*end != '\0' || receivedCrc > 0xFFFF) {
    return;
  }
  const uint16_t calculated = crc16CcittFalse(
      reinterpret_cast<const uint8_t *>(line), strlen(line));
  if (calculated != static_cast<uint16_t>(receivedCrc)) {
    Serial.println("ERR D12x CRC");
    return;
  }

  unsigned long requestId = 0;
  char trailing = '\0';
  unsigned int type = 0;

  unsigned int cancelStatus = 0;
  if (sscanf(line, "SOSX1,%lu%c", &requestId, &trailing) == 1 &&
      requestId != 0) {
    cancelStatus = cancelCareRequest(kCareEmergency,
                                     static_cast<uint32_t>(requestId));
    sendSosCancelAck(static_cast<uint32_t>(requestId),
                     static_cast<uint8_t>(cancelStatus));
    return;
  }

  if (sscanf(line, "CAREX1,%lu,%u%c", &requestId, &type, &trailing) == 2 &&
      requestId != 0 && type >= kCareContact && type <= kCareWellbeing) {
    cancelStatus = cancelCareRequest(static_cast<uint8_t>(type),
                                     static_cast<uint32_t>(requestId));
    sendCareCancelAck(static_cast<uint32_t>(requestId),
                      static_cast<uint8_t>(type),
                      static_cast<uint8_t>(cancelStatus));
    return;
  }

  if (sscanf(line, "FACK1,%lu,%u%c", &requestId, &type, &trailing) == 2 &&
      requestId != 0 && type <= kCareWellbeing) {
    if (removeFamilyNotification(static_cast<uint32_t>(requestId),
                                 static_cast<uint8_t>(type))) {
      Serial.printf("FAMILY D12x ack id=%lu type=%u\n", requestId, type);
    } else {
      Serial.printf("FAMILY ignored unmatched D12x ack id=%lu type=%u\n",
                    requestId, type);
    }
    return;
  }

  if (sscanf(line, "SOS1,%lu%c", &requestId, &trailing) == 1 &&
      requestId != 0) {
    const uint32_t id = static_cast<uint32_t>(requestId);
    if (sosActive && sosRequestId == id) {
      sendSosAck(id);
      return;
    }
    if (careEventWasRecentlyHandled(kCareEmergency, id)) {
      CareEventRecord *event = findCareEvent(kCareEmergency, id);
      if (event != nullptr && (event->outcome == kCareCancelled ||
                               event->outcome == kCareSuperseded)) {
        sendSosCancelAck(id, 1);
      } else {
        sendSosAck(id);
        sendFamilyNotificationFor(id, kCareEmergency);
      }
      return;
    }
    if (sosActive) {
      supersedeActiveCareRequest(kCareEmergency);
    }
    if (!activateCareRequest(kCareEmergency, id)) {
      Serial.printf("ERR unable to persist SOS request id=%lu\n",
                    static_cast<unsigned long>(id));
      return;
    }
    Serial.printf("SOS received id=%lu caregiver=%s\n", requestId,
                  caregiverPhone.isEmpty() ? "not-configured" : "configured");
    sendSosAck(id);
    return;
  }

  if (sscanf(line, "CARE1,%lu,%u%c", &requestId, &type, &trailing) == 2 &&
      requestId != 0 && type >= kCareContact && type <= kCareWellbeing) {
    const uint32_t id = static_cast<uint32_t>(requestId);
    const uint8_t careType = static_cast<uint8_t>(type);
    const bool alreadyActive =
        (careType == kCareContact && contactActive && contactRequestId == id) ||
        (careType == kCareWellbeing && wellbeingActive &&
         wellbeingRequestId == id);
    if (alreadyActive) {
      sendCareAck(id, careType);
      return;
    }
    if (careEventWasRecentlyHandled(careType, id)) {
      CareEventRecord *event = findCareEvent(careType, id);
      if (event != nullptr && (event->outcome == kCareCancelled ||
                               event->outcome == kCareSuperseded)) {
        // A lost CXACK1 can make D12X retry the original CARE1. Preserve the
        // completed cancellation instead of regressing its UI to WAIT_FAMILY.
        sendCareCancelAck(id, careType, 1);
      } else {
        sendCareAck(id, careType);
        sendFamilyNotificationFor(id, careType);
      }
      return;
    }
    if ((careType == kCareContact && contactActive) ||
        (careType == kCareWellbeing && wellbeingActive)) {
      supersedeActiveCareRequest(careType);
    }
    if (!activateCareRequest(careType, id)) {
      Serial.printf("ERR unable to persist CARE request id=%lu type=%s\n",
                    static_cast<unsigned long>(id),
                    careEventTypeName(careType));
      return;
    }
    sendCareAck(id, careType);
    Serial.printf("CARE received id=%lu type=%s caregiver=%s\n", requestId,
                  careEventTypeName(careType),
                  caregiverPhone.isEmpty() ? "not-configured" : "configured");
    return;
  }

  if (strncmp(line, "OK1,", 4) == 0) {
    char *okEnd = nullptr;
    const unsigned long okId = strtoul(line + 4, &okEnd, 10);
    if (okEnd != line + 4 && *okEnd == '\0' && okId != 0) {
      const bool fallStillActive = sensorStates[3] == kAlarm;
      const bool firstConfirmation = fallStillActive && !elderConfirmed;
      if (fallStillActive) {
        elderConfirmed = true;
      }
      if (firstConfirmation) {
        // Invalidate only stale fall/persistent requests. The audio task checks
        // the generation every block, so a contradictory "no response" prompt
        // stops promptly without clearing unrelated family/test audio.
        const uint32_t generation = advanceFallVoiceGeneration();
        elderPromptHandledGeneration = generation;
        const bool voiceQueued = queueAudioPrompt(
            VoicePromptId::kElder, generation, true, true);
        Serial.printf(
            "RX D12x OK1 id=%lu elder confirmed safe; voice=%s\n", okId,
            voiceQueued ? "queued-priority" : "suppressed-or-failed");
      } else {
        noteAudioDeduplicated();
        Serial.printf("RX D12x OK1 id=%lu ignored for voice fall_active=%u already_confirmed=%u\n",
                      okId, fallStillActive ? 1u : 0u,
                      elderConfirmed ? 1u : 0u);
      }
      char ack[40] = {};
      snprintf(ack, sizeof(ack), "OACK1,%lu", okId);
      sendD12xFrame(ack);
    } else {
      Serial.printf("RX D12x OK1 parse fail [%s]\n", line);
    }
  }
}

void readD12xMessages() {
  while (d12xSerial.available() > 0) {
    const char incoming = static_cast<char>(d12xSerial.read());
    if (incoming == '\r') continue;
    if (incoming == '\n') {
      d12xReceiveBuffer[d12xReceiveLength] = '\0';
      if (d12xReceiveLength > 0) handleD12xLine(d12xReceiveBuffer);
      d12xReceiveLength = 0;
      continue;
    }
    if (d12xReceiveLength + 1 < sizeof(d12xReceiveBuffer)) {
      d12xReceiveBuffer[d12xReceiveLength++] = incoming;
    } else {
      d12xReceiveLength = 0;
    }
  }
}

const char *voicePromptName(VoicePromptId prompt) {
  switch (prompt) {
    case VoicePromptId::kNone:
      return "none";
    case VoicePromptId::kFall:
      return "fall";
    case VoicePromptId::kPersistent:
      return "persistent";
    case VoicePromptId::kFamily:
      return "family";
    case VoicePromptId::kElder:
      return "elder";
    case VoicePromptId::kTest:
      return "test";
  }
  return "invalid";
}

const VoicePromptClip *voicePromptClip(VoicePromptId prompt) {
  switch (prompt) {
    case VoicePromptId::kFall:
      return &kVoiceFallClip;
    case VoicePromptId::kPersistent:
      return &kVoicePersistentClip;
    case VoicePromptId::kFamily:
      return &kVoiceFamilyClip;
    case VoicePromptId::kElder:
      return &kVoiceElderClip;
    default:
      return nullptr;
  }
}

uint32_t currentFallVoiceGeneration() {
  portENTER_CRITICAL(&audioStateMux);
  const uint32_t generation = fallVoiceGeneration;
  portEXIT_CRITICAL(&audioStateMux);
  return generation;
}

uint32_t advanceFallVoiceGeneration() {
  portENTER_CRITICAL(&audioStateMux);
  ++fallVoiceGeneration;
  if (fallVoiceGeneration == 0) {
    ++fallVoiceGeneration;
  }
  const uint32_t generation = fallVoiceGeneration;
  portEXIT_CRITICAL(&audioStateMux);
  return generation;
}

void noteAudioDeduplicated() {
  portENTER_CRITICAL(&audioStateMux);
  ++audioStatistics.deduplicated;
  portEXIT_CRITICAL(&audioStateMux);
}

bool queueAudioPrompt(VoicePromptId prompt, uint32_t generation,
                      bool alarmBound, bool highPriority) {
  if (prompt == VoicePromptId::kNone) {
    return false;
  }
  if (alarmBound && alarmMuted()) {
    Serial.printf("AUDIO suppressed prompt=%s reason=muted\n",
                  voicePromptName(prompt));
    return false;
  }
  if (audioQueue == nullptr) {
    portENTER_CRITICAL(&audioStateMux);
    ++audioStatistics.droppedQueueFull;
    portEXIT_CRITICAL(&audioStateMux);
    Serial.printf("AUDIO queue unavailable prompt=%s\n",
                  voicePromptName(prompt));
    return false;
  }

  portENTER_CRITICAL(&audioStateMux);
  const uint32_t epoch = audioCancellationEpoch;
  portEXIT_CRITICAL(&audioStateMux);
  const AudioRequest request = {prompt, generation, epoch, alarmBound};
  const BaseType_t enqueueResult =
      highPriority ? xQueueSendToFront(audioQueue, &request, 0)
                   : xQueueSendToBack(audioQueue, &request, 0);
  if (enqueueResult != pdPASS) {
    portENTER_CRITICAL(&audioStateMux);
    ++audioStatistics.droppedQueueFull;
    portEXIT_CRITICAL(&audioStateMux);
    Serial.printf("AUDIO queue full prompt=%s\n", voicePromptName(prompt));
    return false;
  }

  portENTER_CRITICAL(&audioStateMux);
  ++audioStatistics.enqueued;
  portEXIT_CRITICAL(&audioStateMux);
  Serial.printf("AUDIO queued prompt=%s depth=%u\n", voicePromptName(prompt),
                static_cast<unsigned int>(uxQueueMessagesWaiting(audioQueue)));
  return true;
}

void stopAudioPlayback(bool printMessage) {
  const UBaseType_t queued =
      audioQueue != nullptr ? uxQueueMessagesWaiting(audioQueue) : 0;
  portENTER_CRITICAL(&audioStateMux);
  ++audioCancellationEpoch;
  if (audioCancellationEpoch == 0) {
    ++audioCancellationEpoch;
  }
  const bool wasPlaying = audioCurrentPrompt != VoicePromptId::kNone;
  // Queued items are removed here. The audio task counts the active request
  // itself after it observes the new cancellation epoch, avoiding a double
  // count for the item that was already playing.
  audioStatistics.cancelled += static_cast<uint32_t>(queued);
  portEXIT_CRITICAL(&audioStateMux);
  if (audioQueue != nullptr) {
    xQueueReset(audioQueue);
  }
  if (printMessage) {
    Serial.printf("AUDIO stopped cleared=%u active=%u\n",
                  static_cast<unsigned int>(queued), wasPlaying ? 1u : 0u);
  }
}

bool audioRequestCancelled(const AudioRequest &request) {
  portENTER_CRITICAL(&audioStateMux);
  const uint32_t epoch = audioCancellationEpoch;
  const uint32_t generation = fallVoiceGeneration;
  portEXIT_CRITICAL(&audioStateMux);
  if (request.cancellationEpoch != epoch) {
    return true;
  }
  if (request.alarmBound && alarmMuted()) {
    return true;
  }
  if (request.alarmBound &&
      (request.prompt == VoicePromptId::kFall ||
       request.prompt == VoicePromptId::kPersistent) &&
      request.generation != generation) {
    return true;
  }
  return false;
}

bool initializeAudioOutputInTask() {
  audioI2s.setPins(kAudioBclkPin, kAudioLrcPin, kAudioDataPin);
  const bool ready = audioI2s.begin(I2S_MODE_STD, kAudioSampleRate,
                                    I2S_DATA_BIT_WIDTH_16BIT,
                                    I2S_SLOT_MODE_STEREO,
                                    I2S_STD_SLOT_BOTH);
  portENTER_CRITICAL(&audioStateMux);
  audioReady = ready;
  portEXIT_CRITICAL(&audioStateMux);
  if (!ready) {
    Serial.printf("AUDIO init failed error=%d\n", audioI2s.lastError());
    return false;
  }
  Serial.printf("AUDIO ready MAX98357A DIN=GPIO%d BCLK=GPIO%d LRC=GPIO%d rate=%luHz\n",
                kAudioDataPin, kAudioBclkPin, kAudioLrcPin,
                static_cast<unsigned long>(kAudioSampleRate));
  return true;
}

bool writeVoiceClipInTask(const AudioRequest &request,
                          const VoicePromptClip &clip,
                          bool &cancelled) {
  int16_t stereoSamples[kAudioFramesPerBlock * 2] = {};
  uint32_t completedFrames = 0;
  while (completedFrames < clip.sampleCount) {
    if (audioRequestCancelled(request)) {
      cancelled = true;
      return false;
    }
    const size_t blockFrames =
        min(static_cast<uint32_t>(kAudioFramesPerBlock),
            clip.sampleCount - completedFrames);
    for (size_t frame = 0; frame < blockFrames; ++frame) {
      const int16_t sample = static_cast<int16_t>(
          pgm_read_word(clip.samples + completedFrames + frame));
      stereoSamples[frame * 2] = sample;
      stereoSamples[frame * 2 + 1] = sample;
    }
    const size_t byteCount = blockFrames * 2 * sizeof(int16_t);
    const size_t bytesWritten = audioI2s.write(stereoSamples, byteCount);
    if (bytesWritten != byteCount) {
      portENTER_CRITICAL(&audioStateMux);
      ++audioStatistics.writeErrors;
      portEXIT_CRITICAL(&audioStateMux);
      Serial.printf("AUDIO write failed prompt=%s expected=%u actual=%u error=%d\n",
                    clip.name, static_cast<unsigned int>(byteCount),
                    static_cast<unsigned int>(bytesWritten),
                    audioI2s.lastError());
      return false;
    }
    completedFrames += blockFrames;
    portENTER_CRITICAL(&audioStateMux);
    audioSamplesWritten += blockFrames;
    portEXIT_CRITICAL(&audioStateMux);
    taskYIELD();
  }
  return true;
}

bool writeToneSegmentInTask(const AudioRequest &request, float frequencyHz,
                            uint32_t durationMs, int16_t amplitude,
                            bool &cancelled) {
  int16_t stereoSamples[kAudioFramesPerBlock * 2] = {};
  const uint32_t totalFrames = static_cast<uint32_t>(
      (static_cast<uint64_t>(kAudioSampleRate) * durationMs) / 1000ULL);
  const float phaseStep =
      frequencyHz > 0.0f
          ? (2.0f * PI * frequencyHz) / static_cast<float>(kAudioSampleRate)
          : 0.0f;
  float phase = 0.0f;
  uint32_t completedFrames = 0;
  while (completedFrames < totalFrames) {
    if (audioRequestCancelled(request)) {
      cancelled = true;
      return false;
    }
    const size_t blockFrames =
        min(static_cast<uint32_t>(kAudioFramesPerBlock),
            totalFrames - completedFrames);
    for (size_t frame = 0; frame < blockFrames; ++frame) {
      const int16_t sample = frequencyHz > 0.0f
                                 ? static_cast<int16_t>(sinf(phase) * amplitude)
                                 : 0;
      stereoSamples[frame * 2] = sample;
      stereoSamples[frame * 2 + 1] = sample;
      phase += phaseStep;
      if (phase >= 2.0f * PI) {
        phase -= 2.0f * PI;
      }
    }
    const size_t byteCount = blockFrames * 2 * sizeof(int16_t);
    const size_t bytesWritten = audioI2s.write(stereoSamples, byteCount);
    if (bytesWritten != byteCount) {
      portENTER_CRITICAL(&audioStateMux);
      ++audioStatistics.writeErrors;
      portEXIT_CRITICAL(&audioStateMux);
      Serial.printf("AUDIO test write failed expected=%u actual=%u error=%d\n",
                    static_cast<unsigned int>(byteCount),
                    static_cast<unsigned int>(bytesWritten),
                    audioI2s.lastError());
      return false;
    }
    completedFrames += blockFrames;
    portENTER_CRITICAL(&audioStateMux);
    audioSamplesWritten += blockFrames;
    portEXIT_CRITICAL(&audioStateMux);
    taskYIELD();
  }
  return true;
}

bool playAudioTestInTask(const AudioRequest &request, bool &cancelled) {
  bool ok = writeToneSegmentInTask(request, 0.0f, 40, 0, cancelled);
  ok = ok && writeToneSegmentInTask(request, 659.0f, 180, 3000, cancelled);
  ok = ok && writeToneSegmentInTask(request, 0.0f, 90, 0, cancelled);
  ok = ok && writeToneSegmentInTask(request, 784.0f, 180, 3000, cancelled);
  ok = ok && writeToneSegmentInTask(request, 0.0f, 90, 0, cancelled);
  ok = ok && writeToneSegmentInTask(request, 988.0f, 320, 3000, cancelled);
  return ok && writeToneSegmentInTask(request, 0.0f, 80, 0, cancelled);
}

void audioTaskMain(void *parameter) {
  (void)parameter;
  portENTER_CRITICAL(&audioStateMux);
  audioTaskRunning = true;
  portEXIT_CRITICAL(&audioStateMux);
  initializeAudioOutputInTask();

  AudioRequest request = {};
  for (;;) {
    if (xQueueReceive(audioQueue, &request, portMAX_DELAY) != pdPASS) {
      continue;
    }
    if (audioRequestCancelled(request)) {
      portENTER_CRITICAL(&audioStateMux);
      ++audioStatistics.cancelled;
      portEXIT_CRITICAL(&audioStateMux);
      continue;
    }

    portENTER_CRITICAL(&audioStateMux);
    audioCurrentPrompt = request.prompt;
    const bool ready = audioReady;
    portEXIT_CRITICAL(&audioStateMux);
    bool cancelled = false;
    bool completed = false;
    if (!ready) {
      portENTER_CRITICAL(&audioStateMux);
      ++audioStatistics.writeErrors;
      portEXIT_CRITICAL(&audioStateMux);
      Serial.printf("AUDIO unavailable prompt=%s\n",
                    voicePromptName(request.prompt));
    } else if (request.prompt == VoicePromptId::kTest) {
      completed = playAudioTestInTask(request, cancelled);
    } else {
      const VoicePromptClip *clip = voicePromptClip(request.prompt);
      if (clip != nullptr) {
        completed = writeVoiceClipInTask(request, *clip, cancelled);
      }
    }

    portENTER_CRITICAL(&audioStateMux);
    if (completed) {
      ++audioStatistics.played;
    } else if (cancelled) {
      ++audioStatistics.cancelled;
    }
    audioCurrentPrompt = VoicePromptId::kNone;
    portEXIT_CRITICAL(&audioStateMux);
    Serial.printf("AUDIO %s prompt=%s\n",
                  completed ? "played" : (cancelled ? "cancelled" : "failed"),
                  voicePromptName(request.prompt));
  }
}

bool beginAudioSubsystem() {
  if (audioQueue != nullptr && audioTaskHandle != nullptr) {
    return true;
  }
  audioQueue = xQueueCreate(kAudioQueueLength, sizeof(AudioRequest));
  if (audioQueue == nullptr) {
    Serial.println("AUDIO queue allocation failed");
    return false;
  }
  const BaseType_t created = xTaskCreate(
      audioTaskMain, "velacare_audio", kAudioTaskStackSize, nullptr,
      kAudioTaskPriority, &audioTaskHandle);
  if (created != pdPASS) {
    vQueueDelete(audioQueue);
    audioQueue = nullptr;
    audioTaskHandle = nullptr;
    Serial.println("AUDIO task creation failed");
    return false;
  }
  Serial.printf("AUDIO task queued capacity=%u\n",
                static_cast<unsigned int>(kAudioQueueLength));
  return true;
}

void printAudioStatus() {
  portENTER_CRITICAL(&audioStateMux);
  const bool ready = audioReady;
  const bool running = audioTaskRunning;
  const VoicePromptId current = audioCurrentPrompt;
  const uint32_t generation = fallVoiceGeneration;
  const uint32_t frames = audioSamplesWritten;
  const AudioStatistics statistics = audioStatistics;
  portEXIT_CRITICAL(&audioStateMux);
  const UBaseType_t depth =
      audioQueue != nullptr ? uxQueueMessagesWaiting(audioQueue) : 0;
  Serial.printf(
      "AUDIO ready=%u task=%u playing=%s queue=%u/%u muted=%u fall_gen=%lu "
      "enqueued=%lu played=%lu dropped=%lu dedup=%lu cancelled=%lu "
      "write_errors=%lu frames=%lu DIN=GPIO%d BCLK=GPIO%d LRC=GPIO%d rate=%luHz\n",
      ready ? 1u : 0u, running ? 1u : 0u, voicePromptName(current),
      static_cast<unsigned int>(depth),
      static_cast<unsigned int>(kAudioQueueLength), alarmMuted() ? 1u : 0u,
      static_cast<unsigned long>(generation),
      static_cast<unsigned long>(statistics.enqueued),
      static_cast<unsigned long>(statistics.played),
      static_cast<unsigned long>(statistics.droppedQueueFull),
      static_cast<unsigned long>(statistics.deduplicated),
      static_cast<unsigned long>(statistics.cancelled),
      static_cast<unsigned long>(statistics.writeErrors),
      static_cast<unsigned long>(frames), kAudioDataPin, kAudioBclkPin,
      kAudioLrcPin, static_cast<unsigned long>(kAudioSampleRate));
}

const char *stateName(SensorState state) {
  switch (state) {
    case kOffline:
      return "offline";
    case kNormal:
      return "normal";
    case kAlarm:
      return "alarm";
  }
  return "invalid";
}

void printStatus() {
  Serial.printf("STATUS smoke=%s water=%s door=%s fall=%s wifi=%s elder=%s\n",
                stateName(sensorStates[0]), stateName(sensorStates[1]),
                stateName(sensorStates[2]), stateName(sensorStates[3]),
                wifiOnline ? "online" : "offline",
                elderConfirmed ? "confirmed" : "none");
}

bool parseState(const char *text, SensorState &state) {
  if (strcmp(text, "offline") == 0) {
    state = kOffline;
  } else if (strcmp(text, "normal") == 0) {
    state = kNormal;
  } else if (strcmp(text, "alarm") == 0) {
    state = kAlarm;
  } else {
    return false;
  }
  return true;
}

void handleCommand(char *line) {
  while (*line == ' ') {
    ++line;
  }

  if (strcmp(line, "status") == 0) {
    printStatus();
    return;
  }
  if (strcmp(line, "normal") == 0) {
    for (uint8_t sensorIndex = 0; sensorIndex < 4; ++sensorIndex) {
      setSensorState(sensorIndex, kNormal);
    }
    resetFallDetector();
    Serial.println("OK all sensors normal");
    return;
  }
  if (strcmp(line, "imu status") == 0) {
    printImuStatus();
    return;
  }
  if (strcmp(line, "audio status") == 0) {
    printAudioStatus();
    return;
  }
  if (strcmp(line, "audio test") == 0) {
    if (!queueAudioPrompt(VoicePromptId::kTest, 0, false)) {
      Serial.println("ERR audio test queue full or unavailable");
    }
    return;
  }
  if (strcmp(line, "audio stop") == 0) {
    stopAudioPlayback(true);
    return;
  }
  if (strncmp(line, "audio play ", 11) == 0) {
    const char *promptText = line + 11;
    VoicePromptId prompt = VoicePromptId::kNone;
    if (strcmp(promptText, "fall") == 0) {
      prompt = VoicePromptId::kFall;
    } else if (strcmp(promptText, "persistent") == 0) {
      prompt = VoicePromptId::kPersistent;
    } else if (strcmp(promptText, "family") == 0) {
      prompt = VoicePromptId::kFamily;
    } else if (strcmp(promptText, "elder") == 0) {
      prompt = VoicePromptId::kElder;
    }
    if (prompt == VoicePromptId::kNone) {
      Serial.println("ERR audio play must be fall, persistent, family, or elder");
    } else if (!queueAudioPrompt(prompt, currentFallVoiceGeneration(), false)) {
      Serial.println("ERR audio prompt queue full or unavailable");
    }
    return;
  }
  if (strcmp(line, "alarm ack") == 0) {
    acknowledgeAlarms();
    printStatus();
    return;
  }
  if (strcmp(line, "alarm mute") == 0) {
    muteAlarms();
    printStatus();
    return;
  }
  if (strcmp(line, "alarm unmute") == 0) {
    unmuteAlarms();
    printStatus();
    return;
  }

  char *separator = strchr(line, ' ');
  if (separator == nullptr) {
    Serial.println("ERR use: status | normal | imu status | audio <status|test|stop|play NAME> | alarm <ack|mute|unmute> | <sensor> <state> | wifi <status|portal|reconnect|forget>");
    return;
  }
  *separator = '\0';
  const char *value = separator + 1;
  while (*value == ' ') {
    ++value;
  }

  if (strcmp(line, "wifi") == 0) {
    if (strcmp(value, "status") == 0) {
      printWifiStatus();
    } else if (strcmp(value, "portal") == 0) {
      startConfigPortal();
    } else if (strcmp(value, "reconnect") == 0) {
      beginSavedWifiConnection();
    } else if (strcmp(value, "forget") == 0) {
      clearWifiCredentials();
      WiFi.disconnect(false, true);
      wifiOnline = false;
      wifiConnecting = false;
      startConfigPortal();
      Serial.println("WIFI saved network cleared");
    } else {
      Serial.println("ERR wifi must be status, portal, reconnect, or forget");
    }
    return;
  }

  int sensorIndex = -1;
  if (strcmp(line, "smoke") == 0) sensorIndex = 0;
  if (strcmp(line, "water") == 0) sensorIndex = 1;
  if (strcmp(line, "door") == 0) sensorIndex = 2;
  if (strcmp(line, "fall") == 0) sensorIndex = 3;
  SensorState newState = kOffline;
  if (sensorIndex < 0 || !parseState(value, newState)) {
    Serial.println("ERR use: <smoke|water|door|fall> <offline|normal|alarm>");
    return;
  }
  setSensorState(static_cast<uint8_t>(sensorIndex), newState);
  if (sensorIndex == 3) {
    if (newState == kAlarm) {
      fallPhase = kFallAlarmed;
    } else {
      resetFallDetector();
    }
  }
  printStatus();
}

void readDebugCommands() {
  while (Serial.available() > 0) {
    const char incoming = static_cast<char>(Serial.read());
    if (incoming == '\r') {
      continue;
    }
    if (incoming == '\n') {
      commandBuffer[commandLength] = '\0';
      if (commandLength > 0) {
        handleCommand(commandBuffer);
      }
      commandLength = 0;
      continue;
    }
    if (commandLength + 1 < sizeof(commandBuffer)) {
      commandBuffer[commandLength++] = incoming;
    } else {
      commandLength = 0;
      Serial.println("ERR command too long");
    }
  }
}

void sendHeartbeat() {
  char payload[80] = {};
  const int payloadLength = snprintf(
      payload, sizeof(payload), "VC1,%lu,%u,%u,%u,%u,%u",
      static_cast<unsigned long>(sequenceNumber), sensorStates[0], sensorStates[1],
      sensorStates[2], sensorStates[3], wifiOnline ? 1 : 0);
  if (payloadLength <= 0 || static_cast<size_t>(payloadLength) >= sizeof(payload)) {
    Serial.println("ERR payload overflow");
    return;
  }

  const uint16_t crc = crc16CcittFalse(
      reinterpret_cast<const uint8_t *>(payload),
      static_cast<size_t>(payloadLength));
  char frame[96] = {};
  snprintf(frame, sizeof(frame), "%s*%04X\n", payload, crc);

  d12xSerial.print(frame);
  Serial.print("TX ");
  Serial.print(frame);
  ++sequenceNumber;
  heartbeatDirty = false;
}

}  // namespace

void setup() {
  Serial.begin(kBaudRate);
  d12xSerial.begin(kBaudRate, SERIAL_8N1, kGatewayRxPin, kGatewayTxPin);
  pinMode(kWaterSensorPin, INPUT_PULLUP);
  pinMode(kSmokeSensorPin, INPUT_PULLUP);
  pinMode(kDoorSensorPin, INPUT_PULLUP);
  pinMode(kImuIntPin, INPUT);
  delay(300);
  Serial.println();
  Serial.printf("VelaCare ESP32-S3 gateway v%s\n", kFirmwareVersion);
  Serial.printf("Door sensor: GPIO%d open=HIGH debounce=%lums\n",
                kDoorSensorPin, static_cast<unsigned long>(kDoorDebounceMs));
  Serial.printf("D12x UART: TX=GPIO%d RX=GPIO%d baud=%lu\n", kGatewayTxPin,
                kGatewayRxPin, static_cast<unsigned long>(kBaudRate));
  Serial.printf("Water sensor: DO=GPIO%d active=%s\n", kWaterSensorPin,
                kWaterAlarmLevel == LOW ? "LOW" : "HIGH");
  Serial.printf("Smoke sensor: DO=GPIO%d active=%s warmup=%lus\n",
                kSmokeSensorPin,
                kSmokeAlarmLevel == LOW ? "LOW" : "HIGH",
                static_cast<unsigned long>(kSmokeWarmupMs / 1000));
  Serial.printf("IMU: MPU-6500/9250 I2C SDA=GPIO%d SCL=GPIO%d INT=GPIO%d\n",
                kImuSdaPin, kImuSclPin, kImuIntPin);
  Serial.printf("MAX98357A: DIN=GPIO%d BCLK=GPIO%d LRC=GPIO%d\n",
                kAudioDataPin, kAudioBclkPin, kAudioLrcPin);
  Serial.println("Alarm control: D12X onboard buzzer via VC1/VCB1");
  beginAudioSubsystem();
  loadEventHistory();
  loadCaregiverSettings();
  loadEventUploadSettings();
  loadSosState();
  loadCareEventHistory();
  loadFamilyNotificationQueue();
  migrateLegacyFamilyNotificationState();
  reconcileCareStateAfterLoad();
  if (beginImu()) {
    setSensorState(3, kNormal);
  }
  configureWebRoutes();
  WiFi.onEvent(onWifiDisconnected, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
  beginSavedWifiConnection();
  printStatus();
  sendHeartbeat();
  sendCaregiverFrame();
  updateBuzzer();
  lastHeartbeatMs = millis();
  lastConfigSyncMs = millis();
}

void loop() {
  updateWifiState();
  readDebugCommands();
  readD12xMessages();
  updateWaterSensor();
  updateSmokeSensor();
  updateDoorSensor();
  updateImuSensor();
  updateAlarmEscalation();
  updateBuzzer();
  updateFamilyNotification();
  updateEventUpload();
  const uint32_t now = millis();
  if (heartbeatDirty ||
      static_cast<uint32_t>(now - lastHeartbeatMs) >= kHeartbeatMs) {
    lastHeartbeatMs = now;
    sendHeartbeat();
  }
  if (static_cast<uint32_t>(now - lastConfigSyncMs) >= kConfigSyncMs) {
    lastConfigSyncMs = now;
    sendCaregiverFrame();
  }
  delay(2);
}
