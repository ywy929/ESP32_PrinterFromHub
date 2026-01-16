#include <Arduino.h>
#include <BluetoothSerial.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <Preferences.h>
#include "uRTCLib.h"

// ===== BLE UART SERVICE (Nordic UART Service) =====
#define SERVICE_UUID           "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define CHARACTERISTIC_UUID_RX "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"  // Phone -> ESP32
#define CHARACTERISTIC_UUID_TX "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"  // ESP32 -> Phone

// ===== PIN DEFINITIONS =====
// NOTE: Pins adjusted for swapped left/right headers on PCB
#define HUB_RX 27        // UART RX from Hub ESP32 (was GPIO16, now GPIO27 due to header swap)
#define BUTTON_PIN 17    // Timestamp bypass button (was GPIO26, now GPIO17 due to header swap)
#define LED_PIN 5        // Status LED (was GPIO25, now GPIO5 due to header swap)
// I2C (SDA/SCL) stays on GPIO21/22 - requires bodge wires from RTC header


// ===== RTC =====
uRTCLib rtc(0x68);
const char* daysOfTheWeek[7] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};

// ===== BLUETOOTH =====
BluetoothSerial SerialBT;
Preferences preferences;

// Default printer's Bluetooth MAC address (will be loaded from storage)
uint8_t printerBtAddress[6] = {0xDC, 0x0D, 0x30, 0x00, 0x28, 0x89};
const char* btPin = "0000";
bool btConnected = false;
bool reconnectInProgress = false;  // Flag to prevent multiple reconnect tasks

// Printer type: 0 = THERMAL, 1 = DOT
uint8_t printerType = 0;  // Default to THERMAL

// ===== CONFIG MODE =====
bool configMode = false;
bool bleExitRequested = false;
int buttonPressCount = 0;
unsigned long lastButtonPress = 0;
int lastButtonState = HIGH;  // Button idles HIGH with INPUT_PULLUP
const unsigned long DEBOUNCE_DELAY = 50;
const unsigned long PRESS_TIMEOUT = 4000;  // 4 seconds to complete 4 presses

// ===== BLE =====
BLEServer* pServer = nullptr;
BLECharacteristic* pTxCharacteristic = nullptr;
bool bleClientConnected = false;

// BLE command buffer (to handle character fragmentation and threading)
String bleRxBuffer = "";
String bleCmdReady = "";  // Command ready to process (set by callback, processed by loop)

// ===== FUNCTION DECLARATIONS =====
void printWithTimestamp(const char* data);
void reconnectBluetooth();
void checkButtonForConfigMode();
void enterConfigMode();
void exitConfigMode();
void loadPrinterMac();
void savePrinterMac();
void loadPrinterType();
void savePrinterType();
String macToString(uint8_t* mac);
String printerTypeToString();
bool parseMacAddress(String macStr, uint8_t* mac);
String getTimeString();
void startBLE();
void stopBLE();
void bleSend(const String& msg);
void processSetupCommand(String cmd);
void printBleStatus();

// ===== BLE SERVER CALLBACKS =====
class ServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer* pServer) {
    bleClientConnected = true;
    Serial.println("[BLE] Client connected");
  }
  void onDisconnect(BLEServer* pServer) {
    bleClientConnected = false;
    Serial.println("[BLE] Client disconnected");
    // Restart advertising
    if (configMode) {
      pServer->getAdvertising()->start();
    }
  }
};

// ===== BLE RX CHARACTERISTIC CALLBACKS =====
class RxCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* pCharacteristic) {
    std::string rxValue = pCharacteristic->getValue();
    if (rxValue.length() > 0) {
      // Just append to buffer - don't process here (threading safety)
      for (size_t i = 0; i < rxValue.length(); i++) {
        char c = rxValue[i];
        if (c == '\n' || c == '\r') {
          // Newline - mark command ready for processing
          if (bleRxBuffer.length() > 0 && bleCmdReady.length() == 0) {
            bleCmdReady = bleRxBuffer;  // Copy to ready buffer
            bleRxBuffer = "";
          }
        } else {
          bleRxBuffer += c;
        }
      }
    }
  }
};

