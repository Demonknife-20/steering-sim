/*
  ============================================================
  ESP32-S3 DIREKSIYON SIMULATORU - TEK DOSYA SURUM
  ============================================================
  Ozellikler:
   1) 1.8" TFT (ST7735, YATAY/landscape) uzerinde dashboard
   2) Telefon tarayicisından kontrol (WiFi AP)
   3) USB HID Gamepad (PC'ye takinca joystick gibi gorunur)
   4) ETS2 telemetri (TCP port 5000 uzerinden)
   5) Titresim geri bildirimi
   6) Baglanti guvenligi

  ARDUINO IDE AYARLARI (ZORUNLU):
    Tools -> USB Mode: "USB-OTG (TinyUSB)"
    Tools -> USB CDC On Boot: "Enabled"
    Board: "ESP32S3 Dev Module"

  GEREKLI KUTUPHANELER:
    - TFT_eSPI (Bodmer)
    - ESPAsyncWebServer + AsyncTCP
    - ArduinoJson
  ============================================================
*/

// ========== TFT AYARLARI (User_Setup.h icerigi) ==========
#define ST7735_DRIVER
#define TFT_WIDTH  128
#define TFT_HEIGHT 160
#define TFT_CS   10
#define TFT_DC    9
#define TFT_RST   8
#define TFT_MOSI 11
#define TFT_SCLK 12
#define LOAD_GLCD
#define SPI_FREQUENCY 27000000
// ========================================================

#include <Arduino.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <TFT_eSPI.h>
#include <time.h>

#if ARDUINO_USB_MODE == 1
  #warning "USB Mode 'Hardware CDC and JTAG' secili - USB HID Gamepad calismaz. Tools > USB Mode > USB-OTG (TinyUSB) sec."
#endif
#include "USB.h"
#include "USBHIDGamepad.h"
#include "USBHIDKeyboard.h"
USBHIDGamepad Gamepad;
USBHIDKeyboard Keyboard;

// ============ WIFI AYARLARI - DEGISTIR ============
const char* AP_SSID = "SteeringWheelSim";
const char* AP_PASS = "12345678";

// SENIN WIFI BILGILERINI BURAYA YAZ:
const char* STA_SSID = "SENIN_WIFI_ADI";        // <- DEGISTIR
const char* STA_PASS = "SENIN_WIFI_SIFRESI";   // <- DEGISTIR
const long  GMT_OFFSET_SEC = 3 * 3600;
const int   DST_OFFSET_SEC = 0;
// ================================================

bool timeSynced = false;

// ============ NESNELER ============
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");
TFT_eSPI tft = TFT_eSPI();

const uint16_t TELEMETRY_PORT = 5000;
WiFiServer telemetryServer(TELEMETRY_PORT);
WiFiClient telemetryClient;
// ===================================

// ============ KONTROL DURUMU ============
float steering  = 0.0f;
float throttle  = 0.0f;
float brake     = 0.0f;

int telemetrySpeed = 0;
int telemetryRpm   = 0;
int telemetryGear  = 0;

unsigned long lastGamepadSend   = 0;
unsigned long bootMillis        = 0;

int prevSpeed = -1, prevRpm = -1, prevGear = -999, prevSec = -1;
bool wifiStaOk = false;

unsigned long lastWsMessageTime = 0;
const unsigned long WS_TIMEOUT_MS = 500;
bool safetyKeySent = false;

bool redlineWarned = false;
const int RPM_REDLINE = 2000;
const int RPM_MAX     = 2500;

// ============ VITES KONTROL ============
const uint8_t GAMEPAD_BTN_GEAR_UP   = 0;
const uint8_t GAMEPAD_BTN_GEAR_DOWN = 1;
const unsigned long GEAR_PULSE_MS   = 80;

bool gearUpPending = false, gearDownPending = false;
unsigned long gearUpReleaseAt = 0, gearDownReleaseAt = 0;
// ========================================

// ============ TUSLAR ============
const char KEY_LEFT_SIGNAL  = '[';
const char KEY_RIGHT_SIGNAL = ']';
const char KEY_HAZARD       = 'f';
const char KEY_LIGHTS_MODE  = 'l';
const char KEY_HIGH_BEAM    = 'k';
const char KEY_HORN         = 'h';
const char KEY_CRUISE       = 'c';
// ================================

