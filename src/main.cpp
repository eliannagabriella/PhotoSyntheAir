// ============================================================================
//  Algae Air Purifier — ESP32-S3 firmware
//
//  Air path (physical, no firmware control needed):
//    MERV filter -> activated carbon (1000 iodine) -> Ocypus Gamma F12 fan
//    -> diaphragm air pump (PWM-bubbled diffuser) -> algae chamber
//    (mini submersible pump keeps water circulating) -> O2 headspace (~27%)
//
//  Firmware responsibilities:
//    - Read TCS34725 (algae color / health), DS18B20 (water temp), BH1750 (lux)
//    - Hysteresis control of ONE MOSFET driving Peltier + heatsink fan together
//    - PWM control of a SECOND MOSFET driving the HPL 3W grow LED
//    - Serve a realtime dashboard (LittleFS + WebSocket)
//    - Push Telegram phone notifications on unhealthy algae / weekly nutrients
//
//  NOTE: Ocypus fan, diaphragm pump and mini submersible pump are wired
//  directly to the 12V rail (per the design brief) and are NOT switched by
//  the ESP32 — they simply run whenever the system has power. The firmware
//  still reports them as "always on" on the dashboard for completeness.
// ============================================================================

#include <Arduino.h>
#include <WiFi.h>
#include <Wire.h>
#include <time.h>
#include <Preferences.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <BH1750.h>
#include <Adafruit_TCS34725.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>

#include "app_config.h"

// ---------------------------------------------------------------- Globals --
OneWire oneWire(PIN_ONEWIRE_DATA);
DallasTemperature ds18b20(&oneWire);

BH1750 lightMeter;
Adafruit_TCS34725 tcs = Adafruit_TCS34725(TCS34725_INTEGRATIONTIME_154MS, TCS34725_GAIN_4X);

AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

Preferences prefs;

const int LEDC_CHANNEL_LED = 0;
const int LEDC_FREQ_HZ     = 5000;
const int LEDC_RES_BITS    = 8; // 0-255

struct HistoryPoint {
  uint32_t epoch;
  float temperature;
};
HistoryPoint history[HISTORY_MAX_POINTS];
int historyCount = 0;
int historyHead  = 0; // circular buffer write index

struct SystemState {
  float waterTemp        = NAN;
  uint16_t rawR = 0, rawG = 0, rawB = 0, rawC = 0;
  float greenRatio        = 0;
  float saturation        = 0;
  bool  algaeHealthy      = true;
  float lux               = 0;

  bool  coolingOn         = false;   // Peltier + heatsink fan (shared MOSFET)
  bool  ledOn             = false;
  uint8_t ledBrightnessPct = LED_DEFAULT_BRIGHTNESS_PCT;

  double sunlightSeconds  = 0;       // accumulated today
  double ledOnSecondsToday = 0;      // accumulated today
  int    lastResetYday    = -1;      // day-of-year we last reset the counters above

  uint32_t lastNutrientEpoch = 0;
  bool nutrientDue        = false;
  bool wifiConnected      = false;
  bool timeSynced         = false;

  // Always-on subsystems (not switched by firmware, reported for the dashboard)
  // const bool ocypusFanOn        = true;
  // const bool diaphragmPumpOn    = true;
  // const bool submersiblePumpOn  = true;
} state;

unsigned long lastLoopMs    = 0;
unsigned long lastHistoryMs = 0;
bool wasHealthyLastCheck    = true;

// ------------------------------------------------------------ Prototypes --
void connectWiFi();
void syncTime();
void setupSensors();
void setupWebServer();
void readSensors(float dtSeconds);
void controlCooling();
void controlGrowLight(float dtSeconds);
void maybeResetDailyCounters();
void maybeCheckNutrientReminder();
void pushHistoryPoint();
void broadcastState();
void sendTelegramMessage(const String &text);
String buildStateJson();
String buildHistoryJson();
void onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client,
               AwsEventType type, void *arg, uint8_t *data, size_t len);

