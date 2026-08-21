#include <Arduino.h>
#include <WiFi.h>
#include <DNSServer.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <esp_mac.h>
#include <esp_system.h>
#include <esp_task_wdt.h>
#include <Arduino_GFX_Library.h>
#include <lvgl.h>
#include <Wire.h>
#include "app_config.h"
#include "connection_settings.h"

static constexpr const char* FW_VERSION = BATTERY_DISPLAY_VERSION;
// Client builds contain no Wi-Fi credentials. They live only in protected
// NVS and can be changed from the phone setup page.
static constexpr const char* FACTORY_WIFI_SSID = "";
static constexpr const char* FACTORY_WIFI_PASSWORD = "";
static constexpr const char* DEFAULT_EMULATOR_HOST = "";
// Preserve the already configured address only on the owner's physical unit.
// New displays start with a blank address and must be configured by the user.
static constexpr const char* OWNER_DISPLAY_MAC = LOCAL_OWNER_DISPLAY_MAC;
static constexpr const char* OWNER_EMULATOR_HOST = LOCAL_OWNER_EMULATOR_HOST;
static constexpr const char* SETUP_AP_NAME = "BatteryEmulator-CYD";
static constexpr const char* SETUP_AP_PASSWORD = "123456789";
static constexpr uint8_t DEFAULT_ESPNOW_CHANNEL = 1;
// The upstream transmitter can use ESP-NOW v2 frames.  Its current cell frames
// fit in 250 bytes, but a larger buffer preserves compatibility with future
// fields and multi-cell chunks.
static constexpr size_t ESPNOW_RX_MAX = 1470;

// =====================================================
// LCD wiring. The first profile is the original T-Display-S3; the second is
// the 480x480 RGB/ST7701 panel used by ESP32-4848S040C_I.
// =====================================================
#if defined(TARGET_4848S040)
Arduino_DataBus *bus = new Arduino_SWSPI(
  GFX_NOT_DEFINED, 39, 48, 47, GFX_NOT_DEFINED
);
Arduino_ESP32RGBPanel *rgbpanel = new Arduino_ESP32RGBPanel(
  18, 17, 16, 21,
  11, 12, 13, 14, 0,
  8, 20, 3, 46, 9, 10,
  4, 5, 6, 7, 15,
  1, 10, 8, 50,
  1, 10, 8, 20
);
Arduino_GFX *gfx = new Arduino_RGB_Display(
  480, 480, rgbpanel, 1, true, bus, GFX_NOT_DEFINED,
  st7701_type9_init_operations, sizeof(st7701_type9_init_operations)
);
static constexpr int SCREEN_W=480;
static constexpr int SCREEN_H=480;
static constexpr int PIN_POWER=-1;
static constexpr int PIN_BL=38;
static constexpr int BTN_LEFT=-1;
static constexpr int BTN_RIGHT=-1;
static constexpr int TOUCH_SDA=19;
static constexpr int TOUCH_SCL=45;
static constexpr uint8_t TOUCH_ADDR=0x5D;
#else
Arduino_DataBus *bus = new Arduino_ESP32PAR8Q(
  7, 6, 8, 9, 39, 40, 41, 42, 45, 46, 47, 48
);
Arduino_GFX *gfx = new Arduino_ST7789(
  bus, 5, 0, true, 170, 320, 35, 0, 35, 0
);
static constexpr int SCREEN_W=320;
static constexpr int SCREEN_H=170;
static constexpr int PIN_POWER = 15;
static constexpr int PIN_BL = 38;
static constexpr int BTN_LEFT = 0;
static constexpr int BTN_RIGHT = 14;
#endif
static constexpr int BL_CHANNEL = 7;
static constexpr uint8_t DEFAULT_BRIGHTNESS = 255;
static constexpr uint32_t AUTO_DIM_MS = 120000;
static constexpr uint32_t OFFLINE_AGE_MS = 15000;

Preferences preferences;
WiFiServer configServer(80);
DNSServer configDns;
String wifiSsid;
String wifiPassword;
String emulatorHost=DEFAULT_EMULATOR_HOST;
bool directMode=false;
uint8_t espNowChannel=DEFAULT_ESPNOW_CHANNEL;
bool configPortalActive=false;
volatile bool configPortalRequested=false;
volatile bool configPortalStopRequested=false;
uint32_t configPortalExitAt=0;
uint8_t configPortalClientCount=0;
bool controlServerRunning=false;
uint32_t settingsRestartAt=0;
uint8_t brightness=DEFAULT_BRIGHTNESS;
uint8_t colorTheme=0;
bool showSocBadge=true;
bool autoDimmed=false;
bool onboardingNeeded=false;
volatile int pendingControlAction=-1;
String controlActionResult;

struct Telemetry {
  float soc=NAN, realSoc=NAN, soh=NAN;
  float voltage=NAN, current=NAN;
  float totalScaled=NAN, totalReal=NAN;
  float remainScaled=NAN, remainReal=NAN;
  float maxDisKw=NAN, maxChgKw=NAN;
  float maxDisA=NAN, maxChgA=NAN;
  float tMin=NAN, tMax=NAN;
  uint16_t cMin=0, cMax=0, delta=0;
  String systemStatus="--", powerStatus="--";
  bool emuCont=false, invCont=false;
  bool contactorWarning=false;
  String hvilStatus="--", bmsState="--";
  String hvpContactorState="--", bmsContactorState="--";
  float isolationKOhm=NAN;
};
Telemetry batteryData[3];
Telemetry &t=batteryData[0];
uint32_t batteryLastGood[3]={};
uint8_t espNowBatteryCount=1;

uint32_t lastGood=0, lastFetch=0, lastReconnect=0, lastButton=0;
uint32_t lastTick=0;
int httpCode=0;
uint8_t consecutiveFetchFailures=0;
uint8_t page=0;
bool prevL=HIGH, prevR=HIGH;
bool pulse=false;
volatile bool uiDirty=false;
#if defined(TARGET_4848S040)
uint32_t lastTouchNavigation=0;
struct TouchHardwareTransform {
  bool swapXY=false;
  bool mirrorX=true;
  bool mirrorY=true;
};
TouchHardwareTransform touchHardware;
uint8_t screenOrientation=0;
bool touchCalibrationNeeded=false;
bool touchCalibrationActive=false;
uint8_t touchCalibrationStep=0;
volatile bool touchRawTapReady=false;
volatile uint16_t touchRawTapX=0;
volatile uint16_t touchRawTapY=0;
uint16_t touchCalRawX[3]={};
uint16_t touchCalRawY[3]={};
lv_obj_t *touchCalOverlay=nullptr;
lv_obj_t *touchCalTarget=nullptr;
lv_obj_t *touchCalStepLabel=nullptr;
#endif
bool serviceMode=false;
uint32_t bothPressedAt=0;
bool bothLongHandled=false;
uint32_t rightPressedAt=0;
bool rightLongHandled=false;
uint32_t wifiDisconnectedAt=0;
uint32_t lastSettingsSave=0;
uint32_t httpFailuresTotal=0;
uint32_t parseFailuresTotal=0;
uint32_t wifiReconnectsTotal=0;
bool espNowReady=false;
bool espNowChannelLocked=false;
uint32_t lastChannelHop=0;
bool espNowSystemOkay=false;
bool espNowBatteryAlive=false;
bool espNowInverterAlive=false;
uint32_t espNowPackets=0;
uint32_t lastEspNowGood=0;
uint32_t espNowDropped=0;
uint8_t espNowSourceMac[6]={};

struct EspNowPacket {
  uint16_t len=0;
  uint8_t source[6]={};
  uint8_t data[ESPNOW_RX_MAX]={};
};
QueueHandle_t espNowQueue=nullptr;

// ----------------- LVGL objects -----------------
lv_obj_t *page1;
lv_obj_t *page2;
lv_obj_t *alertBox;
lv_obj_t *alertTitle;
lv_obj_t *alertSub;
lv_obj_t *heartbeat;

lv_obj_t *vSoc, *vPower, *vCurrent, *vVoltage, *vDelta, *vTempMin, *vTempMax;
lv_obj_t *vCellMin, *vCellMax, *vRemain, *vTotal, *vSoh, *vChg, *vDis;

// Cell Monitor and Events pages
lv_obj_t *page3;
lv_obj_t *cellMinLabel, *cellDeltaLabel, *cellMaxLabel;
lv_obj_t *cellBars[96];

struct CellMonitorData {
  uint16_t mv[96] = {};
  bool balancing[96] = {};
  uint16_t minMv=0, maxMv=0, deltaMv=0;
  uint8_t minCell=0, maxCell=0;
  bool valid=false;
  uint32_t lastGood=0;
};
CellMonitorData batteryCells[3];
CellMonitorData &cells=batteryCells[0];

lv_obj_t *page4;
lv_obj_t *evTitle, *evRow1, *evRow2, *evRow3, *evRow4, *evSummary;

lv_obj_t *page5;
lv_obj_t *eventLogTitle, *eventLogCount;
lv_obj_t *eventLogRows[10];

lv_obj_t *servicePage;
lv_obj_t *serviceTitle, *serviceInfo, *serviceFooter;
lv_obj_t *socBadge2, *socBadge3, *socBadge4;

struct EventItem {
  String type;
  String severity;
  String message;
  int count=0;
};
EventItem ev[10];
int evCount=0;
bool eventsFresh=false;
uint32_t lastEventsFetch=0;
uint32_t lastCellsFetch=0;
uint32_t lastAdvancedFetch=0;

struct DtcItem {
  int code=0;
  String ecu;
  String frame;
  String title;
  String shortName;
  String status;
};
static constexpr uint8_t MAX_DISPLAY_DTCS=12;
DtcItem dtcItems[MAX_DISPLAY_DTCS];
uint8_t dtcCount=0;
bool dtcFresh=false;
SemaphoreHandle_t dataMutex;
SemaphoreHandle_t settingsMutex;


lv_color_t COL_BG      = lv_color_hex(0x070B10);
lv_color_t COL_CARD    = lv_color_hex(0x0C131B);
lv_color_t COL_BORDER  = lv_color_hex(0x5E7180);
lv_color_t COL_TEXT    = lv_color_hex(0xF5F7FA);
lv_color_t COL_MUTED   = lv_color_hex(0xA9B4BF);
lv_color_t COL_GREEN   = lv_color_hex(0x52E000);
lv_color_t COL_CYAN    = lv_color_hex(0x26D9FF);
lv_color_t COL_YELLOW  = lv_color_hex(0xFFD21A);
lv_color_t COL_ORANGE  = lv_color_hex(0xFF7A00);
lv_color_t COL_RED     = lv_color_hex(0xFF3434);

void applyThemePalette(uint8_t theme){
  switch(theme){
    case 1: // Pure dark
      COL_BG=lv_color_hex(0x000000); COL_CARD=lv_color_hex(0x101010);
      COL_BORDER=lv_color_hex(0x616161); COL_TEXT=lv_color_hex(0xFFFFFF);
      COL_MUTED=lv_color_hex(0xB8B8B8); COL_GREEN=lv_color_hex(0x62E36A);
      COL_CYAN=lv_color_hex(0x5EDBFF); COL_YELLOW=lv_color_hex(0xFFD45A);
      break;
    case 2: // Amber
      COL_BG=lv_color_hex(0x100A02); COL_CARD=lv_color_hex(0x1C1205);
      COL_BORDER=lv_color_hex(0x80611F); COL_TEXT=lv_color_hex(0xFFF1CC);
      COL_MUTED=lv_color_hex(0xD0B77D); COL_GREEN=lv_color_hex(0xA8E063);
      COL_CYAN=lv_color_hex(0xFFC04A); COL_YELLOW=lv_color_hex(0xFFE066);
      break;
    case 3: // Green instrument
      COL_BG=lv_color_hex(0x031009); COL_CARD=lv_color_hex(0x071A10);
      COL_BORDER=lv_color_hex(0x477A5A); COL_TEXT=lv_color_hex(0xE8FFF0);
      COL_MUTED=lv_color_hex(0x96BFA3); COL_GREEN=lv_color_hex(0x52E000);
      COL_CYAN=lv_color_hex(0x56E6A0); COL_YELLOW=lv_color_hex(0xD8F05A);
      break;
    default: // Classic
      COL_BG=lv_color_hex(0x070B10); COL_CARD=lv_color_hex(0x0C131B);
      COL_BORDER=lv_color_hex(0x5E7180); COL_TEXT=lv_color_hex(0xF5F7FA);
      COL_MUTED=lv_color_hex(0xA9B4BF); COL_GREEN=lv_color_hex(0x52E000);
      COL_CYAN=lv_color_hex(0x26D9FF); COL_YELLOW=lv_color_hex(0xFFD21A);
      break;
  }
}

// LVGL draw buffer
static lv_disp_draw_buf_t drawBuf;
static lv_color_t buf1[SCREEN_W * 32];

// Coordinates in the original UI are expressed for a 320x170 panel.  The
// large panel preserves the layout proportions while using its full area.
static inline int uiX(int x){ return x*SCREEN_W/320; }
static inline int uiY(int y){ return y*SCREEN_H/170; }

void applyBrightness(uint8_t value){
#if defined(TARGET_4848S040)
  // This panel's backlight driver switches off instead of dimming reliably.
  // Brightness adjustment is intentionally disabled and the backlight stays
  // at a stable 100% duty cycle.
  value=DEFAULT_BRIGHTNESS;
  // Keep the backlight pin attached to one PWM channel at every level,
  // including 100%. Switching between GPIO HIGH and PWM could leave some
  // ESP32-4848S040C-I boards with the backlight disabled until another touch.
  ledcSetup(BL_CHANNEL,12000,8);
  ledcAttachPin(PIN_BL,BL_CHANNEL);
  ledcWrite(BL_CHANNEL,value);
#else
  ledcWrite(BL_CHANNEL,value);
#endif
}

void saveSettings(){
  preferences.putUChar("page",page);
  preferences.putUChar("theme",colorTheme);
  preferences.putBool("soc-badge",showSocBadge);
  lastSettingsSave=millis();
}

ConnectionSettings currentConnectionSettings(bool requestedDirect){
  ConnectionSettings settings;
  settings.ssid=wifiSsid;
  settings.password=wifiPassword;
  settings.emulatorHost=emulatorHost;
  settings.directMode=requestedDirect;
  settings.espNowChannel=espNowChannel;
  return settings;
}

bool saveConnectionMode(bool requestedDirect){
  return storeConnectionSettings(preferences,currentConnectionSettings(requestedDirect),settingsMutex);
}

const char* resetReasonText(){
  switch(esp_reset_reason()){
    case ESP_RST_POWERON: return "POWER ON";
    case ESP_RST_SW: return "SOFTWARE";
    case ESP_RST_PANIC: return "CRASH";
    case ESP_RST_INT_WDT:
    case ESP_RST_TASK_WDT:
    case ESP_RST_WDT: return "WATCHDOG";
    case ESP_RST_BROWNOUT: return "BROWNOUT";
    default: return "OTHER";
  }
}

String ageText(uint32_t stamp){
  if(!stamp) return "never";
  uint32_t seconds=(millis()-stamp)/1000;
  if(seconds<60) return String(seconds)+"s";
  return String(seconds/60)+"m";
}

