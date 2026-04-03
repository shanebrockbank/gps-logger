# GPS Position Logger - ESP-IDF Project

## Project Overview
**Purpose:** GPS position logger that displays location, calculates distance to another device, and scans for available wireless protocols (WiFi, BLE, ESP-NOW, LoRa).

**Hardware:**
- ESP32: ESP32 Classic (Xtensa, dual-core)
- ESP-IDF Version: v6.1-dev-450-g286b8cb76d
- GPS: NEO-M8N (UART) - *Not yet connected*
- LoRa: LR02 868/915MHz module (SPI) - *Future*
- Display: 1x SSD1306 128x64 OLED (I2C @ 0x3C) - *Single-display mode with screen cycling*
- Current Monitor: INA226 CJMCU-226 (I2C @ 0x40) - *Working*
- SD Card: Micro SD Card Module (SPI) - *Future*
- Input: 3 buttons - *All functional*
  - Button 1 (GPIO32): Cycle screens (GPS ↔ STATUS)
  - Button 2 (GPIO33): Cycle power modes
  - Button 3 (GPIO27): Toggle system states
- Future: TP4056 18650 charger (may implement later)

**GPIO Assignments:**
- Flexible assignments following dev board defaults
- Document pin mappings in `main/pin_config.h`

**I2C Bus Configuration:**
- Primary I2C Bus (I2C0): SDA=GPIO21, SCL=GPIO22, 100kHz
  - Display (SSD1306): Address 0x3C (single display mode)
  - INA226 Current Monitor: Address 0x40
- Mutex-protected for concurrent multi-task access
- Retry logic: 3 attempts, 200ms timeout per transaction
- Pull-up resistors: Internal pull-ups enabled (can add external 4.7kΩ if needed)

**SPI Bus Configuration:**
- Primary SPI Bus (HSPI/SPI2):
  - LoRa Module (LR02): Dedicated CS pin
  - SD Card Module: Dedicated CS pin
- SPI bus shared between LoRa and SD card via chip select

**Position Broadcast Format:**
All wireless protocols use identical simple format:
```
<device_id>,<latitude>,<longitude>,<timestamp>
Example: "DEV1,37.7749,-122.4194,1699564821"
```

**Multi-Protocol Strategy:**
- **Primary Mode**: All protocols broadcast/listen simultaneously
- **Fallback Mode**: If resource constraints exist, cycle through protocols with Button 1
- Each protocol maintains independent distance calculation
- Button 1 cycles display to show metrics from each protocol
- Goal: Compare protocol performance (range, latency, reliability)

---

## Current Status (Updated 2025-01-14)

### Development Phase Progress
- ✅ **Phase 1: ESP32 Power Management Foundation** - COMPLETE
- ✅ **Phase 2: Code Reorganization** - COMPLETE
- ✅ **Phase 3: Display Integration - Power Aware** - COMPLETE
- 📋 **Phase 4: Battery Power System** - Planned (see Development Roadmap below)
- 📋 **Phase 5: System State Machine Core** - Planned (see Development Roadmap below)
- 🎯 **Phase 6: GPS Integration - Power Aware** - NEXT TARGET
- 📋 **Phase 7-11:** Multi-protocol, logging, optimization - Planned (see Development Roadmap below)

**See complete 11-phase development roadmap in the "Development Roadmap" section below.**

### What's Working
- **I2C Bus:** 100kHz, mutex-protected, retry logic (3 attempts), 200ms timeout
- **INA226:** Real current/voltage/power readings (3-10 mA typical)
- **Display:** Single SSD1306 @ 0x3C, 2Hz refresh, screen cycling
- **Buttons:** All 3 buttons functional with debouncing
- **Power Modes:** 4 modes (ACTIVE, PERFORMANCE, BALANCED, LOW_POWER)
- **System:** Graceful boot without hardware, event-driven architecture

### Implementation Notes

**I2C Configuration (Optimized):**
- Frequency: 100kHz (reduced from 400kHz for stability)
- Mutex protection: Application-level locking for multi-task access
- Retry logic: 3 attempts with 10ms delays on failure
- Timeout: 200ms (increased from 100ms for reliability)
- All transactions protected in both INA226 and SSD1306 drivers

**Display Configuration (Single-Display Mode):**
- Only 1 display @ 0x3C (both displays have same address, would require soldering to change)
- Button 1 cycles screens: GPS ↔ STATUS
- Screen indicators: `[1/2]` for GPS, `[2/2]` for STATUS
- Update rate: 2 Hz (500ms)
- Gracefully handles missing displays (no crash)

**Current Button Mapping:**
- **Button 1 (GPIO32):** Cycle screens (GPS ↔ STATUS) - *Changed from brightness cycling*
- **Button 2 (GPIO33):** Cycle power modes (ACTIVE → PERFORMANCE → BALANCED → LOW_POWER)
- **Button 3 (GPIO27):** Toggle states (LIGHT_SLEEP ↔ ACTIVE)

**Known Limitations:**
- **INA226 Hot-Plug:** Not supported - requires ESP32 reset if disconnected/reconnected
  - Reason: Device handle cached, registers reset on power cycle
  - Workaround: Keep connected or press ESP32 RESET button
- **Second Display:** Both displays @ 0x3C - changing requires solder bridge modification
  - Solution: Single-display mode with screen cycling

### Phase 3 Completion Summary

**Files Created:** 11 modules
```
communication/i2c_bus.c/h          # Shared I2C bus management + scanning
display/ssd1306_driver.c/h         # Low-level OLED driver (5x7 font)
display/display_manager.c/h        # Display coordination + screen cycling
display/screen_gps.c/h             # GPS screen rendering
display/screen_status.c/h          # Status screen rendering (INA226 data)
```

**Key Improvements:**
- I2C bus contention eliminated (mutex + retry logic)
- Reduced I2C timeouts from frequent to zero
- Display working smoothly at 2 Hz
- INA226 providing real-time power measurements
- System boots reliably without hardware connected

**Binary Size:** 0x39d40 (236 KB) - 77% flash free

---

## Development Roadmap - 11 Phases

🎯 **Core Philosophy:** Display-first, battery-powered testing, power-aware from start. Displays provide immediate feedback, battery power enables total system current measurement via INA226. Each phase builds incrementally with clear success criteria.

---

### 📋 PHASE 1: ESP32 Power Management Foundation (1-2 days)

**Goal:** Implement real ESP32 power modes with measurable current reduction

**Tasks:**
1. Enable ESP-IDF power management in menuconfig
2. Implement CPU frequency scaling (240MHz → 80MHz → 40MHz)
3. Configure automatic light sleep
4. Test deep sleep with GPIO wake-up
5. Create power_modes.c/h with mode control functions

**Success Criteria:**
- Active mode: ~80mA @ 240MHz
- Idle mode: ~30mA @ 80MHz with auto light sleep
- Deep sleep: <1mA with GPIO wake capability
- INA226 confirms current readings match expectations

**Testing:**
- Measure current in each mode for 10 seconds
- Verify wake from deep sleep via button
- Log power mode transitions

---

### 📋 PHASE 2: Code Reorganization (1 day)

**Goal:** Clean architecture following claude.md structure

