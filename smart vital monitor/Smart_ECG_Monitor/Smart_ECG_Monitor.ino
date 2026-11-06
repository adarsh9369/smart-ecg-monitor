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
// ECG signal pin: GPIO 34 (ADC1 - dedicated analog input, works seamlessly with BLE)
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
// BLE SAMPLE FIFO (Matches App speed exactly to 100 Hz OLED)
// =====================================================
#define BLE_QUEUE_SIZE 128
float bleQueue[BLE_QUEUE_SIZE];
volatile int bleQueueHead = 0;
volatile int bleQueueTail = 0;

void queueBleEcg(float val) {
  int next = (bleQueueHead + 1) % BLE_QUEUE_SIZE;
  if (next != bleQueueTail) {
    bleQueue[bleQueueHead] = val;
    bleQueueHead = next;
  }
}

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

// =====================================================
// COMMON-CATHODE RGB LED INDICATORS (Non-blocking)
// =====================================================
void updateLEDs() {
  static unsigned long lastBlinkTime = 0;
  static bool blinkState = false;
  unsigned long now = millis();

  if (now - lastBlinkTime >= 500) {
    lastBlinkTime = now;
    blinkState = !blinkState;
  }

  // Priority 1: ECG Lead-Off Alert (Red LED blinks at 1 Hz, Blue strictly OFF)
  if (leadOffConfirmed) {
    digitalWrite(RED_PIN, blinkState ? HIGH : LOW);
    digitalWrite(GREEN_PIN, LOW);
    digitalWrite(BLUE_PIN, LOW);
    return;
  }

  // Priority 2: BLE Disconnected (Blue LED blinks at 1 Hz)
  if (!deviceConnected) {
    digitalWrite(RED_PIN, LOW);
    digitalWrite(GREEN_PIN, LOW);
    digitalWrite(BLUE_PIN, blinkState ? HIGH : LOW);
    return;
  }

  // Priority 3: BLE Connected & Normal Monitoring (Solid Green)
  digitalWrite(RED_PIN, LOW);
  digitalWrite(GREEN_PIN, HIGH);
  digitalWrite(BLUE_PIN, LOW);
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
// BLE DATA TRANSMISSION (Transmits full 100 Hz stream)
// =====================================================
void flushBleQueue() {
  if (!deviceConnected || pCharacteristic == nullptr) {
    bleQueueTail = bleQueueHead; // Drain queue when disconnected
    return;
  }

  // Pull accumulated samples from queue (up to 5 samples per batch)
  float batch[8];
  int count = 0;
  while (bleQueueTail != bleQueueHead && count < 5) {
    batch[count++] = bleQueue[bleQueueTail];
    bleQueueTail = (bleQueueTail + 1) % BLE_QUEUE_SIZE;
  }

  if (count == 0) return;

  char json[240];
  char ecgBatchStr[90] = "[";
  for (int i = 0; i < count; i++) {
    char num[16];
    snprintf(num, sizeof(num), i == 0 ? "%.1f" : ",%.1f", batch[i]);
    strcat(ecgBatchStr, num);
  }
  strcat(ecgBatchStr, "]");

  char spo2Text[12];
  char tempText[12];

  // Strictly real sensor readings only — never forge demo values during live sessions!
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

  char bpmText[12];
  if (!leadOffConfirmed && bpm >= 35.0f && bpm <= 220.0f) {
    snprintf(bpmText, sizeof(bpmText), "%.0f", bpm);
  } else {
    strcpy(bpmText, "null");
  }

  snprintf(
    json,
    sizeof(json),
    "{\"ecg\":%.1f,\"ecgs\":%s,\"bpm\":%s,\"spo2\":%s,\"temp\":%s,\"leadOff\":%d,\"demo\":0,\"sensorDemo\":0}",
    batch[count - 1],
    ecgBatchStr,
    bpmText,
    spo2Text,
    tempText,
    leadOffConfirmed ? 1 : 0
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
    // Leads disconnected: Maintain flat baseline (2048), DO NOT synthesize fake ECG during live mode
    displayECG = 2048.0f;
    bpm = 0.0f;
    previousLeadOff = true;

    ecgBuffer[bufferIndex] = displayECG;
    bufferIndex++;
    if (bufferIndex >= TOTAL_SAMPLES) {
      bufferIndex = 0;
      bufferFilled = true;
    }

    // Push flatline baseline sample to BLE queue
    queueBleEcg(displayECG);

  } else {
    if (previousLeadOff) {
      clearECGBuffer();
      previousLeadOff = false;
    }

    int raw = analogRead(ECG_PIN);
    float filtered = filterECG(raw);

    // Calculate BPM with 0-centered filtered signal
    hr_calculation(filtered);
    bpm = hr_avg(heart_rate);

    // Center display signal around 2048 for unified baseline
    displayECG = 2048.0f + filtered;

    ecgBuffer[bufferIndex] = displayECG;
    bufferIndex++;
    if (bufferIndex >= TOTAL_SAMPLES) {
      bufferIndex = 0;
      bufferFilled = true;
    }

    // Push live sample to BLE queue
    queueBleEcg(displayECG);
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
// OLED DISPLAY ENGINE (128x64 SH1106 Medical Layout)
// - Top Bar (Y: 0..11): College, Mode Badge & BLE Status
// - ECG Trace (Y: 12..49): 38px High Peak-Preserving Waveform
// - Bottom Vitals Dock (Y: 51..64): 3 Balanced Columns (HR | SpO2 | Temp)
// - Battery Icon Removed per specification
// =====================================================
void drawMovingECG() {
  oled.clearBuffer();
  oled.setDrawColor(1);

  // 1. TOP STATUS BAR (Y: 0..11)
  oled.setFont(u8g2_font_5x8_tf);
  oled.drawStr(2, 9, "HITM LKO");

  if (leadOffConfirmed) {
    // Inverted high-visibility alert box for lead-off warning
    oled.drawRBox(46, 1, 48, 9, 2);
    oled.setDrawColor(0);
    oled.setFont(u8g2_font_5x7_tf);
    oled.drawStr(49, 8, "!LEAD OFF!");
    oled.setDrawColor(1);
  } else {
    oled.setFont(u8g2_font_5x7_tf);
    oled.drawStr(52, 8, deviceConnected ? "[LIVE]" : "[DEMO]");
  }

  // BLE Connection Status Indicator (Solid dot = connected, hollow = disconnected)
  oled.setFont(u8g2_font_5x7_tf);
  if (deviceConnected) {
    oled.drawDisc(106, 5, 2);
    oled.drawStr(111, 8, "BLE");
  } else {
    oled.drawCircle(106, 5, 2);
    oled.drawStr(111, 8, "BLE");
  }

  // Top Separator Line
  oled.drawHLine(0, 11, 128);

  // 2. ECG WAVEFORM AREA (Y: 12..49 -> 38 Pixels Height!)
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

    // Lead identifier in top-left corner of ECG area
    oled.setFont(u8g2_font_4x6_tf);
    oled.drawStr(2, 18, "II");

    // Peak-preserving connected waveform rendering across Y: 13 to 49
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

      // Map vertically: 49 (bottom) to 13 (top)
      int yMin = constrain(map((int)localMin, (int)minValue, (int)maxValue, 49, 13), 13, 49);
      int yMax = constrain(map((int)localMax, (int)minValue, (int)maxValue, 49, 13), 13, 49);
      int yStart = constrain(map((int)firstVal, (int)minValue, (int)maxValue, 49, 13), 13, 49);
      int yEnd = constrain(map((int)lastVal, (int)minValue, (int)maxValue, 49, 13), 13, 49);

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

  // 3. BOTTOM VITALS DOCK (Y: 50..64)
  oled.drawHLine(0, 50, 128);

  // Vertical column dividers
  oled.drawVLine(42, 51, 13);
  oled.drawVLine(85, 51, 13);

  oled.setFont(u8g2_font_5x8_tf);

  // Column 1: Heart Rate (x: 2..40)
  char hrText[12];
  if (!leadOffConfirmed && bpm >= 35.0f && bpm <= 220.0f) {
    snprintf(hrText, sizeof(hrText), "HR:%.0f", bpm);
  } else {
    strcpy(hrText, "HR:N/A");
  }
  oled.drawStr(3, 61, hrText);

  // Column 2: SpO2 Blood Oxygen (x: 45..83)
  char spo2Text[12];
  if (fingerDetected && spo2Value >= 70.0f && spo2Value <= 100.0f) {
    snprintf(spo2Text, sizeof(spo2Text), "O2:%.0f%%", spo2Value);
  } else {
    strcpy(spo2Text, "O2:N/A");
  }
  oled.drawStr(46, 61, spo2Text);

  // Column 3: Temperature (x: 88..126)
  char tempText[12];
  if (!isnan(temperatureC) && temperatureC >= 20.0f && temperatureC <= 50.0f) {
    snprintf(tempText, sizeof(tempText), "T:%.1fC", temperatureC);
  } else {
    strcpy(tempText, "T:N/A");
  }
  oled.drawStr(88, 61, tempText);

  oled.sendBuffer();
}

// =====================================================
// SETUP
// =====================================================
void setup() {
  Serial.begin(115200);

  // ADC Configuration (Pin 34)
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
  pinMode(TEMP_PIN, INPUT_PULLUP);
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
  Serial.println("ECG Task: 100Hz on Core 1 (Pin 34)");
}

// =====================================================
// MAIN LOOP (UI, BLE, Sensor Tasks & LEDs on Core 0)
// =====================================================
void loop() {
  static unsigned long lastDisplay = 0;
  static unsigned long lastBLE = 0;

  // 1. Update Common-Cathode RGB LED States
  updateLEDs();

  // 2. SpO2 Sensor (every 250 ms)
  updateSpO2Sensor();

  // 3. Temperature Sensor (every 1000 ms non-blocking)
  if (tempSensorReady) {
    updateTemperature();
  }

  // 4. BLE Notification (transmits all accumulated 100 Hz samples every 35 ms)
  if (millis() - lastBLE >= 35) {
    lastBLE = millis();
    flushBleQueue();
  }

  // 5. OLED Display (20 FPS / every 50 ms)
  if (millis() - lastDisplay >= 50) {
    lastDisplay = millis();
    drawMovingECG();
  }

  // Yield to allow background tasks to run smoothly
  vTaskDelay(pdMS_TO_TICKS(2));
}