String emulatorUrl(const char* path="/"){
  return String("http://")+emulatorHost+path;
}

void onEspNowReceive(const uint8_t *source,const uint8_t *data,int len){
  if(!espNowQueue || !source || !data || len<=0 || len>(int)ESPNOW_RX_MAX){
    espNowDropped++;
    return;
  }
  EspNowPacket packet;
  packet.len=(uint16_t)len;
  memcpy(packet.source,source,6);
  memcpy(packet.data,data,len);
  if(xQueueSend(espNowQueue,&packet,0)!=pdTRUE) espNowDropped++;
}

void stopEspNow(){
  if(espNowReady) esp_now_deinit();
  espNowReady=false;
}

bool startEspNow(bool setFixedChannel){
  stopEspNow();
  WiFi.mode(configPortalActive?WIFI_AP_STA:WIFI_STA);
  WiFi.setSleep(false);
  if(setFixedChannel){
    WiFi.disconnect(false,false);
    delay(40);
    esp_wifi_set_channel(espNowChannel,WIFI_SECOND_CHAN_NONE);
  }
  if(esp_now_init()!=ESP_OK) return false;
  if(esp_now_register_recv_cb(onEspNowReceive)!=ESP_OK){
    esp_now_deinit();
    return false;
  }
  espNowReady=true;
  return true;
}

void startDirectLink(){
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.disconnect(false,false);
  delay(40);
  startEspNow(true);
  espNowChannelLocked=false;
  lastChannelHop=millis();
  wifiDisconnectedAt=0;
}

void connectStoredWiFi(){
  if(directMode){
    startDirectLink();
    return;
  }
  WiFi.mode(configPortalActive?WIFI_AP_STA:WIFI_STA);
  WiFi.setSleep(false);
  WiFi.disconnect();
  delay(50);
  WiFi.begin(wifiSsid.c_str(),wifiPassword.c_str());
  wifiReconnectsTotal++;
}

void stopConfigPortal(){
  if(!configPortalActive) return;
  configDns.stop();
  configServer.stop();
  WiFi.softAPdisconnect(true);
  configPortalActive=false;
  configPortalClientCount=0;
  controlServerRunning=false;
  configPortalExitAt=0;
  if(directMode) startDirectLink();
  else connectStoredWiFi();
}

void startConfigPortal(){
  if(configPortalActive) return;
  stopEspNow();
  configPortalClientCount=0;
  controlServerRunning=false;
  configPortalExitAt=0;
  configServer.stop();
  WiFi.disconnect(true,false);
  WiFi.mode(WIFI_OFF);
  delay(60);
  WiFi.mode(WIFI_AP);
  WiFi.setSleep(false);
  IPAddress apIp(192,168,4,1), gateway(192,168,4,1), subnet(255,255,255,0);
  WiFi.softAPConfig(apIp,gateway,subnet);
  if(!WiFi.softAP(SETUP_AP_NAME,SETUP_AP_PASSWORD,1,false,4)){
    configPortalActive=false;
    return;
  }
  delay(100);
  configServer.setNoDelay(true);
  configServer.begin();
  configDns.start(53,"*",apIp);
  configPortalActive=true;
}

void startControlServer(){
  if(controlServerRunning || configPortalActive || directMode || WiFi.status()!=WL_CONNECTED) return;
  configServer.stop();
  configServer.begin();
  controlServerRunning=true;
  Serial.printf("Display control: http://%s/\n",WiFi.localIP().toString().c_str());
}

String urlDecode(String value){
  value.replace('+',' ');
  String decoded;
  for(size_t i=0;i<value.length();i++){
    if(value[i]=='%' && i+2<value.length()){
      char hex[3]={value[i+1],value[i+2],0};
      decoded+=(char)strtol(hex,nullptr,16);
      i+=2;
    } else decoded+=value[i];
  }
  return decoded;
}

String queryValue(const String& request,const String& key){
  String marker=key+"=";
  int start=request.indexOf(marker);
  if(start<0) return "";
  start+=marker.length();
  int end=request.indexOf('&',start);
  if(end<0) end=request.indexOf(' ',start);
  if(end<0) end=request.length();
  return urlDecode(request.substring(start,end));
}

void handleConfigClient(){
  WiFiClient client=configServer.available();
  if(!client) return;
  client.setNoDelay(true);
  client.setTimeout(1000);
  uint32_t waitStarted=millis();
  while(client.connected() && !client.available() && millis()-waitStarted<1000) vTaskDelay(pdMS_TO_TICKS(2));
  if(!client.available()){ client.stop(); return; }
  String request=client.readStringUntil('\n');
  request.trim();
  // Read the complete header before replying. Closing a socket with unread
  // request bytes can produce a TCP reset on phones, discarding the page.
  while(client.connected()){
    String line=client.readStringUntil('\n');
    if(line=="\r" || line.length()==0) break;
  }

  bool saved=false;
  bool reset=false;
  bool saveError=false;
  bool savedDirectMode=directMode;
  if(request.startsWith("GET /reset")){
    ConnectionSettings cleared;
    cleared.emulatorHost=DEFAULT_EMULATOR_HOST;
    cleared.directMode=true;
    cleared.espNowChannel=DEFAULT_ESPNOW_CHANNEL;
    reset=storeConnectionSettings(preferences,cleared,settingsMutex);
    preferences.putBool("ui13done",true);
    savedDirectMode=true;
    saveError=!reset;
  }
  if(request.startsWith("GET /save?")){
    String mode=queryValue(request,"mode");
    String newSsid=queryValue(request,"ssid");
    String newPassword=queryValue(request,"password");
    String newHost=queryValue(request,"host");
    int newChannel=queryValue(request,"channel").toInt();
    newSsid.trim();
    newHost.trim();
    newHost.replace("http://","");
    newHost.replace("https://","");
    int slash=newHost.indexOf('/');
    if(slash>=0) newHost=newHost.substring(0,slash);
    bool requestedDirect=(mode=="direct");
    if(mode!="direct" && mode!="http") saveError=true;
    if(!requestedDirect && newSsid.isEmpty() && wifiSsid.isEmpty()) saveError=true;
    ConnectionSettings candidate=currentConnectionSettings(requestedDirect);
    if(newHost.length()) candidate.emulatorHost=newHost;
    if(newChannel>=1 && newChannel<=13) candidate.espNowChannel=(uint8_t)newChannel;
    if(newSsid.length()){
      candidate.ssid=newSsid;
      if(newPassword.length()) candidate.password=newPassword;
    }
    if(!requestedDirect && candidate.emulatorHost.isEmpty()) saveError=true;
    saved=!saveError && storeConnectionSettings(preferences,candidate,settingsMutex);
    savedDirectMode=requestedDirect;
    saveError=!saved;
    if(saved) preferences.putBool("ui13done",true);
  }

  String html="<!doctype html><meta name='viewport' content='width=device-width,initial-scale=1'>";
  html+="<style>body{font:17px Arial;max-width:440px;margin:20px auto;padding:20px;background:#071018;color:#fff}label{display:block;margin-top:14px;color:#b9c6d2}input,select,button{box-sizing:border-box;width:100%;padding:13px;margin:6px 0;font-size:17px;border-radius:8px}button{background:#52e000;border:0;font-weight:bold}.reset{background:#394550;color:#fff}.note{color:#9eb0bf;font-size:14px;line-height:1.4}.mac{color:#26d9ff}</style>";
  if(saved || reset){
    html+="<h2>Settings saved</h2><p>The display is starting ";
    html+=savedDirectMode?"direct ESP-NOW mode.":"network mode.";
    html+="</p><p class='note'>This setup network will close automatically in a few seconds.</p>";
  } else if(saveError){
    html+="<h2>Settings were not saved</h2><p>Check the connection mode and network name, then try again.</p><p><a href='/' style='color:#26d9ff'>Return to settings</a></p>";
  } else {
    html+="<h2>Battery Display setup</h2><form action='/save' method='get'>";
    html+="<label>Connection mode</label><select id='mode' name='mode' onchange='toggleWifi()'>";
    html+="<option value='direct'"+String(directMode?" selected":"")+">Direct ESP-NOW (no router)</option>";
    html+="<option value='http'"+String(!directMode?" selected":"")+">Home Wi-Fi + web data</option></select>";
    String ssidHint=wifiSsid.length()?"Saved network present - enter a new name to replace":"Enter network name";
    html+="<div id='wifi'><label>Home Wi-Fi name</label><input name='ssid' placeholder='"+ssidHint+"' maxlength='32'><label>Home Wi-Fi password</label><input name='password' type='password' placeholder='Blank keeps saved password' maxlength='64'></div>";
    html+="<label>Battery Emulator address</label><input name='host' value='"+emulatorHost+"' maxlength='64'>";
    html+="<label>ESP-NOW channel (1-13)</label><input name='channel' type='number' min='1' max='13' value='"+String(espNowChannel)+"'>";
    html+="<p class='note'>Display receiver MAC: <span class='mac'>"+WiFi.macAddress()+"</span><br>Enable ESPNow in Battery Emulator. Leave its receiver list empty for broadcast, or add this MAC.</p>";
    html+="<button>Save settings</button></form><form action='/reset'><button class='reset'>Clear saved network settings</button></form>";
    html+="<p class='note'>The saved Wi-Fi name is intentionally hidden. Firmware "+String(FW_VERSION)+"</p>";
    html+="<script>function toggleWifi(){document.getElementById('wifi').style.display=document.getElementById('mode').value==='http'?'block':'none'}toggleWifi()</script>";
  }
  client.print("HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nCache-Control: no-store\r\nConnection: close\r\nContent-Length: ");
  client.print(html.length());
  client.print("\r\n\r\n");
  client.print(html);
  client.flush();
  delay(120);
  client.stop();
  if(saved || reset){
    // Always reboot from the values that were just committed to NVS. Switching
    // directly from AP mode to STA/ESP-NOW left parts of the Wi-Fi stack in the
    // previous mode on this ESP32-S3, so the saved mode appeared to be ignored.
    configPortalExitAt=millis()+3500;
    settingsRestartAt=configPortalExitAt;
  }
}

void handleControlClient(){
  WiFiClient client=configServer.available();
  if(!client) return;
  client.setTimeout(250);
  String request=client.readStringUntil('\r');
  while(client.available()) client.read();

  bool saved=false;
  bool saveError=false;
  bool themeChanged=false;
  if(request.startsWith("GET /settings?")){
    int newPage=queryValue(request,"page").toInt();
    int newTheme=queryValue(request,"theme").toInt();
    bool newBadge=queryValue(request,"socbadge")=="1";
    if(newPage<0 || newPage>4 || newTheme<0 || newTheme>3){
      saveError=true;
    } else {
      themeChanged=colorTheme!=(uint8_t)newTheme;
      page=(uint8_t)newPage;
      colorTheme=(uint8_t)newTheme;
      showSocBadge=newBadge;
      saveSettings();
      bool verified=preferences.getUChar("page",255)==page &&
                    preferences.getUChar("theme",255)==colorTheme &&
                    preferences.getBool("soc-badge",!showSocBadge)==showSocBadge;
      saved=verified;
      saveError=!verified;
      if(saved){
        uiDirty=true;
        if(themeChanged) settingsRestartAt=millis()+2200;
      }
    }
  }

  String ip=WiFi.localIP().toString();
  String html="<!doctype html><html><head><meta name='viewport' content='width=device-width,initial-scale=1'><title>Battery Display</title>";
  html+="<style>:root{color-scheme:dark}body{font:17px Arial;max-width:520px;margin:20px auto;padding:18px;background:#071018;color:#fff}.card{background:#0c1822;border:1px solid #415363;border-radius:13px;padding:16px;margin:13px 0}.grid{display:grid;grid-template-columns:1fr 1fr;gap:9px}.metric{font-size:24px;color:#52e000}.muted{color:#9eb0bf;font-size:14px;line-height:1.45}label{display:block;margin-top:13px;color:#c3d0da}input,select,button{box-sizing:border-box;width:100%;padding:12px;margin:6px 0;font-size:17px;border-radius:8px}input[type=checkbox]{width:auto;transform:scale(1.35);margin-right:10px}button{background:#52e000;color:#071018;border:0;font-weight:bold}.ok{color:#52e000}.bad{color:#ff6969}a{color:#26d9ff}</style></head><body>";
  html+="<h1>Battery Display</h1><div class='card'><div class='grid'><div><span class='muted'>SOC</span><br><span class='metric'>";
  html+=isnan(t.soc)?"--":String(t.soc,0)+"%";
  html+="</span></div><div><span class='muted'>Connection</span><br><span class='metric'>ONLINE</span></div></div>";
  html+="<p class='muted'>IP "+ip+" &nbsp; Firmware "+String(FW_VERSION)+"<br>Battery Emulator "+emulatorHost+"</p></div>";
  if(saved) html+="<p class='ok'>Settings saved."+String(themeChanged?" Display will restart to apply the new color theme.":"")+"</p>";
  if(saveError) html+="<p class='bad'>Settings were not saved. Check all values and try again.</p>";
  html+="<form class='card' action='/settings' method='get'><h2>Display settings</h2>";
  html+="<label>Start screen</label><select name='page'>";
  const char* pages[]={"Dashboard","Battery capacity","Cell graph","System status","Event log"};
  for(int i=0;i<5;i++) html+="<option value='"+String(i)+"'"+String(page==i?" selected":"")+">"+String(pages[i])+"</option>";
  html+="</select><label>Color theme</label><select name='theme'>";
  const char* themes[]={"Classic blue","Pure dark","Amber","Green instrument"};
  for(int i=0;i<4;i++) html+="<option value='"+String(i)+"'"+String(colorTheme==i?" selected":"")+">"+String(themes[i])+"</option>";
  html+="</select><label><input type='checkbox' name='socbadge' value='1'"+String(showSocBadge?" checked":"")+">Show SOC badge on detail screens</label>";
  html+="<button>Save and apply</button></form><p class='muted'>This page is available only while the display is connected to home Wi-Fi. Direct ESP-NOW mode has no network address.</p></body></html>";

  client.print("HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nCache-Control: no-store\r\nConnection: close\r\nContent-Length: ");
  client.print(html.length());
  client.print("\r\n\r\n");
  client.print(html);
  delay(25);
  client.stop();
}

// ----------------- Helpers -----------------
String between(const String&s,const String&a,const String&b,int from=0){
  int p=s.indexOf(a,from);
  if(p<0) return "";
  p += a.length();
  int q=s.indexOf(b,p);
  if(q<0) return "";
  return s.substring(p,q);
}

float number(String s){
  s.trim();
  return s.length() ? s.toFloat() : NAN;
}

float livePowerW(){
  if(isnan(t.voltage) || isnan(t.current)) return NAN;
  return t.voltage * t.current;
}

String powerText(){
  float p=livePowerW();
  if(isnan(p)) return "--";
  if(fabs(p) < 0.5f) return "0 W";
  char value[24];
  // POWER is an absolute flow value in the card; direction and sign remain
  // explicit in CURRENT, the arrow, colour and CHARGING/DISCHARGING header.
  if(fabs(p) < 1000.0f) snprintf(value,sizeof(value),"%.0f W",fabs(p));
  else snprintf(value,sizeof(value),"%.1f kW",fabs(p)/1000.0f);
  return String(value);
}

