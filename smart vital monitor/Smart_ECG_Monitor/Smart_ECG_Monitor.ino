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
// OLED
// =====================================================

U8G2_SH1106_128X64_NONAME_F_HW_I2C oled(
  U8G2_R0,
  U8X8_PIN_NONE
);

// =====================================================
// BLE
// =====================================================

#define BLE_DEVICE_NAME "SmartVitalMonitor"

#define BLE_SERVICE_UUID \
"6E400001-B5A3-F393-E0A9-E50E24DCCA9E"

#define BLE_CHAR_UUID \
"6E400003-B5A3-F393-E0A9-E50E24DCCA9E"

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
// ORIGINAL ECG SETTINGS - UNCHANGED
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

float displayECG = 0;
float previousDisplayECG = 0;

float bpm = 0;

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
// ORIGINAL ECG FILTER - UNCHANGED
// =====================================================

float filterECG(float x) {
  MyfilterFilter_put(&filter_ecg, x);
  return MyfilterFilter_get(&filter_ecg);
}

// =====================================================
// ORIGINAL LEAD-OFF DETECTION - UNCHANGED
// =====================================================

bool rawLeadOff() {
  return (
    digitalRead(LO_PLUS_PIN) ||
    digitalRead(LO_MINUS_PIN)
  );
}

void updateLeadStatus() {
  bool off = rawLeadOff();

  if (off) {
    if (leadOffStartTime == 0) {
      leadOffStartTime = millis();
    }

    if (millis() - leadOffStartTime >=
        LEAD_OFF_CONFIRM_TIME) {
      leadOffConfirmed = true;
    }
  } else {
    leadOffStartTime = 0;
    leadOffConfirmed = false;
  }
}

// =====================================================
// ORIGINAL BPM RESET - UNCHANGED
// =====================================================

void resetBPM() {
  m_e = 0;
  p_e = 0;
  j_hr = 0;

  sum_e = 0;
  sum_hr = 0;

  bpm = 0;
  heart_rate = 0;
}

// =====================================================
// ORIGINAL ECG BUFFER CLEAR - UNCHANGED
// =====================================================

