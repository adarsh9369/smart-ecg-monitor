#include <Wire.h>
#include <U8g2lib.h>
#include <math.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include "ecg.h"
#include "pulse_cal.h"
#include "mv_hr.h"

// =====================================================
// PIN CONFIGURATION
// =====================================================

#define ECG_PIN       34
#define LO_PLUS_PIN   33
#define LO_MINUS_PIN  32

// RGB PINS (Noted for future use)
// #define RED_PIN       27
// #define GREEN_PIN     13
// #define BLUE_PIN      14

#define SDA_PIN       21
#define SCL_PIN       22

// =====================================================
// OLED
// =====================================================

U8G2_SH1106_128X64_NONAME_F_HW_I2C oled(U8G2_R0, U8X8_PIN_NONE);

// =====================================================
// BLE SETUP
// =====================================================

#define BLE_SERVICE_UUID        "19b10000-e8f2-537e-4f6c-d104768a1214"
#define BLE_CHAR_UUID           "19b10001-e8f2-537e-4f6c-d104768a1214"

BLEServer* pServer = NULL;
BLECharacteristic* pCharacteristic = NULL;
bool deviceConnected = false;

class MyServerCallbacks: public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) {
      deviceConnected = true;
    };
    void onDisconnect(BLEServer* pServer) {
      deviceConnected = false;
      pServer->startAdvertising(); // restart advertising
    }
};

// =====================================================
// ECG SETTINGS
// =====================================================

#define SAMPLE_RATE       100
#define DISPLAY_TIME      3
#define TOTAL_SAMPLES     (SAMPLE_RATE * DISPLAY_TIME)

// =====================================================
// LEAD OFF
// =====================================================

#define LEAD_OFF_CONFIRM_TIME 2000

// =====================================================
// ECG BUFFER
// =====================================================

float ecgBuffer[TOTAL_SAMPLES];
int bufferIndex = 0;
bool bufferFilled = false;

// =====================================================
// FILTER
// =====================================================

MyfilterFilter filter_ecg;

// =====================================================
// LEAD STATUS
// =====================================================

bool leadOffConfirmed = false;
unsigned long leadOffStartTime = 0;

// =====================================================
// ECG DISPLAY SMOOTHING
// =====================================================

float displayECG = 0;
float previousDisplayECG = 0;

// =====================================================
// BPM DETECTION
// =====================================================

float bpm = 0;

// RGB LED (Disabled)
// void setRGB(bool r, bool g, bool b) {
//   digitalWrite(RED_PIN, r);
//   digitalWrite(GREEN_PIN, g);
//   digitalWrite(BLUE_PIN, b);
// }

// =====================================================
// HIGH PASS 0.5 Hz
// =====================================================

float filterECG(float x) {
  MyfilterFilter_put(&filter_ecg, x);
  return MyfilterFilter_get(&filter_ecg);
}

// =====================================================
// RAW LEAD OFF
// =====================================================

bool rawLeadOff() {
  return (digitalRead(LO_PLUS_PIN) || digitalRead(LO_MINUS_PIN));
}

// =====================================================
// LEAD STATUS
// =====================================================

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
// RESET BPM
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
// CLEAR BUFFER
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
// LEADS OFF SCREEN
// =====================================================

void showLeadOff() {
  oled.clearBuffer();
  oled.setFont(u8g2_font_10x20_tf);
  oled.drawStr(10, 30, "LEADS OFF");
  oled.setFont(u8g2_font_6x10_tf);
  oled.drawStr(27, 50, "Check leads");
  oled.sendBuffer();
}

// =====================================================
// DRAW ECG
// =====================================================