String powerValueText(){
  float p=livePowerW();
  if(isnan(p)) return "--";
  if(fabs(p)<0.5f) return "0";
  char value[16];
  if(fabs(p)<1000.0f) snprintf(value,sizeof(value),"%+.0f",p);
  else snprintf(value,sizeof(value),"%+.1f",p/1000.0f);
  return String(value);
}

const char* powerUnitText(){
  float p=livePowerW();
  return !isnan(p) && fabs(p)>=1000.0f?"kW":"W";
}

bool telemetryPlausible();
bool telemetryPlausible(const Telemetry &v);

uint64_t tlvUnsigned(const uint8_t* value,uint16_t length){
  uint64_t out=0;
  uint8_t bytes=length<8?length:8;
  for(uint8_t i=0;i<bytes;i++) out|=((uint64_t)value[i])<<(8*i);
  return out;
}

int64_t tlvSigned(const uint8_t* value,uint16_t length){
  uint64_t raw=tlvUnsigned(value,length);
  if(length && length<8 && (value[length-1]&0x80)) raw|=(~0ULL)<<(length*8);
  return (int64_t)raw;
}

String tlvString(const uint8_t* value,uint16_t length){
  String out;
  out.reserve(length);
  for(uint16_t i=0;i<length;i++) out+=(char)value[i];
  return out;
}

const char* espEventSeverity(uint8_t level){
  switch(level){
    case 1: return "DEBUG";
    case 2: return "WARNING";
    case 3: return "UPDATE";
    case 4: return "ERROR";
    default: return "INFO";
  }
}

bool processEspNowSystem(const uint8_t* data,uint16_t len){
  bool valid=false;
  bool gotIp=false;
  uint8_t ip[4]={};
  uint16_t pos=12;
  while(pos+2<=len){
    uint8_t key=data[pos++];
    uint8_t tag=data[pos++];
    uint8_t lc=tag&0x1F;
    uint16_t n=0;
    if(lc<30) n=lc;
    else if(lc==30){ if(pos>=len) break; n=data[pos++]; }
    else { if(pos+2>len) break; n=data[pos]|(data[pos+1]<<8); pos+=2; }
    if(pos+n>len) break;
    const uint8_t* value=&data[pos];
    if(key==0x03 && n==6) memcpy(espNowSourceMac,value,6);
    else if(key==0x04 && n){
      uint8_t status=(uint8_t)tlvUnsigned(value,n);
      if(status==4){ t.systemStatus="FAULT"; espNowSystemOkay=false; }
      else if(status==5){ t.systemStatus="UPDATING"; espNowSystemOkay=false; }
      else { t.systemStatus="OK"; }
      valid=true;
    } else if(key==0x06 && n){
      if(tlvUnsigned(value,n)>=4){ t.systemStatus="ERROR"; espNowSystemOkay=false; }
    } else if(key==0x07 && n){
      uint8_t status=(uint8_t)tlvUnsigned(value,n);
      espNowSystemOkay=(status==0);
      t.systemStatus=status==0?"OK":(status==1?"WARNING":(status==3?"UPDATING":"ERROR"));
      valid=true;
    } else if(key==0x0A && n){
      espNowBatteryCount=constrain((int)tlvUnsigned(value,n),1,3);
    } else if(key==0x0C && n){
      espNowInverterAlive=tlvUnsigned(value,n)>0;
    } else if(key==0x10 && n==4){
      memcpy(ip,value,4);
      gotIp=true;
    }
    pos+=n;
  }
  t.emuCont=espNowSystemOkay;
  t.invCont=espNowInverterAlive;
  t.powerStatus=(espNowSystemOkay && espNowInverterAlive)?"OK":"CHECK";
  if(gotIp && !directMode){
    String discovered=String(ip[0])+"."+String(ip[1])+"."+String(ip[2])+"."+String(ip[3]);
    if(discovered!="0.0.0.0" && discovered!=emulatorHost){
      emulatorHost=discovered;
      preferences.putString("emu-host",emulatorHost);
    }
  }
  return valid;
}

bool processEspNowBattery(const uint8_t* data,uint16_t len,uint8_t batteryNumber){
  if(batteryNumber<1 || batteryNumber>3) return false;
  Telemetry &bt=batteryData[batteryNumber-1];
  bool valid=false;
  uint16_t pos=12;
  while(pos+2<=len){
    uint8_t key=data[pos++];
    uint8_t tag=data[pos++];
    uint8_t lc=tag&0x1F;
    uint16_t n=0;
    if(lc<30) n=lc;
    else if(lc==30){ if(pos>=len) break; n=data[pos++]; }
    else { if(pos+2>len) break; n=data[pos]|(data[pos+1]<<8); pos+=2; }
    if(pos+n>len) break;
    const uint8_t* value=&data[pos];
    uint64_t u=tlvUnsigned(value,n);
    int64_t s=tlvSigned(value,n);
    switch(key){
      case 0x32: bt.totalReal=u/1000.0f; break;
      case 0x33: bt.totalScaled=u/1000.0f; break;
      case 0x50: bt.soc=u/100.0f; valid=true; break;
      case 0x51: bt.realSoc=u/100.0f; break;
      case 0x52: bt.soh=u/100.0f; break;
      case 0x53: bt.voltage=u/10.0f; valid=true; break;
      case 0x54: bt.current=s/10.0f; valid=true; break;
      case 0x57: bt.remainReal=u/1000.0f; break;
      case 0x58: bt.remainScaled=u/1000.0f; break;
      case 0x59: bt.maxChgKw=u/1000.0f; break;
      case 0x5A: bt.maxDisKw=u/1000.0f; break;
      case 0x5B: bt.maxChgA=u/10.0f; break;
      case 0x5C: bt.maxDisA=u/10.0f; break;
      case 0x5F: bt.cMax=(uint16_t)u; break;
      case 0x60: bt.cMin=(uint16_t)u; break;
      case 0x61: bt.tMax=s/10.0f; break;
      case 0x62: bt.tMin=s/10.0f; break;
      case 0x6B: espNowBatteryAlive=u>0; break;
      case 0x6E: espNowBatteryAlive=u!=0; break;
      default: break;
    }
    pos+=n;
  }
  if(bt.cMax>=bt.cMin) bt.delta=bt.cMax-bt.cMin;
  bt.emuCont=espNowSystemOkay;
  bt.invCont=espNowInverterAlive;
  if(valid && telemetryPlausible(bt)){
    batteryLastGood[batteryNumber-1]=millis();
    if(batteryNumber==1) lastGood=batteryLastGood[0];
    consecutiveFetchFailures=0;
    pulse=!pulse;
    return true;
  }
  return false;
}

bool processEspNowCells(const uint8_t* data,uint16_t len,uint8_t batteryNumber){
  if(batteryNumber<1 || batteryNumber>3) return false;
  CellMonitorData &cellset=batteryCells[batteryNumber-1];
  uint16_t count=96,index=0;
  const uint8_t* voltages=nullptr;
  const uint8_t* balancing=nullptr;
  uint16_t voltageBytes=0,balanceBytes=0;
  uint16_t pos=12;
  while(pos+2<=len){
    uint8_t key=data[pos++];
    uint8_t tag=data[pos++];
    uint8_t lc=tag&0x1F;
    uint16_t n=0;
    if(lc<30) n=lc;
    else if(lc==30){ if(pos>=len) break; n=data[pos++]; }
    else { if(pos+2>len) break; n=data[pos]|(data[pos+1]<<8); pos+=2; }
    if(pos+n>len) break;
    if(key==0x90) count=(uint16_t)tlvUnsigned(&data[pos],n);
    else if(key==0x91) index=(uint16_t)tlvUnsigned(&data[pos],n);
    else if(key==0x92){ voltages=&data[pos]; voltageBytes=n; }
    else if(key==0x93){ balancing=&data[pos]; balanceBytes=n; }
    pos+=n;
  }
  if(!voltages || voltageBytes<2 || index>=96) return false;
  uint16_t available=96-index;
  if(count>index && count-index<available) available=count-index;
  uint16_t chunk=voltageBytes/2;
  if(chunk>available) chunk=available;
  for(uint16_t i=0;i<chunk;i++){
    cellset.mv[index+i]=voltages[i*2]|(voltages[i*2+1]<<8);
    if(balancing && i/8<balanceBytes) cellset.balancing[index+i]=(balancing[i/8]&(1<<(i%8)))!=0;
  }
  uint16_t usable=count<96?count:96;
  if(!usable) return false;
  uint16_t lo=65535,hi=0;
  uint8_t loCell=0,hiCell=0;
  for(uint16_t i=0;i<usable;i++){
    uint16_t mv=cellset.mv[i];
    if(mv<2000 || mv>5000) continue;
    if(mv<lo){ lo=mv; loCell=i; }
    if(mv>hi){ hi=mv; hiCell=i; }
  }
  if(lo==65535 || hi==0) return false;
  cellset.minMv=lo; cellset.maxMv=hi; cellset.deltaMv=hi-lo;
  cellset.minCell=loCell; cellset.maxCell=hiCell;
  cellset.valid=true; cellset.lastGood=millis();
  return true;
}

bool processEspNowEvent(const uint8_t* data,uint16_t len){
  String name,message;
  uint8_t severity=0,index=0,total=0,count=0;
  uint16_t pos=12;
  while(pos+2<=len){
    uint8_t key=data[pos++];
    uint8_t tag=data[pos++];
    uint8_t lc=tag&0x1F;
    uint16_t n=0;
    if(lc<30) n=lc;
    else if(lc==30){ if(pos>=len) break; n=data[pos++]; }
    else { if(pos+2>len) break; n=data[pos]|(data[pos+1]<<8); pos+=2; }
    if(pos+n>len) break;
    if(key==0xA1) name=tlvString(&data[pos],n);
    else if(key==0xA2) severity=(uint8_t)tlvUnsigned(&data[pos],n);
    else if(key==0xA4) count=(uint8_t)tlvUnsigned(&data[pos],n);
    else if(key==0xA7) message=tlvString(&data[pos],n);
    else if(key==0xA8) index=(uint8_t)tlvUnsigned(&data[pos],n);
    else if(key==0xA9) total=(uint8_t)tlvUnsigned(&data[pos],n);
    pos+=n;
  }
  if(index>=10 || (!name.length() && !message.length())) return false;
  if(index==0){ evCount=0; for(auto &item:ev) item=EventItem(); }
  if(name.startsWith("EVENT_")) name.remove(0,6);
  ev[index].type=name;
  ev[index].severity=espEventSeverity(severity);
  ev[index].message=message;
  ev[index].count=count;
  evCount=max(evCount,(int)index+1);
  if(total) evCount=min(evCount,min((int)total,10));
  eventsFresh=true;
  lastEventsFetch=millis();
  return true;
}

bool processEspNowPacket(const EspNowPacket& packet){
  if(packet.len<12 || packet.data[0]!=0x42 || packet.data[1]!=0x45 || packet.data[2]!=2) return false;
  uint8_t batteryNumber=packet.data[6];
  if(batteryNumber>3) return false;
  memcpy(espNowSourceMac,packet.source,6);
  bool ok=false;
  xSemaphoreTake(dataMutex,portMAX_DELAY);
  switch(packet.data[3]){
    case 0x01: ok=processEspNowSystem(packet.data,packet.len); break;
    case 0x02: ok=processEspNowBattery(packet.data,packet.len,batteryNumber?batteryNumber:1); break;
    case 0x03: ok=processEspNowCells(packet.data,packet.len,batteryNumber?batteryNumber:1); break;
    case 0x04: ok=processEspNowEvent(packet.data,packet.len); break;
    default: break;
  }
  xSemaphoreGive(dataMutex);
  if(ok){
    espNowPackets++;
    lastEspNowGood=millis();
    if(directMode && !espNowChannelLocked){
      espNowChannelLocked=true;
      espNowChannel=(uint8_t)WiFi.channel();
      preferences.putUChar("esp-channel",espNowChannel);
    }
    uiDirty=true;
  }
  return ok;
}

void drainEspNowQueue(){
  EspNowPacket packet;
  while(espNowQueue && xQueueReceive(espNowQueue,&packet,0)==pdTRUE) processEspNowPacket(packet);
}

bool parsePage(const String&h){
  String soc=between(h,"Scaled SOC: ","</h4>");
  String soh=between(h,"SOH: ","</h4>");
  String vi=between(h,"Voltage: ","</h4>");
  if(!soc.length() || !vi.length()) return false;

  int rp=soc.indexOf(" (real: ");
  String a=rp>=0 ? soc.substring(0,rp) : soc;
  String b=rp>=0 ? soc.substring(rp+8) : "";
  a.replace("&percnt;","");
  b.replace("&percnt;)","");
  t.soc=number(a);
  t.realSoc=number(b);

  soh.replace("&percnt;","");
  t.soh=number(soh);

  int cp=vi.indexOf(" V &nbsp; Current: ");
  if(cp>=0){
    t.voltage=number(vi.substring(0,cp));
    String amp=vi.substring(cp+19);
    amp.replace(" A","");
    t.current=number(amp);
  }

  auto val=[&](const char*key,const char*unit)->float{
    String x=between(h,key,"</h4>");
    x.replace(unit,"");
    return number(x);
  };

  t.maxDisKw=val("Max discharge power: "," kW");
  t.maxChgKw=val("Max charge power: "," kW");

  String x=between(h,"Max discharge current: ","</h4>");
  int p=x.indexOf(" A");
  if(p>=0) x=x.substring(0,p);
  t.maxDisA=number(x);

  x=between(h,"Max charge current: ","</h4>");
  p=x.indexOf(" A");
  if(p>=0) x=x.substring(0,p);
  t.maxChgA=number(x);

  String cap=between(h,"Scaled total capacity: ","</h4>");
  int rr=cap.indexOf(" kWh (real: ");
  if(rr>=0){
    t.totalScaled=number(cap.substring(0,rr));
    String z=cap.substring(rr+12);
    z.replace(" kWh)","");
    t.totalReal=number(z);
  }

  cap=between(h,"Scaled remaining capacity: ","</h4>");
  rr=cap.indexOf(" kWh (real: ");
  if(rr>=0){
    t.remainScaled=number(cap.substring(0,rr));
    String z=cap.substring(rr+12);
    z.replace(" kWh)","");
    t.remainReal=number(z);
  }

  String cells=between(h,"Cell min/max: ","</h4>");
  int sl=cells.indexOf(" mV / ");
  if(sl>=0){
    t.cMin=(uint16_t)cells.substring(0,sl).toInt();
    String z=cells.substring(sl+6);
    z.replace(" mV","");
    t.cMax=(uint16_t)z.toInt();
  }

  String de=between(h,"Cell delta: ","</h4>");
  de.replace(" mV","");
  t.delta=(uint16_t)de.toInt();

  String temp=between(h,"Temperature min/max: ","</h4>");
  int ts=temp.indexOf(" &deg;C / ");
  if(ts>=0){
    t.tMin=number(temp.substring(0,ts));
    // Correct separator length = 10 characters.
    String z=temp.substring(ts+10);
    z.replace(" &deg;C","");
    t.tMax=number(z);
  }

  t.systemStatus=between(h,"System status: ","</h4>");
  t.powerStatus=between(h,"Power status: ","</h4>");
  t.systemStatus.trim();
  t.powerStatus.trim();
  t.emuCont=h.indexOf("Emulator allows contactor closing: <span>&#10003;</span>")>=0;
  t.invCont=h.indexOf("Inverter allows contactor closing: <span>&#10003;</span>")>=0;
  t.contactorWarning=h.indexOf("Contactors not fully controlled via emulator")>=0;

  return true;
}

