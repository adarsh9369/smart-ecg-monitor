#include <Wire.h>
#include <U8g2lib.h>
#include <math.h>
#include <string.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <Bonezegei_Max30105.h>
#include <OneWire.h>
#include <DallasTemperature.h>

#include "ecg.h"
#include "pulse_cal.h"
#include "mv_hr.h"

// =====================================================
// PIN CONFIGURATION
// =====================================================
// ECG signal pin: GPIO 15
constexpr int ECG_PIN = 15;
constexpr int LO_PLUS_PIN = 33;
constexpr int LO_MINUS_PIN = 32;

constexpr int RED_PIN = 27;
constexpr int GREEN_PIN = 13;
constexpr int BLUE_PIN = 14;

constexpr int TEMP_PIN = 19;

#define SDA_PIN 21
#define SCL_PIN 22

// =====================================================
// OLED DISPLAY (SH1106 I2C 128x64)
// =====================================================
U8G2_SH1106_128X64_NONAME_F_HW_I2C oled(
  U8G2_R0,
  U8X8_PIN_NONE
);

// =====================================================
// BLE SETTINGS
// =====================================================
#define BLE_DEVICE_NAME "SmartVitalMonitor"
#define BLE_SERVICE_UUID "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define BLE_CHAR_UUID    "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"

BLEServer* pServer = nullptr;
BLECharacteristic* pCharacteristic = nullptr;
bool deviceConnected = false;

class MyServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer* server) override {
    deviceConnected = true;
  }

  void onDisconnect(BLEServer* server) override {
    deviceConnected = false;
    server->startAdvertising();
  }
};

// =====================================================
// ECG SETTINGS & BUFFER
// =====================================================
#define SAMPLE_RATE 100
#define DISPLAY_TIME 3
#define TOTAL_SAMPLES (SAMPLE_RATE * DISPLAY_TIME)
#define LEAD_OFF_CONFIRM_TIME 2000

float ecgBuffer[TOTAL_SAMPLES];
int bufferIndex = 0;
bool bufferFilled = false;

MyfilterFilter filter_ecg;

bool leadOffConfirmed = false;
unsigned long leadOffStartTime = 0;
bool previousLeadOff = false;

float displayECG = 2048.0f;
float bpm = 0;

// Autoscale smoothing
float smoothMin = 1948.0f;
float smoothMax = 2148.0f;
bool autoscaleResetNeeded = true;

// =====================================================
// MAX30105 SpO2 SENSOR
// =====================================================
Bonezegei_Max30105 pbx;
bool max30105Ready = false;
bool fingerDetected = false;
float spo2Value = -1.0f;
unsigned long lastPulseRead = 0;

// =====================================================
// DS18B20 TEMPERATURE SENSOR
// =====================================================
OneWire oneWire(TEMP_PIN);
DallasTemperature tempSensor(&oneWire);
bool tempSensorReady = false;
float temperatureC = NAN;
unsigned long lastTempRequest = 0;
bool tempConversionPending = false;

// =====================================================
// ECG FILTER & LEAD-OFF DETECTION
// =====================================================
float filterECG(float x) {
  MyfilterFilter_put(&filter_ecg, x);
  return MyfilterFilter_get(&filter_ecg);
}

bool rawLeadOff() {
  return (digitalRead(LO_PLUS_PIN) || digitalRead(LO_MINUS_PIN));
}

void updateLeadStatus() {
  bool off = rawLeadOff();
  if (off) {
    if (leadOffStartTime == 0) {
      leadOffStartTime = millis();
    }
    if (millis() - leadOffStartTime >= LEAD_OFF_CONFIRM_TIME) {
      leadOffConfirmed = true;
    }
  } else {
    leadOffStartTime = 0;
    leadOffConfirmed = false;
  }
}

void resetBPM() {
  resetPulseCal();
  resetHrAvg();
  bpm = 0.0f;
  heart_rate = 0;
}