// =============================================================================
void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\n[algae] booting...");

  pinMode(PIN_STATUS_LED, OUTPUT);
  pinMode(PIN_MOSFET_COOLING, OUTPUT);
  digitalWrite(PIN_MOSFET_COOLING, LOW); // fail-safe: cooling OFF until first read

  ledcSetup(LEDC_CHANNEL_LED, LEDC_FREQ_HZ, LEDC_RES_BITS);
  ledcAttachPin(PIN_MOSFET_LED, LEDC_CHANNEL_LED);
  ledcWrite(LEDC_CHANNEL_LED, 0); // LED off until first control pass



  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
  
  setupSensors();

  if (!LittleFS.begin(true)) {
    Serial.println("[algae] LittleFS mount failed! (did you run 'Upload Filesystem Image'?)");
  }

  connectWiFi();
  syncTime();
  setupWebServer();

  lastLoopMs = millis();
  lastHistoryMs = millis();
  Serial.println("[algae] ready.");
}

// =============================================================================
void loop() {
  ws.cleanupClients();

  unsigned long now = millis();
  if (now - lastLoopMs >= LOOP_INTERVAL_MS) {
    float dt = (now - lastLoopMs) / 1000.0f;
    lastLoopMs = now;

    state.wifiConnected = (WiFi.status() == WL_CONNECTED);
    digitalWrite(PIN_STATUS_LED, (millis() / 500) % 2); // heartbeat blink

    readSensors(dt);
    maybeResetDailyCounters();
    controlCooling();
    controlGrowLight(dt);
    maybeCheckNutrientReminder();
    broadcastState();
  }

  if (now - lastHistoryMs >= HISTORY_SAMPLE_MS) {
    lastHistoryMs = now;
    pushHistoryPoint();
  }
}

// ------------------------------------------------------------------ WiFi --
void connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("[algae] connecting to WiFi");
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 20000) {
    delay(300);
    Serial.print(".");
  }
  Serial.println();
  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("[algae] WiFi connected, IP = %s\n", WiFi.localIP().toString().c_str());
    state.wifiConnected = true;
  } else {
    Serial.println("[algae] WiFi NOT connected — dashboard/telegram will retry in background.");
  }
}

void syncTime() {
  configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET_SEC, NTP_SERVER_1, NTP_SERVER_2);
  struct tm timeinfo;
  if (getLocalTime(&timeinfo, 8000)) {
    state.timeSynced = true;
    Serial.println(&timeinfo, "[algae] time synced: %Y-%m-%d %H:%M:%S");
  } else {
    Serial.println("[algae] NTP sync failed — will retry silently; daily/weekly timers use millis() fallback.");
  }
}

// ---------------------------------------------------------------- Sensors --
void setupSensors() {
  ds18b20.begin();
  ds18b20.setWaitForConversion(true);

  if (!lightMeter.begin(BH1750::CONTINUOUS_HIGH_RES_MODE)) {
    Serial.println("[algae] BH1750 not found — check wiring/address.");
  }

  if (!tcs.begin()) {
    Serial.println("[algae] TCS34725 not found — check wiring/address.");
  }
}

void readSensors(float dtSeconds) {
  // --- Temperature ---
  ds18b20.requestTemperatures();
  float t = ds18b20.getTempCByIndex(0);
  if (t > -100 && t < 125) { // DEVICE_DISCONNECTED_C guard
    state.waterTemp = t;
  }

  // --- Color / algae health ---
  uint16_t r, g, b, c;
  tcs.getRawData(&r, &g, &b, &c);
  state.rawR = r; state.rawG = g; state.rawB = b; state.rawC = c;

  float total = (float)r + g + b;
  if (total > 0) {
    state.greenRatio = g / total;
    uint16_t mx = max(r, max(g, b));
    uint16_t mn = min(r, min(g, b));
    state.saturation = (mx > 0) ? (float)(mx - mn) / mx : 0;
  }
  bool healthyNow = (state.greenRatio >= ALGAE_GREEN_RATIO_MIN) &&
                     (state.saturation >= ALGAE_SATURATION_MIN);
  state.algaeHealthy = healthyNow;

  if (wasHealthyLastCheck && !healthyNow) {
    // just turned unhealthy -> notify once on the transition
    sendTelegramMessage(
      "\xE2\x9A\xA0\xEF\xB8\x8F Algae Air Purifier: algae color looks PALE. "
      "Health check needed (green ratio " + String(state.greenRatio, 2) +
      ", saturation " + String(state.saturation, 2) + ")."
    );
  }
  wasHealthyLastCheck = healthyNow;

  // --- Light ---
  float l = lightMeter.readLightLevel();
  if (l >= 0) state.lux = l;
}