void clearECGBuffer() {
  float value = analogRead(ECG_PIN);

  for (int i = 0; i < TOTAL_SAMPLES; i++) {
    ecgBuffer[i] = value;
  }

  bufferIndex = 0;
  bufferFilled = false;

  displayECG = value;
  previousDisplayECG = value;

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
// DS18B20 TEMPERATURE UPDATE
// =====================================================

void updateTemperature() {
  unsigned long now = millis();

  // Read the completed conversion.
  if (tempConversionPending &&
      now - lastTempRequest >= 800) {

    float t = tempSensor.getTempCByIndex(0);

    if (t != DEVICE_DISCONNECTED_C &&
        t >= -55.0f &&
        t <= 125.0f) {
      temperatureC = t;
    } else {
      temperatureC = NAN;
    }

    tempConversionPending = false;
  }

  // Start the next conversion without blocking ECG sampling.
  if (!tempConversionPending &&
      now - lastTempRequest >= 1000) {

    tempSensor.requestTemperatures();

    lastTempRequest = now;
    tempConversionPending = true;
  }
}

// =====================================================
// BLE DATA TRANSMISSION
// ECG values and demo waveform generation are unchanged.
// =====================================================

void sendBLEData(
  float ecgValue,
  float heartRate,
  bool leadOff,
  bool demo
) {
  if (!deviceConnected || pCharacteristic == nullptr) {
    return;
  }

  char json[220];
  char spo2Text[16];
  char tempText[16];

  // No finger: illustrative demo SpO2 value.
  if (!fingerDetected) {
    strcpy(spo2Text, "98.0");
  } else if (spo2Value >= 70.0f &&
             spo2Value <= 100.0f) {
    snprintf(
      spo2Text,
      sizeof(spo2Text),
      "%.1f",
      spo2Value
    );
  } else {
    strcpy(spo2Text, "null");
  }

  // If temperature is not available, send a demo value.
  if (isnan(temperatureC)) {
    strcpy(tempText, "36.8");
  } else {
    snprintf(
      tempText,
      sizeof(tempText),
      "%.2f",
      temperatureC
    );
  }

  snprintf(
    json,
    sizeof(json),
    "{\"ecg\":%.2f,\"bpm\":%.1f,"
    "\"spo2\":%s,\"temp\":%s,"
    "\"leadOff\":%d,\"demo\":%d,"
    "\"sensorDemo\":%d}",
    ecgValue,
    heartRate,
    spo2Text,
    tempText,
    leadOff ? 1 : 0,
    demo ? 1 : 0,
    fingerDetected ? 0 : 1
  );

  pCharacteristic->setValue(
    (uint8_t*)json,
    strlen(json)
  );

  pCharacteristic->notify();
}

// =====================================================
// OLED DISPLAY
// Compact layout similar to your reference image.
// The ECG sample/filter/BPM processing is not changed.
// =====================================================

void drawMovingECG() {
  oled.clearBuffer();
  oled.setDrawColor(1);

  oled.setFont(u8g2_font_6x10_tf);

  // HEADER
  oled.drawStr(2, 9, "ECG:");

  if (leadOffConfirmed) {
    oled.drawStr(27, 9, "60");
    oled.drawStr(65, 9, "DEMO");
  } else {
    if (bpm >= 40 && bpm <= 180) {
      char bpmText[8];

      snprintf(
        bpmText,
        sizeof(bpmText),
        "%.0f",
        bpm
      );

      oled.drawStr(27, 9, bpmText);
    } else {
      oled.drawStr(27, 9, "--");
    }

    oled.drawStr(65, 9, "LIVE");
  }

  // Decorative battery icon only.
  oled.drawFrame(108, 2, 15, 7);
  oled.drawBox(123, 4, 2, 3);
  oled.drawBox(110, 4, 9, 3);

  oled.drawHLine(0, 12, 128);

  // ECG GRAPH
  int count = bufferFilled
      ? TOTAL_SAMPLES
      : bufferIndex;

  if (count >= 2) {
    float minValue;
    float maxValue;

    if (leadOffConfirmed) {
      // Original demo scale.
      minValue = 1800.0f;
      maxValue = 2800.0f;
    } else {
      // Original live autoscaling logic.
      float currentMin = ecgBuffer[0];
      float currentMax = ecgBuffer[0];

      for (int i = 1; i < count; i++) {
        if (ecgBuffer[i] < currentMin) {
          currentMin = ecgBuffer[i];
        }

        if (ecgBuffer[i] > currentMax) {
          currentMax = ecgBuffer[i];
        }
      }

      float span = currentMax - currentMin;

      if (span < 200.0f) {
        float center =
            (currentMax + currentMin) / 2.0f;

        currentMax = center + 100.0f;
        currentMin = center - 100.0f;
      }

      float padding =
          (currentMax - currentMin) * 0.1f;

      currentMax += padding;
      currentMin -= padding;

      static float smoothMin = 0;
      static float smoothMax = 0;
      static bool firstFrame = true;

      if (firstFrame || (!bufferFilled && count < 10)) {
        smoothMin = currentMin;
        smoothMax = currentMax;
        firstFrame = false;
      } else {
        smoothMin =
            0.95f * smoothMin +
            0.05f * currentMin;

        smoothMax =
            0.95f * smoothMax +
            0.05f * currentMax;
      }

      minValue = smoothMin;
      maxValue = smoothMax;
    }

    // Peak-preserving waveform rendering.
    // Only the graph's vertical display area is reduced
    // to leave room for SpO2 and temperature.
    int lastY = -1;

    for (int x = 0; x < 128; x++) {
      int sampleStart = (x * count) / 128;
      int sampleEnd = ((x + 1) * count) / 128;

      if (sampleEnd <= sampleStart) {
        sampleEnd = sampleStart + 1;
      }

      if (sampleEnd > count) {
        sampleEnd = count;
      }

      int firstIndex = bufferFilled
          ? (bufferIndex + sampleStart) % TOTAL_SAMPLES
          : sampleStart;

      float localMin = ecgBuffer[firstIndex];
      float localMax = localMin;
      float firstVal = localMin;
      float lastVal = localMin;

      for (int i = sampleStart; i < sampleEnd; i++) {
        int idx = bufferFilled
            ? (bufferIndex + i) % TOTAL_SAMPLES
            : i;

        float val = ecgBuffer[idx];

        if (val < localMin) localMin = val;
        if (val > localMax) localMax = val;

        if (i == sampleStart) firstVal = val;
        if (i == sampleEnd - 1) lastVal = val;
      }

      int yMin = constrain(
        map(
          (int)localMin,
          (int)minValue,
          (int)maxValue,
          40,
          15
        ),
        15,
        40
      );

      int yMax = constrain(
        map(
          (int)localMax,
          (int)minValue,
          (int)maxValue,
          40,
          15
        ),
        15,
        40
      );

      int yStart = constrain(
        map(
          (int)firstVal,
          (int)minValue,
          (int)maxValue,
          40,
          15
        ),
        15,
        40
      );

      int yEnd = constrain(
        map(
          (int)lastVal,
          (int)minValue,
          (int)maxValue,
          40,
          15
        ),
        15,
        40
      );

      if (yMin != yMax) {
        oled.drawLine(x, yMin, x, yMax);
      }

      if (x > 0 && lastY != -1) {
        oled.drawLine(x - 1, lastY, x, yStart);
      } else if (x == 0) {
        oled.drawPixel(x, yStart);
      }

      lastY = yEnd;
    }
  }

  oled.drawHLine(0, 43, 128);

  // SpO2
  oled.setFont(u8g2_font_6x10_tf);
  oled.drawStr(2, 53, "SpO2:");

  char spo2Text[16];

  if (!fingerDetected) {
    strcpy(spo2Text, "98% DEMO");
  } else if (spo2Value >= 70.0f &&
             spo2Value <= 100.0f) {
    snprintf(
      spo2Text,
      sizeof(spo2Text),
      "%.0f%%",
      spo2Value
    );
  } else {
    strcpy(spo2Text, "--");
  }

  oled.drawStr(38, 53, spo2Text);

  // Temperature
  oled.drawStr(2, 63, "Temp:");

  char tempText[20];

  if (isnan(temperatureC)) {
    strcpy(tempText, "36.8 C DEMO");
  } else {
    snprintf(
      tempText,
      sizeof(tempText),
      "%.1f C",
      temperatureC
    );
  }

  oled.drawStr(38, 63, tempText);

  oled.sendBuffer();
}

// =====================================================
// SETUP
// =====================================================

void setup() {
  Serial.begin(115200);

  // Original ECG ADC configuration.
  analogReadResolution(12);
  analogSetPinAttenuation(ECG_PIN, ADC_11db);

  pinMode(LO_PLUS_PIN, INPUT);
  pinMode(LO_MINUS_PIN, INPUT);

  // RGB LEDs remain OFF, as in your reference code.
  pinMode(RED_PIN, OUTPUT);
  pinMode(GREEN_PIN, OUTPUT);
  pinMode(BLUE_PIN, OUTPUT);

  digitalWrite(RED_PIN, LOW);
  digitalWrite(GREEN_PIN, LOW);
  digitalWrite(BLUE_PIN, LOW);

  // I2C
  Wire.begin(SDA_PIN, SCL_PIN);
  Wire.setClock(400000);

  // OLED initialization
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

  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());

  BLEService* service =
      pServer->createService(BLE_SERVICE_UUID);

  pCharacteristic =
      service->createCharacteristic(
        BLE_CHAR_UUID,
        BLECharacteristic::PROPERTY_READ |
        BLECharacteristic::PROPERTY_NOTIFY
      );

  pCharacteristic->addDescriptor(new BLE2902());

  service->start();
  pServer->getAdvertising()->start();

  // MAX30105 initialization
  pbx.begin(Wire);

  pbx.setup(
    0x1F,
    4,
    MAX30105_MODE_MULTILED,
    0x03,
    0x03,
    0x02
  );

  pbx.check();

  max30105Ready = true;

  Serial.println("MAX30105 initialization attempted");

  // DS18B20 initialization
  tempSensor.begin();
  tempSensor.setWaitForConversion(false);

  tempSensorReady =
      tempSensor.getDeviceCount() > 0;

  if (tempSensorReady) {
    tempSensor.requestTemperatures();

    lastTempRequest = millis();
    tempConversionPending = true;

    Serial.println("DS18B20 detected");
  } else {
    Serial.println("DS18B20 not detected");
  }

  // Original ECG filter and buffer initialization.
  MyfilterFilter_init(&filter_ecg);
  clearECGBuffer();

  Serial.println("SMART VITAL MONITOR STARTED");
  Serial.println("ECG Sampling: 100Hz");
  Serial.println("ECG Display Buffer: 3 seconds");
  Serial.println("Lead-Off Demo Confirmation: 2 seconds");
}

