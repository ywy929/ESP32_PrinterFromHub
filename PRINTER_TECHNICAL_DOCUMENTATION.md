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
9. [Code Flow](#code-flow)
10. [Configuration Parameters](#configuration-parameters)
11. [Troubleshooting](#troubleshooting)

---

## Overview

The Printer ESP32 is the output module in the weighing scale system. It receives formatted weight data from the Hub ESP32 via UART, adds timestamps using an RTC module, and transmits to a Bluetooth-connected printer.

### Key Features

- **UART Input**: Receives print-ready data from Hub ESP32
- **RTC Timestamping**: DS3231 module for accurate date/time stamps
- **Bluetooth Output**: Supports thermal and dot-matrix printers
- **Bypass Mode**: Button to skip timestamp for quick prints
- **Auto-Reconnect**: Maintains Bluetooth connection reliability

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
                    │  └──────────┘    │  Serial          │─┼─────►│ Thermal  │
                    │                  │                  │ │      │ Printer  │
                    │  ┌──────────┐    └──────────────────┘ │      └──────────┘
                    │  │  Button  │                         │            or
                    │  │ (Bypass) │                         │      ┌──────────┐
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
| Bluetooth | Wireless connection to physical printer |

---

## Hardware Configuration

### Pin Assignments

| GPIO | Function | Direction | Description |
|------|----------|-----------|-------------|
| 16 | HUB_RX | Input | UART1 RX - Data from Hub ESP32 (GPIO 19) |
| 25 | LED_PIN | Output | Status LED (PWM controlled) |
| 26 | BUTTON_PIN | Input | Timestamp bypass button (active LOW) |
| 21 | I2C SDA | Bidirectional | RTC data line |
| 22 | I2C SCL | Output | RTC clock line |

### PWM Configuration (LED)

| Parameter | Value | Description |
|-----------|-------|-------------|
| LED_CHANNEL | 0 | LEDC channel number |
| LED_FREQ | 5000 Hz | PWM frequency |
| LED_RESOLUTION | 8-bit | 0-255 brightness range |
| LED_BRIGHTNESS | 60 | ~25% brightness (60/255) |

### Wiring Diagram

```
                                    ┌─────────────────────┐
                                    │    Printer ESP32    │
                                    │                     │
    From Hub ESP32                  │                     │
    ┌──────────┐                    │  ┌───────────────┐  │
    │ GPIO 19  │───────────────────►│──│ GPIO 16 (RX)  │  │
    │   (TX)   │                    │  └───────────────┘  │
    └──────────┘                    │                     │
    ┌──────────┐                    │  ┌───────────────┐  │
    │   GND    │───────────────────►│──│     GND       │  │
    └──────────┘                    │  └───────────────┘  │
                                    │                     │
    DS3231 RTC Module               │                     │
    ┌──────────┐                    │  ┌───────────────┐  │
    │   SDA    │◄──────────────────►│──│ GPIO 21 (SDA) │  │
    │   SCL    │◄───────────────────│──│ GPIO 22 (SCL) │  │
    │   VCC    │◄───────────────────│──│     3.3V      │  │
    │   GND    │◄───────────────────│──│     GND       │  │
    └──────────┘                    │  └───────────────┘  │
                                    │                     │
    Bypass Button                   │  ┌───────────────┐  │
    ┌──────────┐                    │  │ GPIO 26       │  │
    │    NO    │────────────────────│──│ (INPUT_PULLUP)│  │
    │   COM    │────────────────────│──│     GND       │  │
    └──────────┘                    │  └───────────────┘  │
                                    │                     │
    Status LED                      │  ┌───────────────┐  │
    ┌──────────┐                    │  │ GPIO 25       │  │
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
| Serial1 | 9600 | 8N1 | RX only from Hub ESP32 |

### I2C Configuration

| Parameter | Value |
|-----------|-------|
| Clock Speed | 100 kHz (standard) |
| RTC Address | 0x68 |
| Pull-ups | External 4.7kΩ (on DS3231 module) |

---

## Communication Interfaces

### UART Input (from Hub ESP32)

**Connection**: One-way (RX only)
**Baud Rate**: 9600
**Format**: ASCII string terminated with `\n`

**Expected Data Format**:
```
 *     123.45 g\n      ← 16-char LCD-formatted weight
```

**Data Flow**:
```
Hub ESP32 (GPIO 19 TX) ──────► Printer ESP32 (GPIO 16 RX)
                │
                └─► Serial1.available()
                    Serial1.readStringUntil('\n')
```

### Bluetooth Output (to Printer)

**Mode**: Master (ESP32 initiates connection)
**Protocol**: Bluetooth Classic SPP (Serial Port Profile)
**PIN**: Configurable (default "0000")

**Supported Printers**:
| Type | Characteristics |
|------|-----------------|
| Thermal | Fast, quiet, uses `\x1B\x64\x02` for paper feed |
| Dot-matrix | Slower, louder, may ignore ESC commands |

**Connection Parameters**:
```cpp
uint8_t printerBtAddress[6] = {0xDC, 0x0D, 0x30, 0x00, 0x28, 0x89};
const char* btPin = "0000";
```

**Reconnection Settings**:
| Parameter | Value | Description |
|-----------|-------|-------------|
| Reconnect interval | 5000ms | Time between reconnection attempts |
| Connection timeout | 5000ms | SerialBT.connected() timeout |

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

Certain strings are filtered out to avoid printing unwanted content:

| Filtered String | Reason |
|-----------------|--------|
| `------------------------` | Separator line (printed by formatter) |
| `........................` | Signature line (printed by formatter) |
| `Signature` | Label (printed by formatter) |

**Special Handling**:
| String | Action |
|--------|--------|
| `NO STABILITY` | Silently ignored (no print output) |

### Filter Logic

```cpp
void printWithTimestamp(const char* data) {
    String strData = String(data);
    strData.trim();
    
    // Filter out formatting strings
    if (strData == "------------------------") return;
    if (strData == "........................") return;
    if (strData == "Signature") return;
    
    // Silently ignore unstable readings
    if (strData == "NO STABILITY") {
        return;
    }
    
    // Continue with normal print...
}
```

---

## Timestamp & Formatting

### RTC Time Retrieval

```cpp
rtc.refresh();  // Must call before reading values

int day    = rtc.day();      // 1-31
int month  = rtc.month();    // 1-12
int year   = rtc.year();     // 0-99 (20xx)
int dow    = rtc.dayOfWeek();// 1-7 (Sunday=1)
int hour   = rtc.hour();     // 0-23
int minute = rtc.minute();   // 0-59
int second = rtc.second();   // 0-59
```

### Print Format Modes

#### Mode 1: Full Receipt (Button Released)

```
┌────────────────────────────────────┐
│ 11/12/2025 (Thursday) 14:30:45     │  ← Timestamp line
│  *     123.45 g                    │  ← Weight data
│ Signature                          │  ← Signature label
│                                    │  ← Blank line
│ ------------------------           │  ← Separator
│ ........................           │  ← Signature dots
│                                    │  ← Paper feed
└────────────────────────────────────┘
```

**Code**:
```cpp
if (digitalRead(BUTTON_PIN) == LOW) {  // Button pressed
    // Print timestamp
    SerialBT.print(rtc.day());
    SerialBT.print('/');
    SerialBT.print(rtc.month());
    SerialBT.print('/');
    SerialBT.print(rtc.year());
    SerialBT.print(" (");
    SerialBT.print(daysOfTheWeek[rtc.dayOfWeek() - 1]);
    SerialBT.print(") ");
    SerialBT.print(rtc.hour());
    SerialBT.print(':');
    if (rtc.minute() < 10) SerialBT.print('0');
    SerialBT.print(rtc.minute());
    SerialBT.print(':');
    if (rtc.second() < 10) SerialBT.print('0');
    SerialBT.println(rtc.second());
    
    // Print weight
    SerialBT.println(strData);
    
    // Print receipt footer
    SerialBT.println("Signature");
    SerialBT.println("");
    SerialBT.println("------------------------");
    SerialBT.println("........................");
    SerialBT.println("\x1B\x64\x0C");  // ESC d 12 = Feed 12 lines
}

// Blink LED to confirm print
ledcWrite(LED_CHANNEL, 0);
delay(100);
ledcWrite(LED_CHANNEL, LED_BRIGHTNESS);
```

#### Mode 2: Quick Print (Button Pressed)

```
┌────────────────────────────────────┐
│  *     123.45 g                    │  ← Weight data only
└────────────────────────────────────┘
```

**Code**:
```cpp
if (digitalRead(BUTTON_PIN) == HIGH) {  // Button pressed (grounded)
    SerialBT.println(strData);  // Weight only, no timestamp
}
```

### ESC/POS Commands

| Command | Hex | Description |
|---------|-----|-------------|
| Paper Feed | `\x1B\x64\x0C` | ESC d 12 - Feed 12 lines (adjust for printer) |

**Note**: The paper feed value (`0x0C` = 12 lines) may need adjustment based on your printer model.

---

## Bluetooth Connection Management

### Connection State Machine

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                    Bluetooth Connection State Machine                        │
└─────────────────────────────────────────────────────────────────────────────┘

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
                         │   Set PIN code  │
                         └────────┬────────┘
                                  │
                                  ▼
                         ┌─────────────────┐
                         │ Connect to      │
                         │ printer MAC     │
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
                    │        Reconnect attempt
                    │         (in main loop)
                    ▼
           ┌─────────────────┐
           │  Check every    │
           │  5000ms         │
           └────────┬────────┘
                    │
         ┌──────────┴──────────┐
         │ Still connected     │ Disconnected
         ▼                     ▼
    [Continue]          [Attempt reconnect]
```

### Reconnection Logic

The reconnection is handled by a dedicated `reconnectBluetooth()` function with rate limiting:

```cpp
void reconnectBluetooth() {
    static unsigned long lastReconnectAttempt = 0;
    unsigned long now = millis();
    
    // Try reconnect every 5 seconds
    if (now - lastReconnectAttempt >= 5000) {
        lastReconnectAttempt = now;
        Serial.println("Attempting Bluetooth reconnect...");
        
        if (SerialBT.connect(printerBtAddress)) {
            btConnected = true;
            ledcWrite(LED_CHANNEL, LED_BRIGHTNESS);
            Serial.println("Bluetooth reconnected!");
        }
    }
}
```

**Main Loop Connection Management**:
```cpp
void loop() {
    // ... data handling ...
    
    if (!SerialBT.connected(5000)) {
        if (btConnected) {
            Serial.println("Bluetooth disconnected");
            ledcWrite(LED_CHANNEL, 0);
            btConnected = false;
        }
        reconnectBluetooth();
    } else {
        if (!btConnected) {
            Serial.println("Bluetooth reconnected!");
            ledcWrite(LED_CHANNEL, LED_BRIGHTNESS);
            btConnected = true;
        }
    }
}
```

### Connection Timeout Handling

| Scenario | Timeout | Action |
|----------|---------|--------|
| Initial connect | None (blocking) | Wait until connected or fail |
| Connection check | 5000ms | If no response, attempt reconnect |
| Print while disconnected | Immediate | Skip print, log error |

---

## Button Operation Modes

### Physical Button Configuration

```
              3.3V
               │
               │
         ┌─────┴─────┐
         │  Internal │
         │  Pull-up  │
         └─────┬─────┘
               │
    GPIO 26 ───┼─────────┐
               │         │
               │       ──┴──
               │       ─┬──   Momentary
               │        │     Button (NO)
               │       ─┴─
               │        │
              GND ──────┘
```

### Button States

| Physical State | GPIO Reading | Print Mode |
|----------------|--------------|------------|
| Not pressed | HIGH (pulled up) | Quick print (weight only) |
| Pressed | LOW (grounded) | Full receipt (timestamp + signature) |

**Note**: The logic is inverted for safety - the default (not pressed) mode gives the simpler output.

### Mode Selection Flow

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                         Button Mode Selection                                │
└─────────────────────────────────────────────────────────────────────────────┘

                    Print Request Received
                              │
                              ▼
                    ┌─────────────────┐
                    │ Read BUTTON_PIN │
                    └────────┬────────┘
                             │
               ┌─────────────┴─────────────┐
               │ HIGH                      │ LOW
               │ (Not pressed)             │ (Pressed)
               ▼                           ▼
      ┌─────────────────┐         ┌─────────────────┐
      │  BYPASS MODE    │         │  FULL RECEIPT   │
      │                 │         │                 │
      │ • Weight only   │         │ • Timestamp     │
      │ • No timestamp  │         │ • Weight        │
      │ • No signature  │         │ • Signature     │
      │ • Fast          │         │ • Separator     │
      └─────────────────┘         └─────────────────┘
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
                         │ Serial.begin    │
                         │ (9600)          │  ← Debug output
                         └────────┬────────┘
                                  │
                                  ▼
                         ┌─────────────────┐
                         │ Serial1.begin   │
                         │ (9600, RX=16)   │  ← UART from Hub
                         └────────┬────────┘
                                  │
                                  ▼
                         ┌─────────────────┐
                         │ pinMode         │
                         │ BUTTON_PIN      │  ← INPUT_PULLUP
                         └────────┬────────┘
                                  │
                                  ▼
                         ┌─────────────────┐
                         │ ledcSetup()     │
                         │ ledcAttachPin() │  ← Configure PWM for LED
                         │ ledcWrite(0)    │  ← LED off initially
                         └────────┬────────┘
                                  │
                                  ▼
                         ┌─────────────────┐
                         │ SerialBT.begin  │
                         │ (Master mode)   │  ← Initialize Bluetooth
                         └────────┬────────┘
                                  │
                                  ▼
                         ┌─────────────────┐
                         │ SerialBT.setPin │
                         │ ("0000")        │  ← Set pairing PIN
                         └────────┬────────┘
                                  │
                                  ▼
                         ┌─────────────────┐
                         │ SerialBT.connect│
                         │ (printerMAC)    │  ← Connect to printer
                         └────────┬────────┘
                                  │
                    ┌─────────────┴─────────────┐
                    │ Success                   │ Failure
                    ▼                           ▼
           ┌─────────────────┐         ┌─────────────────┐
           │ btConnected=true│         │ btConnected=    │
           │ LED = BRIGHTNESS│         │   false         │
           └─────────────────┘         │ LED = OFF       │
                                       │ (retry in loop) │
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
                         │ Check Serial1   │
                         │ .available()    │
                         └────────┬────────┘
                                  │
                    ┌─────────────┴─────────────┐
                    │ Data available            │ No data
                    ▼                           │
           ┌─────────────────┐                  │
           │ Read line       │                  │
           │ Serial1         │                  │
           │ .readString     │                  │
           │ Until('\n')     │                  │
           └────────┬────────┘                  │
                    │                           │
                    ▼                           │
           ┌─────────────────┐                  │
           │ btConnected?    │                  │
           └────────┬────────┘                  │
                    │                           │
         ┌──────────┴──────────┐                │
         │ Yes                 │ No             │
         ▼                     ▼                │
┌─────────────────┐  ┌─────────────────┐        │
│ printWith       │  │ Log "BT not    │        │
│ Timestamp()     │  │  connected"    │        │
└─────────────────┘  └─────────────────┘        │
                                                │
                    ┌───────────────────────────┘
                    │
                    ▼
           ┌─────────────────┐
           │ SerialBT        │
           │ .connected(5000)│
           └────────┬────────┘
                    │
         ┌──────────┴──────────┐
         │ Connected           │ Not connected
         ▼                     ▼
┌─────────────────┐    ┌─────────────────┐
│ If !btConnected │    │ If btConnected  │
│ → Log reconnect │    │ → Log disconnect│
│ → LED = ON      │    │ → LED = OFF     │
│ → btConnected   │    │ → btConnected   │
│   = true        │    │   = false       │
└─────────────────┘    └────────┬────────┘
                                │
                                ▼
                       ┌─────────────────┐
                       │ reconnect       │
                       │ Bluetooth()     │
                       └─────────────────┘
```

### Reconnect Function Flow

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                      reconnectBluetooth()                                    │
└─────────────────────────────────────────────────────────────────────────────┘
                                  │
                                  ▼
                         ┌─────────────────┐
                         │ Calculate       │
                         │ elapsed time    │
                         │ since last try  │
                         └────────┬────────┘
                                  │
                                  ▼
                         ┌─────────────────┐
                         │ Elapsed >= 5s?  │
                         └────────┬────────┘
                                  │
               ┌──────────────────┴──────────────────┐
               │ No (< 5s)                           │ Yes (>= 5s)
               ▼                                     ▼
      [Return immediately]                  ┌─────────────────┐
                                            │ Update last     │
                                            │ attempt time    │
                                            └────────┬────────┘
                                                     │
                                                     ▼
                                            ┌─────────────────┐
                                            │ Log "Attempting │
                                            │  reconnect..."  │
                                            └────────┬────────┘
                                                     │
                                                     ▼
                                            ┌─────────────────┐
                                            │ SerialBT        │
                                            │ .connect(MAC)   │
                                            └────────┬────────┘
                                                     │
                                          ┌──────────┴──────────┐
                                          │ Success             │ Failure
                                          ▼                     ▼
                                   ┌─────────────┐       [Return, try
                                   │ btConnected │        again in 5s]
                                   │ = true      │
                                   │ LED = ON    │
                                   │ Log success │
                                   └─────────────┘
```

### Print Processing Flow

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                      printWithTimestamp(data)                                │
└─────────────────────────────────────────────────────────────────────────────┘
                                  │
                                  ▼
                         ┌─────────────────┐
                         │ Convert to      │
                         │ String, trim    │
                         └────────┬────────┘
                                  │
                                  ▼
                         ┌─────────────────┐     Yes
                         │ Is filtered?    │──────────► [Return]
                         │ (separator,     │
                         │  signature,etc) │
                         └────────┬────────┘
                                  │ No
                                  ▼
                         ┌─────────────────┐     Yes
                         │ Is "NO          │──────────► [Return silently]
                         │  STABILITY"?    │
                         └────────┬────────┘
                                  │ No
                                  ▼
                         ┌─────────────────┐
                         │ rtc.refresh()   │
                         └────────┬────────┘
                                  │
                                  ▼
                         ┌─────────────────┐
                         │ Read BUTTON_PIN │
                         └────────┬────────┘
                                  │
               ┌──────────────────┴──────────────────┐
               │ HIGH (bypass)                       │ LOW (full)
               ▼                                     ▼
      ┌─────────────────┐                   ┌─────────────────┐
      │ Print weight    │                   │ Print timestamp │
      │ only            │                   │ Print weight    │
      └────────┬────────┘                   │ Print signature │
               │                            │ Print separator │
               │                            │ Paper feed      │
               │                            └────────┬────────┘
               │                                     │
               └──────────────┬──────────────────────┘
                              │
                              ▼
                     ┌─────────────────┐
                     │ Blink LED (PWM) │
                     │ OFF → delay →   │
                     │ BRIGHTNESS      │
                     └─────────────────┘
```

---

## Configuration Parameters

### Compile-Time Constants

```cpp
// Pin Definitions
#define HUB_RX 16           // UART RX from Hub
#define BUTTON_PIN 26       // Timestamp bypass button
#define LED_PIN 25          // Status LED

// PWM Settings
#define LED_CHANNEL 0       // LEDC channel
#define LED_FREQ 5000       // PWM frequency (Hz)
#define LED_RESOLUTION 8    // 8-bit (0-255)
#define LED_BRIGHTNESS 60   // ~25% brightness

// Bluetooth
uint8_t printerBtAddress[6] = {0xDC, 0x0D, 0x30, 0x00, 0x28, 0x89};
const char* btPin = "0000";

// RTC
#define RTC_ADDRESS 0x68    // DS3231 I2C address
```

### Day Names Array

```cpp
const char* daysOfTheWeek[7] = {
    "Sunday",     // Index 0 (dayOfWeek = 1)
    "Monday",     // Index 1 (dayOfWeek = 2)
    "Tuesday",    // Index 2 (dayOfWeek = 3)
    "Wednesday",  // Index 3 (dayOfWeek = 4)
    "Thursday",   // Index 4 (dayOfWeek = 5)
    "Friday",     // Index 5 (dayOfWeek = 6)
    "Saturday"    // Index 6 (dayOfWeek = 7)
};

// Usage: daysOfTheWeek[rtc.dayOfWeek() - 1]
```

### Configuring Bluetooth MAC Address

To find your printer's MAC address:

1. **Android**: Settings → Bluetooth → Paired devices → Tap printer → View details
2. **Windows**: Device Manager → Bluetooth → Printer properties
3. **ESP32 Scan**: Use `SerialBT.discover()` to scan nearby devices

**Format Conversion**:
```
Display:  DC:0D:30:00:28:89
Code:     {0xDC, 0x0D, 0x30, 0x00, 0x28, 0x89}
```

---

## Troubleshooting

### LED Status Reference

| LED State | Meaning | Action |
|-----------|---------|--------|
| OFF at startup | Bluetooth failed to connect | Check printer power, MAC address |
| Solid ON | Normal operation | None |
| Brief OFF then ON | Print sent successfully | None |
| OFF during operation | Bluetooth disconnected | Wait for auto-reconnect |

### Common Issues

#### 1. Bluetooth Won't Connect

**Symptoms**: LED stays OFF, serial shows "BT failed"

**Solutions**:
- Verify printer is powered ON and in pairing mode
- Confirm MAC address is correct
- Check PIN code matches printer settings
- Ensure printer isn't connected to another device
- Power cycle both ESP32 and printer

#### 2. Garbled Print Output

**Symptoms**: Printed text is corrupted or has wrong characters

**Solutions**:
- Verify baud rates match (9600)
- Check for loose UART connections
- Ensure Hub and Printer ESP32 share common GND

#### 3. No Timestamp on Prints

**Symptoms**: Weight prints but no date/time

**Solutions**:
- Check RTC module wiring (SDA/SCL)
- Verify RTC has battery backup
- Check if button is stuck pressed (bypass mode)

#### 4. RTC Shows Wrong Time

**Solutions**:
- Program RTC with current time once:
```cpp
// In setup(), run once then comment out:
rtc.set(0, 30, 14, 5, 12, 12, 25);  // sec, min, hr, dow, day, month, year
```

#### 5. Prints Missing or Duplicated

**Symptoms**: Some prints don't appear, or print multiple times

**Solutions**:
- Check UART connection integrity
- Verify Hub is sending data correctly
- Add debounce delay if needed

### Debug Output Reference

**Normal Startup**:
```
===== Printer ESP32 =====
UART RX from Hub + Bluetooth TX to Printer
==========================

Connecting to Bluetooth printer...
Bluetooth connected!
Printer ESP32 ready!
```

**Print Received**:
```
>>> Print data received:  *     123.45 g
```

**Bluetooth Reconnection**:
```
BT reconnecting...
BT reconnected!
```

---

## Dependencies

### Libraries Required

| Library | Version | Purpose |
|---------|---------|---------|
| BluetoothSerial | Built-in | Bluetooth Classic SPP |
| Wire | Built-in | I2C communication |
| uRTCLib | 6.2.7+ | DS3231 RTC interface |

### PlatformIO Configuration

```ini
[env:esp32dev]
platform = espressif32
board = esp32dev
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

### Example 1: Full Receipt Mode

**Input from Hub**: ` *     123.45 g`

**Printed Output**:
```
12/12/2025 (Thursday) 14:30:45
 *     123.45 g
Signature

------------------------
........................

```

### Example 2: Bypass Mode

**Input from Hub**: ` *     123.45 g`

**Printed Output**:
```
 *     123.45 g
```

### Example 3: Instability Warning

**Input from Hub**: `NO STABILITY`

**Printed Output**: *(No output - silently ignored)*

**Note**: Unstable readings are filtered to prevent printing incomplete weighments.

---

*Document generated for Printer ESP32*
*Compatible with Hub ESP32 v3.2*