**Tasks:**
1. Create directory structure: power/, display/, gps/, wireless/, input/
2. Move INA226 code: power_manager.c → power/ina226_driver.c
3. Create power/power_modes.c for mode control (from Phase 1)
4. Create power/power_manager.c as orchestrator (no hardware logic)
5. Update main.c to be minimal entry point only
6. Update CMakeLists.txt for new structure

**Success Criteria:**
- Builds successfully
- Same functionality as before
- main.c <50 lines
- power/power_manager.c has no I2C code
- Each file has single responsibility

**Testing:**
- Build and flash
- Verify INA226 still reads correctly
- Verify power modes still work

---

### 📋 PHASE 3: Display Integration - Power Aware (3-4 days)

**Goal:** Dual SSD1306 OLEDs operational with power management

**Tasks:**
1. Create display/ssd1306_driver.c/h - I2C OLED driver
2. Create display/display_manager.c/h - Dual display coordinator
3. Create display/screen_gps.c/h - Display 1 rendering (placeholder data for now)
4. Create display/screen_status.c/h - Display 2 rendering
5. Implement display power states:
   - OFF: Displays powered down
   - DIM: Reduced brightness
   - FULL: Full brightness
6. Share I2C bus with INA226 using mutex
7. Button 1: Toggle display brightness (OFF → DIM → FULL)

**Display 1 Content (Phase 3):**
- System state text
- Placeholder position (will show real GPS later)
- Button test counter

**Display 2 Content:**
- Current draw (mA) from INA226
- Voltage (V) from INA226
- Power (mW) from INA226
- System uptime

**Success Criteria:**
- Both displays work on shared I2C bus
- No I2C conflicts with INA226
- INA226 current readings shown in real-time on Display 2
- Display off reduces current by 15-20mA
- DIM mode reduces current by 5-10mA vs FULL
- Button 1 cycles display brightness reliably

**Testing:**
- Show current draw on Display 2 (meta!)
- Button press updates Display 1 counter
- Power test: measure current with displays in each mode
- I2C stress test: rapid display updates with INA226 reads

---

### 📋 PHASE 4: Battery Power System (2-3 days)

**Goal:** Device runs from battery, INA226 measures total system current

**Tasks:**
1. Create power/battery_monitor.c/h - Battery voltage estimation
2. Wire INA226 shunt resistor to measure battery current (total system)
3. Add battery voltage sensing (ADC on battery divider if available)
4. Display battery voltage on Display 2
5. Implement battery state estimation (%, remaining time)
6. Add low battery warning (visual indicator on display)
7. Optional: Add TP4056 18650 charging circuit

**Power Architecture:**
```
Battery → INA226 Shunt → ESP32 + All Peripherals
                ↓
           Current measurement (total system)
```

**Success Criteria:**
- Device boots and runs from battery only (no USB)
- INA226 measures total system current accurately
- Battery voltage displayed on Display 2
- Battery percentage estimation shown
- Can run for >1 hour on single charge
- Low battery warning appears at 20%

**Testing:**
- Disconnect USB, verify device runs from battery
- Compare INA226 reading with external ammeter
- Discharge test: run until battery depleted, verify estimates
- Charge cycle: verify battery charges correctly (if TP4056 added)

---

### 📋 PHASE 5: System State Machine Core (2-3 days)

**Goal:** Implement working state machine with 3 power states

**Tasks:**
1. Define system states: BOOT, IDLE, ACTIVE, SLEEP
2. Implement event queue and state transition logic
3. Create state handlers for each state
4. Wire button events to trigger state transitions
5. Each state sets appropriate power mode
6. Display current state on both displays

**Power Impact Per State:**
- BOOT: Full power (240MHz), initialize hardware
- IDLE: 80MHz, auto light sleep, displays DIM
- ACTIVE: 160MHz, displays FULL
- SLEEP: Deep sleep, wake on button

**Button Mapping:**
- Button 1: Cycle display brightness (in IDLE/ACTIVE)
- Button 2: Toggle IDLE ↔ ACTIVE
- Button 3 (short): No function yet
- Button 3 (long hold): Enter SLEEP

**Success Criteria:**
- State transitions logged and shown on Display 1
- Button 2 cycles: IDLE → ACTIVE → IDLE
- Button 3 (long press) → SLEEP → Button 3 → BOOT → IDLE
- INA226 shows distinct current for each state on Display 2
- 30-minute idle test: device stays in light sleep, <35mA average

**Testing:**
- Manual button press sequences
- Measure and display current in each state for 30 seconds
- Verify no spurious wake-ups during idle
- Battery life extrapolation based on measured currents

---

### 📋 PHASE 6: GPS Integration - Power Aware (3-4 days)

**Goal:** NEO-M8N GPS with power management and real position display

**Tasks:**
1. Create gps/neo_m8n.c/h - UART driver for GPS
2. Create gps/nmea_parser.c/h - Parse NMEA sentences
3. Create gps/gps_manager.c/h - Lifecycle control
4. Implement GPS power states:
   - OFF: No UART, no power
   - STANDBY: Low power mode command to GPS
   - ACQUISITION: Full power, 10Hz updates
   - TRACKING: Reduced power, 1Hz updates
5. System state controls GPS mode:
   - IDLE: GPS in STANDBY
   - ACTIVE: GPS in TRACKING
   - SLEEP: GPS OFF
6. Update Display 1 with real GPS data

**Display 1 Content (Updated):**
- Current GPS position (lat/lon)
- Number of satellites
- GPS fix status
- Altitude

**Success Criteria:**
- Parse GGA/RMC sentences correctly
- GPS fix acquired outdoors in <1 minute
- GPS standby mode reduces current by 15-20mA (visible on Display 2)
- Valid lat/lon displayed on Display 1 at 1Hz in ACTIVE state
- GPS resumes from standby in <5 seconds

**Testing:**
- Indoor: verify UART communication, see satellites in view
- Outdoor: verify GPS fix, watch real position on Display 1
- Power test: observe Display 2 current change with GPS in each mode
- State transition: IDLE→ACTIVE, verify GPS resumes quickly
- Battery life test: measure runtime with GPS active

---

### 📋 PHASE 7: ESP-NOW Communication (3-4 days)

**Goal:** Two devices communicate position data, handle in/out of range gracefully

**Tasks:**
1. Create wireless/protocol_types.h - Common data structures
2. Create wireless/espnow_manager.c/h - ESP-NOW implementation
3. Implement broadcast: send GPS position every 1s
4. Implement receive: parse incoming positions
5. Create gps/distance_calc.c/h - Haversine distance
6. Handle device out-of-range (timeout after 5s no packets)
7. Display range status on Display 1 ("IN RANGE" / "OUT OF RANGE")
8. Add wireless state to system: new state RANGING
9. Button 2: Start/stop ranging (ACTIVE → RANGING)

**Display 1 Content (Updated):**
- Local position (lat/lon)
- Remote position (lat/lon)
- Distance to remote device (meters)
- Range status (IN/OUT)
- Packet loss %

**Display 2 Content (Updated):**
- Current draw with ESP-NOW active
- RSSI (signal strength)
- Packets received
- Battery %