void drawMovingECG() {
  oled.clearBuffer();

  int count = bufferFilled ? TOTAL_SAMPLES : bufferIndex;
  if (count < 2) return;

  float minValue, maxValue;

  if (leadOffConfirmed) {
    // Fixed scale for stable demo graph
    minValue = 1800.0;
    maxValue = 2800.0;
  } else {
    // AUTOSCALE DISPLAY RANGE
    float currentMin = ecgBuffer[0];
    float currentMax = ecgBuffer[0];
    for (int i = 1; i < count; i++) {
      if (ecgBuffer[i] < currentMin) currentMin = ecgBuffer[i];
      if (ecgBuffer[i] > currentMax) currentMax = ecgBuffer[i];
    }
    
    // Ensure a minimum vertical span so flatlines/noise aren't amplified to fill the screen
    float span = currentMax - currentMin;
    if (span < 200.0) {
      float center = (currentMax + currentMin) / 2.0;
      currentMax = center + 100.0;
      currentMin = center - 100.0;
    }
    
    // Add a 10% visual padding to top and bottom
    float padding = (currentMax - currentMin) * 0.1;
    currentMax += padding;
    currentMin -= padding;

    // Smooth the scaling to prevent the graph from shaking
    static float smoothMin = 0;
    static float smoothMax = 0;
    static bool firstFrame = true;
    
    if (firstFrame || (!bufferFilled && count < 10)) {
      smoothMin = currentMin;
      smoothMax = currentMax;
      firstFrame = false;
    } else {
      // 95% old, 5% new for a very stable transition
      smoothMin = 0.95 * smoothMin + 0.05 * currentMin;
      smoothMax = 0.95 * smoothMax + 0.05 * currentMax;
    }
    
    minValue = smoothMin;
    maxValue = smoothMax;
  }

  // ECG GRAPH (Peak-preserving render to fix aliasing/fluctuation)
  int lastY = -1;
  for (int x = 0; x < 128; x++) {
    int sampleStart = (x * count) / 128;
    int sampleEnd = ((x + 1) * count) / 128;
    if (sampleEnd <= sampleStart) sampleEnd = sampleStart + 1;
    if (sampleEnd > count) sampleEnd = count;

    float localMin = ecgBuffer[bufferFilled ? (bufferIndex + sampleStart) % TOTAL_SAMPLES : sampleStart];
    float localMax = localMin;
    float lastVal = localMin;
    float firstVal = localMin;

    for (int i = sampleStart; i < sampleEnd; i++) {
      int idx = bufferFilled ? (bufferIndex + i) % TOTAL_SAMPLES : i;
      float val = ecgBuffer[idx];
      if (val < localMin) localMin = val;
      if (val > localMax) localMax = val;
      if (i == sampleEnd - 1) lastVal = val;
    }

    int yMin = constrain(map((int)localMin, (int)minValue, (int)maxValue, 61, 16), 16, 61);
    int yMax = constrain(map((int)localMax, (int)minValue, (int)maxValue, 61, 16), 16, 61);
    int yEnd = constrain(map((int)lastVal, (int)minValue, (int)maxValue, 61, 16), 16, 61);
    int yStart = constrain(map((int)firstVal, (int)minValue, (int)maxValue, 61, 16), 16, 61);

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

  // TOP STATUS (Simple text header as requested)
  oled.setDrawColor(1);
  oled.setFont(u8g2_font_6x10_tf);
  
  if (leadOffConfirmed) {
    oled.drawStr(2, 10, "ECG DEMO");
    oled.drawStr(90, 10, "60 BPM");
  } else {
    oled.drawStr(2, 10, "ECG LIVE");
    oled.drawStr(85, 10, "BPM:");
    if (bpm >= 40 && bpm <= 180) {
      char text[8];
      sprintf(text, "%d", (int)round(bpm));
      oled.drawStr(110, 10, text);
    } else {
      oled.drawStr(110, 10, "--");
    }
  }
  
  oled.drawHLine(0, 12, 128); // Draw a separation line under the header
  oled.sendBuffer();
}

// =====================================================
// SETUP
// =====================================================

void setup() {
  Serial.begin(115200);

  // ADC
  analogReadResolution(12);
  analogSetPinAttenuation(ECG_PIN, ADC_11db);

  // Lead pins
  pinMode(LO_PLUS_PIN, INPUT);
  pinMode(LO_MINUS_PIN, INPUT);

  // RGB (Disabled but explicitly forced OFF to prevent glowing)
  pinMode(27, OUTPUT); // RED
  pinMode(13, OUTPUT); // GREEN
  pinMode(14, OUTPUT); // BLUE
  digitalWrite(27, LOW);
  digitalWrite(13, LOW);
  digitalWrite(14, LOW);

  // I2C
  Wire.begin(SDA_PIN, SCL_PIN);
  Wire.setClock(400000);

  // BLE Initialization
  BLEDevice::init("Smart_ECG_Monitor");
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());
  BLEService *pService = pServer->createService(BLE_SERVICE_UUID);
  pCharacteristic = pService->createCharacteristic(
                      BLE_CHAR_UUID,
                      BLECharacteristic::PROPERTY_READ   |
                      BLECharacteristic::PROPERTY_NOTIFY
                    );
  pCharacteristic->addDescriptor(new BLE2902());
  pService->start();
  pServer->getAdvertising()->start();

  // OLED
  oled.begin();
  oled.clearBuffer();
  
  // Draw professional rounded frame
  oled.drawRFrame(0, 0, 128, 64, 5);
  
  // Main Title
  oled.setFont(u8g2_font_ncenB10_tf);
  oled.drawStr(20, 22, "SMART ECG");
  
  // Subtitle
  oled.setFont(u8g2_font_5x7_tf);
  oled.drawStr(36, 34, "MONITORING");
  
  // Draw an ECG trace decoration
  oled.drawLine(20, 48, 45, 48);
  oled.drawLine(45, 48, 50, 42);
  oled.drawLine(50, 42, 58, 58);
  oled.drawLine(58, 58, 65, 38);
  oled.drawLine(65, 38, 70, 48);
  oled.drawLine(70, 48, 108, 48);
  
  oled.sendBuffer();

  delay(2500);

  // Filters
  MyfilterFilter_init(&filter_ecg);

  // Buffer
  clearECGBuffer();

  Serial.println("SMART ECG STARTED");
  Serial.println("100Hz Sampling");
  Serial.println("3 Second Display");
  Serial.println("50Hz Notch");
  Serial.println("0.5-40Hz Filter");
  Serial.println("Stable BPM Detection");
  Serial.println("2 Second Lead Off");
}