void clearECGBuffer() {
  for (int i = 0; i < TOTAL_SAMPLES; i++) {
    ecgBuffer[i] = 2048.0f;
  }
  bufferIndex = 0;
  bufferFilled = false;
  displayECG = 2048.0f;
  autoscaleResetNeeded = true;
  resetBPM();
}

// =====================================================
// MAX30105 SENSOR UPDATE
// =====================================================
void updateSpO2Sensor() {
  if (!max30105Ready) {
    fingerDetected = false;
    spo2Value = -1.0f;
    return;
  }

  if (millis() - lastPulseRead < 250) {
    return;
  }
  lastPulseRead = millis();

  pbx.check();
  fingerDetected = pbx.isFingerDetected();

  if (fingerDetected) {
    float newSpO2 = pbx.getSpO2();
    if (newSpO2 >= 70.0f && newSpO2 <= 100.0f) {
      spo2Value = newSpO2;
    } else {
      spo2Value = -1.0f;
    }
  } else {
    spo2Value = -1.0f;
  }
}

// =====================================================
// DS18B20 TEMPERATURE UPDATE (Non-blocking)
// =====================================================
void updateTemperature() {
  unsigned long now = millis();

  if (tempConversionPending && (now - lastTempRequest >= 800)) {
    float t = tempSensor.getTempCByIndex(0);
    if (t != DEVICE_DISCONNECTED_C && t >= -55.0f && t <= 125.0f) {
      temperatureC = t;
    } else {
      temperatureC = NAN;
    }
    tempConversionPending = false;
  }

  if (!tempConversionPending && (now - lastTempRequest >= 1000)) {
    tempSensor.requestTemperatures();
    lastTempRequest = now;
    tempConversionPending = true;
  }
}

// =====================================================
// BLE DATA TRANSMISSION
// Streamlined JSON fits cleanly within negotiated MTU
// =====================================================
void sendBLEData(float ecgValue, float heartRate, bool leadOff, bool demo) {
  if (!deviceConnected || pCharacteristic == nullptr) {
    return;
  }

  char json[160];
  char spo2Text[12];
  char tempText[12];

  // Only send real sensor readings — never send demo values during live patient sessions!
  if (fingerDetected && spo2Value >= 70.0f && spo2Value <= 100.0f) {
    snprintf(spo2Text, sizeof(spo2Text), "%.1f", spo2Value);
  } else {
    strcpy(spo2Text, "null");
  }

  if (tempSensorReady && !isnan(temperatureC) && temperatureC >= 20.0f && temperatureC <= 50.0f) {
    snprintf(tempText, sizeof(tempText), "%.2f", temperatureC);
  } else {
    strcpy(tempText, "null");
  }

  snprintf(
    json,
    sizeof(json),
    "{\"ecg\":%.1f,\"bpm\":%.0f,\"spo2\":%s,\"temp\":%s,\"leadOff\":%d,\"demo\":%d,\"sensorDemo\":%d}",
    ecgValue,
    heartRate,
    spo2Text,
    tempText,
    leadOff ? 1 : 0,
    demo ? 1 : 0,
    fingerDetected ? 0 : 1
  );

  pCharacteristic->setValue((uint8_t*)json, strlen(json));
  pCharacteristic->notify();
}