**Success Criteria:**
- Two devices exchange positions
- Distance calculated and displayed correctly
- Graceful handling: "OUT OF RANGE" shown when no packets for 5s
- Devices reconnect automatically when back in range
- Range test: devices maintain link at 100m outdoors
- ESP-NOW adds ~15mA (visible on Display 2)
- Packet reception rate >95% at close range

**Testing:**
- Two devices, stationary: verify distance ≈ 0m
- Walk 50m apart: verify distance matches GPS reading
- Range boundary test: walk slowly away until "OUT OF RANGE" appears, note distance
- Walk back: verify automatic reconnection
- Power test: observe Display 2 current with ESP-NOW active
- Battery life: measure runtime during ranging

---

### 📋 PHASE 8: Multi-Protocol Support (4-5 days)

**Goal:** WiFi, BLE, LoRa alongside ESP-NOW, handle range boundaries

**Tasks:**
1. Create wireless/wifi_scanner.c/h
2. Create wireless/ble_scanner.c/h
3. Create wireless/lora_manager.c/h (SPI, LR02 module)
4. Protocol view cycling with Button 1
5. Display protocol-specific metrics
6. Each protocol maintains independent state
7. Each protocol handles timeout and reconnection
8. Range boundary detection: Track which protocols work at each distance

**Button Mapping (Updated):**
- Button 1: Cycle protocol view (ALL→WiFi→BLE→ESP-NOW→LoRa→ALL)
- Button 2: Start/stop ranging
- Button 3: Long press for sleep

**Display 1 Content (Updated):**
- Shows data from selected protocol
- Distance from selected protocol
- Protocol name displayed
- Range status per protocol

**Display 2 Content (Updated):**
- RSSI per protocol
- Packet loss per protocol
- Which protocols are IN RANGE
- Total current draw

**Success Criteria:**
- All 4 protocols broadcast/receive simultaneously
- Protocol view cycles through: ALL→WiFi→BLE→ESP-NOW→LoRa
- Each protocol shows IN/OUT RANGE status independently
- LoRa achieves >500m range outdoors
- Multi-protocol mode current: <120mA
- Graceful reconnection when devices return to range
- Can identify which protocol has longest range

**Testing:**
- Range comparison: walk away slowly, note when each protocol drops (the boundary!)
- Power comparison: Display 2 shows current per protocol combination
- Reliability test: packet loss at various distances for each protocol
- Real-world boundary test: walk 1km, record which protocols work at each 50m interval
- Re-approach test: verify all protocols reconnect automatically

---

### 📋 PHASE 9: SD Card Logging (2-3 days)

**Goal:** Log position data, distance, power, and range boundaries

**Tasks:**
1. Create storage/sd_logger.c/h (SPI)
2. Share SPI bus with LoRa via chip select
3. Log format: CSV with timestamp, position, distance, power, RSSI per protocol, in-range status per protocol
4. Periodic flush: every 10 seconds
5. SD card only active when RANGING state
6. File management: new file each session
7. Log range boundary transitions (when IN→OUT or OUT→IN)

**CSV Columns:**
```
timestamp,lat,lon,battery_v,current_ma,wifi_rssi,wifi_in_range,ble_rssi,ble_in_range,espnow_rssi,espnow_in_range,lora_rssi,lora_in_range,distance_m
```

**Success Criteria:**
- Data logged correctly to SD card
- Files readable on PC (CSV format)
- Range boundaries clearly visible in data (in_range transitions)
- No data loss during power transitions
- SD card adds <10mA when writing
- Can log for >6 hours continuously

**Testing:**
- 1-hour ranging session, verify CSV data on PC
- Range test: walk 500m away, walk back, verify CSV shows all range transitions
- Power test: Display 2 shows current increase with SD logging
- Data integrity: verify no corrupted entries
- Analysis: plot distance vs RSSI for each protocol from CSV

---

### 📋 PHASE 10: Advanced Power Optimization (2-3 days)

**Goal:** Automatic power mode transitions and optimization

**Tasks:**
1. Implement idle timeout: ACTIVE → IDLE after 30s inactivity
2. Implement auto-sleep: IDLE → SLEEP after 5 minutes
3. Power budget enforcement (shed features if battery <20%)
4. Wake-on-GPS-fix (if GPS gets fix in STANDBY, transition to ACTIVE)
5. Display auto-dim after 10s in ACTIVE
6. Selective protocol disable to extend battery (e.g., disable WiFi/BLE if only LoRa needed)

**Success Criteria:**
- Device auto-sleeps if unused for 5 minutes
- Wakes immediately on button press
- GPS acquisition triggers automatic wake
- Average current in typical use <45mA
- Battery lasts >12 hours in RANGING mode
- Low battery mode disables power-hungry features

**Testing:**
- Leave device idle for 10 minutes, verify sleep
- Wake from sleep, verify all systems resume
- 12-hour battery endurance test with ranging
- Low battery test: verify graceful degradation at 20%

---

### 📋 PHASE 11: Polish & Optimization (2-3 days)

**Goal:** Production-ready device

**Tasks:**
1. Refine button handling (debouncing, long-press detection)
2. Add startup logo on displays
3. Add error handling for missing hardware (SD card, GPS)
4. Settings menu via buttons (future: NVS storage)
5. Optimize task stack sizes
6. Documentation: user guide, test results
7. Range test report: create graphs from CSV data

**Success Criteria:**
- Device feels responsive
- No crashes during 24-hour run
- Graceful handling of missing GPS fix
- Power consumption documented per mode
- Complete range test report with graphs

---

### 🧪 Testing Strategy Summary

**Per-Phase Testing:**
- Build and flash successfully
- Visual confirmation on displays
- Power measurement visible on Display 2
- Serial log verification
- Battery runtime estimation

**Integration Testing (After Phase 7):**
- Two-device ranging test
- Range boundary identification
- Battery life measurement
- State machine stress test

**System Testing (After Phase 9):**
- Real-world range test: 1km walk, test all protocols
- CSV data analysis: plot distance vs RSSI
- Multi-day reliability test
- Complete power consumption profile

---

### 📊 Power Budget Target

| State          | Target Current | Components Active    | Battery Life (2500mAh) |
|----------------|----------------|----------------------|------------------------|
| SLEEP          | <1mA           | RTC only             | >100 days              |
| IDLE           | ~30mA          | CPU, Displays DIM    | >80 hours              |
| ACTIVE         | ~45mA          | +GPS, Displays FULL  | >50 hours              |
| RANGING        | ~110mA         | +ESP-NOW, SD logging | >20 hours              |
| MULTI-PROTOCOL | ~120mA         | +WiFi+BLE+LoRa       | >18 hours              |

---

### 🎯 Key Changes from Original Plan

1. **Displays First (Phase 3):** Immediate visual feedback, can see power consumption in real-time
2. **Battery Power Early (Phase 4):** Enables total system current measurement via INA226
3. **Power Optimization Last (Phase 10):** Save optimizations until all features working
4. **Range Boundary Focus:** Every wireless phase emphasizes detecting the exact range limit where protocols transition IN↔OUT of range

---

## Architecture Philosophy

### Top-Down System Management Approach
This project uses a **system manager architecture** - essentially a basic OS/UX layer that orchestrates all functionality. The system manager acts as a state machine controlling module lifecycle, user experience flow, and power optimization. This is intentional practice for future rowing computer development.