// =====================================================
// LOOP
// =====================================================

void loop() {
  static unsigned long lastSample = 0;
  static unsigned long lastDisplay = 0;
  static bool previousLeadOff = false;

  // Prevent catch-up loop freezing
  if (lastSample == 0) lastSample = micros();

  // ===================================================
  // ECG SAMPLE
  // ===================================================

  if (micros() - lastSample >= 10000) {
    lastSample += 10000;

    // LEAD STATUS
    updateLeadStatus();

    // LEAD OFF
    if (leadOffConfirmed) {
      // (LED blinking removed)
      
      // Simulate ECG for DEMO mode (P-QRS-T mathematical model)
      static int demo_counter = 0;
      float phase = (demo_counter % 100) / 100.0;
      float val = 0.0;
      
      if (phase >= 0.12 && phase <= 0.22) {
        val = 1.8 * sin(((phase - 0.12) / 0.10) * PI);
      } else if (phase >= 0.27 && phase <= 0.29) {
        val = -2.0 * sin(((phase - 0.27) / 0.02) * PI);
      } else if (phase >= 0.29 && phase <= 0.32) {
        val = 22.0 * sin(((phase - 0.29) / 0.03) * PI);
      } else if (phase >= 0.32 && phase <= 0.35) {
        val = -5.5 * sin(((phase - 0.32) / 0.03) * PI);
      } else if (phase >= 0.45 && phase <= 0.60) {
        val = 4.5 * sin(((phase - 0.45) / 0.15) * PI);
      }

      float fake_val = 2048.0 + (val * 30.0);

      displayECG = fake_val;
      ecgBuffer[bufferIndex] = displayECG;
      bufferIndex++;
      if (bufferIndex >= TOTAL_SAMPLES) {
        bufferIndex = 0;
        bufferFilled = true;
      }
      demo_counter++;
      
      // Notify Leads Off continuously but send demo data
      char outputBuffer[64];
      snprintf(outputBuffer, sizeof(outputBuffer), "%.2f %.2f 60 1", fake_val, fake_val);
      Serial.println(fake_val);
      if (deviceConnected) {
        pCharacteristic->setValue(outputBuffer);
        pCharacteristic->notify();
      }
      
      previousLeadOff = true;
    } else {
      // CONNECTED
      // (LED reset removed)
      if (previousLeadOff) {
        clearECGBuffer();
        previousLeadOff = false;
      }

      // READ
      int raw = analogRead(ECG_PIN);

      // FILTER
      float filtered = filterECG(raw);

      // DISPLAY SMOOTHING (Removed to preserve sharp QRS peaks)
      displayECG = filtered;

      // BPM
      hr_calculation(displayECG);
      bpm = hr_avg(heart_rate);

      // BUFFER
      ecgBuffer[bufferIndex] = displayECG;
      bufferIndex++;

      if (bufferIndex >= TOTAL_SAMPLES) {
        bufferIndex = 0;
        bufferFilled = true;
      }

      // OUTPUT: raw_val filtered_val bpm leads_off
      char outputBuffer[64];
      snprintf(outputBuffer, sizeof(outputBuffer), "%.2f %.2f %d 0", (float)raw, displayECG, (int)round(bpm));
      
      // SERIAL
      Serial.println(displayECG);

      // BLE NOTIFY
      if (deviceConnected) {
        pCharacteristic->setValue(outputBuffer);
        pCharacteristic->notify();
      }
    }
  }

  // ===================================================
  // OLED UPDATE
  // ===================================================

  // 20 FPS
  if (millis() - lastDisplay >= 50) {
    lastDisplay = millis();
    drawMovingECG();
  }
}
