#pragma once
// ============================================================================
//  config.h — every value you are likely to want to change lives here.
// ============================================================================
// ---------------------------------------------------------------- WiFi -----
#define WIFI_SSID       "moon"
#define WIFI_PASSWORD   "kosongtujuh"

// ------------------------------------------------------------ Telegram ----
// Phone notifications are sent through a Telegram bot (works even if nobody
// has the website open). Create a bot with @BotFather, then message
// @userinfobot to get your numeric chat id.
#define TELEGRAM_ENABLED     true
#define TELEGRAM_BOT_TOKEN   "8929898243:AAE_l4kxrwc-241SJGGp3-cykEQA1mvfgDc"
#define TELEGRAM_CHAT_ID     "6801556690"

// ------------------------------------------------------------- Time -------
// Bandung, Indonesia = UTC+7, no DST.
#define GMT_OFFSET_SEC       (7 * 3600)
#define DAYLIGHT_OFFSET_SEC  0
#define NTP_SERVER_1         "pool.ntp.org"
#define NTP_SERVER_2         "time.nist.gov"

// -------------------------------------------------------------- Pins ------
// I2C bus (TCS34725 color sensor + BH1750 light sensor)
#define PIN_I2C_SDA          8
#define PIN_I2C_SCL          9

// DS18B20 waterproof temperature probe (OneWire)
#define PIN_ONEWIRE_DATA     4      // needs a 4.7k pull-up to 3.3V, see wiring guide

// MOSFET #1 (IRLB8721) — switches Peltier TEC-12706 + heatsink fan TOGETHER
#define PIN_MOSFET_COOLING   5      // digital ON/OFF only

// MOSFET #2 (IRLB8721) — switches / PWM-dims the HPL 3W grow LED
#define PIN_MOSFET_LED       6      // PWM (LEDC)

// Onboard status LED (optional, purely for a heartbeat blink)
#define PIN_STATUS_LED       2

// ---------------------------------------------------- Control thresholds --
// Temperature (°C) hysteresis for the Peltier cooling loop
#define TEMP_COOL_ON_C        35.0f
#define TEMP_COOL_OFF_C       30.0f

// Lux level above which we consider the algae to be getting "real" sunlight.
// Direct sun is 30,000-100,000 lux, open shade / bright overcast is
// 1,000-10,000 lux. Start at 2000 and tune with your own BH1750 readings
// logged over a few days.
#define DAYLIGHT_LUX_THRESHOLD   2000.0f

// Target total hours of light (natural + grow LED) per day
#define TARGET_LIGHT_HOURS       16.0f

// Grow LED brightness when it IS on, as a duty-cycle percentage (0-100).
// Adjustable at runtime from the website too.
#define LED_DEFAULT_BRIGHTNESS_PCT   80

// Algae health heuristic (TCS34725). These are STARTING POINTS — print
// gRatio/saturation to Serial with a known-fresh sample and a known-pale
// sample, then tighten these numbers for your specific algae/water/chamber.
#define ALGAE_GREEN_RATIO_MIN     0.38f   // g / (r+g+b)
#define ALGAE_SATURATION_MIN      0.15f   // (max-min)/max of r,g,b

// Nutrient dosing reminder interval
#define NUTRIENT_INTERVAL_DAYS    7

// Sensor loop / broadcast period
#define LOOP_INTERVAL_MS          2000UL
// How often a temperature point is pushed onto the history buffer for the graph
#define HISTORY_SAMPLE_MS         30000UL
#define HISTORY_MAX_POINTS        200
