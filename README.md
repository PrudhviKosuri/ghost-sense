# Ghost Sense

Camera-free motion detection with an ESP32 and Wi-Fi signal strength, built toward an LLM-queryable "room sensor" (MCP) layer.

**Status: working prototype.** Detects a person moving across the router-to-ESP32 link. LLM/MCP layer is in progress.

## How it works

1. A Mac runs `tools/sender.py`, which sends a steady UDP stream (~90 packets/s) to the ESP32 through the router.
2. The ESP32 receives the router's frames through the Wi-Fi CSI callback and records the signal strength (RSSI) of each one.
3. Every second it computes the mean and standard deviation of RSSI, compares the mean to a slowly adapting baseline, and flags the second as "motion" if the level moves more than 1 dB or wobbles more than 1.8 dB.
4. Motion is declared when at least 2 of the last 5 seconds are flagged. The result is shown on the OLED.

## Hardware

- OceanLabz ESP32 AI starter kit (classic ESP32-D0WD-V3, 30-pin)
- 0.96" SSD1306 OLED (I2C: SDA = GPIO 21, SCL = GPIO 22)
- A 2.4 GHz Wi-Fi router and a laptop to run the sender

## Setup

1. Install Arduino IDE 2.x and the "esp32 by Espressif Systems" board package.
2. Install the libraries Adafruit SSD1306 and Adafruit GFX.
3. Copy `firmware/ghost_sense/secrets.example.h` to `secrets.h` and enter your Wi-Fi details.
4. Select board "ESP32 Dev Module", upload `firmware/ghost_sense/ghost_sense.ino` (use upload speed 115200 if uploads fail).
5. Open the Serial Monitor at 115200 baud and note the ESP32's IP address.
6. On your computer, on the same Wi-Fi: `python3 tools/sender.py <ESP32_IP>`
7. Stay still for the first ~10 s (baseline learning), then walk across the line between the router and the ESP32.

## Results (single run, ~12 ft link, plastic table in between)

| State | RSSI std dev (1 s) | Deviation from baseline |
|---|---|---|
| Still | under 1.0 dB | under 0.25 dB |
| Walking across the link | 3 to 4.4 dB | up to 4.6 dB |

- About 40 s of stillness: 0 false alarms.
- About 40 s of walking: held MOTION, with 2 single-second dropouts that the 2-of-5 rule mostly smoothed over.
- Returned to CLEAR about 3 s after stopping.

This is one run, so it is a first estimate and not a benchmark. A proper test (repeated still and walking runs, hit and miss counts) is planned.

## What I tried first (engineering log)

- **Pinging the router from the ESP32** to generate CSI: the packet rate collapsed after about 20 s with `ping_sock: send error` messages. Replaced by a UDP stream from the laptop, which gave a steady ~85 packets/s.
- **CSI amplitude-change score** (mean absolute change between consecutive packets, normalized): it did not separate still from moving reliably. It stayed at a high plateau for reasons I could not explain, including in an empty room.
- **Suspected Wi-Fi mode switching**: logged sig_mode, MCS, bandwidth and noise floor. They were nearly constant, so this was not the cause.
- **RSSI deviation from a slow baseline** worked, and is the current detector.

## Limitations

- Uses RSSI (signal strength), not full CSI features, so it is coarse.
- Detects movement crossing the link. It does not detect a person sitting perfectly still.
- Needs a second device running `sender.py`.
- Thresholds (1 dB, 1.8 dB) were tuned on one room and one layout.
- Classic ESP32 only (no PSRAM), so on-device LLMs are not feasible.

## Roadmap

- [ ] `/status` JSON endpoint on the ESP32 with motion history
- [ ] Python MCP server so an LLM agent can ask "is anyone in the room?"
- [ ] Repeated accuracy measurements
- [ ] Better CSI features and a small trained classifier
- [ ] Demo video and architecture diagram