bool telemetryPlausible(const Telemetry &v){
  if(isnan(v.soc) || v.soc<0 || v.soc>100) return false;
  if(isnan(v.voltage) || v.voltage<50 || v.voltage>1000) return false;
  if(isnan(v.current) || fabs(v.current)>2000) return false;
  if(v.cMin<2000 || v.cMin>5000 || v.cMax<2000 || v.cMax>5000 || v.cMax<v.cMin) return false;
  if(!isnan(v.tMin) && (v.tMin<-50 || v.tMin>150)) return false;
  if(!isnan(v.tMax) && (v.tMax<-50 || v.tMax>150)) return false;
  return true;
}
bool telemetryPlausible(){ return telemetryPlausible(t); }

bool fetchBattery(){
  lastFetch=millis();
  if(WiFi.status()!=WL_CONNECTED){
    httpCode=-1;
    consecutiveFetchFailures++;
    httpFailuresTotal++;
    return false;
  }

  HTTPClient h;
  h.setConnectTimeout(900);
  h.setTimeout(1800);
  h.begin(emulatorUrl("/"));
  httpCode=h.GET();

  if(httpCode!=200){
    h.end();
    consecutiveFetchFailures++;
    httpFailuresTotal++;
    return false;
  }

  String html=h.getString();
  h.end();

  xSemaphoreTake(dataMutex,portMAX_DELAY);
  Telemetry previous=t;
  bool parsed=parsePage(html) && telemetryPlausible();
  if(!parsed) t=previous;
  xSemaphoreGive(dataMutex);
  if(!parsed){
    consecutiveFetchFailures++;
    parseFailuresTotal++;
    return false;
  }

  lastGood=millis();
  consecutiveFetchFailures=0;
  pulse=!pulse;
  return true;
}

bool parseNumberArray(const String& html,const char* marker,uint16_t *out,int expected){
  int p=html.indexOf(marker);
  if(p<0) return false;
  p=html.indexOf('[',p);
  int end=html.indexOf(']',p);
  if(p<0 || end<0) return false;
  for(int i=0;i<expected;i++){
    while(p<end && !(html[p]>='0' && html[p]<='9')) p++;
    if(p>=end) return false;
    out[i]=(uint16_t)html.substring(p,end).toInt();
    while(p<end && html[p]>='0' && html[p]<='9') p++;
  }
  return true;
}

bool parseBoolArray(const String& html,const char* marker,bool *out,int expected){
  int p=html.indexOf(marker);
  if(p<0) return false;
  p=html.indexOf('[',p);
  int end=html.indexOf(']',p);
  if(p<0 || end<0) return false;
  for(int i=0;i<expected;i++){
    int tpos=html.indexOf("true",p);
    int fpos=html.indexOf("false",p);
    if(tpos<0 || tpos>=end) tpos=INT_MAX;
    if(fpos<0 || fpos>=end) fpos=INT_MAX;
    if(tpos==INT_MAX && fpos==INT_MAX) return false;
    out[i]=tpos<fpos;
    p=(out[i]?tpos+4:fpos+5);
  }
  return true;
}

bool fetchCells(){
  if(WiFi.status()!=WL_CONNECTED) return false;
  HTTPClient h;
  h.setConnectTimeout(900);
  h.setTimeout(1800);
  h.begin(emulatorUrl("/cellmonitor"));
  int code=h.GET();
  if(code!=200){ h.end(); return false; }
  String html=h.getString();
  h.end();

  uint16_t values[96];
  bool balancing[96];
  if(!parseNumberArray(html,"const data",values,96) ||
     !parseBoolArray(html,"const balancing",balancing,96)){
    parseFailuresTotal++;
    return false;
  }

  uint16_t lo=values[0], hi=values[0];
  uint8_t loCell=0, hiCell=0;
  if(values[0]<2000 || values[0]>5000){ parseFailuresTotal++; return false; }
  for(int i=1;i<96;i++){
    if(values[i]<2000 || values[i]>5000){ parseFailuresTotal++; return false; }
    if(values[i]<lo){ lo=values[i]; loCell=i; }
    if(values[i]>hi){ hi=values[i]; hiCell=i; }
  }
  xSemaphoreTake(dataMutex,portMAX_DELAY);
  memcpy(cells.mv,values,sizeof(values));
  memcpy(cells.balancing,balancing,sizeof(balancing));
  cells.minMv=lo; cells.maxMv=hi; cells.deltaMv=hi-lo;
  cells.minCell=loCell; cells.maxCell=hiCell;
  cells.valid=true; cells.lastGood=millis();
  xSemaphoreGive(dataMutex);
  return true;
}

int eventSeverityRank();
enum Health { H_OK, H_WARN, H_OFFLINE };

Health health(){
  if(directMode && (!lastGood || millis()-lastGood>OFFLINE_AGE_MS)) return H_OFFLINE;
  if(consecutiveFetchFailures>=4 || (lastGood && millis()-lastGood>OFFLINE_AGE_MS)) return H_OFFLINE;
  if(t.systemStatus!="OK" || !t.emuCont || !t.invCont ||
     (eventsFresh && eventSeverityRank()>0)) return H_WARN;
  return H_OK;
}


bool parseEventsPage(const String&h){
  evCount=0;
  int pos=0;
  while(evCount<10){
    int st=h.indexOf("<div class='event'",pos);
    if(st<0) break;
    int en=h.indexOf("</div></div>",st);
    if(en<0) break;

    String block=h.substring(st,en+12);

    // Skip header row if somehow matched
    if(block.indexOf("Event Type")>=0){ pos=en+12; continue; }

    String vals[6];
    // Start after the outer event container. Otherwise the first extracted
    // value includes the child opening tag (for example "<div>WIFI_CONNECT").
    int bp=block.indexOf('>')+1;
    int idx=0;
    while(idx<6){
      int d1=block.indexOf("<div",bp);
      if(d1<0) break;
      int gt=block.indexOf(">",d1);
      if(gt<0) break;
      int d2=block.indexOf("</div>",gt);
      if(d2<0) break;
      vals[idx++]=block.substring(gt+1,d2);
      bp=d2+6;
    }

    if(idx>=6){
      ev[evCount].type=vals[0];
      ev[evCount].severity=vals[1];
      ev[evCount].count=vals[3].toInt();
      ev[evCount].message=vals[5];

      // Strip any nested markup if present
      while(ev[evCount].message.indexOf("<")>=0){
        int a=ev[evCount].message.indexOf("<");
        int b=ev[evCount].message.indexOf(">",a);
        if(b<0) break;
        ev[evCount].message.remove(a,b-a+1);
      }
      evCount++;
    }
    pos=en+12;
  }
  return evCount>0;
}

bool fetchEvents(){
  if(WiFi.status()!=WL_CONNECTED) return false;
  HTTPClient h;
  h.setConnectTimeout(800);
  h.setTimeout(1400);
  h.begin(emulatorUrl("/events"));
  int code=h.GET();
  if(code!=200){ h.end(); eventsFresh=false; return false; }
  String html=h.getString();
  h.end();
  xSemaphoreTake(dataMutex,portMAX_DELAY);
  parseEventsPage(html);
  eventsFresh=true;
  lastEventsFetch=millis();
  xSemaphoreGive(dataMutex);
  return true;
}

static void fillKnownTeslaDtc(DtcItem &item,int code){
  item.code=code;
  switch(code){
    case 100: item.ecu="BMS"; item.frame="0x320"; item.title="Pack Config Mismatch"; item.shortName="BMS_a001_Pack_Config_Mismatch"; break;
    case 111: item.ecu="BMS"; item.frame="0x320"; item.title="Isolation"; item.shortName="BMS_a035_SW_Isolation"; break;
    case 128: item.ecu="BMS"; item.frame="0x320"; item.title="HV Chain Model Fault"; item.shortName="BMS_a055_SW_HvChain_Model_Fault"; break;
    case 148: item.ecu="BMS"; item.frame="0x320"; item.title="Charge Port MIA"; item.shortName="BMS_a091_SW_ChargePort_MIA"; break;
    case 149: item.ecu="BMS"; item.frame="0x320"; item.title="Charge Port MIA On HVS"; item.shortName="BMS_a092_SW_ChargePort_Mia_On_Hvs"; break;
    case 192: item.ecu="BMS"; item.frame="0x320"; item.title="Limp Mode"; item.shortName="BMS_a170_SW_Limp_Mode"; break;
    case 222: item.ecu="PCS"; item.frame="0x3A4"; item.title="CP MIA"; item.shortName="PCS_a023_cpMia"; break;
    case 223: item.ecu="PCS"; item.frame="0x3A4"; item.title="VCFRONT MIA"; item.shortName="PCS_a024_vcfrontMia"; break;
    default: item.ecu="DTC"; item.frame=""; item.title="Unknown diagnostic code"; item.shortName="CODE "+String(code); break;
  }
}

bool parseAdvancedPage(const String&h){
  String value=between(h,"HVIL Status: ","</h4>");
  if(value.length()){ value.trim(); t.hvilStatus=value; }
  value=between(h,"BMS State: ","</h4>");
  if(value.length()){ value.trim(); t.bmsState=value; }
  value=between(h,"HVP Contactor State: ","</h4>");
  if(value.length()){ value.trim(); t.hvpContactorState=value; }
  value=between(h,"BMS Contactor State: ","</h4>");
  if(value.length()){ value.trim(); t.bmsContactorState=value; }
  value=between(h,"Isolation Resistance: ","</h4>");
  if(value.length()){
    value.replace(" kOhms","");
    t.isolationKOhm=number(value);
  }

  dtcCount=0;
  int pos=0;
  while(dtcCount<MAX_DISPLAY_DTCS){
    int attr=h.indexOf("data-dtc-code='",pos);
    if(attr<0) break;
    int start=attr+15;
    int end=h.indexOf('\'',start);
    if(end<0) break;
    int code=h.substring(start,end).toInt();
    int rowStart=h.lastIndexOf("<tr",attr);
    int rowEnd=h.indexOf("</tr>",attr);
    String row=(rowStart>=0 && rowEnd>rowStart)?h.substring(rowStart,rowEnd):String();
    DtcItem item;
    fillKnownTeslaDtc(item,code);
    item.status=row.indexOf(">Active<")>=0?"ACTIVE":(row.indexOf(">Confirmed<")>=0?"CONFIRMED":"STORED");
    dtcItems[dtcCount++]=item;
    pos=end+1;
  }
  dtcFresh=true;
  lastAdvancedFetch=millis();
  return t.hvilStatus!="--" || t.bmsState!="--" || dtcCount>0;
}

bool fetchAdvanced(){
  if(WiFi.status()!=WL_CONNECTED) return false;
  HTTPClient h;
  h.setConnectTimeout(1000);
  h.setTimeout(2600);
  h.begin(emulatorUrl("/advanced"));
  int code=h.GET();
  if(code!=200){ h.end(); dtcFresh=false; return false; }
  String html=h.getString();
  h.end();
  xSemaphoreTake(dataMutex,portMAX_DELAY);
  bool parsed=parseAdvancedPage(html);
  xSemaphoreGive(dataMutex);
  return parsed;
}

int eventSeverityRank(){
  int rank=0;
  for(int i=0;i<evCount;i++){
    String s=ev[i].severity;
    s.toUpperCase();
    if(s.indexOf("ERROR")>=0 || s.indexOf("CRITICAL")>=0 || s.indexOf("FATAL")>=0) return 2;
    if(s.indexOf("WARN")>=0) rank=max(rank,1);
  }
  return rank;
}

int firstAlertEventIndex(){
  for(int i=0;i<evCount;i++){
    String s=ev[i].severity;
    s.toUpperCase();
    if(s.indexOf("WARN")>=0 || s.indexOf("ERROR")>=0 ||
       s.indexOf("CRITICAL")>=0 || s.indexOf("FATAL")>=0) return i;
  }
  return -1;
}

int firstWarningEventIndex(){
  for(int i=0;i<evCount;i++){
    String s=ev[i].severity;
    s.toUpperCase();
    if(s.indexOf("WARN")>=0) return i;
  }
  return -1;
}

String fullEventText(const EventItem &e){
  String type=e.type;
  type.trim();
  if(type=="WIFI_CONNECT") return "Wi-Fi        CONNECTED";
  if(type=="CAN_BATTERY_DETECTED") return "Battery CAN  CONNECTED";
  if(type=="CAN_INVERTER_DETECTED") return "Inverter CAN CONNECTED";
  if(type=="RESET_SW") return "Last reset   NORMAL";

  String out=e.message.length()?e.message:type;
  out.trim();
  return out;
}

String shortEventText(const EventItem &e){
  String out=fullEventText(e);
  if(out.length()>31) out=out.substring(0,31);
  return out;
}

#include "ui480.inc"


// ----------------- Display flush -----------------
void lvFlush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p){
  uint16_t w=area->x2-area->x1+1;
  uint16_t h=area->y2-area->y1+1;
  gfx->draw16bitRGBBitmap(area->x1, area->y1, (uint16_t*)color_p, w, h);
  lv_disp_flush_ready(disp);
}

// ----------------- UI helpers -----------------
lv_obj_t* makeCard(lv_obj_t* parent,int x,int y,int w,int h,const char* title){
  lv_obj_t* card=lv_obj_create(parent);
  lv_obj_set_pos(card,uiX(x),uiY(y));
  lv_obj_set_size(card,uiX(w),uiY(h));
  lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_set_style_bg_color(card,COL_CARD,0);
  lv_obj_set_style_bg_opa(card,LV_OPA_COVER,0);
  lv_obj_set_style_border_color(card,COL_BORDER,0);
  lv_obj_set_style_border_width(card,1,0);
  lv_obj_set_style_radius(card,8,0);
  lv_obj_set_style_pad_all(card,0,0);
  lv_obj_set_style_shadow_width(card,0,0);

  lv_obj_t* label=lv_label_create(card);
  lv_label_set_text(label,title);
  lv_obj_set_style_text_color(label,COL_MUTED,0);
  lv_obj_set_style_text_font(label,&lv_font_montserrat_12,0);
  lv_obj_align(label,LV_ALIGN_TOP_LEFT,uiX(7),uiY(5));
  return card;
}

lv_obj_t* makeValue(lv_obj_t* parent,const lv_font_t* font,lv_color_t color,int y=23){
  lv_obj_t* l=lv_label_create(parent);
  lv_label_set_text(l,"--");
  lv_obj_set_style_text_color(l,color,0);
  lv_obj_set_style_text_font(l,font,0);
  lv_obj_set_width(l,lv_pct(100));
  lv_obj_set_style_text_align(l,LV_TEXT_ALIGN_CENTER,0);
  lv_obj_align(l,LV_ALIGN_TOP_MID,0,uiY(y));
  return l;
}