// =====================================================
// 100 Hz REAL-TIME ECG SAMPLING & PROCESSING
// =====================================================
void sampleAndProcessECG() {
  updateLeadStatus();

  if (leadOffConfirmed) {
    // Continuous P-QRS-T mathematical simulation
    static int demo_counter = 0;
    float phase = (demo_counter % 100) / 100.0f;
    float val = 0.0f;

    if (phase >= 0.12f && phase <= 0.22f) {
      val = 1.8f * sinf(((phase - 0.12f) / 0.10f) * PI);
    } else if (phase >= 0.27f && phase <= 0.29f) {
      val = -2.0f * sinf(((phase - 0.27f) / 0.02f) * PI);
    } else if (phase >= 0.29f && phase <= 0.32f) {
      val = 22.0f * sinf(((phase - 0.29f) / 0.03f) * PI);
    } else if (phase >= 0.32f && phase <= 0.35f) {
      val = -5.5f * sinf(((phase - 0.32f) / 0.03f) * PI);
    } else if (phase >= 0.45f && phase <= 0.60f) {
      val = 4.5f * sinf(((phase - 0.45f) / 0.15f) * PI);
    }

    float fake_val = 2048.0f + (val * 30.0f);
    displayECG = fake_val;
    bpm = 60.0f;

    ecgBuffer[bufferIndex] = displayECG;
    bufferIndex++;
    if (bufferIndex >= TOTAL_SAMPLES) {
      bufferIndex = 0;
      bufferFilled = true;
    }
    demo_counter++;
    previousLeadOff = true;

  } else {
    if (previousLeadOff) {
      clearECGBuffer();
      previousLeadOff = false;
    }

    int raw = analogRead(ECG_PIN);
    float filtered = filterECG(raw);

    // Calculate BPM with 0-centered filtered signal (required by pulse_cal algorithm)
    hr_calculation(filtered);
    bpm = hr_avg(heart_rate);

    // Center display signal around 2048 for unified, glitch-free baseline
    displayECG = 2048.0f + filtered;

    ecgBuffer[bufferIndex] = displayECG;
    bufferIndex++;
    if (bufferIndex >= TOTAL_SAMPLES) {
      bufferIndex = 0;
      bufferFilled = true;
    }
  }
}

// Dedicated FreeRTOS Task pinned to Core 1: Guarantees strict 100 Hz sampling without OLED/I2C jitter
void ecgSamplingTask(void* pvParameters) {
  TickType_t xLastWakeTime = xTaskGetTickCount();
  const TickType_t xFrequency = pdMS_TO_TICKS(10); // Exactly 10ms = 100 Hz

  for (;;) {
    vTaskDelayUntil(&xLastWakeTime, xFrequency);
    sampleAndProcessECG();
  }
}