**System Hierarchy:**
1. **Core Layer** - Always on: System manager, button monitoring
2. **GPS Layer** - Controlled by system state (tracking, idle, off)
3. **Display Layer** - Controlled by system state (active, dimmed, off)
4. **Wireless Layer** - Controlled by system state (scanning, communicating, off)

**Key Principles:** 
- System manager is the single source of truth for application state
- All modules are explicitly controlled by system manager - no autonomous behavior
- State transitions happen through system manager (not module-to-module)
- Think of it as a basic operating system kernel for this embedded app

---

## Code Organization

### Directory Structure
```
├── main/
│   ├── main.c                    # Entry point, system manager init
│   ├── system_manager.c/h        # Central state machine & module orchestration
│   ├── pin_config.h              # GPIO definitions
│   ├── gps/
│   │   ├── gps_manager.c/h       # GPS init/read/sleep
│   │   └── distance_calc.c/h     # Distance calculations (haversine)
│   ├── wireless/
│   │   ├── protocol_types.h      # Common structs for multi-protocol
│   │   ├── wifi_scanner.c/h      # WiFi scan operations
│   │   ├── ble_scanner.c/h       # BLE scan operations
│   │   ├── espnow_manager.c/h    # ESP-NOW broadcast/listen
│   │   └── lora_manager.c/h      # LoRa broadcast/listen (LR02)
│   ├── display/
│   │   ├── display_manager.c/h   # Dual display control & coordination
│   │   ├── ssd1306_driver.c/h    # SSD1306 OLED driver (I2C)
│   │   ├── screen_gps.c/h        # Display 1: GPS data rendering
│   │   └── screen_status.c/h     # Display 2: Status/metrics rendering
│   ├── power/
│   │   ├── ina226_driver.c/h     # INA226 current monitor (I2C)
│   │   └── battery_monitor.c/h   # Battery state estimation
│   ├── storage/
│   │   └── sd_logger.c/h         # SD card logging (SPI)
│   └── input/
│       └── button_handler.c/h    # Button debounce/events (3 buttons)
├── components/                    # Custom components if needed
└── CMakeLists.txt
```

---

## Embedded C Coding Compliance Checklist

This section defines strict coding standards for embedded C development on this project. All code must comply with these requirements to ensure reliability, maintainability, and auditability.

---

### 1. Early Exit & Control Flow

**Rules:**
- ✅ Functions do not exceed two nested control levels
- ✅ Early-return patterns used for error and boundary handling
- ✅ No deep nested `if/else` chains

**Example - CORRECT (Negative Space Pattern):**
```c
esp_err_t gps_read_position(gps_data_t *out_data) {
    // Error cases first (negative space)
    if (out_data == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!gps_is_initialized()) {
        return ESP_ERR_INVALID_STATE;
    }

    if (!gps_has_fix()) {
        return ESP_ERR_NOT_FOUND;
    }

    // Happy path - only reached if all checks pass
    out_data->latitude = get_latitude();
    out_data->longitude = get_longitude();
    return ESP_OK;
}
```

**Example - WRONG (Deep Nesting):**
```c
esp_err_t bad_example(data_t *data) {
    if (data != NULL) {
        if (is_initialized()) {
            if (has_data()) {
                if (is_ready()) {
                    // TOO DEEP - violates 2-level rule
                    process_data(data);
                    return ESP_OK;
                }
            }
        }
    }
    return ESP_FAIL;
}
```

---

### 2. Function Responsibilities

**Rules:**
- ✅ Each function performs one clearly defined operation
- ✅ No function mixes logic, hardware access, and parsing
- ✅ Function length is reasonable and auditable (<100 lines as guideline)

**Example - CORRECT (Single Responsibility):**
```c
// Separate functions for each responsibility
static esp_err_t validate_gps_data(const gps_data_t *data);
static esp_err_t read_gps_uart(uint8_t *buffer, size_t len);
static esp_err_t parse_nmea_sentence(const char *sentence, gps_data_t *out);

esp_err_t gps_read_position(gps_data_t *out_data) {
    // Orchestrates, but doesn't mix concerns
    uint8_t buffer[256];
    ESP_RETURN_ON_ERROR(read_gps_uart(buffer, sizeof(buffer)), TAG, "UART read failed");
    ESP_RETURN_ON_ERROR(parse_nmea_sentence((char*)buffer, out_data), TAG, "Parse failed");
    ESP_RETURN_ON_ERROR(validate_gps_data(out_data), TAG, "Validation failed");
    return ESP_OK;
}
```

---

### 3. Separation of Concerns

**Rules:**
- ✅ Hardware drivers isolated from application logic
- ✅ State machines isolated from I/O code
- ✅ No cross-layer calls outside defined interfaces

**Architecture Layers:**
```
Application Layer (system_manager.c)
        ↓ (uses public API only)
Manager Layer (gps_manager.c, power_manager.c)
        ↓ (uses public API only)
Driver Layer (neo_m8n.c, ina226_driver.c)
        ↓ (uses ESP-IDF only)
Hardware Layer (ESP-IDF drivers)
```

**Example - CORRECT:**
```c
// Application calls manager
system_manager.c:  gps_manager_start();

// Manager calls driver
gps_manager.c:     neo_m8n_enable();

// Driver calls hardware
neo_m8n.c:         uart_driver_install(...);
```

**Example - WRONG:**
```c
// Application directly calls driver (skips manager layer)
system_manager.c:  neo_m8n_enable();  // ❌ WRONG - violates layer separation
```

---

### 4. Global State Control

**Rules:**
- ✅ No unauthorized global variables
- ✅ All module-level `static` variables reviewed for concurrency safety
- ✅ Shared state documented with ownership rules

**Example - CORRECT:**
```c
// Module-private state with mutex protection
static gps_state_t g_gps_state = GPS_STATE_OFF;  // Owned by gps_manager.c
static SemaphoreHandle_t g_gps_mutex = NULL;     // Protects g_gps_state

esp_err_t gps_manager_init(void) {
    g_gps_mutex = xSemaphoreCreateMutex();
    return (g_gps_mutex != NULL) ? ESP_OK : ESP_FAIL;
}

gps_state_t gps_manager_get_state(void) {
    gps_state_t state;
    xSemaphoreTake(g_gps_mutex, portMAX_DELAY);
    state = g_gps_state;
    xSemaphoreGive(g_gps_mutex);
    return state;
}
```

---

### 5. Input Validation

**Rules:**
- ✅ All external data validated before use
- ✅ Pointer and buffer boundaries explicitly checked
- ✅ Enum/range constraints enforced

**Example:**
```c
esp_err_t gps_parse_nmea(const char *sentence, size_t len, gps_data_t *out) {
    // Validate all inputs
    if (sentence == NULL || out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (len == 0 || len > GPS_MAX_SENTENCE_LEN) {
        return ESP_ERR_INVALID_SIZE;
    }

    // Validate NMEA format (starts with $, ends with \r\n)
    if (sentence[0] != '$') {
        return ESP_ERR_INVALID_ARG;
    }

    // Safe to process
    return parse_nmea_internal(sentence, len, out);
}
```

---

### 6. Explicitness & Readability

**Rules:**
- ✅ No magic numbers; all constants named
- ✅ No "clever" or ambiguous expressions
- ✅ Macros do not hide side effects

