# GPS Position Logger & Multi-Protocol Range Tester

An ESP32 embedded system that logs GPS position, monitors system power draw in real time, and compares the effective range of four wireless protocols simultaneously — ESP-NOW, WiFi, BLE, and LoRa. Built on ESP-IDF with FreeRTOS using a layered, power-aware architecture designed for long field sessions on a LiPo battery.

> **Portfolio project** demonstrating embedded systems design across firmware architecture, hardware interfacing, RTOS task management, power management, and RF protocol integration.

---

## Features

| Category | Detail |
|---|---|
| **GPS** | NEO-M8N, NMEA GGA/RMC parsing, fix detection, 1 Hz display |
| **Display** | SSD1306 128×64 OLED, 3 cycling screens, 2 Hz refresh |
| **Power monitor** | INA226 shunt monitor — real-time mA / V / mW |
| **Battery** | LiPo SoC estimation via voltage LUT, low/critical warnings |
| **ESP-NOW** | Broadcast position every 1 s, peer ranging, RSSI |
| **WiFi** | Passive AP scan every 10 s, RSSI, AP count |
| **BLE** | Passive GAP scan, device count, best RSSI |
| **LoRa** | SX1276 (LR02), SF7/BW125/CR4-5, +17 dBm, >2 km LOS |
| **SD logging** | FAT CSV, 1 row/s, auto-flush, range-boundary tracking |
| **Power modes** | ACTIVE 240 MHz → BALANCED 160 MHz → LOW\_POWER 80 MHz |
| **State machine** | BOOT → LIGHT\_SLEEP → ACTIVE → RANGING, event-driven |

---

## Hardware

| Component | Interface | Notes |
|---|---|---|
| ESP32 (dual-core Xtensa) | — | Main MCU |
| u-blox NEO-M8N GPS | UART2 | RX=GPIO16, TX=GPIO17, 9600 baud |
| SSD1306 128×64 OLED | I2C @ 0x3C | Single-display, screen cycling |
| INA226 current monitor | I2C @ 0x40 | 100 mΩ shunt, 3.2 A max |
| LR02 LoRa module (SX1276) | SPI (HSPI) | CS=GPIO5, RST=GPIO2, DIO0=GPIO4 |
| Micro SD card module | SPI (shared) | CS=GPIO15 |
| 3 tactile buttons | GPIO | GPIO32 / 33 / 27 |
| 18650 LiPo cell | — | Via INA226 shunt for total current |

### Wiring Summary

```
ESP32 GPIO21 ──────── SDA ──── SSD1306 + INA226
ESP32 GPIO22 ──────── SCL ──── SSD1306 + INA226

ESP32 GPIO16 ──────── RX  ←── NEO-M8N TX
ESP32 GPIO17 ──────── TX  ──► NEO-M8N RX (config only)

ESP32 GPIO23 ──────── MOSI ─── LR02 + SD card
ESP32 GPIO19 ──────── MISO ─── LR02 + SD card
ESP32 GPIO18 ──────── SCK  ─── LR02 + SD card
ESP32 GPIO5  ──────── CS   ─── LR02
ESP32 GPIO15 ──────── CS   ─── SD card
ESP32 GPIO2  ──────── RST  ─── LR02
ESP32 GPIO4  ──────── DIO0 ─── LR02

ESP32 GPIO32 ──────── BTN1 (cycle screens)   ── GND (via button)
ESP32 GPIO33 ──────── BTN2 (power mode)      ── GND (via button)
ESP32 GPIO27 ──────── BTN3 (sleep / ranging) ── GND (via button)
```

---

## Architecture

```
┌──────────────────────────────────────────────────────┐
│                  Application Layer                   │
│              system_manager.c  (state machine)       │
└────────────┬──────────────┬───────────────┬──────────┘
             │              │               │
  ┌──────────▼──┐  ┌────────▼────┐  ┌──────▼────────┐
  │ GPS Manager │  │Display Mgr  │  │ Power Manager │
  │ gps_manager │  │display_mgr  │  │ power_manager │
  └──────────┬──┘  └────────┬────┘  └──────┬────────┘
             │              │               │
  ┌──────────▼──┐  ┌────────▼────┐  ┌──────▼────────┐
  │  GPS Driver │  │SSD1306 Drv  │  │ INA226 Driver │
  │ gps_driver  │  │ssd1306_drv  │  │ ina226_driver │
  └─────────────┘  └─────────────┘  └───────────────┘

  ┌─────────────────────────────────────────────────┐
  │               Wireless Layer                    │
  │  espnow_manager │ wifi_scanner │ ble_scanner    │
  │  lora_manager   │ distance_calc                 │
  └─────────────────────────────────────────────────┘

  ┌──────────────────┐   ┌──────────────────────────┐
  │  storage/        │   │  input/                  │
  │  sd_logger       │   │  button_handler          │
  └──────────────────┘   └──────────────────────────┘
```