// =====================================================
// OLED DISPLAY ENGINE
// ECG Waveform: Y=14..48 (35 pixels tall)
// Footer: SpO2 (left) and Temperature (right) on Y=52..64
// =====================================================
void drawMovingECG() {
  oled.clearBuffer();
  oled.setDrawColor(1);

  // --- HEADER (Y: 0..12) ---
  oled.setFont(u8g2_font_6x10_tf);
  oled.drawStr(2, 9, "ECG:");

  if (leadOffConfirmed) {
    oled.drawStr(28, 9, "60");
    oled.drawStr(66, 9, "DEMO");
  } else {
    if (bpm >= 40 && bpm <= 220) {
      char bpmText[8];
      snprintf(bpmText, sizeof(bpmText), "%.0f", bpm);
      oled.drawStr(28, 9, bpmText);
    } else {
      oled.drawStr(28, 9, "--");
    }
    oled.drawStr(66, 9, "LIVE");
  }

  // Decorative battery icon (top right)
  oled.drawFrame(108, 2, 15, 7);
  oled.drawBox(123, 4, 2, 3);
  oled.drawBox(110, 4, 9, 3);

  oled.drawHLine(0, 12, 128);

  // --- ECG GRAPH (Y: 14..48 -> 35 pixels height) ---
  int count = bufferFilled ? TOTAL_SAMPLES : bufferIndex;

  if (count >= 2) {
    float minValue, maxValue;

    if (leadOffConfirmed) {
      minValue = 1800.0f;
      maxValue = 2800.0f;
    } else {
      float currentMin = ecgBuffer[0];
      float currentMax = ecgBuffer[0];
      for (int i = 1; i < count; i++) {
        if (ecgBuffer[i] < currentMin) currentMin = ecgBuffer[i];
        if (ecgBuffer[i] > currentMax) currentMax = ecgBuffer[i];
      }

      float span = currentMax - currentMin;
      if (span < 120.0f) {
        float center = (currentMax + currentMin) / 2.0f;
        currentMax = center + 60.0f;
        currentMin = center - 60.0f;
      }

      float padding = (currentMax - currentMin) * 0.08f;
      currentMax += padding;
      currentMin -= padding;

      if (autoscaleResetNeeded || (!bufferFilled && count < 10)) {
        smoothMin = currentMin;
        smoothMax = currentMax;
        autoscaleResetNeeded = false;
      } else {
        smoothMin = 0.90f * smoothMin + 0.10f * currentMin;
        smoothMax = 0.90f * smoothMax + 0.10f * currentMax;
      }

      minValue = smoothMin;
      maxValue = smoothMax;
    }

    // Peak-preserving connected waveform rendering
    int lastY = -1;
    for (int x = 0; x < 128; x++) {
      int sampleStart = (x * count) / 128;
      int sampleEnd = ((x + 1) * count) / 128;
      if (sampleEnd <= sampleStart) sampleEnd = sampleStart + 1;
      if (sampleEnd > count) sampleEnd = count;

      int firstIndex = bufferFilled ? (bufferIndex + sampleStart) % TOTAL_SAMPLES : sampleStart;
      float localMin = ecgBuffer[firstIndex];
      float localMax = localMin;
      float firstVal = localMin;
      float lastVal = localMin;

      for (int i = sampleStart; i < sampleEnd; i++) {
        int idx = bufferFilled ? (bufferIndex + i) % TOTAL_SAMPLES : i;
        float val = ecgBuffer[idx];
        if (val < localMin) localMin = val;
        if (val > localMax) localMax = val;
        if (i == sampleStart) firstVal = val;
        if (i == sampleEnd - 1) lastVal = val;
      }

      // Map vertically: 48 (bottom) to 14 (top)
      int yMin = constrain(map((int)localMin, (int)minValue, (int)maxValue, 48, 14), 14, 48);
      int yMax = constrain(map((int)localMax, (int)minValue, (int)maxValue, 48, 14), 14, 48);
      int yStart = constrain(map((int)firstVal, (int)minValue, (int)maxValue, 48, 14), 14, 48);
      int yEnd = constrain(map((int)lastVal, (int)minValue, (int)maxValue, 48, 14), 14, 48);

      if (yMin != yMax) {
        oled.drawLine(x, yMin, x, yMax);
      }

      if (x > 0 && lastY != -1) {
        oled.drawLine(x - 1, lastY, x, yStart);
      } else if (x == 0 && yMin == yMax) {
        oled.drawPixel(x, yStart);
      }

      lastY = yEnd;
    }
  }

  // --- SEPARATOR & BOTTOM VITALS (Y: 50..64) ---
  oled.drawHLine(0, 50, 128);
  oled.setFont(u8g2_font_6x10_tf);

  // SpO2 (Left) — show real readings only
  char spo2Text[16];
  if (fingerDetected && spo2Value >= 70.0f && spo2Value <= 100.0f) {
    snprintf(spo2Text, sizeof(spo2Text), "SpO2:%.0f%%", spo2Value);
  } else {
    strcpy(spo2Text, "SpO2:--");
  }
  oled.drawStr(2, 62, spo2Text);

  // Temperature (Right) — show real readings only
  char tempText[16];
  if (!isnan(temperatureC) && temperatureC >= 20.0f && temperatureC <= 50.0f) {
    snprintf(tempText, sizeof(tempText), "T:%.1fC", temperatureC);
  } else {
    strcpy(tempText, "T:--");
  }
  oled.drawStr(70, 62, tempText);

  oled.sendBuffer();
}