**Example - CORRECT:**
```c
#define GPS_UART_BAUD_RATE    9600
#define GPS_UART_NUM          UART_NUM_1
#define GPS_MAX_SENTENCE_LEN  82
#define GPS_FIX_TIMEOUT_MS    60000

esp_err_t gps_init(void) {
    uart_config_t config = {
        .baud_rate = GPS_UART_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        // ...
    };
    return uart_param_config(GPS_UART_NUM, &config);
}
```

**Example - WRONG:**
```c
// Magic numbers, unclear intent
esp_err_t gps_init(void) {
    uart_config_t config = {
        .baud_rate = 9600,  // ❌ What device? Why 9600?
        .data_bits = 8,     // ❌ Use enum constant
        // ...
    };
    return uart_param_config(1, &config);  // ❌ Which UART?
}
```

---

### 7. Error Handling Consistency

**Rules:**
- ✅ Module uses a single unified error-handling strategy
- ✅ Return codes or result enums standardized and documented
- ✅ Errors propagated consistently

**Strategy for this project:**
- Use `esp_err_t` return values for all functions that can fail
- Use `ESP_ERROR_CHECK()` for critical initialization that must succeed
- Use `ESP_RETURN_ON_ERROR()` macro for clean error propagation
- Log errors at point of detection

**Example:**
```c
esp_err_t gps_manager_start(void) {
    esp_err_t ret;

    // Critical init - must succeed
    ESP_ERROR_CHECK(gps_manager_init());

    // Recoverable operations - propagate errors
    ret = neo_m8n_enable();
    ESP_RETURN_ON_ERROR(ret, TAG, "Failed to enable GPS module");

    ret = neo_m8n_configure(GPS_MODE_TRACKING);
    ESP_RETURN_ON_ERROR(ret, TAG, "Failed to configure GPS");

    return ESP_OK;
}
```

---

### 8. Immutability

**Rules:**
- ✅ `const` used wherever mutation is unnecessary
- ✅ Immutable tables and buffers properly marked read-only
- ✅ Mutable state minimized and justified

**Example:**
```c
// Immutable configuration table
static const uart_config_t GPS_UART_CONFIG = {
    .baud_rate = 9600,
    .data_bits = UART_DATA_8_BITS,
    .parity = UART_PARITY_DISABLE,
    .stop_bits = UART_STOP_BITS_1,
    .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
};

// Function takes const pointer (won't modify input)
esp_err_t gps_validate_fix(const gps_data_t *data) {
    if (data == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    // Can read but not modify data
    return (data->fix_quality > 0) ? ESP_OK : ESP_ERR_INVALID_STATE;
}
```

---

### 9. Side Effect Isolation

**Rules:**
- ✅ Functions with side effects clearly identified
- ✅ Pure logic functions do not write to global/static state
- ✅ Hardware register writes isolated in dedicated functions

**Example:**
```c
// Pure function - no side effects
static float calculate_distance(float lat1, float lon1, float lat2, float lon2) {
    // Only performs calculation, no state changes
    return haversine_formula(lat1, lon1, lat2, lon2);
}

// Side effect function - clearly named
esp_err_t gps_enable_power(void) {
    // Name makes it clear this has side effects
    gpio_set_level(GPS_POWER_PIN, 1);
    vTaskDelay(pdMS_TO_TICKS(100));  // Wait for power stabilization
    return ESP_OK;
}
```

---

### 10. Edge Case Handling

**Rules:**
- ✅ All boundary conditions handled explicitly
- ✅ Overflows, wraparounds, and off-by-one conditions tested
- ✅ Invalid state transitions handled or detected

**Example:**
```c
esp_err_t gps_parse_coordinate(const char *str, float *out_coord) {
    if (str == NULL || out_coord == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    // Check for empty string
    if (str[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }

    // Check for overflow before conversion
    errno = 0;
    float value = strtof(str, NULL);
    if (errno == ERANGE) {
        ESP_LOGE(TAG, "Coordinate value overflow");
        return ESP_ERR_INVALID_ARG;
    }

    // Validate coordinate range
    if (value < -180.0f || value > 180.0f) {
        ESP_LOGE(TAG, "Coordinate out of valid range: %.6f", value);
        return ESP_ERR_INVALID_ARG;
    }

    *out_coord = value;
    return ESP_OK;
}
```

---

### 11. Debug Instrumentation

**Rules:**
- ✅ Deterministic, minimal debug hooks present
- ✅ Logging is controlled, bounded, and removable
- ✅ Debug code does not alter functional behavior

**Logging Standard:**
```c
static const char *TAG = "GPS_MGR";  // At top of each .c file

// Use appropriate log levels
ESP_LOGI(TAG, "GPS initialized successfully");           // Info: normal operation
ESP_LOGW(TAG, "GPS fix lost, age: %d sec", age);         // Warning: degraded but working
ESP_LOGE(TAG, "GPS UART failed: %s", esp_err_to_name(ret));  // Error: operation failed
ESP_LOGD(TAG, "Raw sentence: %s", sentence);             // Debug: verbose (disabled in release)
```

**Debug Assertions:**
```c
// Use assertions for invariants that should never be violated
#include "esp_check.h"

esp_err_t gps_manager_transition_state(gps_state_t new_state) {
    // Assert valid state enum
    ESP_RETURN_ON_FALSE(new_state < GPS_STATE_MAX, ESP_ERR_INVALID_ARG, TAG,
                        "Invalid state: %d", new_state);

    // State machine logic
    g_gps_state = new_state;
    return ESP_OK;
}
```

---

### 12. Modular Architecture

**Rules:**
- ✅ No oversized "god modules"
- ✅ Modules are testable, isolated, and well-defined
- ✅ Interfaces documented and versioned

**Module Structure:**
```
gps/
├── gps_manager.h         # Public API (documented)
├── gps_manager.c         # Implementation
├── neo_m8n.h             # Driver API
├── neo_m8n.c             # Driver implementation
└── nmea_parser.c/h       # Parser (pure functions)
```

**Module API Documentation:**
```c
/**
 * @file gps_manager.h
 * @brief GPS Module Manager - High-level GPS lifecycle control
 *
 * This module manages the GPS hardware lifecycle and provides a simple
 * API for the system manager to control GPS operation.
 *
 * Thread Safety: All public functions are thread-safe
 * Dependencies: neo_m8n (driver), nmea_parser
 */

/**
 * @brief Initialize GPS manager
 *
 * Must be called once during system initialization before any other
 * GPS manager functions.
 *
 * @return ESP_OK on success
 * @return ESP_ERR_NO_MEM if mutex creation fails
 */
esp_err_t gps_manager_init(void);

/**
 * @brief Start GPS tracking
 *
 * Enables GPS hardware and begins position acquisition.
 *
 * @return ESP_OK on success
 * @return ESP_ERR_INVALID_STATE if not initialized
 * @return ESP_FAIL if hardware enable fails
 */
esp_err_t gps_manager_start(void);
```

---

### Naming Conventions

**Functions:**
- Format: `module_action_object()`
- Examples: `gps_read_position()`, `power_enable_module()`, `display_update_screen()`

**Structs:**
- Format: `module_name_t`
- Examples: `gps_data_t`, `power_state_t`, `display_config_t`