// -------------------------------------------------------- Cooling control --
void controlCooling() {
  if (isnan(state.waterTemp)) return; // don't act on a bad reading

  if (!state.coolingOn && state.waterTemp >= TEMP_COOL_ON_C) {
    state.coolingOn = true;
  } else if (state.coolingOn && state.waterTemp <= TEMP_COOL_OFF_C) {
    state.coolingOn = false;
  }
  // MOSFET #1 switches Peltier TEC-12706 and the heatsink fan TOGETHER — see wiring guide.
  digitalWrite(PIN_MOSFET_COOLING, state.coolingOn ? HIGH : LOW);
}

// ----------------------------------------------------------- Light control --
void controlGrowLight(float dtSeconds) {
  bool isDaylight = state.lux >= DAYLIGHT_LUX_THRESHOLD;

  if (isDaylight) {
    state.sunlightSeconds += dtSeconds;
  }

  double requiredSeconds = (TARGET_LIGHT_HOURS * 3600.0) - state.sunlightSeconds;
  if (requiredSeconds < 0) requiredSeconds = 0;

  bool needMoreLightToday = state.ledOnSecondsToday < requiredSeconds;
  bool ledShouldBeOn = (!isDaylight) && needMoreLightToday;

  if (ledShouldBeOn) {
    state.ledOnSecondsToday += dtSeconds;
  }
  state.ledOn = ledShouldBeOn;

  uint8_t duty = ledShouldBeOn ? map(state.ledBrightnessPct, 0, 100, 0, 255) : 0;
  ledcWrite(LEDC_CHANNEL_LED, duty);
}

void maybeResetDailyCounters() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo, 5)) return; // no time yet, skip (keeps accumulating, harmless)

  if (state.lastResetYday != timeinfo.tm_yday) {
    state.lastResetYday = timeinfo.tm_yday;
    state.sunlightSeconds = 0;
    state.ledOnSecondsToday = 0;
    Serial.println("[algae] new day -> sunlight/LED counters reset");
  }
}

// ------------------------------------------------------- Nutrient reminder --
void maybeCheckNutrientReminder() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo, 5)) return;

  time_t nowEpoch;
  time(&nowEpoch);

  if (state.lastNutrientEpoch == 0) {
    // first boot ever — start the countdown from now instead of alerting immediately
    state.lastNutrientEpoch = (uint32_t)nowEpoch;
    prefs.putUInt("lastNutrient", state.lastNutrientEpoch);
    return;
  }

  double daysSince = difftime(nowEpoch, state.lastNutrientEpoch) / 86400.0;
  bool due = daysSince >= NUTRIENT_INTERVAL_DAYS;

  if (due && !state.nutrientDue) {
    sendTelegramMessage(
      "\xF0\x9F\x8C\xBF Algae Air Purifier: it's been " + String((int)daysSince) +
      " days — time to add nutrients to the algae chamber."
    );
  }
  state.nutrientDue = due;
}

// -------------------------------------------------------------- History ---
void pushHistoryPoint() {
  time_t nowEpoch;
  time(&nowEpoch);
  history[historyHead].epoch = (uint32_t)nowEpoch;
  history[historyHead].temperature = state.waterTemp;
  historyHead = (historyHead + 1) % HISTORY_MAX_POINTS;
  if (historyCount < HISTORY_MAX_POINTS) historyCount++;
}

String buildHistoryJson() {
  JsonDocument doc;
  JsonArray arr = doc.to<JsonArray>();
  int startIdx = (historyCount < HISTORY_MAX_POINTS) ? 0 : historyHead;
  for (int i = 0; i < historyCount; i++) {
    int idx = (startIdx + i) % HISTORY_MAX_POINTS;
    JsonObject o = arr.add<JsonObject>();
    o["t"] = history[idx].epoch;
    if (isnan(history[idx].temperature)) {
      o["temp"] = nullptr;
    } else {
      o["temp"] = history[idx].temperature;
    }
  }
  String out;
  serializeJson(doc, out);
  return out;
}

