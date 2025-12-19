#include <Arduino.h>
#include <BluetoothSerial.h>
#include "uRTCLib.h"

// ===== PIN DEFINITIONS =====
#define HUB_RX 16        // UART RX from Hub ESP32 (13)
#define BUTTON_PIN 26    // Timestamp bypass button
#define LED_PIN 25       // Status LED

// ===== PWM SETTINGS =====
#define LED_CHANNEL 0
#define LED_FREQ 5000
#define LED_RESOLUTION 8
#define LED_BRIGHTNESS 60  // 50% of 255

// ===== RTC =====
uRTCLib rtc(0x68);
const char* daysOfTheWeek[7] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};

// ===== BLUETOOTH =====
BluetoothSerial SerialBT;

// TODO: Replace with your printer's Bluetooth MAC address
uint8_t printerBtAddress[6] = {0xDC, 0x0D, 0x30, 0x00, 0x28, 0x89};
const char* btPin = "0000";
bool btConnected = false;

// ===== FUNCTION DECLARATIONS =====
void printWithTimestamp(const char* data);
void reconnectBluetooth();

// ===== SETUP =====
void setup() {
  delay(3000);
  
  URTCLIB_WIRE.begin();
  Serial.begin(9600);
  Serial1.begin(9600, SERIAL_8N1, HUB_RX, -1);  // RX only from Hub
  
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  // Setup PWM for LED
  ledcSetup(LED_CHANNEL, LED_FREQ, LED_RESOLUTION);
  ledcAttachPin(LED_PIN, LED_CHANNEL);
  ledcWrite(LED_CHANNEL, 0);  // Start with LED off
  
  Serial.println("\n===== Printer ESP32 =====");
  Serial.println("UART RX from Hub + Bluetooth TX to Printer");
  Serial.println("==========================\n");
  
  // Init Bluetooth
  SerialBT.begin("ESP32_Printer", true);  // true = master mode
  SerialBT.setPin(btPin);
  
  Serial.println("Connecting to Bluetooth printer...");
  btConnected = SerialBT.connect(printerBtAddress);
  
  if (btConnected) {
    Serial.println("Bluetooth connected!");
    ledcWrite(LED_CHANNEL, LED_BRIGHTNESS);
  } else {
    Serial.println("Bluetooth failed, will retry...");
  }
  
  Serial.println("Printer ESP32 ready!");
}

// ===== LOOP =====
void loop() {
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
    SerialBT.println("\x1B\x64\xC");  // Feed paper
  }
  
  // Blink LED to confirm print
  ledcWrite(LED_CHANNEL, 0);
  delay(100);
  ledcWrite(LED_CHANNEL, LED_BRIGHTNESS);
}

// ===== RECONNECT BLUETOOTH =====
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