**Enums:**
- Format: `MODULE_STATE_NAME`
- Examples: `GPS_STATE_ACTIVE`, `POWER_MODE_LOW`, `DISPLAY_BRIGHTNESS_FULL`

**Constants:**
- Format: `MODULE_CONSTANT_NAME`
- Examples: `GPS_UART_BAUD_RATE`, `POWER_SAMPLE_RATE_HZ`, `DISPLAY_WIDTH_PX`

**Static Variables:**
- Format: `g_module_variable` (g_ prefix for global/module scope)
- Examples: `g_gps_state`, `g_gps_mutex`, `g_power_metrics`

---

## FreeRTOS Usage

### Task Structure
```c
// System manager runs highest priority - it's the orchestrator
#define SYSTEM_MANAGER_PRIORITY   (configMAX_PRIORITIES - 1)
#define BUTTON_TASK_PRIORITY      (tskIDLE_PRIORITY + 4)  // High - user input
#define GPS_TASK_PRIORITY         (tskIDLE_PRIORITY + 3)
#define DISPLAY_TASK_PRIORITY     (tskIDLE_PRIORITY + 2)
#define WIRELESS_TASK_PRIORITY    (tskIDLE_PRIORITY + 2)

// Create tasks suspended, system manager enables them based on state
xTaskCreatePinnedToCore(
    gps_task,
    "gps_task",
    4096,
    NULL,
    GPS_TASK_PRIORITY,
    &gps_task_handle,
    0  // Pin to core 0
);
vTaskSuspend(gps_task_handle);  // Start suspended, system manager resumes when needed
```

### Inter-Task Communication (Event-Driven)
- Use **system event queue** for all module → system manager communication
- Use **module queues** for system manager → module commands
- Use **event groups** for module status flags (GPS has fix, display ready, etc.)
- Use **mutexes** for shared resource protection (I2C bus, SPI bus)
```c
// Example: Button task posts event to system manager
event_t event = {
    .type = EVENT_BUTTON_SCAN,
    .timestamp = xTaskGetTickCount()
};
xQueueSend(system_event_queue, &event, pdMS_TO_TICKS(10));

// System manager sends command to display module
display_cmd_t cmd = DISPLAY_CMD_SHOW_SCAN;
xQueueSend(display_cmd_queue, &cmd, 0);
```

---

## System Manager Module

### Core Responsibilities
The system manager is the **central orchestrator** - think of it as a tiny OS kernel:

1. **State Machine**: Manages application states (boot, idle, tracking, scanning, sleep)
2. **Module Lifecycle**: Initialize, enable, disable all hardware modules
3. **UX Flow**: Coordinate transitions between user-facing modes
4. **Event Routing**: Distribute button presses, GPS fixes, scan results to appropriate handlers
5. **Power Optimization**: Put unused modules to sleep as side-effect of state management

### Application States
```c
typedef enum {
    SYS_STATE_BOOT,          // Initializing hardware
    SYS_STATE_IDLE,          // Waiting for user input, GPS running
    SYS_STATE_TRACKING,      // Actively displaying position (local only)
    SYS_STATE_MULTI_RANGING, // All protocols broadcasting/listening simultaneously
    SYS_STATE_SINGLE_RANGING,// Single protocol mode (cycling with button)
    SYS_STATE_SLEEP,         // Deep sleep, wake on button
} system_state_t;

// Protocol view state (which protocol's data to display)
typedef enum {
    PROTOCOL_VIEW_ALL,       // Show aggregated data from all protocols
    PROTOCOL_VIEW_WIFI,      // Show WiFi-specific metrics
    PROTOCOL_VIEW_BLE,       // Show BLE-specific metrics
    PROTOCOL_VIEW_ESPNOW,    // Show ESP-NOW-specific metrics
    PROTOCOL_VIEW_LORA,      // Show LoRa-specific metrics
} protocol_view_t;
```

### State Transition Examples

**IDLE → MULTI_RANGING (user presses start/stop scanning button):**
```c
system_state_t sys_state_handle_start_ranging(void) {
    // 1. Update displays to show ranging screens
    display_1_show_gps_screen();  // Positions, distance, speed
    display_2_show_status_screen();  // Signal strength, power

    // 2. Enable ALL wireless modules to broadcast/listen simultaneously
    wifi_enable_broadcast_mode();
    ble_enable_broadcast_mode();
    espnow_enable_broadcast_mode();
    lora_enable_broadcast_mode();

    // 3. Start all wireless tasks
    vTaskResume(wifi_task_handle);
    vTaskResume(ble_task_handle);
    vTaskResume(espnow_task_handle);
    vTaskResume(lora_task_handle);

    // 4. GPS needs high update rate for accurate tracking
    gps_set_update_rate(GPS_UPDATE_10HZ);

    // 5. Enable current monitoring
    ina226_start_continuous_monitoring();

    return SYS_STATE_MULTI_RANGING;
}
```

**MULTI_RANGING → IDLE (user presses stop scanning button):**
```c
system_state_t sys_state_handle_stop_ranging(void) {
    // 1. Disable all wireless modules
    wifi_disable();
    ble_disable();
    espnow_disable();
    lora_disable();

    // 2. Suspend wireless tasks
    vTaskSuspend(wifi_task_handle);
    vTaskSuspend(ble_task_handle);
    vTaskSuspend(espnow_task_handle);
    vTaskSuspend(lora_task_handle);

    // 3. Reduce GPS update rate
    gps_set_update_rate(GPS_UPDATE_1HZ);

    // 4. Update display to idle screen
    display_1_show_idle_screen();

    return SYS_STATE_IDLE;
}
```

**Protocol View Cycling (user presses cycle display button):**
```c
protocol_view_t handle_cycle_protocol_view(protocol_view_t current_view) {
    // Cycle through protocol views: ALL → WiFi → BLE → ESP-NOW → LoRa → ALL
    protocol_view_t next_view;

    switch (current_view) {
        case PROTOCOL_VIEW_ALL:
            next_view = PROTOCOL_VIEW_WIFI;
            break;
        case PROTOCOL_VIEW_WIFI:
            next_view = PROTOCOL_VIEW_BLE;
            break;
        case PROTOCOL_VIEW_BLE:
            next_view = PROTOCOL_VIEW_ESPNOW;
            break;
        case PROTOCOL_VIEW_ESPNOW:
            next_view = PROTOCOL_VIEW_LORA;
            break;
        case PROTOCOL_VIEW_LORA:
            next_view = PROTOCOL_VIEW_ALL;
            break;
    }

    // Update both displays to show selected protocol's metrics
    display_1_set_protocol_view(next_view);
    display_2_set_protocol_view(next_view);

    ESP_LOGI(TAG, "Protocol view changed to: %s", protocol_to_string(next_view));

    return next_view;
}
```