// ===== SETUP =====
void setup() {
  delay(3000);

  URTCLIB_WIRE.begin();
  Serial.begin(9600);
  Serial1.begin(9600, SERIAL_8N1, HUB_RX, -1);  // RX only from Hub

  // Load settings from storage
  loadPrinterMac();
  loadPrinterType();
  Serial.print("Printer MAC: ");
  Serial.println(macToString(printerBtAddress));
  Serial.print("Printer Type: ");
  Serial.println(printerTypeToString());
  
  pinMode(BUTTON_PIN, INPUT_PULLUP);  // Button connects to GND when pressed
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);  // Start with LED off
  
  Serial.println("\n===== Printer ESP32 =====");
  Serial.println("UART RX from Hub + Bluetooth TX to Printer");
  Serial.println("==========================\n");
  
  // Init Bluetooth
  SerialBT.begin("ESP32_Printer", true);  // true = master mode
  SerialBT.setPin(btPin);

  // Don't block here - let reconnect task handle initial connection
  Serial.println("Printer ESP32 ready!");
  Serial.println("Will connect to printer in background...");
}

// ===== LOOP =====
void loop() {
  // ===== CHECK FOR CONFIG MODE ENTRY =====
  checkButtonForConfigMode();

  // ===== CONFIG MODE =====
  if (configMode) {
    // Fast blink LED every 100ms in config mode
    static unsigned long lastLedToggle = 0;
    if (millis() - lastLedToggle >= 100) {
      lastLedToggle = millis();
      digitalWrite(LED_PIN, !digitalRead(LED_PIN));
    }

    // Process command if ready (set by BLE callback)
    if (bleCmdReady.length() > 0) {
      processSetupCommand(bleCmdReady);
      bleCmdReady = "";
    }

    // Check if exit was requested via BLE
    if (bleExitRequested) {
      exitConfigMode();
    }
    return;  // Skip normal operation in config mode
  }

  // ===== CHECK FOR DATA FROM HUB =====
  if (Serial1.available()) {
    String data = Serial1.readStringUntil('\n');
    data.trim();

    if (data.length() > 0) {
      Serial.print("Received: ");
      Serial.println(data);

      if (btConnected) {
        printWithTimestamp(data.c_str());
      } else {
        Serial.println("BT not connected, print skipped");
      }
    }
  }

  // ===== MAINTAIN BLUETOOTH CONNECTION =====
  // Use timeout=0 for non-blocking check to keep button responsive
  if (!SerialBT.connected(0)) {
    if (btConnected) {
      Serial.println("Bluetooth disconnected");
      digitalWrite(LED_PIN, LOW);
      btConnected = false;
    }
    // Reconnect runs in background task (non-blocking)
    reconnectBluetooth();
  } else {
    if (!btConnected) {
      Serial.println("Bluetooth reconnected!");
      digitalWrite(LED_PIN, HIGH);
      btConnected = true;
    }
  }
}

// ===== PRINT WITH TIMESTAMP =====
void printWithTimestamp(const char* data) {
  String strData = String(data);
  strData.trim();
  
  // Filter out unwanted strings
  if (strData == "------------------------") return;
  if (strData == "........................") return;
  if (strData == "Signature") return;
  
  // Handle unstable reading
  if (strData == "NO STABILITY") {
    //SerialBT.println(strData);
    //SerialBT.println("\x1B\x64\xC");  // Feed paper
    return;
  }
  
  rtc.refresh();
  
  if (digitalRead(BUTTON_PIN) == HIGH) {
    // Direct print without timestamp (bypass mode)
    SerialBT.println(strData);
    uint8_t flushCmd[] = {0x1B, 0x4A, 0x00};  // ESC J 0 - Print buffer, no feed
    SerialBT.write(flushCmd, 3);
  } else {
    // Print with timestamp
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
    
    SerialBT.println(strData);
    SerialBT.println("Signature");
    SerialBT.println("");
    SerialBT.println("------------------------");
    SerialBT.println("........................");
    if (printerType == 0) {
      SerialBT.println("\x1B\x64\x03");  // THERMAL: Feed 3 lines
    } else {
      SerialBT.println("\x1B\x64\x0C");  // DOT: Feed 12 lines
    }
  }
  
  // Blink LED to confirm print
  digitalWrite(LED_PIN, LOW);
  delay(100);
  digitalWrite(LED_PIN, HIGH);
}