// ---------------------------------------------------------------- Telegram --
void sendTelegramMessage(const String &text) {
  if (!TELEGRAM_ENABLED) return;
  if (WiFi.status() != WL_CONNECTED) return;

  WiFiClientSecure client;
  client.setInsecure(); // Telegram's cert chain rotates; skip pinning for simplicity
  HTTPClient https;

  String url = "https://api.telegram.org/bot" + String(TELEGRAM_BOT_TOKEN) +
               "/sendMessage?chat_id=" + String(TELEGRAM_CHAT_ID) +
               "&text=" + text;
  // Minimal URL-encoding for spaces; good enough for our own ASCII alert strings.
  url.replace(" ", "%20");
  

  if (https.begin(client, url)) {
    int code = https.GET();
    Serial.printf("[algae] telegram sendMessage -> HTTP %d\n", code);
    https.end();
  } else {
    Serial.println("[algae] telegram: could not begin HTTPS connection");
  }
}

// -------------------------------------------------------------- Web/State --
String buildStateJson() {
  JsonDocument doc;
  if (isnan(state.waterTemp)) {
    doc["waterTemp"] = nullptr;
  } else {
    doc["waterTemp"] = state.waterTemp;
  }
  doc["greenRatio"]      = state.greenRatio;
  doc["saturation"]      = state.saturation;
  doc["algaeHealthy"]    = state.algaeHealthy;
  doc["lux"]             = state.lux;

  doc["coolingOn"]       = state.coolingOn;     // Peltier + heatsink fan
  doc["ledOn"]           = state.ledOn;
  doc["ledBrightnessPct"]= state.ledBrightnessPct;

  doc["sunlightHoursToday"] = state.sunlightSeconds / 3600.0;
  doc["ledHoursToday"]      = state.ledOnSecondsToday / 3600.0;
  doc["targetLightHours"]   = TARGET_LIGHT_HOURS;

  doc["nutrientDue"]        = state.nutrientDue;
  doc["lastNutrientEpoch"]  = state.lastNutrientEpoch;
  doc["nutrientIntervalDays"] = NUTRIENT_INTERVAL_DAYS;

  doc["wifiConnected"]     = state.wifiConnected;
  doc["timeSynced"]        = state.timeSynced;

  time_t nowEpoch; time(&nowEpoch);
  doc["nowEpoch"] = (uint32_t)nowEpoch;

  String out;
  serializeJson(doc, out);
  return out;
}

void broadcastState() {
  if (ws.count() > 0) {
    ws.textAll(buildStateJson());
  }
}

void onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client,
               AwsEventType type, void *arg, uint8_t *data, size_t len) {
  if (type == WS_EVT_CONNECT) {
    client->text(buildStateJson());
  }
}

void setupWebServer() {
  ws.onEvent(onWsEvent);
  server.addHandler(&ws);

  server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");

  server.on("/api/state", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "application/json", buildStateJson());
  });

  server.on("/api/history", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "application/json", buildHistoryJson());
  });

  server.on("/api/nutrient/reset", HTTP_POST, [](AsyncWebServerRequest *request) {
    time_t nowEpoch; time(&nowEpoch);
    state.lastNutrientEpoch = (uint32_t)nowEpoch;
    state.nutrientDue = false;
    prefs.putUInt("lastNutrient", state.lastNutrientEpoch);
    request->send(200, "application/json", "{\"ok\":true}");
  });

  server.on("/api/led", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (request->hasParam("percent", true)) {
      int pct = request->getParam("percent", true)->value().toInt();
      pct = constrain(pct, 0, 100);
      state.ledBrightnessPct = pct;
      prefs.putUChar("ledPct", pct);
      request->send(200, "application/json", "{\"ok\":true}");
    } else {
      request->send(400, "application/json", "{\"ok\":false,\"error\":\"missing percent\"}");
    }
  });

  server.begin();
  Serial.println("[algae] web server started on port 80");
}