### System Manager Task
```c
void system_manager_task(void *pvParameters) {
    system_state_t current_state = SYS_STATE_BOOT;
    event_t event;
    
    while (1) {
        // Wait for events (button press, GPS fix, timeout, etc.)
        if (xQueueReceive(system_event_queue, &event, pdMS_TO_TICKS(100))) {
            
            // State machine - handle event based on current state
            switch (current_state) {
                case SYS_STATE_IDLE:
                    if (event.type == EVENT_BUTTON_SCAN) {
                        current_state = sys_state_handle_scan_button();
                    } else if (event.type == EVENT_BUTTON_RANGE) {
                        current_state = sys_state_handle_range_button();
                    }
                    // Early return pattern for invalid events
                    break;
                    
                case SYS_STATE_SCANNING:
                    if (event.type == EVENT_SCAN_COMPLETE) {
                        display_show_scan_results(event.data);
                        current_state = sys_state_return_to_tracking();
                    }
                    break;
                    
                // ... other states
            }
            
            ESP_LOGI(TAG, "State transition: %s", state_to_string(current_state));
        }
        
        // Periodic housekeeping (idle timeouts, battery checks, etc.)
        sys_state_periodic_check(current_state);
    }
}
```

### Event-Driven Architecture
All modules communicate through the system manager via events:
```c
typedef enum {
    // Button events
    EVENT_BUTTON_CYCLE_VIEW,      // Button 1: Cycle display/protocol view
    EVENT_BUTTON_START_STOP,      // Button 2: Start/stop ranging
    EVENT_BUTTON_POWER_WAKE,      // Button 3: Power/wake

    // GPS events
    EVENT_GPS_FIX_ACQUIRED,
    EVENT_GPS_FIX_LOST,
    EVENT_GPS_POSITION_UPDATE,

    // Wireless protocol events
    EVENT_WIFI_POSITION_RECEIVED,
    EVENT_BLE_POSITION_RECEIVED,
    EVENT_ESPNOW_POSITION_RECEIVED,
    EVENT_LORA_POSITION_RECEIVED,

    // System events
    EVENT_IDLE_TIMEOUT,
    EVENT_BATTERY_LOW,
    EVENT_BATTERY_CRITICAL,
    EVENT_SD_CARD_FULL,
} event_type_t;

typedef struct {
    event_type_t type;
    void *data;           // Event-specific data
    uint32_t timestamp;
} event_t;

// Position data structure (shared across all protocols)
typedef struct {
    char device_id[16];
    double latitude;
    double longitude;
    uint32_t timestamp;
    float signal_strength;  // Protocol-specific (RSSI, SNR, etc.)
} position_data_t;
```

Modules post events, system manager routes them:
```c
// Button module posts event
event_t event = {
    .type = EVENT_BUTTON_SCAN,
    .data = NULL,
    .timestamp = xTaskGetTickCount()
};
xQueueSend(system_event_queue, &event, pdMS_TO_TICKS(10));

// GPS module posts event
event_t gps_event = {
    .type = EVENT_GPS_FIX_ACQUIRED,
    .data = &current_position,
    .timestamp = xTaskGetTickCount()
};
xQueueSend(system_event_queue, &gps_event, 0);
```

---

## Multi-Protocol Data Tracking

### Independent Protocol Tracking
Each wireless protocol maintains its own state for the remote device:

```c
typedef struct {
    position_data_t last_position;    // Last received position
    double distance_meters;           // Calculated distance
    double relative_speed_mps;        // Relative speed (m/s)
    uint32_t last_update_time;        // When last position was received
    float rssi_dbm;                   // Signal strength (protocol-specific)
    uint32_t packet_count;            // Total packets received
    uint32_t packet_loss;             // Missed packets
    bool is_active;                   // Currently receiving data
} protocol_state_t;

// Global state for each protocol
protocol_state_t wifi_state;
protocol_state_t ble_state;
protocol_state_t espnow_state;
protocol_state_t lora_state;
```

### Distance Calculation
Each protocol independently calculates distance when new position is received:

```c
// Haversine formula for great-circle distance
double calculate_distance(double lat1, double lon1, double lat2, double lon2) {
    const double R = 6371000.0;  // Earth radius in meters
    double dLat = (lat2 - lat1) * M_PI / 180.0;
    double dLon = (lon2 - lon1) * M_PI / 180.0;

    double a = sin(dLat/2) * sin(dLat/2) +
               cos(lat1 * M_PI / 180.0) * cos(lat2 * M_PI / 180.0) *
               sin(dLon/2) * sin(dLon/2);

    double c = 2 * atan2(sqrt(a), sqrt(1-a));
    return R * c;
}

// Each protocol updates its state when position received
void update_protocol_state(protocol_state_t *state, position_data_t *new_pos) {
    // Calculate distance from our position to received position
    state->distance_meters = calculate_distance(
        our_gps_lat, our_gps_lon,
        new_pos->latitude, new_pos->longitude
    );

    // Calculate relative speed (change in distance / time)
    uint32_t time_delta = new_pos->timestamp - state->last_update_time;
    if (time_delta > 0) {
        double distance_change = state->distance_meters -
                                calculate_distance(our_gps_lat, our_gps_lon,
                                                  state->last_position.latitude,
                                                  state->last_position.longitude);
        state->relative_speed_mps = distance_change / (time_delta / 1000.0);
    }

    state->last_position = *new_pos;
    state->last_update_time = xTaskGetTickCount();
    state->is_active = true;
}
```

### Protocol View Display Logic
When user cycles protocol view, displays show data from selected protocol:

```c
void display_update(protocol_view_t view) {
    protocol_state_t *state;
    const char *protocol_name;

    switch (view) {
        case PROTOCOL_VIEW_WIFI:
            state = &wifi_state;
            protocol_name = "WiFi";
            break;
        case PROTOCOL_VIEW_BLE:
            state = &ble_state;
            protocol_name = "BLE";
            break;
        case PROTOCOL_VIEW_ESPNOW:
            state = &espnow_state;
            protocol_name = "ESP-NOW";
            break;
        case PROTOCOL_VIEW_LORA:
            state = &lora_state;
            protocol_name = "LoRa";
            break;
        case PROTOCOL_VIEW_ALL:
            // Show aggregate/best available
            state = get_best_protocol_state();
            protocol_name = "ALL";
            break;
    }

    // Display 1: GPS data
    display_1_show_position(our_gps_lat, our_gps_lon,
                           state->last_position.latitude,
                           state->last_position.longitude,
                           state->distance_meters,
                           state->relative_speed_mps,
                           protocol_name);

    // Display 2: Status
    display_2_show_status(state->rssi_dbm,
                         state->packet_count,
                         state->packet_loss,
                         battery_voltage,
                         current_draw_ma,
                         protocol_name);
}
```

---

## ESP-IDF Components & Libraries

### Prefer Built-in Components
- **GPS**: UART driver + NMEA parsing library (e.g., TinyGPS++)
- **WiFi**: `esp_wifi` for WiFi Direct broadcasting (Vendor Specific Action frames)
- **BLE**: `esp_gap_ble` for BLE advertising
- **ESP-NOW**: `esp_now` protocol (2.4GHz)
- **LoRa**: SX127x library for LR02 module (SPI)
- **Display**: SSD1306 driver library (I2C)
- **Current Monitor**: INA226 library (I2C)
- **SD Card**: `esp_vfs_fat` + SPI SD card driver
- **NVS**: `nvs_flash` for storing settings, device ID
- **Power**: `esp_pm` for power management configuration

### Custom Code Only When Necessary
- Distance calculation (haversine formula)
- Relative speed calculation
- Protocol state management
- Multi-display coordination

---

