# PhotoSyntheAir - Algae-Based Smart Air Purifier
A smart hybrid bio-mechanical system for advanced air remediation that could turn toxic gas emissions into clearer air!

![Description of image](assets/banner.jpeg)

[Check out the demo vid!]()

## Features

## Background

## How it works

## Repository Structure
`/cad` 
`/src` 
`/firmware`

## Bill of Materials (BOM)

## Hardware

## Schematic

## 3D Models

## Build Instruction

## Firmware setup
1. Open this folder in VS Code with the PlatformIO extension installed.
2. Edit `include/config.h`:
   - `WIFI_SSID`
   - `TELEGRAM_BOT_TOKEN` / `TELEGRAM_CHAT_ID` (see below)
   - Tune `ALGAE_GREEN_RATIO_MIN` / `ALGAE_SATURATION_MIN` / `DAYLIGHT_LUX_THRESHOLD` after a few days of real sensor logging (print raw values to Serial first).
3. **Upload the filesystem first** (PlatformIO sidebar → project tasks → your env → Platform → "Upload Filesystem Image"), so `data/index.html`, `style.css`, `script.js` land on LittleFS.
4. Then build & upload the firmware normally (Upload button / `pio run --target upload`).
5. Open the Serial Monitor at 115200 baud to see the assigned IP address, then browse to `http://<that-ip>/` from any device on the same network for the live dashboard.

### Telegram bot setup (phone notifications)
1. Message **@BotFather** on Telegram `/newbot`, copy the token into `TELEGRAM_BOT_TOKEN`.
2. Message **@userinfobot** to get **your** numeric chat ID, put it in `TELEGRAM_CHAT_ID`.
3. Send your new bot any message for testing (telegram requires the user to message the bot first before it can message back).
4. After that, you'll get a message every time the algae turns pale.

## Notes
- IRLB8721 is a **logic-level** MOSFET (fully on around 4.5V Vgs), so driving it straight from a 3.3V GPIO (through R2) is fine, no gate driver IC needed.
- The peltier alone can pull ~5A; make sure MOSFET1 has a small heatsink if it runs long stretches at that current and use wire gauge rated for 5A+ on the drain/source path.

## Common Mistakes

## Credits







The website also asks the browser for notification permission and will pop an in-browser notification for the same two events whenever the dashboard tab is open — Telegram is the one that reaches your phone even when the site is closed.