// =====================================================
// SETUP
// =====================================================
void setup() {
  Serial.begin(115200);

  // ADC Configuration (Pin 15)
  pinMode(ECG_PIN, INPUT);
  analogReadResolution(12);
  analogSetPinAttenuation(ECG_PIN, ADC_11db);

  pinMode(LO_PLUS_PIN, INPUT_PULLDOWN);
  pinMode(LO_MINUS_PIN, INPUT_PULLDOWN);

  // RGB Pins
  pinMode(RED_PIN, OUTPUT);
  pinMode(GREEN_PIN, OUTPUT);
  pinMode(BLUE_PIN, OUTPUT);
  digitalWrite(RED_PIN, LOW);
  digitalWrite(GREEN_PIN, LOW);
  digitalWrite(BLUE_PIN, LOW);

  // I2C
  Wire.begin(SDA_PIN, SCL_PIN);
  Wire.setClock(400000);

  // OLED splash screen
  oled.begin();
  oled.clearBuffer();
  oled.drawRFrame(0, 0, 128, 64, 5);
  oled.setFont(u8g2_font_ncenB10_tf);
  oled.drawStr(15, 22, "SMART VITAL");
  oled.setFont(u8g2_font_5x7_tf);
  oled.drawStr(36, 34, "MONITOR");
  oled.drawLine(20, 48, 45, 48);
  oled.drawLine(45, 48, 50, 42);
  oled.drawLine(50, 42, 58, 58);
  oled.drawLine(58, 58, 65, 38);
  oled.drawLine(65, 38, 70, 48);
  oled.drawLine(70, 48, 108, 48);
  oled.sendBuffer();
  delay(1500);

  // BLE initialization
  BLEDevice::init(BLE_DEVICE_NAME);
  BLEDevice::setMTU(150); // Request larger MTU to prevent packet truncation

  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());

  BLEService* service = pServer->createService(BLE_SERVICE_UUID);
  pCharacteristic = service->createCharacteristic(
    BLE_CHAR_UUID,
    BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY
  );
  pCharacteristic->addDescriptor(new BLE2902());
  service->start();
  pServer->getAdvertising()->start();

  // MAX30105
  pbx.begin(Wire);
  pbx.setup(0x1F, 4, MAX30105_MODE_MULTILED, 0x03, 0x03, 0x02);
  pbx.check();
  max30105Ready = true;

  // DS18B20
  tempSensor.begin();
  tempSensor.setWaitForConversion(false);
  tempSensorReady = (tempSensor.getDeviceCount() > 0);
  if (tempSensorReady) {
    tempSensor.requestTemperatures();
    lastTempRequest = millis();
    tempConversionPending = true;
  }

  // Filter & Buffer
  MyfilterFilter_init(&filter_ecg);
  clearECGBuffer();

  // Launch real-time 100 Hz ECG sampling task on Core 1 (Priority 5)
  xTaskCreatePinnedToCore(
    ecgSamplingTask,
    "ECGSampler",
    4096,
    NULL,
    5,
    NULL,
    1
  );

  Serial.println("SMART VITAL MONITOR READY");
  Serial.println("ECG Task: 100Hz on Core 1 (Pin 15)");
}

// =====================================================
// MAIN LOOP (UI, BLE, and Sensor Tasks on Core 0)
// =====================================================
void loop() {
  static unsigned long lastDisplay = 0;
  static unsigned long lastBLE = 0;

  // 1. SpO2 Sensor (every 250 ms)
  updateSpO2Sensor();

  // 2. Temperature Sensor (every 1000 ms non-blocking)
  if (tempSensorReady) {
    updateTemperature();
  }

  // 3. BLE Notification (clean 50 Hz rate)
  if (millis() - lastBLE >= 20) {
    lastBLE = millis();
    sendBLEData(displayECG, bpm, leadOffConfirmed, leadOffConfirmed);
    Serial.println(displayECG);
  }

  // 4. OLED Display (20 FPS / every 50 ms)
  if (millis() - lastDisplay >= 50) {
    lastDisplay = millis();
    drawMovingECG();
  }

  // Yield to allow background tasks to run smoothly
  vTaskDelay(pdMS_TO_TICKS(2));
}