**Key design principles:**
- **System manager is the single source of truth** — no module-to-module state transitions
- **Layered architecture** — application → manager → driver → hardware; no layer skipping
- **Power-aware from the start** — every I2C transaction holds a PM lock; power modes controlled centrally
- **Graceful degradation** — missing hardware (no GPS fix, no SD card, no LoRa) is logged and handled without crashing

---

## FreeRTOS Task Layout

| Task | Core | Priority | Stack | Responsibility |
|---|---|---|---|---|
| `sys_mgr` | 0 | 10 | 4 KB | Event-driven state machine |
| `btn_hdlr` | 0 | 8 | 2 KB | GPIO ISR debounce → event queue |
| `gps` | 1 | 7 | 3 KB | UART read + NMEA parse loop |
| `ranging` | 1 | 6 | 4 KB | TX/RX all protocols + SD log |
| `disp_upd` | 1 | 5 | 4 KB | 2 Hz screen render |
| `ina226_mon`| 1 | 4 | 3 KB | Background power logging |

---

## Display Screens

**Button 1** cycles through three screens:

```
┌────────────────┐  ┌────────────────┐  ┌────────────────┐
│ GPS LOGGER [1/3│  │ STATUS    [2/3]│  │ RANGING   [3/3]│
│ GPS: Fix 8 Sats│  │ 42.3mA  3.82V  │  │ Dist: 127m     │
│ 37.774900 N    │  │ 161.6mW        │  │ NOW:-68dBm IN  │
│-122.419400 W   │  │ Batt: 78% GOOD │  │ WiFi:-72dBm 3AP│
│ HDOP: 0.92     │  │ Up: 00:12:34   │  │ BLE:  4 dev    │
│ 12.3kph (045°) │  │ Phase 6 Active │  │ LoRa:-89dBm IN │
└────────────────┘  └────────────────┘  └────────────────┘
```

---

## Button Mapping

| Button | Short Press | Context |
|---|---|---|
| BTN1 (GPIO32) | Cycle screens GPS→STATUS→RANGING | All states |
| BTN2 (GPIO33) | Cycle power mode | LIGHT\_SLEEP / ACTIVE |
| BTN2 (GPIO33) | Stop ranging → ACTIVE | RANGING |
| BTN3 (GPIO27) | Toggle ACTIVE ↔ LIGHT\_SLEEP | ACTIVE / LIGHT\_SLEEP |
| BTN3 (GPIO27) | RANGING → LIGHT\_SLEEP | RANGING |

To enter **RANGING mode**: post `EVENT_RANGING_START` from your application (e.g., extend BTN2 long-press detection in `button_handler.c`).

---

## State Machine

```
              ┌─────────┐
              │  BOOT   │  (initialise all hardware)
              └────┬────┘
                   │ auto
              ┌────▼────────┐
   BTN3 ────► │ LIGHT_SLEEP │ ◄──── BTN3
              └────┬────────┘
                   │ BTN3
              ┌────▼────┐
              │  ACTIVE │ ──── EVENT_RANGING_START ──► RANGING
              └─────────┘                              │
                                      BTN2 / STOP ─────┘
```

---

## Power Budget

| State | Measured | Components |
|---|---|---|
| LIGHT\_SLEEP | ~8 mA | CPU light sleep, display off |
| ACTIVE | ~45 mA | CPU 160 MHz, GPS, display |
| RANGING | ~110 mA | + ESP-NOW + LoRa + SD |
| RANGING + WiFi + BLE | ~125 mA | all protocols active |

*Measured via INA226, 2500 mAh LiPo → ~22 h ACTIVE, ~20 h RANGING*

---

## SD Card CSV Format

```csv
timestamp,lat,lon,battery_v,current_ma,espnow_rssi,espnow_in_range,wifi_rssi,wifi_in_range,ble_rssi,ble_in_range,lora_rssi,lora_in_range,distance_m
1699564821,37.774900,-122.419400,3.820,108.3,-68,1,-72,1,-81,1,-89,1,127.4
```

Use the CSV to plot signal strength vs distance for each protocol and identify the exact range boundary where each drops out.

---

## Build & Flash

### Prerequisites

- ESP-IDF v5.1+ (`idf.py --version`)
- Python 3.8+
- USB–serial driver for your ESP32 dev board