// ===== RECONNECT TASK (runs in background) =====
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

// ===== RECONNECT BLUETOOTH (non-blocking) =====
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

    // Create background task for reconnection (doesn't block main loop)
    xTaskCreate(
      reconnectTask,     // Task function
      "BT_Reconnect",    // Task name
      4096,              // Stack size
      NULL,              // Parameters
      1,                 // Priority (low)
      NULL               // Task handle
    );
  }
}

// ===== CHECK BUTTON FOR CONFIG MODE =====
void checkButtonForConfigMode() {
  static unsigned long lastDebounceTime = 0;
  int reading = digitalRead(BUTTON_PIN);

  if (reading != lastButtonState) {
    lastDebounceTime = millis();
  }

  if ((millis() - lastDebounceTime) > DEBOUNCE_DELAY) {
    static int currentButtonState = HIGH;  // Idles HIGH with INPUT_PULLUP
    if (reading != currentButtonState) {
      currentButtonState = reading;

      // Button pressed (LOW when connected to GND)
      if (currentButtonState == LOW) {
        unsigned long now = millis();

        // Reset count if too much time passed
        if (now - lastButtonPress > PRESS_TIMEOUT) {
          buttonPressCount = 0;
        }

        buttonPressCount++;
        lastButtonPress = now;
        Serial.print("Button press: ");
        Serial.println(buttonPressCount);

        if (buttonPressCount >= 4) {
          buttonPressCount = 0;
          if (configMode) {
            exitConfigMode();
          } else {
            enterConfigMode();
          }
        }
      }
    }
  }
  lastButtonState = reading;
}

// ===== ENTER CONFIG MODE =====
void enterConfigMode() {
  configMode = true;
  bleExitRequested = false;

  // Clear BLE buffers
  bleRxBuffer = "";
  bleCmdReady = "";

  Serial.println("\n===== ENTERING CONFIG MODE =====");

  // Wait for any reconnect task to finish before switching to BLE
  while (reconnectInProgress) {
    delay(100);
  }

  // Disconnect from printer and stop Bluetooth Classic
  if (btConnected) {
    SerialBT.disconnect();
    btConnected = false;
  }
  SerialBT.end();
  delay(500);

  // Start BLE server
  startBLE();
  Serial.println("BLE Started: PrinterConfig");
  Serial.println("Use 'LightBlue' or 'nRF Connect' app");
  Serial.println("Connect to 'PrinterConfig', find UART service");
  Serial.println("");
  Serial.println("Commands:");
  Serial.println("  TIME YY/MM/DD HH:MM:SS DOW");
  Serial.println("  MAC XX:XX:XX:XX:XX:XX");
  Serial.println("  TYPE THERMAL / TYPE DOT");
  Serial.println("  STATUS / EXIT / HELP");
  Serial.println("Or toggle button 4 times to exit\n");
}

// ===== EXIT CONFIG MODE =====
void exitConfigMode() {
  Serial.println("\n===== EXITING CONFIG MODE =====");
  bleSend("OK: Exiting config mode...");
  delay(500);

  // Stop BLE
  stopBLE();
  delay(500);

  // Restart Bluetooth Classic in master mode for printer
  SerialBT.begin("ESP32_Printer", true);
  SerialBT.setPin(btPin);

  Serial.print("Reconnecting to printer MAC: ");
  Serial.println(macToString(printerBtAddress));
  btConnected = SerialBT.connect(printerBtAddress);

  if (btConnected) {
    Serial.println("Bluetooth connected!");
    digitalWrite(LED_PIN, HIGH);
  } else {
    Serial.println("Bluetooth failed, will retry in background...");
    digitalWrite(LED_PIN, LOW);
  }

  configMode = false;
  bleExitRequested = false;
  Serial.println("Printer ESP32 ready!");
}