## Build & Flash

### Build Commands
```bash
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

### Menuconfig Key Settings
- Component config → ESP32-specific → Power Management → Enable
- Component config → FreeRTOS → Tick rate Hz → 1000
- GPS UART settings in custom Kconfig
- LoRa SPI settings in custom Kconfig

---

## Development Guidelines

### DO:
- ✅ Start every function with error checks (negative space)
- ✅ Use `static` for functions not exposed in headers
- ✅ Check return values explicitly
- ✅ Log all system state transitions and events
- ✅ Use const for read-only data
- ✅ Document GPIO assignments in pin_config.h
- ✅ Suspend tasks when not needed by current system state
- ✅ Post events to system manager, don't call other modules directly
- ✅ Keep modules independent - they shouldn't know about each other

### DON'T:
- ❌ Leave peripherals running when not needed
- ❌ Use delay loops (use vTaskDelay instead)
- ❌ Nest positive logic (use early returns)
- ❌ Ignore return values
- ❌ Use magic numbers (define constants)
- ❌ Block in ISRs (use queues to defer work)

---

## Testing & Debugging

### System State Machine Testing
- Log all state transitions with timestamps
- Verify states transition correctly on each event type
- Test invalid event handling (e.g., scan button during scanning)
- Monitor task suspension/resumption patterns

### Power Consumption Monitoring
- Use `esp_pm_dump_locks()` to see what's keeping CPU awake
- Monitor current draw to verify modules actually sleep in idle states
- Verify system manager puts modules to sleep when not needed

### UX Flow Testing
- Test all button combinations in each state
- Verify display updates match system state
- Test timeout behaviors (idle → sleep transitions)
- Handle rapid button presses gracefully

### GPS Testing
- Verify NMEA parsing with known-good sentences
- Test behavior with no GPS fix (indoor)
- Validate distance calculations against known coordinates

### Wireless Testing
- Scan with phone/laptop to verify detection
- Test range of LoRa vs ESP-NOW vs WiFi
- Handle case where no protocols detected

---

## Future Enhancements (Notes for Rowing Computer)
- Add more system states (e.g., configuration mode, logging mode)
- Implement state persistence across deep sleep (NVS)
- Add battery voltage monitoring as a system state consideration
- SD card logging integrated into system state machine
- Web server configuration mode (new system state)
- Real-time clock for timestamping events
- Geofencing as a system state trigger
- Multi-device communication via LoRa/ESP-NOW (coordinator state)

---

## GPIO Pin Planning (ESP32 Classic)

### Suggested Pin Assignments
Based on ESP32 classic capabilities and avoiding boot-sensitive pins:

**UART (GPS - NEO-M8N):**
- GPIO16 (RXD2): GPS TX → ESP32 RX
- GPIO17 (TXD2): ESP32 TX → GPS RX (optional, for configuration)

**I2C Bus (Displays + Current Monitor):**
- GPIO21 (SDA): I2C Data
- GPIO22 (SCL): I2C Clock
- Devices:
  - Display 1 (SSD1306): 0x3C
  - Display 2 (SSD1306): 0x3D
  - INA226: 0x40

**SPI Bus (LoRa + SD Card):**
- GPIO18 (SCK): SPI Clock
- GPIO19 (MISO): SPI Master In Slave Out
- GPIO23 (MOSI): SPI Master Out Slave In
- GPIO5: LoRa CS (Chip Select)
- GPIO15: SD Card CS (Chip Select)
- GPIO2: LoRa Reset (optional)
- GPIO4: LoRa DIO0/IRQ (interrupt)

**Buttons (with internal pull-up):**
- GPIO32: Button 1 (Cycle View)
- GPIO33: Button 2 (Start/Stop)
- GPIO27: Button 3 (Power/Wake)

**Status LED (optional):**
- GPIO25: Status LED (PWM capable for breathing effect)

**Reserved/Avoid:**
- GPIO0: Boot mode (pull-up, avoid)
- GPIO2: Boot mode (pull-down, used for LoRa reset)
- GPIO12: Boot voltage (avoid if possible)
- GPIO15: Boot mode (pull-up, used for SD CS)
- GPIO1/3: UART0 (USB/serial debug)
- GPIO6-11: Flash (never use)

**Available for Future:**
- GPIO13, GPIO14, GPIO26, GPIO34-39 (input only)

### Pin Configuration Notes
- All button pins use internal pull-ups (buttons connect to GND)
- I2C requires external 4.7kΩ pull-ups
- SPI pins can be remapped via software if needed
- GPIO34-39 are input-only (no pull-up/pull-down)
- Avoid GPIO0, GPIO2, GPIO12, GPIO15 for critical boot functions

---

## Questions for Claude Code
When working on this project, Claude should ask about:
- Actual board being used (may affect default pin availability)
- Target battery life goals (affects sleep strategy)
- Desired GPS update rate in different modes (affects power consumption)
- SD card logging frequency and format preference
- Display refresh rate preferences

---

## UX & State Machine Design Principles

### Why This Architecture?
This system manager approach provides several benefits for embedded systems:

1. **Centralized Control**: Single place to understand application flow
2. **Testability**: Can test state machine logic independently from hardware
3. **Power Efficiency**: Natural side-effect of proper state management
4. **Scalability**: Easy to add new states/features without touching existing code
5. **Debugging**: Clear audit trail of state transitions

### State Machine Best Practices
```c
// Keep state handlers independent - no cross-dependencies
system_state_t handle_state_X(event_t *event) {
    // Exit early if event doesn't apply to this state
    if (!is_valid_event_for_state(event, SYS_STATE_X)) {
        ESP_LOGW(TAG, "Invalid event %d in state X", event->type);
        return SYS_STATE_X;  // Stay in current state
    }
    
    // Clean up current state before transitioning
    cleanup_state_X();
    
    // Setup next state
    setup_state_Y();
    
    return SYS_STATE_Y;
}
```

### Module Independence
Modules should be **dumb workers** - they don't make decisions:
- GPS module: "I have a fix" (event) → System manager decides what to do with it
- Button module: "Button pressed" (event) → System manager interprets context
- Display module: "Show this screen" (command) → Display renders, doesn't decide what to show

**Anti-pattern to avoid:**
```c
// BAD - GPS module shouldn't control display directly
void gps_task(void) {
    if (gps_has_fix()) {
        display_update_position(lat, lon);  // ❌ Module coupling
    }
}

// GOOD - GPS posts event, system manager coordinates
void gps_task(void) {
    if (gps_has_fix()) {
        event_t evt = { .type = EVENT_GPS_FIX_ACQUIRED, .data = &pos };
        xQueueSend(system_event_queue, &evt, 0);  // ✅ Let system manager decide
    }
}
```

### Practicing for Rowing Computer
Key patterns to master here that will transfer to rowing computer:
- Event-driven architecture scales well (add more sensors/inputs easily)
- State machine makes complex UX flows manageable
- Power optimization becomes automatic when states are well-designed
- Testing complex embedded systems becomes tractable

---

## Related Documentation
- ESP-IDF Power Management: https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/system/power_management.html
- FreeRTOS: https://www.freertos.org/a00106.html
- NEO-M8N Manual: [Link to datasheet]
- LRO2 LoRa Module: [Link to datasheet]