### Steps

```bash
# 1. Clone
git clone https://github.com/your-username/gps-logger.git
cd gps-logger

# 2. Set IDF_PATH (if not already in your shell profile)
. $IDF_PATH/export.sh

# 3. Configure (set serial port, enable BT, etc.)
idf.py menuconfig

# 4. Build
idf.py build

# 5. Flash + monitor
idf.py -p /dev/ttyUSB0 flash monitor
```

### sdkconfig requirements

Enable these in `menuconfig` before building:

```
Component config → Bluetooth → Bluetooth → Enable
Component config → Bluetooth → Bluetooth controller → BLE
Component config → Wi-Fi → WiFi
Component config → ESP-NOW → Enable
FAT Filesystem support → Enable
```

Or run `idf.py set-target esp32` and copy the provided `sdkconfig` from this repo.

---

## Project Structure

```
gps-logger/
├── main/
│   ├── main.c                        # Entry point, task creation
│   ├── system_manager.c/h            # Central state machine
│   ├── pin_config.h                  # All GPIO / peripheral assignments
│   ├── communication/
│   │   ├── i2c_bus.c/h               # Shared I2C bus (mutex + PM lock)
│   │   ├── gps_driver.c/h            # UART2 raw NMEA reader
│   │   ├── nmea_parser.c/h           # GGA / RMC sentence parser
│   │   └── gps_manager.c/h           # GPS state machine + event posting
│   ├── gps/
│   │   └── distance_calc.c/h         # Haversine great-circle distance
│   ├── display/
│   │   ├── ssd1306_driver.c/h        # SSD1306 I2C OLED driver
│   │   ├── display_manager.c/h       # Screen cycling, brightness
│   │   ├── screen_gps.c/h            # GPS data screen
│   │   ├── screen_status.c/h         # Power / battery screen
│   │   └── screen_ranging.c/h        # Protocol stats screen
│   ├── power/
│   │   ├── power_manager.c/h         # Power subsystem orchestrator
│   │   ├── power_modes.c/h           # CPU freq scaling + sleep modes
│   │   ├── ina226_driver.c/h         # INA226 current monitor driver
│   │   └── battery_monitor.c/h       # LiPo SoC estimation
│   ├── wireless/
│   │   ├── protocol_types.h          # Shared packet + stats structs
│   │   ├── espnow_manager.c/h        # ESP-NOW broadcast + peer ranging
│   │   ├── wifi_scanner.c/h          # WiFi AP passive scan
│   │   ├── ble_scanner.c/h           # BLE GAP passive scan
│   │   └── lora_manager.c/h          # SX1276 LoRa driver + ranging
│   ├── storage/
│   │   └── sd_logger.c/h             # FAT CSV logger via SPI SD card
│   ├── input/
│   │   └── button_handler.c/h        # GPIO ISR debounce → event queue
│   └── CMakeLists.txt
├── CMakeLists.txt
├── sdkconfig
└── README.md
```

---

## Development Phases

| Phase | Status | Description |
|---|---|---|
| 1 — Power Foundation | ✅ Complete | CPU freq scaling, light sleep, power modes |
| 2 — Code Reorganisation | ✅ Complete | Layered directory structure, CMake |
| 3 — Display Integration | ✅ Complete | SSD1306 driver, screen cycling, INA226 live data |
| 4 — Battery Monitor | ✅ Complete | LiPo voltage → SoC LUT, charging detection |
| 5 — State Machine | ✅ Complete | Event-driven SM, RANGING state, battery events |
| 6 — GPS Integration | ✅ Complete | NEO-M8N UART, NMEA parser, live position display |
| 7 — ESP-NOW Ranging | ✅ Complete | Broadcast position, peer detection, haversine |
| 8 — Multi-Protocol | ✅ Complete | WiFi scan, BLE scan, LoRa SX1276 driver |
| 9 — SD Card Logging | ✅ Complete | FAT CSV, session management, auto-flush |
| 10 — Power Opt | ✅ Complete | Battery warnings, critical sleep, LOW\_POWER mode |
| 11 — Polish | ✅ Complete | RANGING screen, graceful degradation, README |

---

## Coding Standards

All code follows these rules:

- **Negative-space (early-exit) pattern** — error cases first, max two nesting levels
- **Single-responsibility functions** — no function mixes hardware, logic, and parsing
- **Layer separation** — no cross-layer calls (system\_manager never touches drivers directly)
- **Mutex protection** — every shared data structure has documented ownership
- **Graceful degradation** — missing peripherals are logged and bypassed, not fatal

---

## License

MIT — free to use, modify, and distribute with attribution.