// ===== LOAD PRINTER MAC FROM STORAGE =====
void loadPrinterMac() {
  preferences.begin("printer", true);  // Read-only
  if (preferences.isKey("mac")) {
    preferences.getBytes("mac", printerBtAddress, 6);
  }
  preferences.end();
}

// ===== SAVE PRINTER MAC TO STORAGE =====
void savePrinterMac() {
  preferences.begin("printer", false);  // Read-write
  preferences.putBytes("mac", printerBtAddress, 6);
  preferences.end();
}

// ===== LOAD PRINTER TYPE FROM STORAGE =====
void loadPrinterType() {
  preferences.begin("printer", true);  // Read-only
  if (preferences.isKey("type")) {
    printerType = preferences.getUChar("type", 0);
  }
  preferences.end();
}

// ===== SAVE PRINTER TYPE TO STORAGE =====
void savePrinterType() {
  preferences.begin("printer", false);  // Read-write
  preferences.putUChar("type", printerType);
  preferences.end();
}

// ===== PRINTER TYPE TO STRING =====
String printerTypeToString() {
  return (printerType == 0) ? "THERMAL" : "DOT";
}

// ===== MAC TO STRING =====
String macToString(uint8_t* mac) {
  char macStr[18];
  sprintf(macStr, "%02X:%02X:%02X:%02X:%02X:%02X",
          mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  return String(macStr);
}

// ===== PARSE MAC ADDRESS =====
bool parseMacAddress(String macStr, uint8_t* mac) {
  // Expected format: XX:XX:XX:XX:XX:XX or XX-XX-XX-XX-XX-XX
  macStr.replace("-", ":");
  macStr.toUpperCase();

  if (macStr.length() != 17) return false;

  int values[6];
  int result = sscanf(macStr.c_str(), "%x:%x:%x:%x:%x:%x",
                      &values[0], &values[1], &values[2],
                      &values[3], &values[4], &values[5]);

  if (result != 6) return false;

  for (int i = 0; i < 6; i++) {
    if (values[i] < 0 || values[i] > 255) return false;
    mac[i] = (uint8_t)values[i];
  }

  return true;
}

// ===== GET TIME STRING =====
String getTimeString() {
  char timeStr[32];
  sprintf(timeStr, "%02d/%02d/%02d %02d:%02d:%02d %d",
          rtc.year(), rtc.month(), rtc.day(),
          rtc.hour(), rtc.minute(), rtc.second(),
          rtc.dayOfWeek());
  return String(timeStr);
}

// ===== BLE SEND HELPER =====
void bleSend(const String& msg) {
  if (pTxCharacteristic && bleClientConnected) {
    pTxCharacteristic->setValue(msg.c_str());
    pTxCharacteristic->notify();
    delay(150);  // Delay so phone can receive notification
  }
  Serial.println(msg);  // Also echo to serial
}

// ===== PRINT BLE STATUS =====
void printBleStatus() {
  rtc.refresh();
  bleSend("=== PrinterConfig Status ===");
  bleSend("TIME: " + getTimeString());
  bleSend("MAC: " + macToString(printerBtAddress));
  bleSend("TYPE: " + printerTypeToString());
  bleSend("============================");
}

// ===== PROCESS SETUP COMMAND =====
void processSetupCommand(String cmd) {
  cmd.trim();
  String cmdUpper = cmd;
  cmdUpper.toUpperCase();

  Serial.print("[BLE CMD] ");
  Serial.println(cmd);

  if (cmdUpper.startsWith("TIME ")) {
    // Format: TIME YY/MM/DD HH:MM:SS DOW
    String timeStr = cmd.substring(5);
    timeStr.trim();

    int year, month, day, hour, minute, second, dow;
    if (sscanf(timeStr.c_str(), "%d/%d/%d %d:%d:%d %d",
               &year, &month, &day, &hour, &minute, &second, &dow) == 7) {
      rtc.set(second, minute, hour, dow, day, month, year);
      bleSend("OK: Time set to " + timeStr);
    } else {
      bleSend("ERROR: Format: TIME YY/MM/DD HH:MM:SS DOW");
    }
  }
  else if (cmdUpper.startsWith("MAC ")) {
    String mac = cmd.substring(4);
    mac.trim();
    mac.toUpperCase();

    uint8_t newMac[6];
    if (parseMacAddress(mac, newMac)) {
      memcpy(printerBtAddress, newMac, 6);
      savePrinterMac();
      bleSend("OK: MAC=" + mac);
    } else {
      bleSend("ERROR: Format: MAC XX:XX:XX:XX:XX:XX");
    }
  }
  else if (cmdUpper.startsWith("TYPE ")) {
    String typeStr = cmd.substring(5);
    typeStr.trim();
    typeStr.toUpperCase();

    if (typeStr == "THERMAL" || typeStr == "T" || typeStr == "0") {
      printerType = 0;
      savePrinterType();
      bleSend("OK: TYPE=THERMAL");
    } else if (typeStr == "DOT" || typeStr == "D" || typeStr == "1") {
      printerType = 1;
      savePrinterType();
      bleSend("OK: TYPE=DOT");
    } else {
      bleSend("ERROR: TYPE THERMAL or TYPE DOT");
    }
  }
  else if (cmdUpper == "STATUS" || cmdUpper == "S" || cmdUpper == "?") {
    printBleStatus();
  }
  else if (cmdUpper == "EXIT" || cmdUpper == "X" || cmdUpper == "Q") {
    bleExitRequested = true;
  }
  else if (cmdUpper == "HELP" || cmdUpper == "H") {
    bleSend("=== PrinterConfig Commands ===");
    bleSend("TIME YY/MM/DD HH:MM:SS DOW");
    bleSend("MAC XX:XX:XX:XX:XX:XX");
    bleSend("TYPE THERMAL or TYPE DOT");
    bleSend("STATUS - Show current settings");
    bleSend("EXIT - Save and exit");
    bleSend("==============================");
  }
  else {
    bleSend("ERR: Unknown command. Type HELP");
  }
}

// ===== START BLE SERVER (Nordic UART Service) =====
void startBLE() {
  BLEDevice::init("PrinterConfig");
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new ServerCallbacks());

  // Create UART Service
  BLEService* pService = pServer->createService(SERVICE_UUID);

  // Create TX Characteristic (ESP32 -> Phone) - Notify
  pTxCharacteristic = pService->createCharacteristic(
    CHARACTERISTIC_UUID_TX,
    BLECharacteristic::PROPERTY_NOTIFY | BLECharacteristic::PROPERTY_READ
  );
  pTxCharacteristic->addDescriptor(new BLE2902());
  pTxCharacteristic->setValue("Ready");

  // Create RX Characteristic (Phone -> ESP32) - Write
  BLECharacteristic* pRxCharacteristic = pService->createCharacteristic(
    CHARACTERISTIC_UUID_RX,
    BLECharacteristic::PROPERTY_WRITE
  );
  pRxCharacteristic->setCallbacks(new RxCallbacks());

  // Start service and advertising
  pService->start();

  BLEAdvertising* pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setScanResponse(true);
  pAdvertising->setMinPreferred(0x06);
  pAdvertising->setMaxPreferred(0x12);
  BLEDevice::startAdvertising();

  Serial.println("BLE UART server started, advertising...");
}

// ===== STOP BLE SERVER =====
void stopBLE() {
  if (pServer != nullptr) {
    BLEDevice::stopAdvertising();
    pServer->disconnect(pServer->getConnId());
    delay(100);
  }
  BLEDevice::deinit(true);
  pServer = nullptr;
  pTxCharacteristic = nullptr;
  bleClientConnected = false;
  Serial.println("BLE server stopped");
}