// =====================================================
// LOOP
// =====================================================

void loop() {
  static unsigned long lastSample = 0;
  static unsigned long lastDisplay = 0;
  static bool previousLeadOff = false;

  // Update added sensors without delay().
  updateSpO2Sensor();

  if (tempSensorReady) {
    updateTemperature();
  }

  // Original ECG sampling timing.
  if (lastSample == 0) {
    lastSample = micros();
  }

  if (micros() - lastSample >= 10000) {
    lastSample += 10000;

    // Original lead detection.
    updateLeadStatus();

    // ===============================================
    // ORIGINAL ECG DEMO WAVEFORM - UNCHANGED
    // ===============================================

    if (leadOffConfirmed) {
      static int demo_counter = 0;

      float phase =
          (demo_counter % 100) / 100.0f;

      float val = 0.0f;

      if (phase >= 0.12 && phase <= 0.22) {
        val = 1.8 * sin(
          ((phase - 0.12) / 0.10) * PI
        );
      } else if (phase >= 0.27 && phase <= 0.29) {
        val = -2.0 * sin(
          ((phase - 0.27) / 0.02) * PI
        );
      } else if (phase >= 0.29 && phase <= 0.32) {
        val = 22.0 * sin(
          ((phase - 0.29) / 0.03) * PI
        );
      } else if (phase >= 0.32 && phase <= 0.35) {
        val = -5.5 * sin(
          ((phase - 0.32) / 0.03) * PI
        );
      } else if (phase >= 0.45 && phase <= 0.60) {
        val = 4.5 * sin(
          ((phase - 0.45) / 0.15) * PI
        );
      }

      float fake_val = 2048.0f + (val * 30.0f);

      displayECG = fake_val;

      ecgBuffer[bufferIndex] = displayECG;

      bufferIndex++;

      if (bufferIndex >= TOTAL_SAMPLES) {
        bufferIndex = 0;
        bufferFilled = true;
      }

      demo_counter++;

      Serial.println(fake_val);

      // Added BLE transmission; original waveform unchanged.
      sendBLEData(
        fake_val,
        60.0f,
        true,
        true
      );

      previousLeadOff = true;

    } else {
      // =============================================
      // ORIGINAL LIVE ECG PROCESSING - UNCHANGED
      // =============================================

      if (previousLeadOff) {
        clearECGBuffer();
        previousLeadOff = false;
      }

      int raw = analogRead(ECG_PIN);

      float filtered = filterECG(raw);

      displayECG = filtered;

      hr_calculation(displayECG);

      bpm = hr_avg(heart_rate);

      ecgBuffer[bufferIndex] = displayECG;

      bufferIndex++;

      if (bufferIndex >= TOTAL_SAMPLES) {
        bufferIndex = 0;
        bufferFilled = true;
      }

      Serial.println(displayECG);

      // Added BLE JSON; original ECG calculations unchanged.
      sendBLEData(
        displayECG,
        bpm,
        false,
        false
      );
    }
  }

  // OLED update: original 20 FPS timing.
  if (millis() - lastDisplay >= 50) {
    lastDisplay = millis();
    drawMovingECG();
  }
}