// ============ HTML SAYFA ============
const char PAGE_HTML[] PROGMEM = R"HTML(
<!DOCTYPE html>
<html>
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1, user-scalable=no">
<title>Direksiyon Kontrol</title>
<style>
  @keyframes pulse {
    0%   { box-shadow: 0 0 0 0 rgba(0,255,120,0.55); }
    70%  { box-shadow: 0 0 0 9px rgba(0,255,120,0); }
    100% { box-shadow: 0 0 0 0 rgba(0,255,120,0); }
  }
  @keyframes flashGood { 0%{background:#0c4;} 100%{background:inherit;} }
  @keyframes spin { from{transform:rotate(0turn);} to{transform:rotate(1turn);} }

  * { box-sizing: border-box; }
  body{margin:0;background:linear-gradient(160deg,#0b0c10,#181a22 60%,#0b0c10);
       color:#fff;font-family:'Segoe UI',sans-serif;user-select:none;
       display:flex;flex-direction:column;height:100vh;touch-action:none;overflow:hidden;}

  #topBar{display:flex;align-items:center;justify-content:center;gap:8px;padding:8px 6px 2px;}
  #statusDot{width:10px;height:10px;border-radius:50%;background:#a11;
             transition:background 0.3s;flex-shrink:0;}
  #statusDot.connected{background:#1e2;animation:pulse 1.8s infinite;}
  #status{text-align:center;font-size:13px;color:#aaa;letter-spacing:.3px;}

  #wheelWrap{display:flex;flex-direction:column;align-items:center;padding:6px 0 2px;}
  #wheel{width:88px;height:88px;border-radius:50%;
         border:6px solid #333;border-top-color:#0f8;
         background:radial-gradient(circle at 50% 50%,#242833 55%,#14161c 100%);
         position:relative;transition:transform 0.05s linear;
         box-shadow:0 0 14px rgba(0,255,150,0.15) inset;}
  #wheel::before{content:'';position:absolute;top:6px;left:50%;width:4px;height:16px;
                 background:#0f8;border-radius:2px;transform:translateX(-50%);}
  #wheel::after{content:'';position:absolute;top:50%;left:50%;width:14px;height:14px;
                background:#0f8;border-radius:50%;transform:translate(-50%,-50%);
                box-shadow:0 0 8px #0f8;}
  #steerVal{text-align:center;font-size:15px;padding:4px 0 2px;color:#0f8;font-weight:600;}

  .row{display:flex;flex:1;min-height:100px;gap:0;}
  .pedal{flex:1;margin:6px;border-radius:14px;position:relative;overflow:hidden;
         display:flex;align-items:flex-end;justify-content:center;
         font-size:18px;font-weight:bold;color:#fff;background:#20222a;
         box-shadow:inset 0 0 0 1px #333;}
  .pedal .fill{position:absolute;bottom:0;left:0;width:100%;height:0%;}
  #brake .fill{background:linear-gradient(#ff5555,#aa1111);}
  #throttle .fill{background:linear-gradient(#5cff7a,#118a2c);}
  .pedal .label{position:relative;z-index:2;padding-bottom:10px;text-shadow:0 1px 3px #000;}

  .gearRow{display:flex;align-items:stretch;gap:6px;padding:6px;}
  .gearBtnRound{flex:1;font-size:28px;font-weight:700;text-align:center;color:#fff;
                background:#2a2d36;border:1px solid #383c47;border-radius:12px;}
  #gearBig{flex:2;text-align:center;font-size:34px;font-weight:800;color:#0af;
           background:#20222a;border:1px solid #333;border-radius:12px;
           display:flex;align-items:center;justify-content:center;}

  .quickRow{display:flex;gap:6px;padding:0 6px 6px;}
  .quickBtn{flex:1;padding:8px 0;text-align:center;background:#2a2d36;color:#ccc;
            font-size:13px;border:1px solid #383c47;border-radius:8px;}

  .funcRow{display:flex;gap:6px;padding:2px 6px 8px;}
  .funcBtn{flex:1;padding:12px 0;text-align:center;font-size:19px;color:#ccc;
           background:#2a2d36;border:1px solid #383c47;border-radius:10px;}
  .funcBtn.horn{background:#2a2d36;}
</style>
</head>
<body>
  <div id="topBar">
    <div id="statusDot"></div>
    <div id="status">Baglaniyor...</div>
  </div>

  <div id="wheelWrap">
    <div id="wheel"></div>
    <div id="steerVal">Direksiyon: 0.00</div>
  </div>

  <div class="row">
    <div class="pedal" id="brake" ontouchstart="pedalMove(event,'brake')" ontouchmove="pedalMove(event,'brake')" ontouchend="pedalEnd('brake')">
      <div class="fill" id="brakeFill"></div>
      <div class="label">FREN</div>
    </div>
    <div class="pedal" id="throttle" ontouchstart="pedalMove(event,'throttle')" ontouchmove="pedalMove(event,'throttle')" ontouchend="pedalEnd('throttle')">
      <div class="fill" id="throttleFill"></div>
      <div class="label">GAZ</div>
    </div>
  </div>

  <div class="gearRow">
    <button class="gearBtnRound" onclick="changeGear(-1)">-</button>
    <div id="gearBig">N</div>
    <button class="gearBtnRound" onclick="changeGear(1)">+</button>
  </div>

  <div class="quickRow">
    <button class="quickBtn" onclick="tapFunc(this,'leftSignal')">SOL</button>
    <button class="quickBtn" onclick="tapFunc(this,'hazard')">4LU</button>
    <button class="quickBtn" onclick="tapFunc(this,'rightSignal')">SAG</button>
  </div>

  <div class="funcRow">
    <button class="funcBtn" onclick="tapFunc(this,'lights')">FAR</button>
    <button class="funcBtn" onclick="tapFunc(this,'highBeam')">UZUN</button>
    <button class="funcBtn" onclick="tapFunc(this,'cruise')">CRUISE</button>
    <button class="funcBtn horn" ontouchstart="hornStart(event,this)" ontouchend="hornEnd(event,this)">KORNA</button>
  </div>

<script>
let ws;
let steering=0, throttle=0, brake=0;
let currentGear=0;
let steerOffset = 0;
let lastGamma = 0;
let smoothedSteering = 0;
const SMOOTHING = 0.15;
const DEADZONE_DEG = 2;

function connect(){
  ws = new WebSocket("ws://" + location.hostname + "/ws");
  ws.onopen = () => {
    document.getElementById('status').innerText = "Baglandi";
    document.getElementById('statusDot').classList.add('connected');
  };
  ws.onclose = () => {
    document.getElementById('status').innerText = "Koptu...";
    document.getElementById('statusDot').classList.remove('connected');
    setTimeout(connect, 1000);
  };
  ws.onmessage = (e) => {
    try {
      const msg = JSON.parse(e.data);
      if (msg.vibrate && navigator.vibrate) navigator.vibrate(msg.vibrate);
      if (typeof msg.gear !== 'undefined') {
        currentGear = msg.gear;
        document.getElementById('gearBig').innerText = gearLabel(currentGear);
      }
    } catch (err) {}
  };
}
connect();

function gearLabel(g){
  if (g === 0) return 'N';
  if (g < 0) return 'R';
  return g;
}

function send(){
  if(ws && ws.readyState === 1){
    ws.send(JSON.stringify({steering, throttle, brake}));
  }
}

function sendKey(action){
  if(ws && ws.readyState === 1){
    ws.send(JSON.stringify({key:action}));
  }
}

function pedalMove(e, which){
  e.preventDefault();
  const box = document.getElementById(which).getBoundingClientRect();
  const clientY = e.touches ? e.touches[0].clientY : e.clientY;
  let intensity = 1 - (clientY - box.top) / box.height;
  intensity = Math.max(0, Math.min(1, intensity));
  if (which === 'brake') brake = intensity; else throttle = intensity;
  document.getElementById(which + 'Fill').style.height = (intensity * 100) + '%';
  send();
}
function pedalEnd(which){
  if (which === 'brake') brake = 0; else throttle = 0;
  document.getElementById(which + 'Fill').style.height = '0%';
  send();
}

function changeGear(delta){
  sendKey(delta > 0 ? 'gearUp' : 'gearDown');
}

function tapFunc(el, action){
  sendKey(action);
  el.classList.add('active');
  setTimeout(() => el.classList.remove('active'), 200);
}

function hornStart(e, el){
  e.preventDefault();
  sendKey('hornDown');
  el.classList.add('active');
}
function hornEnd(e, el){
  e.preventDefault();
  sendKey('hornUp');
  el.classList.remove('active');
}

function handleOrientation(e){
  let g = e.gamma || 0;
  lastGamma = g;
  g = g - steerOffset;
  if (Math.abs(g) < DEADZONE_DEG) g = 0;
  g = Math.max(-45, Math.min(45, g));
  const target = g / 45;
  smoothedSteering += (target - smoothedSteering) * SMOOTHING;
  steering = smoothedSteering;
  document.getElementById('steerVal').innerText = "Direksiyon: " + steering.toFixed(2);
  document.getElementById('wheel').style.transform = "rotate(" + (steering * 90) + "deg)";
  send();
}
if (typeof DeviceOrientationEvent !== 'undefined' &&
    typeof DeviceOrientationEvent.requestPermission === 'function') {
  document.body.addEventListener('click', function once(){
    DeviceOrientationEvent.requestPermission().then(state => {
      if (state === 'granted') window.addEventListener('deviceorientation', handleOrientation);
    });
    document.body.removeEventListener('click', once);
  });
} else {
  window.addEventListener('deviceorientation', handleOrientation);
}

setInterval(send, 100);
</script>
</body>
</html>
)HTML";

// ============ WEBSOCKET ISLEMCISI ============
void handleKeyAction(const char* action) {
  String a = action;
  if (a == "gearUp") {
    Gamepad.pressButton(GAMEPAD_BTN_GEAR_UP);
    gearUpPending = true;
    gearUpReleaseAt = millis() + GEAR_PULSE_MS;
    sendVibration(60);
  } else if (a == "gearDown") {
    Gamepad.pressButton(GAMEPAD_BTN_GEAR_DOWN);
    gearDownPending = true;
    gearDownReleaseAt = millis() + GEAR_PULSE_MS;
    sendVibration(60);
  } else if (a == "leftSignal") {
    Keyboard.write(KEY_LEFT_SIGNAL);
  } else if (a == "rightSignal") {
    Keyboard.write(KEY_RIGHT_SIGNAL);
  } else if (a == "hazard") {
    Keyboard.write(KEY_HAZARD);
  } else if (a == "lights") {
    Keyboard.write(KEY_LIGHTS_MODE);
  } else if (a == "highBeam") {
    Keyboard.write(KEY_HIGH_BEAM);
  } else if (a == "cruise") {
    Keyboard.write(KEY_CRUISE);
  } else if (a == "hornDown") {
    Keyboard.press(KEY_HORN);
  } else if (a == "hornUp") {
    Keyboard.release(KEY_HORN);
  }
}

void onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client,
               AwsEventType type, void *arg, uint8_t *data, size_t len) {
  if (type == WS_EVT_DATA) {
    lastWsMessageTime = millis();
    StaticJsonDocument<200> doc;
    if (deserializeJson(doc, data, len)) return;

    if (doc.containsKey("key")) {
      handleKeyAction(doc["key"].as<const char*>());
      return;
    }

    steering = doc["steering"] | steering;
    throttle = doc["throttle"] | throttle;
    brake    = doc["brake"]    | brake;
  }
}

void processGearPulses() {
  if (gearUpPending && millis() >= gearUpReleaseAt) {
    Gamepad.releaseButton(GAMEPAD_BTN_GEAR_UP);
    gearUpPending = false;
  }
  if (gearDownPending && millis() >= gearDownReleaseAt) {
    Gamepad.releaseButton(GAMEPAD_BTN_GEAR_DOWN);
    gearDownPending = false;
  }
}

void broadcastGearToClients() {
  static int lastBroadcastGear = -999;
  static unsigned long lastBroadcast = 0;
  if (telemetryGear == lastBroadcastGear && millis() - lastBroadcast < 1000) return;
  if (millis() - lastBroadcast < 300) return;
  lastBroadcastGear = telemetryGear;
  lastBroadcast = millis();
  char msg[24];
  snprintf(msg, sizeof(msg), "{\"gear\":%d}", telemetryGear);
  ws.textAll(msg);
}
// ============================================

// ============ BAGLANTI GUVENLIGI ============
void checkConnectionSafety() {
  if (lastWsMessageTime != 0 && millis() - lastWsMessageTime > WS_TIMEOUT_MS) {
    steering = 0;
    throttle = 0;
    brake    = 1;
    // Baglanti koptu - acil duruma gec, F1 basma
    if (!safetyKeySent) {
      Keyboard.press(KEY_F1);
      delay(100);
      Keyboard.release(KEY_F1);
      safetyKeySent = true;
    }
  } else {
    safetyKeySent = false;
  }
}
// ===========================================

// ============ TITRESIM GERI BILDIRIMI ============
void sendVibration(int ms) {
  char msg[32];
  snprintf(msg, sizeof(msg), "{\"vibrate\":%d}", ms);
  ws.textAll(msg);
}

void checkVibrationFeedback() {
  if (telemetryRpm > RPM_REDLINE && !redlineWarned) {
    sendVibration(250);
    redlineWarned = true;
  } else if (telemetryRpm < RPM_REDLINE - 300) {
    redlineWarned = false;
  }
}
// ================================================

// ============ USB HID GAMEPAD ============
void updateGamepad() {
  if (millis() - lastGamepadSend < 20) return;
  lastGamepadSend = millis();

  int8_t stX = (int8_t)constrain(steering * 127, -127, 127);
  int8_t thr = (int8_t)constrain(throttle * 127, 0, 127);
  int8_t brk = (int8_t)constrain(brake * 127, 0, 127);

  Gamepad.leftStick(stX, 0);
  Gamepad.rightTrigger(thr);
  Gamepad.leftTrigger(brk);
}
// ==========================================

// ============ TELEMETRI (PC -> ESP32) ============
void handleTelemetryJson(uint8_t* data, size_t len) {
  StaticJsonDocument<256> doc;
  if (deserializeJson(doc, data, len)) return;
  telemetrySpeed = doc["speed"] | 0;
  telemetryRpm   = doc["rpm"]   | 0;
  telemetryGear  = doc["gear"]  | 0;
}

bool readExact(WiFiClient& c, uint8_t* buf, size_t len, unsigned long timeoutMs) {
  size_t got = 0;
  unsigned long start = millis();
  while (got < len) {
    if (!c.connected()) return false;
    int n = c.read(buf + got, len - got);
    if (n > 0) got += n;
    else if (millis() - start > timeoutMs) return false;
  }
  return true;
}

void pollTelemetry() {
  if (!telemetryClient || !telemetryClient.connected()) {
    telemetryClient = telemetryServer.available();
    return;
  }
  if (telemetryClient.available() >= 5) {
    uint8_t header[5];
    if (!readExact(telemetryClient, header, 5, 200)) return;

    uint8_t type = header[0];
    uint32_t len = ((uint32_t)header[1] << 24) | ((uint32_t)header[2] << 16) |
                   ((uint32_t)header[3] << 8)  | header[4];
    if (len == 0 || len > 1024) return;

    static uint8_t buf[1024];
    if (!readExact(telemetryClient, buf, len, 200)) return;

    if (type == 0x01) handleTelemetryJson(buf, len);
  }
}
// =================================================

// ============ TFT DASHBOARD ============
String gearLabel(int g) {
  if (g == 0) return "N";
  if (g < 0) return "R";
  return String(g);
}

const int SPEED_X = 8,  SPEED_Y = 16;
const int BAR_X = 8,    BAR_Y = 66, BAR_W = 108, BAR_H = 14;
const int GEAR_X = 8,   GEAR_Y = 90, GEAR_W = 48, GEAR_H = 34;
const int CLOCK_X = 118, CLOCK_Y = 4;
const int WIFI_DOT_X = 152, WIFI_DOT_Y = 8;

void drawStaticLayout() {
  tft.fillScreen(TFT_BLACK);
  tft.drawFastHLine(0, 0, 160, TFT_DARKGREY);
  tft.setTextColor(TFT_SILVER, TFT_BLACK);
  tft.setTextSize(1);
  tft.drawString("ETS2 DASH", 6, 4);
  tft.drawString("km/h", SPEED_X + 68, SPEED_Y + 12);
  tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
  tft.drawRoundRect(BAR_X - 1, BAR_Y - 1, BAR_W + 2, BAR_H + 2, 3, TFT_DARKGREY);
  tft.drawRoundRect(GEAR_X, GEAR_Y, GEAR_W, GEAR_H, 4, TFT_DARKGREY);
}

void drawWifiStatus(bool ok) {
  tft.fillCircle(WIFI_DOT_X, WIFI_DOT_Y, 3, ok ? TFT_GREEN : TFT_RED);
}

float dispSpeed = 0, dispRpm = 0;
unsigned long gearFlashUntil = 0;
unsigned long lastRedBlink = 0;
bool redBlinkOn = false;

void drawSpeed(int v) {
  tft.setTextColor(TFT_GREEN, TFT_BLACK);
  tft.setTextSize(4);
  tft.fillRect(SPEED_X, SPEED_Y, 64, 32, TFT_BLACK);
  tft.drawNumber(v, SPEED_X, SPEED_Y);
}

void drawRpmBar(int v) {
  int vClamped = constrain(v, 0, RPM_MAX);
  int fillW = map(vClamped, 0, RPM_MAX, 0, BAR_W);
  bool inRedline = (vClamped >= RPM_REDLINE);
  uint16_t color;
  if (vClamped < RPM_REDLINE - 300) color = TFT_GREEN;
  else if (vClamped < RPM_REDLINE) color = TFT_YELLOW;
  else color = redBlinkOn ? TFT_RED : TFT_MAROON;

  tft.fillRect(BAR_X, BAR_Y, BAR_W, BAR_H, TFT_BLACK);
  if (fillW > 0) tft.fillRoundRect(BAR_X, BAR_Y, fillW, BAR_H, 3, color);
  tft.drawRoundRect(BAR_X - 1, BAR_Y - 1, BAR_W + 2, BAR_H + 2, 3,
                     inRedline ? color : TFT_DARKGREY);

  tft.setTextColor(TFT_SILVER, TFT_BLACK);
  tft.setTextSize(1);
  tft.fillRect(BAR_X, BAR_Y - 12, 60, 10, TFT_BLACK);
  tft.drawNumber(v, BAR_X, BAR_Y - 12);
  tft.drawString("rpm", BAR_X + 40, BAR_Y - 12);
}

void drawGear(int g, bool flash) {
  uint16_t boxColor = flash ? TFT_WHITE : TFT_DARKGREY;
  tft.drawRoundRect(GEAR_X, GEAR_Y, GEAR_W, GEAR_H, 4, boxColor);
  tft.setTextColor(flash ? TFT_WHITE : TFT_CYAN, TFT_BLACK);
  tft.setTextSize(3);
  tft.fillRect(GEAR_X + 3, GEAR_Y + 3, GEAR_W - 6, GEAR_H - 6, TFT_BLACK);
  tft.drawString(gearLabel(g), GEAR_X + 14, GEAR_Y + 6);
}

void drawClock(const char* buf) {
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.setTextSize(1);
  tft.fillRect(CLOCK_X, CLOCK_Y, 34, 10, TFT_BLACK);
  tft.drawString(buf, CLOCK_X, CLOCK_Y);
}

void updateDashboardAnim() {
  dispSpeed += (telemetrySpeed - dispSpeed) * 0.25f;
  dispRpm   += (telemetryRpm   - dispRpm)   * 0.25f;
  if (fabs(telemetrySpeed - dispSpeed) < 0.4f) dispSpeed = telemetrySpeed;
  if (fabs(telemetryRpm   - dispRpm)   < 3.0f) dispRpm   = telemetryRpm;

  int roundedSpeed = (int)roundf(dispSpeed);
  int roundedRpm   = (int)roundf(dispRpm);

  if (roundedSpeed != prevSpeed) { prevSpeed = roundedSpeed; drawSpeed(roundedSpeed); }

  bool wasRed = redBlinkOn;
  if (roundedRpm >= RPM_REDLINE && millis() - lastRedBlink > 220) {
    redBlinkOn = !redBlinkOn;
    lastRedBlink = millis();
  }
  if (roundedRpm != prevRpm || wasRed != redBlinkOn) { prevRpm = roundedRpm; drawRpmBar(roundedRpm); }

  if (telemetryGear != prevGear) {
    prevGear = telemetryGear;
    gearFlashUntil = millis() + 350;
    drawGear(telemetryGear, true);
  } else if (gearFlashUntil != 0 && millis() > gearFlashUntil) {
    gearFlashUntil = 0;
    drawGear(telemetryGear, false);
  }

  char buf[9];
  int curSecKey;
  if (timeSynced) {
    struct tm ti;
    if (getLocalTime(&ti, 5)) {
      sprintf(buf, "%02d:%02d", ti.tm_hour, ti.tm_min);
      curSecKey = ti.tm_hour * 60 + ti.tm_min;
    } else {
      curSecKey = prevSec;
      strcpy(buf, "--:--");
    }
  } else {
    unsigned long upMin = (millis() - bootMillis) / 60000;
    sprintf(buf, "%02d:%02d", (int)(upMin / 60) % 24, (int)(upMin % 60));
    curSecKey = (int)upMin;
  }
  if (curSecKey != prevSec) {
    prevSec = curSecKey;
    drawClock(buf);
  }
}
// =====================================

// ============ SETUP ============
void setup() {
  Serial.begin(115200);
  bootMillis = millis();

  tft.init();
  tft.setRotation(1);

  Gamepad.begin();
  Keyboard.begin();
  USB.begin();

  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(AP_SSID, AP_PASS);
  Serial.print("Telefon AP IP: ");
  Serial.println(WiFi.softAPIP());

  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_SILVER, TFT_BLACK);
  tft.setTextSize(2);
  tft.drawString("ETS2 DASH", 18, 40);
  tft.drawRoundRect(20, 74, 120, 10, 4, TFT_DARKGREY);

  WiFi.begin(STA_SSID, STA_PASS);
  Serial.print("Ev WiFi'ye baglaniliyor");
  unsigned long staStart = millis();
  int animFrame = 0;
  while (WiFi.status() != WL_CONNECTED && millis() - staStart < 8000) {
    tft.fillRoundRect(22, 76, 116, 6, 3, TFT_BLACK);
    int w = 16 + (animFrame % 8) * 14;
    tft.fillRoundRect(22, 76, min(w, 116), 6, 3, TFT_GREEN);
    animFrame++;
    delay(150);
    Serial.print(".");
  }

  drawStaticLayout();
  drawWifiStatus(false);
  drawSpeed(0);
  drawRpmBar(0);
  drawGear(0, false);
  wifiStaOk = (WiFi.status() == WL_CONNECTED);
  drawWifiStatus(wifiStaOk);

  if (wifiStaOk) {
    Serial.print("\nEv WiFi'ye baglandi, IP: ");
    Serial.println(WiFi.localIP());
    configTime(GMT_OFFSET_SEC, DST_OFFSET_SEC, "pool.ntp.org", "time.nist.gov");
    struct tm ti;
    if (getLocalTime(&ti, 5000)) {
      timeSynced = true;
      Serial.println("Saat senkron edildi.");
    } else {
      Serial.println("NTP zaman asimi, uptime saati kullanilacak.");
    }
  } else {
    Serial.println("\nEv WiFi'ye baglanilamadi.");
  }

  ws.onEvent(onWsEvent);
  server.addHandler(&ws);
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send_P(200, "text/html", PAGE_HTML);
  });
  server.begin();

  telemetryServer.begin();
}
// ===============================

// ============ LOOP ============
void loop() {
  ws.cleanupClients();
  checkConnectionSafety();
  updateGamepad();
  processGearPulses();
  checkVibrationFeedback();
  pollTelemetry();
  broadcastGearToClients();

  static unsigned long lastDrawUpdate = 0;
  if (millis() - lastDrawUpdate > 30) {
    updateDashboardAnim();
    lastDrawUpdate = millis();
  }

  static unsigned long lastWifiCheck = 0;
  if (millis() - lastWifiCheck > 2000) {
    bool ok = (WiFi.status() == WL_CONNECTED);
    if (ok != wifiStaOk) { wifiStaOk = ok; drawWifiStatus(ok); }
    lastWifiCheck = millis();
  }
}
// ==============================