# Printer ESP32 - Technical Documentation

## Table of Contents

1. [Overview](#overview)
2. [System Architecture](#system-architecture)
3. [Hardware Configuration](#hardware-configuration)
4. [Communication Interfaces](#communication-interfaces)
5. [Print Processing Logic](#print-processing-logic)
6. [Timestamp & Formatting](#timestamp--formatting)
7. [Bluetooth Connection Management](#bluetooth-connection-management)
8. [Button Operation Modes](#button-operation-modes)
9. [Config Mode (BLE)](#config-mode-ble)
10. [Persistent Storage](#persistent-storage)
11. [Code Flow](#code-flow)
12. [Configuration Parameters](#configuration-parameters)
13. [Troubleshooting](#troubleshooting)

---

## Overview

The Printer ESP32 is the output module in the weighing scale system. It receives formatted weight data from the Hub ESP32 via UART, adds timestamps using an RTC module, and transmits to a Bluetooth-connected printer.

### Key Features

- **UART Input**: Receives print-ready data from Hub ESP32
- **RTC Timestamping**: DS3231 module for accurate date/time stamps
- **Bluetooth Output**: Supports thermal and dot-matrix printers
- **Bypass Mode**: Latched button to skip timestamp for quick prints
- **Auto-Reconnect**: Non-blocking FreeRTOS task maintains Bluetooth connection
- **BLE Config Mode**: Configure RTC time, printer MAC, and printer type via mobile app
- **Persistent Settings**: MAC address and printer type stored in NVS flash

---

## System Architecture

### System Context

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                         WEIGHING SCALE SYSTEM                                │
└─────────────────────────────────────────────────────────────────────────────┘

    ┌─────────────────┐         ┌─────────────────┐         ┌─────────────────┐
    │  Mettler Toledo │         │                 │ ESP-NOW │  LCD Receiver   │
    │    or A&D       │  RS232  │                 │────────►│     ESP32       │
    │     Scale       │────────►│   Hub ESP32     │         │   (16x1 LCD)    │
    └─────────────────┘         │                 │         └─────────────────┘
                                │  - Poll scale   │
                                │  - Detect print │
                                │  - Route data   │
                                └────────┬────────┘
                                         │
                                         │ UART1 (TX only)
                                         │
                                         ▼
                    ┌────────────────────────────────────────┐
                    │                                        │
                    │          Printer ESP32                 │
                    │                                        │
                    │  ┌──────────┐    ┌──────────────────┐ │
                    │  │  DS3231  │    │                  │ │
                    │  │   RTC    │    │  Bluetooth       │ │      ┌──────────┐
                    │  └──────────┘    │  Classic/BLE     │─┼─────►│ Thermal  │
                    │                  │                  │ │      │ Printer  │
                    │  ┌──────────┐    └──────────────────┘ │      └──────────┘
                    │  │  Latched │                         │            or
                    │  │  Button  │                         │      ┌──────────┐
                    │  └──────────┘                         │      │Dotmatrix │
                    │                                       │      │ Printer  │
                    └───────────────────────────────────────┘      └──────────┘
```

### Module Responsibilities

| Component | Responsibility |
|-----------|----------------|
| Hub ESP32 | Detect print button, send data to Printer ESP32 |
| **Printer ESP32** | Add timestamp, format output, send to Bluetooth printer |
| RTC (DS3231) | Provide accurate date/time for receipts |
| Bluetooth Classic | Wireless connection to physical printer |
| BLE | Configuration via mobile app (Nordic UART Service) |

---

## Hardware Configuration

### PCB Header Swap Issue

**IMPORTANT**: The PCB has left/right headers swapped. Pin remapping is done in software:

| Original GPIO | Remapped GPIO | Function | Notes |
|---------------|---------------|----------|-------|
| 25 | 5 | LED_PIN | Software fixable |
| 26 | 17 | BUTTON_PIN | Software fixable |
| 16 | 27 | HUB_RX (UART) | Software fixable |
| 22 | 36 | I2C SCL | INPUT ONLY - needs bodge wire |
| 21 | 35 | I2C SDA | INPUT ONLY - needs bodge wire |

### Pin Assignments (After Remapping)

| GPIO | Function | Direction | Description |
|------|----------|-----------|-------------|
| 27 | HUB_RX | Input | UART1 RX - Data from Hub ESP32 |
| 5 | LED_PIN | Output | Status LED (simple on/off) |
| 17 | BUTTON_PIN | Input | Config mode toggle / Timestamp bypass (latched, active LOW) |
| 21 | I2C SDA | Bidirectional | RTC data line (requires bodge wire) |
| 22 | I2C SCL | Output | RTC clock line (requires bodge wire) |

### LED Configuration

| State | Meaning |
|-------|---------|
| OFF | Not connected to printer |
| ON (solid) | Connected to printer |
| Fast blink (100ms) | Config mode active |

### Wiring Diagram

```
                                    ┌─────────────────────┐
                                    │    Printer ESP32    │
                                    │   (Headers Swapped) │
    From Hub ESP32                  │                     │
    ┌──────────┐                    │  ┌───────────────┐  │
    │ GPIO 19  │───────────────────►│──│ GPIO 27 (RX)  │  │
    │   (TX)   │                    │  └───────────────┘  │
    └──────────┘                    │                     │
    ┌──────────┐                    │  ┌───────────────┐  │
    │   GND    │───────────────────►│──│     GND       │  │
    └──────────┘                    │  └───────────────┘  │
                                    │                     │
    DS3231 RTC Module               │                     │
    ┌──────────┐                    │  ┌───────────────┐  │
    │   SDA    │◄──────────────────►│──│ GPIO 21 (SDA) │  │  ← Bodge wire needed
    │   SCL    │◄───────────────────│──│ GPIO 22 (SCL) │  │  ← Bodge wire needed
    │   VCC    │◄───────────────────│──│     3.3V      │  │
    │   GND    │◄───────────────────│──│     GND       │  │
    └──────────┘                    │  └───────────────┘  │
                                    │                     │
    Latched Button (Toggle Switch)  │  ┌───────────────┐  │
    ┌──────────┐                    │  │ GPIO 17       │  │
    │  Common  │────────────────────│──│ (INPUT_PULLUP)│  │
    │   Pole   │────────────────────│──│     GND       │  │
    └──────────┘                    │  └───────────────┘  │
                                    │                     │
    Status LED                      │  ┌───────────────┐  │
    ┌──────────┐                    │  │ GPIO 5        │  │
    │  Anode   │◄───[330Ω]──────────│──│   (OUTPUT)    │  │
    │ Cathode  │────────────────────│──│     GND       │  │
    └──────────┘                    │  └───────────────┘  │
                                    │                     │
                                    │     )))  Bluetooth  │
                                    │                     │
                                    └─────────────────────┘
```

### UART Configuration

| UART | Baud Rate | Config | Purpose |
|------|-----------|--------|---------|
| Serial | 9600 | 8N1 | Debug output (USB) |
| Serial1 | 9600 | 8N1 | RX only from Hub ESP32 (GPIO 27) |

### I2C Configuration

| Parameter | Value |
|-----------|-------|
| Clock Speed | 100 kHz (standard) |
| RTC Address | 0x68 |
| Pull-ups | External 4.7kΩ (on DS3231 module) |

**Note**: GPIO 35/36 are input-only on ESP32. If using swapped headers, I2C requires hardware bodge wires to GPIO 21/22.

---

## Communication Interfaces

### UART Input (from Hub ESP32)

**Connection**: One-way (RX only)
**Baud Rate**: 9600
**GPIO**: 27 (remapped from 16)
**Format**: ASCII string terminated with `\n`

**Expected Data Format**:
```
 *     123.45 g\n      ← 16-char LCD-formatted weight
```

### Bluetooth Classic Output (to Printer)

**Mode**: Master (ESP32 initiates connection)
**Protocol**: Bluetooth Classic SPP (Serial Port Profile)
**PIN**: Configurable (default "0000")

**Supported Printers**:
| Type | Value | Paper Feed | Buffer Flush |
|------|-------|------------|--------------|
| THERMAL | 0 | 3 lines | ESC J 0 needed |
| DOT | 1 | 12 lines | Not needed |

**Connection Parameters**:
```cpp
uint8_t printerBtAddress[6] = {0xDC, 0x0D, 0x30, 0x00, 0x28, 0x89};  // Loaded from NVS
const char* btPin = "0000";
uint8_t printerType = 0;  // 0=THERMAL, 1=DOT (Loaded from NVS)
```

### BLE Configuration Interface (Nordic UART Service)

**Mode**: Server (ESP32 advertises, phone connects)
**Service**: Nordic UART Service (NUS)

**UUIDs**:
```cpp
SERVICE_UUID:           "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
CHARACTERISTIC_UUID_RX: "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"  // Phone → ESP32
CHARACTERISTIC_UUID_TX: "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"  // ESP32 → Phone
```

**Compatible Apps**:
- LightBlue (iOS/Android)
- nRF Connect (iOS/Android)

**Important**: BLE and Bluetooth Classic cannot run simultaneously on ESP32. Config mode stops Bluetooth Classic before starting BLE.

---

## Print Processing Logic

### Data Reception Flow

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                         Data Reception Flow                                  │
└─────────────────────────────────────────────────────────────────────────────┘

    Serial1.available()
            │
            ▼
    ┌───────────────┐
    │ Read until    │
    │ '\n'          │
    └───────┬───────┘
            │
            ▼
    ┌───────────────┐
    │ Trim          │
    │ whitespace    │
    └───────┬───────┘
            │
            ▼
    ┌───────────────┐     Yes    ┌───────────────┐
    │ Is filtered   │───────────►│ Discard       │
    │ string?       │            │ (return)      │
    └───────┬───────┘            └───────────────┘
            │ No
            ▼
    ┌───────────────┐
    │ Check BT      │
    │ connection    │
    └───────┬───────┘
            │
     ┌──────┴──────┐
     │ Connected   │ Not Connected
     ▼             ▼
┌─────────┐  ┌─────────────┐
│ Print   │  │ Log error,  │
│ data    │  │ skip print  │
└─────────┘  └─────────────┘
```

### String Filtering

| Filtered String | Reason |
|-----------------|--------|
| `------------------------` | Separator line (printed by formatter) |
| `........................` | Signature line (printed by formatter) |
| `Signature` | Label (printed by formatter) |
| `NO STABILITY` | Silently ignored (no print output) |

---

## Timestamp & Formatting

### Print Format Modes

#### Mode 1: Full Receipt (Button Pressed/Latched - LOW)

```
┌────────────────────────────────────┐
│ 11/12/2025 (Thursday) 14:30:45     │  ← Timestamp line
│  *     123.45 g                    │  ← Weight data
│ Signature                          │  ← Signature label
│                                    │  ← Blank line
│ ------------------------           │  ← Separator
│ ........................           │  ← Signature dots
│                                    │  ← Paper feed (3 or 12 lines)
└────────────────────────────────────┘
```

**Paper Feed by Printer Type**:
| Type | Command | Lines |
|------|---------|-------|
| THERMAL | `\x1B\x64\x03` | 3 lines |
| DOT | `\x1B\x64\x0C` | 12 lines |

#### Mode 2: Bypass Print (Button Not Pressed - HIGH)

```
┌────────────────────────────────────┐
│  *     123.45 g                    │  ← Weight data only
└────────────────────────────────────┘
```

**Buffer Flush**: Uses `ESC J 0` (`{0x1B, 0x4A, 0x00}`) to flush print buffer without paper feed.

---

## Bluetooth Connection Management

### Non-Blocking Reconnection (FreeRTOS)

The reconnection uses a FreeRTOS background task to prevent blocking the main loop:

```cpp
void reconnectTask(void* parameter) {
    Serial.println("Attempting Bluetooth reconnect...");

    if (SerialBT.connect(printerBtAddress)) {
        btConnected = true;
        digitalWrite(LED_PIN, HIGH);
        Serial.println("Bluetooth reconnected!");
    } else {
        Serial.println("Bluetooth reconnect failed");
    }

    reconnectInProgress = false;
    vTaskDelete(NULL);  // Delete this task when done
}

void reconnectBluetooth() {
    static unsigned long lastReconnectAttempt = 0;
    static bool firstAttempt = true;
    unsigned long now = millis();

    // First attempt immediately, then every 10 seconds
    unsigned long interval = firstAttempt ? 0 : 10000;
    if (!reconnectInProgress && (now - lastReconnectAttempt >= interval)) {
        firstAttempt = false;
        lastReconnectAttempt = now;
        reconnectInProgress = true;

        xTaskCreate(reconnectTask, "BT_Reconnect", 4096, NULL, 1, NULL);
    }
}
```

**Benefits**:
- Main loop remains responsive (button still works during reconnect)
- SerialBT.connect() blocks for ~10 seconds - now runs in background
- Task self-deletes when complete

### Connection State Machine

```
                         ┌─────────────────┐
                         │     STARTUP     │
                         └────────┬────────┘
                                  │
                                  ▼
                         ┌─────────────────┐
                         │  SerialBT.begin │
                         │  (Master mode)  │
                         └────────┬────────┘
                                  │
                                  ▼
                         ┌─────────────────┐
                         │ Background task │
                         │ connects        │
                         └────────┬────────┘
                                  │
                    ┌─────────────┴─────────────┐
                    │ Success                   │ Failure
                    ▼                           ▼
           ┌─────────────────┐         ┌─────────────────┐
           │   CONNECTED     │         │  DISCONNECTED   │
           │   LED = ON      │         │   LED = OFF     │
           └────────┬────────┘         └────────┬────────┘
                    │                           │
                    │◄──────────────────────────┘
                    │        Reconnect task
                    │        (every 10 seconds)
                    ▼
           ┌─────────────────┐
           │ Non-blocking    │
           │ check in loop   │
           │ connected(0)    │
           └─────────────────┘
```

---

## Button Operation Modes

### Physical Button Configuration

The system uses a **latched button (toggle switch)**, not a momentary button.

```
              3.3V
               │
               │
         ┌─────┴─────┐
         │  Internal │
         │  Pull-up  │
         └─────┬─────┘
               │
    GPIO 17 ───┼─────────┐
               │         │
               │       ──┴──
               │       ─┬──   Latched
               │        │     Toggle Switch
               │       ─┴─
               │        │
              GND ──────┘
```

### Button Functions

| Action | Result |
|--------|--------|
| Toggle ON (LOW) | Timestamp mode - full receipt with date/time |
| Toggle OFF (HIGH) | Bypass mode - weight only |
| Press 4 times in 4 seconds | Toggle config mode |

### Config Mode Entry/Exit

```cpp
const unsigned long DEBOUNCE_DELAY = 50;
const unsigned long PRESS_TIMEOUT = 4000;  // 4 seconds for 4 presses

// 4 presses detected:
if (buttonPressCount >= 4) {
    buttonPressCount = 0;
    if (configMode) {
        exitConfigMode();   // Exit config, reconnect to printer
    } else {
        enterConfigMode();  // Enter config, start BLE
    }
}
```

---

## Config Mode (BLE)

### Entry Methods
1. Press button 4 times within 4 seconds

### Exit Methods
1. Press button 4 times within 4 seconds
2. Send "EXIT", "X", or "Q" command via BLE

### BLE Commands

| Command | Description | Example |
|---------|-------------|---------|
| `TIME YY/MM/DD HH:MM:SS DOW` | Set RTC time (DOW = day of week 1-7) | `TIME 25/01/17 14:30:00 6` |
| `MAC XX:XX:XX:XX:XX:XX` | Set printer Bluetooth MAC address | `MAC DC:0D:30:00:28:89` |
| `TYPE THERMAL` or `TYPE DOT` | Set printer type (also: T, D, 0, 1) | `TYPE THERMAL` |
| `STATUS` or `S` or `?` | Show current settings | `STATUS` |
| `EXIT` or `X` or `Q` | Exit config mode and reconnect | `EXIT` |
| `HELP` or `H` | Show command list | `HELP` |

### BLE Threading Safety

Callbacks only buffer data; processing happens in main loop:

```cpp
class RxCallbacks : public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic* pCharacteristic) {
        std::string rxValue = pCharacteristic->getValue();
        // Buffer characters until newline
        for (size_t i = 0; i < rxValue.length(); i++) {
            char c = rxValue[i];
            if (c == '\n' || c == '\r') {
                if (bleRxBuffer.length() > 0 && bleCmdReady.length() == 0) {
                    bleCmdReady = bleRxBuffer;  // Ready for main loop
                    bleRxBuffer = "";
                }
            } else {
                bleRxBuffer += c;
            }
        }
    }
};

// In loop():
if (bleCmdReady.length() > 0) {
    processSetupCommand(bleCmdReady);
    bleCmdReady = "";
}
```

---

## Persistent Storage

### Preferences (NVS) Configuration

**Namespace**: `"printer"`

| Key | Type | Description |
|-----|------|-------------|
| `mac` | 6 bytes | Printer Bluetooth MAC address |
| `type` | uint8_t | Printer type (0=THERMAL, 1=DOT) |

### Persistence Behavior

| Action | Settings Preserved? |
|--------|---------------------|
| Reboot | Yes |
| Firmware flash | Yes |
| Full flash erase (`esptool erase_flash`) | No |

### Load/Save Functions

```cpp
void loadPrinterMac() {
    preferences.begin("printer", true);  // Read-only
    if (preferences.isKey("mac")) {
        preferences.getBytes("mac", printerBtAddress, 6);
    }
    preferences.end();
}

void savePrinterMac() {
    preferences.begin("printer", false);  // Read-write
    preferences.putBytes("mac", printerBtAddress, 6);
    preferences.end();
}

void loadPrinterType() {
    preferences.begin("printer", true);
    if (preferences.isKey("type")) {
        printerType = preferences.getUChar("type", 0);
    }
    preferences.end();
}

void savePrinterType() {
    preferences.begin("printer", false);
    preferences.putUChar("type", printerType);
    preferences.end();
}
```

---

## Code Flow

### Initialization Sequence

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                              setup()                                         │
└─────────────────────────────────────────────────────────────────────────────┘
                                  │
                                  ▼
                         ┌─────────────────┐
                         │  delay(3000)    │  ← Wait for power stabilization
                         └────────┬────────┘
                                  │
                                  ▼
                         ┌─────────────────┐
                         │ URTCLIB_WIRE    │
                         │ .begin()        │  ← Initialize I2C for RTC
                         └────────┬────────┘
                                  │
                                  ▼
                         ┌─────────────────┐
                         │ Load settings   │
                         │ from NVS        │  ← loadPrinterMac(), loadPrinterType()
                         └────────┬────────┘
                                  │
                                  ▼
                         ┌─────────────────┐
                         │ pinMode         │
                         │ BUTTON (17)     │  ← INPUT_PULLUP
                         │ LED (5)         │  ← OUTPUT, initially OFF
                         └────────┬────────┘
                                  │
                                  ▼
                         ┌─────────────────┐
                         │ SerialBT.begin  │
                         │ (Master mode)   │  ← Initialize Bluetooth Classic
                         └────────┬────────┘
                                  │
                                  ▼
                         ┌─────────────────┐
                         │ Ready!          │
                         │ (No blocking    │  ← Connection handled by
                         │  connect here)  │     background task in loop
                         └─────────────────┘
```

### Main Loop Flow

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                               loop()                                         │
└─────────────────────────────────────────────────────────────────────────────┘
                                  │
                                  ▼
                         ┌─────────────────┐
                         │ Check button    │
                         │ for config mode │  ← 4 presses = toggle config
                         └────────┬────────┘
                                  │
                    ┌─────────────┴─────────────┐
                    │ Config Mode               │ Normal Mode
                    ▼                           ▼
           ┌─────────────────┐         ┌─────────────────┐
           │ Fast blink LED  │         │ Check Serial1   │
           │ Process BLE cmd │         │ for data        │
           │ Check exit flag │         │                 │
           └─────────────────┘         └────────┬────────┘
                                                │
                                                ▼
                                       ┌─────────────────┐
                                       │ If data &&      │
                                       │ btConnected     │
                                       │ → print         │
                                       └────────┬────────┘
                                                │
                                                ▼
                                       ┌─────────────────┐
                                       │ Check BT status │
                                       │ connected(0)    │  ← Non-blocking
                                       └────────┬────────┘
                                                │
                                       ┌────────┴────────┐
                                       │ Disconnected    │
                                       ▼                 │
                              ┌─────────────────┐        │
                              │ reconnect       │        │
                              │ Bluetooth()     │  ← Background task
                              └─────────────────┘        │
                                                         │
                              [Continue loop] ◄──────────┘
```

---

## Configuration Parameters

### Compile-Time Constants

```cpp
// Pin Definitions (remapped for swapped headers)
#define HUB_RX 27           // UART RX from Hub (was GPIO 16)
#define BUTTON_PIN 17       // Config/bypass button (was GPIO 26)
#define LED_PIN 5           // Status LED (was GPIO 25)

// BLE Nordic UART Service
#define SERVICE_UUID           "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define CHARACTERISTIC_UUID_RX "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"
#define CHARACTERISTIC_UUID_TX "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"

// Button timing
const unsigned long DEBOUNCE_DELAY = 50;
const unsigned long PRESS_TIMEOUT = 4000;  // 4 seconds for 4 presses
```

### Runtime Variables

```cpp
// Bluetooth (loaded from NVS)
uint8_t printerBtAddress[6] = {0xDC, 0x0D, 0x30, 0x00, 0x28, 0x89};
uint8_t printerType = 0;  // 0=THERMAL, 1=DOT
const char* btPin = "0000";

// State
bool btConnected = false;
bool reconnectInProgress = false;
bool configMode = false;
bool bleExitRequested = false;
```

---

## Troubleshooting

### LED Status Reference

| LED State | Meaning | Action |
|-----------|---------|--------|
| OFF | Not connected to printer | Wait for auto-reconnect |
| Solid ON | Connected to printer | Normal operation |
| Fast blink (100ms) | Config mode active | Use BLE app to configure |
| Brief OFF then ON | Print sent successfully | None |

### Common Issues

#### 1. Bluetooth Won't Connect

**Solutions**:
- Verify printer is powered ON
- Check MAC address via BLE config (`STATUS` command)
- Update MAC via BLE (`MAC XX:XX:XX:XX:XX:XX`)
- Ensure printer isn't connected to another device
- Power cycle both ESP32 and printer

#### 2. Button Not Responding

**Check**:
- Button is latched type (toggle switch), not momentary
- Wiring: one terminal to GPIO 17, other to GND
- INPUT_PULLUP is configured (reads HIGH when not pressed)

#### 3. Config Mode Entry

**To enter config mode**:
1. Press button 4 times within 4 seconds
2. LED should start fast blinking
3. Connect with LightBlue or nRF Connect app
4. Look for "PrinterConfig" device
5. Find Nordic UART service and subscribe to TX characteristic

#### 4. RTC Shows Wrong Time

**Set time via BLE**:
```
TIME 25/01/17 14:30:00 6
```
Format: `TIME YY/MM/DD HH:MM:SS DOW` (DOW = day of week, 1=Sunday to 7=Saturday)

#### 5. Wrong Paper Feed Amount

**Set printer type via BLE**:
- For thermal printer: `TYPE THERMAL` (feeds 3 lines)
- For dot matrix: `TYPE DOT` (feeds 12 lines)

### Debug Output Reference

**Normal Startup**:
```
Printer MAC: DC:0D:30:00:28:89
Printer Type: THERMAL

===== Printer ESP32 =====
UART RX from Hub + Bluetooth TX to Printer
==========================

Printer ESP32 ready!
Will connect to printer in background...
```

**Config Mode Entry**:
```
Button press: 1
Button press: 2
Button press: 3
Button press: 4

===== ENTERING CONFIG MODE =====
BLE UART server started, advertising...
BLE Started: PrinterConfig
```

**BLE Command Processing**:
```
[BLE] Client connected
[BLE CMD] STATUS
=== PrinterConfig Status ===
TIME: 25/01/17 14:30:00 6
MAC: DC:0D:30:00:28:89
TYPE: THERMAL
============================
```

---

## Dependencies

### Libraries Required

| Library | Version | Purpose |
|---------|---------|---------|
| BluetoothSerial | Built-in | Bluetooth Classic SPP |
| BLEDevice, BLEServer, BLEUtils, BLE2902 | Built-in | BLE Nordic UART Service |
| Wire | Built-in | I2C communication |
| Preferences | Built-in | NVS persistent storage |
| uRTCLib | 6.2.7+ | DS3231 RTC interface |

### PlatformIO Configuration

```ini
[env:esp32dev]
platform = espressif32
board = esp32doit-devkit-v1
framework = arduino
monitor_speed = 9600
upload_speed = 921600

lib_deps =
    naguissa/uRTCLib@^6.2.7

build_flags =
    -D CORE_DEBUG_LEVEL=0
```

---

## Appendix: Complete Print Output Examples

### Example 1: Full Receipt Mode (THERMAL)

**Input from Hub**: ` *     123.45 g`

**Printed Output**:
```
12/12/2025 (Thursday) 14:30:45
 *     123.45 g
Signature

------------------------
........................
[3 line feed]
```

### Example 2: Full Receipt Mode (DOT)

**Input from Hub**: ` *     123.45 g`

**Printed Output**:
```
12/12/2025 (Thursday) 14:30:45
 *     123.45 g
Signature

------------------------
........................
[12 line feed]
```

### Example 3: Bypass Mode

**Input from Hub**: ` *     123.45 g`

**Printed Output**:
```
 *     123.45 g
[buffer flush, no feed]
```

### Example 4: Instability Warning

**Input from Hub**: `NO STABILITY`

**Printed Output**: *(No output - silently ignored)*

---

*Document updated: January 2025*
*Firmware version with BLE config mode, FreeRTOS reconnect, printer type support*