void buildMainPage(){
  page1=lv_obj_create(lv_scr_act());
  lv_obj_set_size(page1,SCREEN_W,SCREEN_H);
  lv_obj_set_pos(page1,0,0);
  lv_obj_clear_flag(page1,LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(page1,COL_BG,0);
  lv_obj_set_style_bg_opa(page1,LV_OPA_COVER,0);
  lv_obj_set_style_border_width(page1,0,0);
  lv_obj_set_style_pad_all(page1,0,0);

  lv_obj_t* c1=makeCard(page1,2,2,103,80,"SOC");
  lv_obj_t* c2=makeCard(page1,108,2,103,80,"POWER");
  lv_obj_t* c3=makeCard(page1,214,2,104,80,"CURRENT");
  lv_obj_t* c4=makeCard(page1,2,86,103,82,"VOLTAGE");
  lv_obj_t* c5=makeCard(page1,108,86,103,82,"CELL DELTA");
  lv_obj_t* c6=makeCard(page1,214,86,104,82,"TEMP");

  vSoc=makeValue(c1,&lv_font_montserrat_32,COL_GREEN,23);
  vPower=makeValue(c2,&lv_font_montserrat_22,COL_YELLOW,28);
  vCurrent=makeValue(c3,&lv_font_montserrat_28,COL_CYAN,23);
  vVoltage=makeValue(c4,&lv_font_montserrat_24,COL_TEXT,27);
  vDelta=makeValue(c5,&lv_font_montserrat_28,COL_YELLOW,22);

  // Temperature two smooth rows
  vTempMin=lv_label_create(c6);
  lv_obj_set_style_text_color(vTempMin,COL_CYAN,0);
  lv_obj_set_style_text_font(vTempMin,&lv_font_montserrat_16,0);
  lv_obj_align(vTempMin,LV_ALIGN_TOP_LEFT,9,27);

  vTempMax=lv_label_create(c6);
  lv_obj_set_style_text_color(vTempMax,COL_CYAN,0);
  lv_obj_set_style_text_font(vTempMax,&lv_font_montserrat_16,0);
  lv_obj_align(vTempMax,LV_ALIGN_TOP_LEFT,9,51);
}

void buildSecondPage(){
  page2=lv_obj_create(lv_scr_act());
  lv_obj_set_size(page2,SCREEN_W,SCREEN_H);
  lv_obj_set_pos(page2,0,0);
  lv_obj_clear_flag(page2,LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(page2,COL_BG,0);
  lv_obj_set_style_bg_opa(page2,LV_OPA_COVER,0);
  lv_obj_set_style_border_width(page2,0,0);
  lv_obj_set_style_pad_all(page2,0,0);

  lv_obj_t* a=makeCard(page2,2,2,156,80,"CELL MIN");
  lv_obj_t* b=makeCard(page2,162,2,156,80,"CELL MAX");
  lv_obj_t* c=makeCard(page2,2,86,156,82,"REMAINING");
  lv_obj_t* d=makeCard(page2,162,86,156,82,"TOTAL");

  vCellMin=makeValue(a,&lv_font_montserrat_28,COL_GREEN,24);
  vCellMax=makeValue(b,&lv_font_montserrat_28,COL_RED,24);
  vRemain=makeValue(c,&lv_font_montserrat_28,COL_CYAN,24);
  vTotal=makeValue(d,&lv_font_montserrat_28,COL_CYAN,24);

  // Smooth footer text inside lower cards
  vSoh=lv_label_create(c);
  lv_obj_set_style_text_color(vSoh,COL_GREEN,0);
  lv_obj_set_style_text_font(vSoh,&lv_font_montserrat_14,0);
  lv_obj_align(vSoh,LV_ALIGN_BOTTOM_LEFT,7,-5);

  vChg=lv_label_create(d);
  lv_obj_set_style_text_color(vChg,COL_TEXT,0);
  lv_obj_set_style_text_font(vChg,&lv_font_montserrat_14,0);
  lv_obj_align(vChg,LV_ALIGN_BOTTOM_LEFT,7,-5);

  vDis=lv_label_create(d);
  lv_obj_set_style_text_color(vDis,COL_TEXT,0);
  lv_obj_set_style_text_font(vDis,&lv_font_montserrat_14,0);
  lv_obj_align(vDis,LV_ALIGN_BOTTOM_RIGHT,-7,-5);

  socBadge2=lv_label_create(page2);
  lv_obj_set_style_text_color(socBadge2,COL_GREEN,0);
  lv_obj_set_style_text_font(socBadge2,&lv_font_montserrat_12,0);
  lv_obj_set_width(socBadge2,48);
  lv_obj_set_style_text_align(socBadge2,LV_TEXT_ALIGN_RIGHT,0);
  lv_obj_set_pos(socBadge2,105,7);
}


void buildCellPage(){
  page3=lv_obj_create(lv_scr_act());
  lv_obj_set_size(page3,SCREEN_W,SCREEN_H);
  lv_obj_set_pos(page3,0,0);
  lv_obj_clear_flag(page3,LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(page3,COL_BG,0);
  lv_obj_set_style_bg_opa(page3,LV_OPA_COVER,0);
  lv_obj_set_style_border_width(page3,0,0);
  lv_obj_set_style_pad_all(page3,0,0);

  cellMinLabel=lv_label_create(page3);
  lv_obj_set_style_text_font(cellMinLabel,&lv_font_montserrat_16,0);
  lv_label_set_recolor(cellMinLabel,true);
  lv_obj_set_pos(cellMinLabel,5,3);

  cellDeltaLabel=lv_label_create(page3);
  lv_obj_set_width(cellDeltaLabel,100);
  lv_obj_set_style_text_font(cellDeltaLabel,&lv_font_montserrat_28,0);
  lv_obj_set_style_text_color(cellDeltaLabel,COL_YELLOW,0);
  lv_obj_set_style_text_align(cellDeltaLabel,LV_TEXT_ALIGN_LEFT,0);
  lv_obj_set_pos(cellDeltaLabel,216,5);

  // Draw a real delta glyph so it stays crisp even though the built-in
  // Montserrat font does not contain the Greek capital delta character.
  static lv_point_t deltaOutline[]={{0,31},{13,1},{26,31},{0,31}};
  lv_obj_t* deltaMark=lv_line_create(page3);
  lv_line_set_points(deltaMark,deltaOutline,4);
  lv_obj_set_style_line_color(deltaMark,COL_YELLOW,0);
  lv_obj_set_style_line_width(deltaMark,2,0);
  lv_obj_set_style_line_rounded(deltaMark,true,0);
  lv_obj_set_pos(deltaMark,184,5);
  static lv_point_t deltaBar[]={{4,23},{22,23}};
  lv_obj_t* deltaCrossbar=lv_line_create(page3);
  lv_line_set_points(deltaCrossbar,deltaBar,2);
  lv_obj_set_style_line_color(deltaCrossbar,COL_YELLOW,0);
  lv_obj_set_style_line_width(deltaCrossbar,2,0);
  lv_obj_set_style_line_rounded(deltaCrossbar,true,0);
  lv_obj_set_pos(deltaCrossbar,184,5);

  cellMaxLabel=lv_label_create(page3);
  lv_obj_set_width(cellMaxLabel,80);
  lv_obj_set_style_text_font(cellMaxLabel,&lv_font_montserrat_16,0);
  lv_obj_set_style_text_color(cellMaxLabel,COL_CYAN,0);
  lv_obj_set_style_text_align(cellMaxLabel,LV_TEXT_ALIGN_LEFT,0);
  lv_obj_set_pos(cellMaxLabel,108,3);

  // 96 bars fit as 2 px columns with a 1 px gap. LVGL's rounded
  // rectangles retain anti-aliased tops while keeping the plot compact.
  for(int i=0;i<96;i++){
    cellBars[i]=lv_obj_create(page3);
    lv_obj_set_size(cellBars[i],2,4);
    lv_obj_set_pos(cellBars[i],16+i*3,165-4);
    lv_obj_clear_flag(cellBars[i],LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(cellBars[i],0,0);
    lv_obj_set_style_border_width(cellBars[i],0,0);
    lv_obj_set_style_radius(cellBars[i],1,0);
    lv_obj_set_style_bg_opa(cellBars[i],LV_OPA_COVER,0);
    lv_obj_set_style_bg_color(cellBars[i],lv_color_hex(0x46677E),0);
  }

  socBadge3=lv_label_create(page3);
  lv_obj_set_style_text_color(socBadge3,COL_GREEN,0);
  lv_obj_set_style_text_font(socBadge3,&lv_font_montserrat_12,0);
  lv_obj_set_width(socBadge3,46);
  lv_obj_set_style_text_align(socBadge3,LV_TEXT_ALIGN_RIGHT,0);
  lv_obj_set_pos(socBadge3,269,47);
}

void buildEventsPage(){
  page4=lv_obj_create(lv_scr_act());
  lv_obj_set_size(page4,SCREEN_W,SCREEN_H);
  lv_obj_set_pos(page4,0,0);
  lv_obj_clear_flag(page4,LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(page4,COL_BG,0);
  lv_obj_set_style_bg_opa(page4,LV_OPA_COVER,0);
  lv_obj_set_style_border_width(page4,0,0);
  lv_obj_set_style_pad_all(page4,0,0);

  evTitle=lv_label_create(page4);
  lv_obj_set_style_text_font(evTitle,&lv_font_montserrat_20,0);
  lv_obj_set_style_text_color(evTitle,COL_GREEN,0);
  lv_obj_align(evTitle,LV_ALIGN_TOP_MID,0,5);

  evSummary=lv_label_create(page4);
  lv_obj_set_style_text_font(evSummary,&lv_font_montserrat_12,0);
  lv_obj_set_style_text_color(evSummary,COL_MUTED,0);
  lv_obj_align(evSummary,LV_ALIGN_TOP_MID,0,31);

  lv_obj_t **rows[4] = {&evRow1,&evRow2,&evRow3,&evRow4};
  for(int i=0;i<4;i++){
    *rows[i]=lv_label_create(page4);
    lv_obj_set_width(*rows[i],300);
    lv_obj_set_style_text_font(*rows[i],&lv_font_montserrat_14,0);
    lv_obj_set_style_text_color(*rows[i],COL_GREEN,0);
    lv_obj_set_style_text_align(*rows[i],LV_TEXT_ALIGN_LEFT,0);
    lv_obj_set_pos(*rows[i],10,55+i*27);
  }

  socBadge4=lv_label_create(page4);
  lv_obj_set_style_text_color(socBadge4,COL_GREEN,0);
  lv_obj_set_style_text_font(socBadge4,&lv_font_montserrat_12,0);
  lv_obj_set_width(socBadge4,46);
  lv_obj_set_style_text_align(socBadge4,LV_TEXT_ALIGN_RIGHT,0);
  lv_obj_set_pos(socBadge4,269,34);
}

void buildEventLogPage(){
  page5=lv_obj_create(lv_scr_act());
  lv_obj_set_size(page5,SCREEN_W,SCREEN_H);
  lv_obj_set_pos(page5,0,0);
  lv_obj_clear_flag(page5,LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(page5,COL_BG,0);
  lv_obj_set_style_bg_opa(page5,LV_OPA_COVER,0);
  lv_obj_set_style_border_width(page5,0,0);
  lv_obj_set_style_pad_all(page5,0,0);

  eventLogTitle=lv_label_create(page5);
  lv_label_set_text(eventLogTitle,"EVENT LOG");
  lv_obj_set_style_text_font(eventLogTitle,&lv_font_montserrat_18,0);
  lv_obj_set_style_text_color(eventLogTitle,COL_CYAN,0);
  lv_obj_set_pos(eventLogTitle,7,3);

  eventLogCount=lv_label_create(page5);
  lv_obj_set_width(eventLogCount,100);
  lv_obj_set_style_text_font(eventLogCount,&lv_font_montserrat_12,0);
  lv_obj_set_style_text_color(eventLogCount,COL_MUTED,0);
  lv_obj_set_style_text_align(eventLogCount,LV_TEXT_ALIGN_RIGHT,0);
  lv_obj_set_pos(eventLogCount,205,7);

  for(int i=0;i<10;i++){
    eventLogRows[i]=lv_label_create(page5);
    lv_obj_set_size(eventLogRows[i],306,14);
    lv_label_set_long_mode(eventLogRows[i],LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_font(eventLogRows[i],&lv_font_montserrat_12,0);
    lv_obj_set_style_text_color(eventLogRows[i],COL_GREEN,0);
    lv_obj_set_pos(eventLogRows[i],7,25+i*14);
  }
}

void buildServicePage(){
  servicePage=lv_obj_create(lv_scr_act());
  lv_obj_set_size(servicePage,SCREEN_W,SCREEN_H);
  lv_obj_set_pos(servicePage,0,0);
  lv_obj_clear_flag(servicePage,LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(servicePage,COL_BG,0);
  lv_obj_set_style_bg_opa(servicePage,LV_OPA_COVER,0);
  lv_obj_set_style_border_width(servicePage,0,0);
  lv_obj_set_style_pad_all(servicePage,0,0);

  serviceTitle=lv_label_create(servicePage);
  lv_obj_set_style_text_font(serviceTitle,&lv_font_montserrat_18,0);
  lv_obj_set_style_text_color(serviceTitle,COL_CYAN,0);
  lv_obj_align(serviceTitle,LV_ALIGN_TOP_MID,0,4);

  serviceInfo=lv_label_create(servicePage);
  lv_obj_set_width(serviceInfo,308);
  lv_obj_set_style_text_font(serviceInfo,&lv_font_montserrat_14,0);
  lv_obj_set_style_text_color(serviceInfo,COL_TEXT,0);
  lv_obj_set_style_text_line_space(serviceInfo,3,0);
  lv_obj_set_pos(serviceInfo,8,30);

  serviceFooter=lv_label_create(servicePage);
  lv_obj_set_width(serviceFooter,310);
  lv_obj_set_style_text_font(serviceFooter,&lv_font_montserrat_12,0);
  lv_obj_set_style_text_color(serviceFooter,COL_MUTED,0);
  lv_obj_set_style_text_align(serviceFooter,LV_TEXT_ALIGN_CENTER,0);
  lv_obj_align(serviceFooter,LV_ALIGN_BOTTOM_MID,0,-4);
}

void buildAlert(){
  alertBox=lv_obj_create(lv_scr_act());
  lv_obj_set_size(alertBox,SCREEN_W,SCREEN_H);
  lv_obj_set_pos(alertBox,0,0);
  lv_obj_clear_flag(alertBox,LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(alertBox,lv_color_hex(0x140606),0);
  lv_obj_set_style_bg_opa(alertBox,LV_OPA_COVER,0);
  lv_obj_set_style_border_width(alertBox,0,0);
  lv_obj_set_style_pad_all(alertBox,0,0);

  alertTitle=lv_label_create(alertBox);
  lv_obj_set_style_text_font(alertTitle,&lv_font_montserrat_24,0);
  lv_obj_set_style_text_color(alertTitle,COL_RED,0);
  lv_obj_align(alertTitle,LV_ALIGN_CENTER,0,-20);

  alertSub=lv_label_create(alertBox);
  lv_obj_set_style_text_font(alertSub,&lv_font_montserrat_14,0);
  lv_obj_set_style_text_color(alertSub,COL_TEXT,0);
  lv_obj_set_width(alertSub,280);
  lv_obj_set_style_text_align(alertSub,LV_TEXT_ALIGN_CENTER,0);
  lv_obj_align(alertSub,LV_ALIGN_CENTER,0,25);
}

void buildHeartbeat(){
  heartbeat=lv_obj_create(lv_scr_act());
  lv_obj_set_size(heartbeat,9,9);
  lv_obj_set_pos(heartbeat,309,2);
  lv_obj_clear_flag(heartbeat,LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_radius(heartbeat,LV_RADIUS_CIRCLE,0);
  lv_obj_set_style_border_width(heartbeat,0,0);
  lv_obj_set_style_pad_all(heartbeat,0,0);
  lv_obj_set_style_bg_opa(heartbeat,LV_OPA_COVER,0);
}

void buildUI(){
#if defined(TARGET_4848S040)
  lv_obj_set_style_bg_color(lv_scr_act(),U_BG(),0);
  ui480Build();
  return;
#endif
  lv_obj_set_style_bg_color(lv_scr_act(),COL_BG,0);
  buildMainPage();
  buildSecondPage();
  buildCellPage();
  buildEventsPage();
  buildEventLogPage();
  buildServicePage();
  buildAlert();
  buildHeartbeat();
  lv_obj_add_flag(page2,LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(page3,LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(page4,LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(page5,LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(servicePage,LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(alertBox,LV_OBJ_FLAG_HIDDEN);
}

// ----------------- UI update -----------------
void updateNormalLabels(){
  char b[64];

  snprintf(b,sizeof(b),"%.0f%%",t.soc);
  lv_label_set_text(vSoc,isnan(t.soc)?"--":b);

  String ps=powerText();
  lv_label_set_text(vPower,ps.c_str());
  float p=livePowerW();
  lv_obj_set_style_text_color(vPower,(!isnan(p)&&p<0)?COL_YELLOW:COL_GREEN,0);

  snprintf(b,sizeof(b),"%.1f A",t.current);
  lv_label_set_text(vCurrent,isnan(t.current)?"--":b);

  snprintf(b,sizeof(b),"%.1f V",t.voltage);
  lv_label_set_text(vVoltage,isnan(t.voltage)?"--":b);

  snprintf(b,sizeof(b),"%u mV",t.delta);
  lv_label_set_text(vDelta,b);
  lv_color_t dc=t.delta<=20?COL_GREEN:(t.delta<=50?COL_YELLOW:COL_ORANGE);
  lv_obj_set_style_text_color(vDelta,dc,0);

  snprintf(b,sizeof(b),"MIN %.1f",t.tMin);
  lv_label_set_text(vTempMin,isnan(t.tMin)?"MIN --":b);

  snprintf(b,sizeof(b),"MAX %.1f",t.tMax);
  lv_label_set_text(vTempMax,isnan(t.tMax)?"MAX --":b);

  snprintf(b,sizeof(b),"%.3f V",t.cMin/1000.0f);
  lv_label_set_text(vCellMin,b);

  snprintf(b,sizeof(b),"%.3f V",t.cMax/1000.0f);
  lv_label_set_text(vCellMax,b);

  snprintf(b,sizeof(b),"%.1f kWh",t.remainScaled);
  lv_label_set_text(vRemain,isnan(t.remainScaled)?"--":b);

  snprintf(b,sizeof(b),"%.1f kWh",t.totalScaled);
  lv_label_set_text(vTotal,isnan(t.totalScaled)?"--":b);

  snprintf(b,sizeof(b),"SOH %.0f%%",t.soh);
  lv_label_set_text(vSoh,isnan(t.soh)?"SOH --":b);

  snprintf(b,sizeof(b),"CHG %.1f",t.maxChgKw);
  lv_label_set_text(vChg,isnan(t.maxChgKw)?"CHG --":b);

  snprintf(b,sizeof(b),"DIS %.1f",t.maxDisKw);
  lv_label_set_text(vDis,isnan(t.maxDisKw)?"DIS --":b);
}

void updateSocBadges(){
  char value[16];
  if(isnan(t.soc)) snprintf(value,sizeof(value),"SOC --");
  else snprintf(value,sizeof(value),"%.0f%%",t.soc);
  lv_obj_t* badges[]={socBadge2,socBadge3,socBadge4};
  for(lv_obj_t* badge:badges){
    lv_label_set_text(badge,value);
    if(showSocBadge) lv_obj_clear_flag(badge,LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(badge,LV_OBJ_FLAG_HIDDEN);
  }
}

void updateCellPage(){
  if(!cells.valid){
    lv_label_set_text(cellMinLabel,"#FF3434 MIN --#\n#52E000 MAX --#");
    lv_label_set_text(cellMaxLabel,"CELL --\nCELL --");
    lv_label_set_text(cellDeltaLabel,"-- mV");
    return;
  }
  char b[80];
  snprintf(b,sizeof(b),"#FF3434 MIN %.3f V#\n#52E000 MAX %.3f V#",
           cells.minMv/1000.0f,cells.maxMv/1000.0f);
  lv_label_set_text(cellMinLabel,b);
  snprintf(b,sizeof(b),"CELL %u\nCELL %u",cells.minCell+1,cells.maxCell+1);
  lv_label_set_text(cellMaxLabel,b);
  snprintf(b,sizeof(b),"%u mV",cells.deltaMv);
  lv_label_set_text(cellDeltaLabel,b);

  int plotMin=(int)cells.minMv-20;
  int plotMax=(int)cells.maxMv+20;
  int span=max(1,plotMax-plotMin);
  for(int i=0;i<96;i++){
    int height=8+((int)cells.mv[i]-plotMin)*95/span;
    height=constrain(height,8,103);
    lv_obj_set_y(cellBars[i],165-height);
    lv_obj_set_height(cellBars[i],height);
    lv_color_t c=lv_color_hex(0x46677E);
    // Extremes take priority so they remain visible even while balancing.
    if(i==cells.minCell) c=COL_RED;
    else if(i==cells.maxCell) c=COL_GREEN;
    else if(cells.balancing[i]) c=COL_CYAN;
    lv_obj_set_style_bg_color(cellBars[i],c,0);
  }
}


void updateEventsPage(){
  int rank=eventSeverityRank();

  if(!eventsFresh){
    lv_label_set_text(evTitle,"EVENTS");
    lv_obj_set_style_text_color(evTitle,COL_ORANGE,0);
    lv_label_set_text(evSummary,"Waiting for /events...");
  } else if(rank==0){
    lv_label_set_text(evTitle,"SYSTEM STATUS  OK");
    lv_obj_set_style_text_color(evTitle,COL_GREEN,0);
    lv_label_set_text(evSummary,evCount?"Battery Emulator diagnostics":"No active events");
  } else if(rank==1){
    lv_label_set_text(evTitle,"EVENT WARNING");
    lv_obj_set_style_text_color(evTitle,COL_ORANGE,0);
    lv_label_set_text(evSummary,"Warning event detected");
  } else {
    lv_label_set_text(evTitle,"EVENT ERROR");
    lv_obj_set_style_text_color(evTitle,COL_RED,0);
    lv_label_set_text(evSummary,"Error event detected");
  }

  lv_obj_t *rows[4]={evRow1,evRow2,evRow3,evRow4};
  for(int i=0;i<4;i++){
    if(i<evCount){
      String sev=ev[i].severity;
      sev.toUpperCase();
      lv_color_t c=COL_GREEN;
      if(sev.indexOf("ERROR")>=0 || sev.indexOf("CRITICAL")>=0 || sev.indexOf("FATAL")>=0) c=COL_RED;
      else if(sev.indexOf("WARN")>=0) c=COL_ORANGE;

      String line=String("\xE2\x80\xA2 ")+shortEventText(ev[i]);
      lv_label_set_text(rows[i],line.c_str());
      lv_obj_set_style_text_color(rows[i],c,0);
    } else {
      lv_label_set_text(rows[i],"");
    }
  }
}

void updateEventLogPage(){
  char countText[20];
  snprintf(countText,sizeof(countText),"LATEST %d/10",evCount);
  lv_label_set_text(eventLogCount,eventsFresh?countText:"WAITING");

  for(int i=0;i<10;i++){
    if(i>=evCount){
      lv_label_set_text(eventLogRows[i],i==0 && eventsFresh?"No events reported":"");
      lv_obj_set_style_text_color(eventLogRows[i],COL_MUTED,0);
      continue;
    }

    String sev=ev[i].severity;
    sev.toUpperCase();
    const char* tag="I";
    lv_color_t color=COL_GREEN;
    if(sev.indexOf("ERROR")>=0 || sev.indexOf("CRITICAL")>=0 || sev.indexOf("FATAL")>=0){
      tag="E";
      color=COL_RED;
    } else if(sev.indexOf("WARN")>=0){
      tag="W";
      color=COL_ORANGE;
    }

    String text=ev[i].type;
    if(text.startsWith("EVENT_")) text.remove(0,6);
    if(!text.length()) text=ev[i].message;
    text.trim();
    if(ev[i].count>1) text += " x"+String(ev[i].count);
    if(text.length()>37) text=text.substring(0,37);

    char prefix[10];
    snprintf(prefix,sizeof(prefix),"%02d %s  ",i+1,tag);
    String line=String(prefix)+text;
    lv_label_set_text(eventLogRows[i],line.c_str());
    lv_obj_set_style_text_color(eventLogRows[i],color,0);
  }
}

void updateServicePage(){
  if(configPortalActive){
    bool phoneConnected=WiFi.softAPgetStationNum()>0;

    lv_label_set_text(serviceTitle,phoneConnected?"PHONE CONNECTED":"PHONE SETUP");
    lv_obj_set_style_text_color(serviceTitle,phoneConnected?COL_GREEN:COL_ORANGE,0);

    lv_obj_set_width(serviceInfo,312);
    lv_obj_set_pos(serviceInfo,4,38);
    lv_obj_set_style_text_font(serviceInfo,&lv_font_montserrat_16,0);
    lv_obj_set_style_text_align(serviceInfo,LV_TEXT_ALIGN_CENTER,0);
    lv_obj_set_style_text_line_space(serviceInfo,10,0);
    lv_obj_set_style_text_color(serviceInfo,COL_TEXT,0);
    lv_label_set_text(serviceInfo,
      "Wi-Fi: BatteryDisplay-Setup\n"
      "Password: 123456789\n"
      "Open: 192.168.4.1");

    lv_obj_set_style_text_color(serviceFooter,phoneConnected?COL_GREEN:COL_MUTED,0);
    lv_label_set_text(serviceFooter,phoneConnected?
      "PHONE CONNECTED - OPEN BROWSER":
      "CONNECT PHONE TO CONTINUE");
    return;
  }

  String ip=WiFi.status()==WL_CONNECTED?WiFi.localIP().toString():"--";
  uint32_t uptime=millis()/1000;
  uint8_t brightPercent=(uint16_t)brightness*100/255;
  char info[360];
  snprintf(info,sizeof(info),
    "FW %-12s  IP %s\n"
    "LINK %s  CH %u\n"
    "DATA %-6s  CELLS %-6s\n"
    "RX %-5lu DROP %-4lu HTTP %-4lu\n"
    "RECONNECT %-4lu  UPTIME %luh %02lum\n"
    "RESET %-10s  LIGHT %u%%",
    FW_VERSION,ip.c_str(),directMode?"ESPNOW":"WI-FI",directMode?espNowChannel:(uint8_t)WiFi.channel(),
    ageText(lastGood).c_str(),ageText(cells.lastGood).c_str(),
    (unsigned long)espNowPackets,(unsigned long)espNowDropped,(unsigned long)httpFailuresTotal,
    (unsigned long)wifiReconnectsTotal,(unsigned long)(uptime/3600),(unsigned long)((uptime/60)%60),
    resetReasonText(),brightPercent);
  lv_label_set_text(serviceInfo,info);

  lv_obj_set_width(serviceInfo,308);
  lv_obj_set_pos(serviceInfo,8,30);
  lv_obj_set_style_text_font(serviceInfo,&lv_font_montserrat_14,0);
  lv_obj_set_style_text_align(serviceInfo,LV_TEXT_ALIGN_LEFT,0);
  lv_obj_set_style_text_line_space(serviceInfo,3,0);
  lv_obj_set_style_text_color(serviceInfo,COL_TEXT,0);
  lv_label_set_text(serviceTitle,"SERVICE INFORMATION");
  lv_obj_set_style_text_color(serviceTitle,COL_CYAN,0);
  lv_obj_set_style_text_color(serviceFooter,COL_MUTED,0);
  lv_label_set_text(serviceFooter,"R: LIGHT   HOLD R: SETUP   BOTH: EXIT");
}

void updateView(){
#if defined(TARGET_4848S040)
  xSemaphoreTake(dataMutex,portMAX_DELAY);
  ui480Update();
  xSemaphoreGive(dataMutex);
  return;
#endif
  xSemaphoreTake(dataMutex,portMAX_DELAY);
  Health h=health();
  updateSocBadges();

  if(serviceMode){
    lv_obj_add_flag(page1,LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(page2,LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(page3,LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(page4,LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(page5,LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(alertBox,LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(servicePage,LV_OBJ_FLAG_HIDDEN);
    updateServicePage();
  }

  else if(h==H_OFFLINE){
    lv_obj_add_flag(servicePage,LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(page1,LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(page2,LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(page3,LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(page4,LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(page5,LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(alertBox,LV_OBJ_FLAG_HIDDEN);

    bool phoneConnected=configPortalActive && WiFi.softAPgetStationNum()>0;
    lv_obj_set_style_bg_color(alertBox,configPortalActive?COL_BG:lv_color_hex(0x160406),0);
    lv_obj_set_style_text_color(alertTitle,configPortalActive?(phoneConnected?COL_GREEN:COL_CYAN):COL_RED,0);
    lv_label_set_text(alertTitle,configPortalActive?(phoneConnected?"PHONE CONNECTED":"PHONE SETUP"):"BATTERY OFFLINE");

    String s;
    if(configPortalActive){
      s="1  Wi-Fi: BatteryDisplay-Setup\n";
      s+="2  Password: 123456789\n";
      s+="3  Open: 192.168.4.1";
    }
    else if(directMode){
      s="ESP-NOW DIRECT   CH "+String(espNowChannel);
      s += "\nWaiting for Battery Emulator";
      if(lastGood) s += "   AGE "+ageText(lastGood);
    } else {
      s=(WiFi.status()==WL_CONNECTED?"WiFi OK":"WiFi DOWN");
      s += "   HTTP ";
      s += String(httpCode);
      if(lastGood) s += "   AGE "+ageText(lastGood);
    }
    lv_label_set_text(alertSub,s.c_str());
  }
  else if(h==H_WARN){
    lv_obj_add_flag(servicePage,LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(page1,LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(page2,LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(page3,LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(page4,LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(page5,LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(alertBox,LV_OBJ_FLAG_HIDDEN);

    lv_obj_set_style_bg_color(alertBox,lv_color_hex(0x181006),0);
    lv_obj_set_style_text_color(alertTitle,COL_ORANGE,0);
    lv_label_set_text(alertTitle,"BATTERY WARNING");

    String s="System: "+t.systemStatus;
    s += "\nEMU ";
    s += t.emuCont?"OK":"NO";
    s += "   INV ";
    s += t.invCont?"OK":"NO";
    lv_label_set_text(alertSub,s.c_str());
  }
  else{
    lv_obj_add_flag(servicePage,LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(alertBox,LV_OBJ_FLAG_HIDDEN);
    if(page==0){
      lv_obj_clear_flag(page1,LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(page2,LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(page3,LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(page4,LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(page5,LV_OBJ_FLAG_HIDDEN);
      updateNormalLabels();
    } else if(page==1){
      lv_obj_add_flag(page1,LV_OBJ_FLAG_HIDDEN);
      lv_obj_clear_flag(page2,LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(page3,LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(page4,LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(page5,LV_OBJ_FLAG_HIDDEN);
      updateNormalLabels();
    } else if(page==2) {
      lv_obj_add_flag(page1,LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(page2,LV_OBJ_FLAG_HIDDEN);
      lv_obj_clear_flag(page3,LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(page4,LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(page5,LV_OBJ_FLAG_HIDDEN);
      updateCellPage();
    } else if(page==3) {
      lv_obj_add_flag(page1,LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(page2,LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(page3,LV_OBJ_FLAG_HIDDEN);
      lv_obj_clear_flag(page4,LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(page5,LV_OBJ_FLAG_HIDDEN);
      updateEventsPage();
    } else {
      lv_obj_add_flag(page1,LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(page2,LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(page3,LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(page4,LV_OBJ_FLAG_HIDDEN);
      lv_obj_clear_flag(page5,LV_OBJ_FLAG_HIDDEN);
      updateEventLogPage();
    }
  }

  lv_color_t dot;
  if(h==H_OFFLINE) dot=COL_RED;
  else if(consecutiveFetchFailures>0 || (lastGood && millis()-lastGood>2500)) dot=COL_YELLOW;
  else dot=h==H_OK ? COL_GREEN : COL_ORANGE;
  if(!pulse) dot=lv_color_hex(0x26303A);
  lv_obj_set_style_bg_color(heartbeat,dot,0);
  lv_obj_set_pos(heartbeat,309,(!serviceMode && page==2) ? 45 : 2);
  lv_obj_move_foreground(heartbeat);
  xSemaphoreGive(dataMutex);
}

void buttons(){
#if defined(TARGET_4848S040)
  return;
}
#else
  bool l=digitalRead(BTN_LEFT);
  bool r=digitalRead(BTN_RIGHT);
  bool changed=false;

  if((l==LOW || r==LOW) && autoDimmed){
    autoDimmed=false;
    applyBrightness(brightness);
  }

  // Holding both buttons opens/closes diagnostics without adding another
  // page to the normal five-page rotation.
  if(l==LOW && r==LOW){
    if(!bothPressedAt) bothPressedAt=millis();
    if(!bothLongHandled && millis()-bothPressedAt>=1800){
      bothLongHandled=true;
      serviceMode=!serviceMode;
      lastButton=millis();
      changed=true;
    }
    prevL=l; prevR=r;
    if(changed) updateView();
    return;
  } else {
    bothPressedAt=0;
    bothLongHandled=false;
  }

  if(serviceMode){
    if(prevL==HIGH && l==LOW && millis()-lastButton>150){
      serviceMode=false;
      lastButton=millis();
      changed=true;
    }

    if(prevR==HIGH && r==LOW){
      rightPressedAt=millis();
      rightLongHandled=false;
    }
    if(r==LOW && rightPressedAt && !rightLongHandled && millis()-rightPressedAt>=2500){
      rightLongHandled=true;
      configPortalRequested=true;
      changed=true;
    }
    if(prevR==LOW && r==HIGH && rightPressedAt){
      if(!rightLongHandled && millis()-rightPressedAt<1200){
        if(brightness<110) brightness=150;
        else if(brightness<180) brightness=210;
        else if(brightness<240) brightness=255;
        else brightness=90;
        applyBrightness(brightness);
        saveSettings();
        changed=true;
      }
      rightPressedAt=0;
    }

    prevL=l; prevR=r;
    if(changed) updateView();
    return;
  }

  if(prevL==HIGH && l==LOW && millis()-lastButton>150){
    page = (page + 4) % 5;
    lastButton=millis();
    saveSettings();
    changed=true;
  }
  if(prevR==HIGH && r==LOW && millis()-lastButton>150){
    page = (page + 1) % 5;
    lastButton=millis();
    saveSettings();
    changed=true;
  }

  prevL=l;
  prevR=r;

  if(!autoDimmed && millis()-lastButton>AUTO_DIM_MS){
    autoDimmed=true;
    uint8_t dimValue=brightness/5;
    applyBrightness(dimValue<28?28:dimValue);
  }

  // Redraw immediately; do not wait for a potentially slow HTTP request.
  if(changed) updateView();
}
#endif

void runPendingControlAction(){
  int action=pendingControlAction;
  if(action<0 || action>14) return;
  if(directMode || WiFi.status()!=WL_CONNECTED){
    pendingControlAction=-1;
    controlActionResult=directMode?"WEB ONLY - ESP-NOW IS READ ONLY":"WEB CONNECTION REQUIRED";
    uiDirty=true;
    return;
  }
  pendingControlAction=-1;
  static const char* paths[]={
    "/pause?value=true", "/pause?value=false", "/settings", "/advanced", "/canlog",
    "/canreplay", "/log", "/cellmonitor", "/events", "/reboot"
  };
  HTTPClient http;
  http.setConnectTimeout(1800);
  http.setTimeout(2500);
  const char *path=nullptr;
  bool putAction=false;
  if(action<=9) path=paths[action];
  else {
    static const char* putPaths[]={"/readDTC","/clearIsolation","/resetBMS","/resetSOC","/contactorOpen"};
    path=putPaths[action-10];
    putAction=true;
  }
  if(!http.begin(emulatorUrl(path))){
    controlActionResult="REQUEST FAILED";
    uiDirty=true;
    return;
  }
  int code=putAction?http.PUT("0"):http.GET();
  http.end();
  controlActionResult=(code>=200 && code<400)?"COMMAND ACCEPTED":("HTTP ERROR "+String(code));
  uiDirty=true;
}

void networkTask(void*){
  uint32_t batteryAt=0, cellsAt=0, eventsAt=0, advancedAt=0, reconnectAt=0;
  esp_task_wdt_add(NULL);
  for(;;){
    uint32_t now=millis();

    if(configPortalStopRequested){
      configPortalStopRequested=false;
      configPortalRequested=false;
      if(configPortalActive) stopConfigPortal();
      uiDirty=true;
    }
    if(configPortalRequested){
      configPortalRequested=false;
      startConfigPortal();
      uiDirty=true;
    }
    if(configPortalActive){
      configDns.processNextRequest();
      handleConfigClient();
      uint8_t clientCount=WiFi.softAPgetStationNum();
      if(clientCount!=configPortalClientCount){
        configPortalClientCount=clientCount;
        uiDirty=true;
      }
      // Restart directly from the setup AP after a successful save so all
      // connection settings are reloaded from NVS in a clean radio state.
      if(settingsRestartAt && (int32_t)(millis()-settingsRestartAt)>=0) ESP.restart();
      if(configPortalExitAt && (int32_t)(millis()-configPortalExitAt)>=0){
        stopConfigPortal();
        uiDirty=true;
      }
      esp_task_wdt_reset();
      vTaskDelay(pdMS_TO_TICKS(25));
      continue;
    }

    drainEspNowQueue();

    if(directMode){
      wifiDisconnectedAt=0;
      if(!espNowReady && now-reconnectAt>=5000){
        reconnectAt=now;
        startDirectLink();
      }
      // A direct receiver cannot know the transmitter's Wi-Fi channel in
      // advance. Scan 1-13 until the first valid Battery Emulator frame, then
      // retain that channel in NVS for fast future starts.
      if(espNowReady && !espNowChannelLocked && now-lastChannelHop>=900){
        lastChannelHop=now;
        espNowChannel=(espNowChannel%13)+1;
        esp_wifi_set_channel(espNowChannel,WIFI_SECOND_CHAN_NONE);
        uiDirty=true;
      }
    } else {
      if(WiFi.status()!=WL_CONNECTED){
        controlServerRunning=false;
        if(!wifiDisconnectedAt) wifiDisconnectedAt=now;
      } else {
        wifiDisconnectedAt=0;
        if(!espNowReady) startEspNow(false);
        startControlServer();
        handleControlClient();
        esp_task_wdt_reset();
      }

      if(WiFi.status()!=WL_CONNECTED && now-reconnectAt>=5000){
        reconnectAt=now;
        connectStoredWiFi();
      }
      runPendingControlAction();
      esp_task_wdt_reset();
      // Prefer ESP-NOW and use HTTP only as a low-rate fallback. Multiple
      // displays must not saturate the Battery Emulator's small web server.
      bool espNowFresh=lastEspNowGood && now-lastEspNowGood<6000;
      uint32_t httpBackoff=5000UL << min((int)consecutiveFetchFailures,2);
      if(!espNowFresh && now-batteryAt>=httpBackoff){
        batteryAt=now;
        fetchBattery();
        esp_task_wdt_reset();
        uiDirty=true;
      }
      // Keep last good cell values. A failed request never clears them.
      if(!espNowFresh && page==1 && now-cellsAt>=15000){
        cellsAt=now;
        fetchCells();
        esp_task_wdt_reset();
        uiDirty=true;
      }
      // Events feed the dashboard warning banner, so refresh them at low rate
      // on every screen and faster while the Events page is visible.
      uint32_t eventsPeriod=page==2?5000:10000;
      if(now-eventsAt>=eventsPeriod){
        eventsAt=now;
        fetchEvents();
        esp_task_wdt_reset();
        uiDirty=true;
      }
#if defined(TARGET_4848S040)
      // Tesla contactor/HVIL/DTC data lives on /advanced. Request it only
      // while Battery Info or DTC/Faults is open.
      if((page==3 || uDtcVisible) && now-advancedAt>=5000){
        advancedAt=now;
        fetchAdvanced();
        esp_task_wdt_reset();
        uiDirty=true;
      }
#endif
    }

    if(settingsRestartAt && (int32_t)(millis()-settingsRestartAt)>=0) ESP.restart();

    // A prolonged connection loss normally indicates a wedged radio stack.
    // The setup portal is exempt so the client can configure it at leisure.
    if(!configPortalActive && wifiDisconnectedAt && now-wifiDisconnectedAt>600000){
      ESP.restart();
    }
    esp_task_wdt_reset();
    vTaskDelay(pdMS_TO_TICKS(25));
  }
}

#if defined(TARGET_4848S040)
// GT911 exposes a compact report at 0x814E.  We only need one deliberate
// tap: left/right halves move through the same pages as the two buttons did.
bool gt911Read(uint16_t reg,uint8_t *out,size_t count){
  Wire.beginTransmission(TOUCH_ADDR);
  Wire.write((uint8_t)(reg>>8));
  Wire.write((uint8_t)reg);
  if(Wire.endTransmission(false)!=0) return false;
  if(Wire.requestFrom((int)TOUCH_ADDR,(int)count)!=(int)count) return false;
  for(size_t i=0;i<count;i++) out[i]=Wire.read();
  return true;
}

void gt911Clear(){
  Wire.beginTransmission(TOUCH_ADDR);
  Wire.write(0x81); Wire.write(0x4E); Wire.write(0);
  Wire.endTransmission();
}

// Board revisions of ESP32-4848S040C_I have been shipped with the GT911
// sensor mounted in different orientations. Keep that permanent hardware
// correction separate from the user-selectable display rotation.
TouchHardwareTransform touchHardwareFromMapId(uint8_t mapId){
  TouchHardwareTransform result;
  result.swapXY=(mapId&4)!=0;
  result.mirrorX=(mapId&1)!=0;
  result.mirrorY=(mapId&2)!=0;
  return result;
}

void applyTouchHardwareTransform(uint16_t rawX,uint16_t rawY,uint16_t &baseX,uint16_t &baseY,
                                 const TouchHardwareTransform &hardware){
  int x=hardware.swapXY?rawY:rawX;
  int y=hardware.swapXY?rawX:rawY;
  if(hardware.mirrorX) x=SCREEN_W-1-x;
  if(hardware.mirrorY) y=SCREEN_H-1-y;
  baseX=constrain(x,0,SCREEN_W-1);
  baseY=constrain(y,0,SCREEN_H-1);
}

// Arduino_GFX rotates the framebuffer clockwise. Touch coordinates therefore
// need the inverse transform to remain in LVGL's logical 480x480 space.
void applyTouchRotation(uint16_t baseX,uint16_t baseY,uint16_t &logicalX,uint16_t &logicalY,uint8_t rotation){
  int x=baseX;
  int y=baseY;
  switch(rotation&3){
    case 0: x=baseX;             y=baseY;              break;
    case 1: x=baseY;             y=SCREEN_H-1-baseX;   break;
    case 2: x=SCREEN_W-1-baseX;  y=SCREEN_H-1-baseY;   break;
    case 3: x=SCREEN_W-1-baseY;  y=baseX;              break;
  }
  logicalX=constrain(x,0,SCREEN_W-1);
  logicalY=constrain(y,0,SCREEN_H-1);
}

void mapTouchPoint(uint16_t rawX,uint16_t rawY,uint16_t &logicalX,uint16_t &logicalY,
                   const TouchHardwareTransform &hardware,uint8_t rotation){
  uint16_t baseX=0,baseY=0;
  applyTouchHardwareTransform(rawX,rawY,baseX,baseY,hardware);
  applyTouchRotation(baseX,baseY,logicalX,logicalY,rotation);
}

void lvTouchRead(lv_indev_drv_t*,lv_indev_data_t *data){
  static bool down=false;
  static uint16_t lastX=0,lastY=0;
  static uint16_t lastRawX=0,lastRawY=0;
  static bool tracking=false;
  static uint8_t samples=0;
  static uint32_t lastPointAt=0;
  auto finishTouch=[&](){
    if(tracking && touchCalibrationActive && samples){
      touchRawTapX=lastRawX;
      touchRawTapY=lastRawY;
      touchRawTapReady=true;
    }
    tracking=false;
    down=false;
  };
  uint8_t status=0;
  if(gt911Read(0x814E,&status,1) && (status&0x80)){
    uint8_t count=status&0x0F;
    if(count){
      // 0x814F contains the track id. Reading from 0x8150 intentionally
      // starts at Xlo, followed by Xhi, Ylo and Yhi on this panel.
      uint8_t point[4]={};
      if(gt911Read(0x8150,point,sizeof(point))){
        const uint16_t rawX=point[0]|((uint16_t)point[1]<<8);
        const uint16_t rawY=point[2]|((uint16_t)point[3]<<8);
        lastRawX=rawX;
        lastRawY=rawY;
        mapTouchPoint(rawX,rawY,lastX,lastY,touchHardware,screenOrientation);
        bool began=!tracking;
        if(began){ tracking=true; samples=0; }
        if(samples<255) samples++;
        lastPointAt=millis();
        if(began || down) down=true;
      }
    } else {
      finishTouch();
    }
    gt911Clear();
  }
  // Some GT911 panels omit the final zero-touch report.  Without this
  // watchdog LVGL can believe that a finger is held forever, delaying clicks
  // and later replaying them as seemingly spontaneous actions.
  // Release quickly enough for short taps. Keep gesture tracking alive a bit
  // longer so a sparse stream of GT911 coordinates can still form a swipe.
  if(down && millis()-lastPointAt>35) down=false;
  if(tracking && millis()-lastPointAt>100) finishTouch();
  data->point.x=lastX; data->point.y=lastY;
  // Calibration consumes raw taps itself. Hiding them from LVGL prevents an
  // incorrectly mapped first tap from activating a control behind the overlay.
  data->state=(!touchCalibrationActive && down)?LV_INDEV_STATE_PR:LV_INDEV_STATE_REL;
}

static const int16_t TOUCH_CAL_X[3]={60,420,60};
static const int16_t TOUCH_CAL_Y[3]={80,80,400};
static constexpr uint8_t TOUCH_MAPPING_VERSION=4;
static constexpr uint8_t TOUCH_CALIBRATION_VERSION=2;

void showTouchCalibrationTarget(){
  if(!touchCalTarget || !touchCalStepLabel) return;
  lv_obj_set_pos(touchCalTarget,TOUCH_CAL_X[touchCalibrationStep]-25,TOUCH_CAL_Y[touchCalibrationStep]-25);
  lv_label_set_text_fmt(touchCalStepLabel,"Touch target %u of 3",touchCalibrationStep+1);
  lv_obj_move_foreground(touchCalTarget);
}

void startTouchCalibration(){
  touchCalibrationActive=true;
  touchCalibrationStep=0;
  touchRawTapReady=false;

  touchCalOverlay=lv_obj_create(lv_scr_act());
  lv_obj_remove_style_all(touchCalOverlay);
  lv_obj_set_size(touchCalOverlay,SCREEN_W,SCREEN_H);
  lv_obj_set_pos(touchCalOverlay,0,0);
  lv_obj_set_style_bg_color(touchCalOverlay,lv_color_hex(0x050B16),0);
  lv_obj_set_style_bg_opa(touchCalOverlay,LV_OPA_COVER,0);
  lv_obj_clear_flag(touchCalOverlay,LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *title=lv_label_create(touchCalOverlay);
  lv_label_set_text(title,"TOUCH CALIBRATION");
  lv_obj_set_style_text_color(title,lv_color_hex(0x75F04A),0);
  lv_obj_set_style_text_font(title,&lv_font_montserrat_28,0);
  lv_obj_align(title,LV_ALIGN_CENTER,0,-38);

  touchCalStepLabel=lv_label_create(touchCalOverlay);
  lv_obj_set_style_text_color(touchCalStepLabel,lv_color_white(),0);
  lv_obj_set_style_text_font(touchCalStepLabel,&lv_font_montserrat_20,0);
  lv_obj_align(touchCalStepLabel,LV_ALIGN_CENTER,0,4);

  lv_obj_t *hint=lv_label_create(touchCalOverlay);
  lv_label_set_text(hint,"Press the center, then release");
  lv_obj_set_style_text_color(hint,lv_color_hex(0x40D9FF),0);
  lv_obj_set_style_text_font(hint,&lv_font_montserrat_16,0);
  lv_obj_align(hint,LV_ALIGN_CENTER,0,39);

  touchCalTarget=lv_obj_create(touchCalOverlay);
  lv_obj_remove_style_all(touchCalTarget);
  lv_obj_set_size(touchCalTarget,50,50);
  lv_obj_set_style_radius(touchCalTarget,LV_RADIUS_CIRCLE,0);
  lv_obj_set_style_bg_opa(touchCalTarget,LV_OPA_TRANSP,0);
  lv_obj_set_style_border_width(touchCalTarget,5,0);
  lv_obj_set_style_border_color(touchCalTarget,lv_color_hex(0xFF3B30),0);
  lv_obj_t *cross=lv_label_create(touchCalTarget);
  lv_label_set_text(cross,"+");
  lv_obj_set_style_text_color(cross,lv_color_hex(0xFF3B30),0);
  lv_obj_set_style_text_font(cross,&lv_font_montserrat_28,0);
  lv_obj_center(cross);

  showTouchCalibrationTarget();
  lv_obj_move_foreground(touchCalOverlay);
}

void processTouchCalibration(){
  if(!touchCalibrationActive || !touchRawTapReady) return;
  touchRawTapReady=false;
  touchCalRawX[touchCalibrationStep]=touchRawTapX;
  touchCalRawY[touchCalibrationStep]=touchRawTapY;
  Serial.printf("Touch calibration %u raw=%u,%u\n",touchCalibrationStep+1,touchRawTapX,touchRawTapY);
  touchCalibrationStep++;
  if(touchCalibrationStep<3){
    showTouchCalibrationTarget();
    return;
  }

  uint8_t bestMap=0;
  uint32_t bestError=UINT32_MAX;
  for(uint8_t candidate=0;candidate<8;candidate++){
    uint32_t error=0;
    TouchHardwareTransform candidateHardware=touchHardwareFromMapId(candidate);
    for(uint8_t i=0;i<3;i++){
      uint16_t x=0,y=0;
      mapTouchPoint(touchCalRawX[i],touchCalRawY[i],x,y,candidateHardware,screenOrientation);
      int32_t dx=(int32_t)x-TOUCH_CAL_X[i];
      int32_t dy=(int32_t)y-TOUCH_CAL_Y[i];
      error+=(uint32_t)(dx*dx+dy*dy);
    }
    if(error<bestError){ bestError=error; bestMap=candidate; }
  }
  touchHardware=touchHardwareFromMapId(bestMap);
  preferences.putBool("touch-swap",touchHardware.swapXY);
  preferences.putBool("touch-mx",touchHardware.mirrorX);
  preferences.putBool("touch-my",touchHardware.mirrorY);
  preferences.putUChar("touch-map-v",TOUCH_MAPPING_VERSION);
  preferences.putUChar("touch-cal-v",TOUCH_CALIBRATION_VERSION);
  touchCalibrationNeeded=false;
  touchCalibrationActive=false;
  Serial.printf("Touch calibration complete swapXY=%u mirrorX=%u mirrorY=%u error=%lu\n",
                touchHardware.swapXY,touchHardware.mirrorX,touchHardware.mirrorY,(unsigned long)bestError);

  if(touchCalOverlay){
    lv_obj_del(touchCalOverlay);
    touchCalOverlay=nullptr;
    touchCalTarget=nullptr;
    touchCalStepLabel=nullptr;
  }
  lastButton=millis();
  updateView();
}

#endif

void setup(){
  Serial.begin(115200);
  Serial.printf("\nBattery Display %s starting\n",FW_VERSION);
  Serial.printf("Hardware memory: flash %u MB, PSRAM %u MB (%s)\n",
    (unsigned)(ESP.getFlashChipSize()/(1024U*1024U)),
    (unsigned)(ESP.getPsramSize()/(1024U*1024U)),
    psramFound()?"ready":"NOT FOUND");

  preferences.begin("battery-ui",false);
  settingsMutex=xSemaphoreCreateMutex();
  // One-time migration for this already configured unit. Fresh client units
  // have empty factory values and immediately expose the protected setup AP.
  if(!preferences.isKey("ssid") && strlen(FACTORY_WIFI_SSID)){
    preferences.putString("ssid",FACTORY_WIFI_SSID);
    preferences.putString("password",FACTORY_WIFI_PASSWORD);
  }
  wifiSsid=preferences.getString("ssid","");
  wifiPassword=preferences.getString("password","");
  emulatorHost=preferences.getString("emu-host",DEFAULT_EMULATOR_HOST);
  emulatorHost.trim();
  bool hostValid=emulatorHost.length()>0 && emulatorHost.length()<=64;
  for(size_t i=0;i<emulatorHost.length();i++) {
    uint8_t ch=(uint8_t)emulatorHost[i];
    if(ch<33 || ch>126) { hostValid=false; break; }
  }
  uint8_t stationMac[6]={};
  esp_read_mac(stationMac,ESP_MAC_WIFI_STA);
  char displayMacText[18];
  snprintf(displayMacText,sizeof(displayMacText),"%02X:%02X:%02X:%02X:%02X:%02X",stationMac[0],stationMac[1],stationMac[2],stationMac[3],stationMac[4],stationMac[5]);
  String displayMac=displayMacText;
  bool ownerDisplay=strlen(OWNER_DISPLAY_MAC)>0 && displayMac==OWNER_DISPLAY_MAC;
  if(ownerDisplay && !hostValid && strlen(OWNER_EMULATOR_HOST)>0) {
    emulatorHost=OWNER_EMULATOR_HOST;
    preferences.putString("emu-host",emulatorHost);
    hostValid=true;
  } else if(!hostValid) {
    emulatorHost="";
    preferences.remove("emu-host");
  }
  if(preferences.isKey("conn-mode")){
    directMode=preferences.getUChar("conn-mode",0)==1;
  } else {
    directMode=preferences.getBool("direct",false);
    // Earlier builds could save valid router credentials while leaving the
    // legacy direct flag set. A populated SSID is evidence of the requested
    // Wi-Fi setup, so migrate this unit into network mode once.
    if(wifiSsid.length()) directMode=false;
    saveConnectionMode(directMode);
  }
  espNowChannel=preferences.getUChar("esp-channel",DEFAULT_ESPNOW_CHANNEL);
  if(espNowChannel<1 || espNowChannel>13) espNowChannel=DEFAULT_ESPNOW_CHANNEL;
  page=preferences.getUChar("page",0);
#if defined(TARGET_4848S040)
  if(page>5) page=0;
  onboardingNeeded=!preferences.getBool("ui13done",false);
  const uint8_t savedTouchMappingVersion=preferences.getUChar("touch-map-v",0);
  const uint8_t savedTouchCalibrationVersion=preferences.getUChar("touch-cal-v",0);
  touchHardware.swapXY=preferences.getBool("touch-swap",false);
  touchHardware.mirrorX=preferences.getBool("touch-mx",true);
  touchHardware.mirrorY=preferences.getBool("touch-my",true);
  touchCalibrationNeeded=!preferences.isKey("touch-swap") ||
                         !preferences.isKey("touch-mx") ||
                         !preferences.isKey("touch-my") ||
                         savedTouchCalibrationVersion!=TOUCH_CALIBRATION_VERSION ||
                         savedTouchMappingVersion!=TOUCH_MAPPING_VERSION;
  screenOrientation=preferences.getUChar("screen-rot",0);
  if(screenOrientation>3) screenOrientation=0;
  Serial.printf("Touch mapping swapXY=%u mirrorX=%u mirrorY=%u stored_version=%u current_version=%u calibration=%s\n",
                touchHardware.swapXY,touchHardware.mirrorX,touchHardware.mirrorY,
                savedTouchMappingVersion,TOUCH_MAPPING_VERSION,
                touchCalibrationNeeded?"required":"ready");
#else
  if(page>4) page=0;
#endif
  brightness=DEFAULT_BRIGHTNESS;
  colorTheme=preferences.getUChar("theme",0);
  if(colorTheme>3) colorTheme=0;
  showSocBadge=preferences.getBool("soc-badge",true);
  applyThemePalette(colorTheme);

  if(PIN_POWER>=0){
    pinMode(PIN_POWER,OUTPUT);
    digitalWrite(PIN_POWER,HIGH);
  }

  pinMode(PIN_BL,OUTPUT);
  digitalWrite(PIN_BL,LOW);
#if !defined(TARGET_4848S040)
  ledcSetup(BL_CHANNEL,5000,8);
  ledcAttachPin(PIN_BL,BL_CHANNEL);
#endif

  if(BTN_LEFT>=0) pinMode(BTN_LEFT,INPUT_PULLUP);
  if(BTN_RIGHT>=0) pinMode(BTN_RIGHT,INPUT_PULLUP);
#if defined(TARGET_4848S040)
  Wire.begin(TOUCH_SDA,TOUCH_SCL);
  Wire.setClock(400000);
#endif

  gfx->begin();
#if defined(TARGET_4848S040)
  gfx->setRotation((1+screenOrientation)%4);
#else
  gfx->setRotation(1);
#endif
  gfx->fillScreen(BLACK);
  applyBrightness(brightness);

  lv_init();
  lv_disp_draw_buf_init(&drawBuf,buf1,NULL,SCREEN_W*32);

  static lv_disp_drv_t disp;
  lv_disp_drv_init(&disp);
  disp.hor_res=SCREEN_W;
  disp.ver_res=SCREEN_H;
  disp.flush_cb=lvFlush;
  disp.draw_buf=&drawBuf;
  lv_disp_drv_register(&disp);

#if defined(TARGET_4848S040)
  static lv_indev_drv_t indev;
  lv_indev_drv_init(&indev);
  indev.type=LV_INDEV_TYPE_POINTER;
  indev.read_cb=lvTouchRead;
  indev.gesture_limit=50;
  indev.gesture_min_velocity=3;
  lv_indev_drv_register(&indev);
#endif

  dataMutex=xSemaphoreCreateMutex();
  espNowQueue=xQueueCreate(6,sizeof(EspNowPacket));
  buildUI();

#if defined(TARGET_4848S040)
  if(touchCalibrationNeeded) startTouchCalibration();
#endif

  if(onboardingNeeded){
    startConfigPortal();
  } else if(directMode){
    startDirectLink();
  } else {
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);
    if(wifiSsid.length()) WiFi.begin(wifiSsid.c_str(),wifiPassword.c_str());

    uint32_t start=millis();
    while(wifiSsid.length() && WiFi.status()!=WL_CONNECTED && millis()-start<15000){
      delay(50);
      lv_tick_inc(50);
      lv_timer_handler();
    }

    if(WiFi.status()!=WL_CONNECTED) startConfigPortal();
    else {
      startEspNow(false);
      startControlServer();
    }
  }

  lastButton=millis();
  lastTick=millis();
  esp_task_wdt_init(8,true);
  esp_task_wdt_add(NULL);
  updateView();
  xTaskCreatePinnedToCore(networkTask,"battery-link",12288,NULL,1,NULL,0);
}

void loop(){
  uint32_t now=millis();
  uint32_t dt=now-lastTick;
  if(dt){
    lv_tick_inc(dt);
    lastTick=now;
  }

  buttons();
#if defined(TARGET_4848S040)
  processTouchCalibration();
  ui480Animate(now);
#endif

  static uint32_t lastUiRefresh=0;
  if(uiDirty && now-lastUiRefresh>=100){
    uiDirty=false;
#if defined(TARGET_4848S040)
    bool wizardVisible=uWizard && !lv_obj_has_flag(uWizard,LV_OBJ_FLAG_HIDDEN);
    if(!wizardVisible) updateView();
#else
    updateView();
#endif
    lastUiRefresh=now;
  }

  // Keeps LVGL animations/rendering responsive and flicker-free.
  lv_timer_handler();
  esp_task_wdt_reset();
  delay(5);
}
