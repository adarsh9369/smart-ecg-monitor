#ifndef MAX30105_H
#define MAX30105_H

#include <Arduino.h>
#include <Wire.h>

// =====================================================================
// MAX30105 / MAX30102 REGISTER DEFINITIONS
// Direct native I2C implementation — no external library installation needed!
// =====================================================================
#define MAX30105_ADDRESS          0x57

// Status Registers
#define REG_INT_STATUS_1          0x00
#define REG_INT_STATUS_2          0x01
#define REG_INT_ENABLE_1          0x02
#define REG_INT_ENABLE_2          0x03

// FIFO Registers
#define REG_FIFO_WR_PTR           0x04
#define REG_OVF_COUNTER           0x05
#define REG_FIFO_RD_PTR           0x06
#define REG_FIFO_DATA             0x07

// Configuration Registers
#define REG_FIFO_CONFIG           0x08
#define REG_MODE_CONFIG           0x09
#define REG_SPO2_CONFIG           0x0A
#define REG_LED1_PA               0x0C // Red LED pulse amplitude
#define REG_LED2_PA               0x0D // IR LED pulse amplitude
#define REG_LED3_PA               0x0E // Green LED pulse amplitude

// Die Temperature Registers
#define REG_DIE_TEMP_INT          0x1F
#define REG_DIE_TEMP_FRAC         0x20
#define REG_DIE_TEMP_CONFIG       0x21

// Part ID
#define REG_PART_ID               0xFF

class MAX30105_Sensor {
private:
  TwoWire* _i2c;
  uint32_t _redValue;
  uint32_t _irValue;
  
  // SpO2 calculation variables
  float _dcRed;
  float _dcIr;
  float _acSqSumRed;
  float _acSqSumIr;
  int _sampleCount;
  int _lastCalculatedSpO2;
  bool _fingerPresent;

  void writeRegister(uint8_t reg, uint8_t val) {
    _i2c->beginTransmission(MAX30105_ADDRESS);
    _i2c->write(reg);
    _i2c->write(val);
    _i2c->endTransmission();
  }

  uint8_t readRegister(uint8_t reg) {
    _i2c->beginTransmission(MAX30105_ADDRESS);
    _i2c->write(reg);
    _i2c->endTransmission(false);
    _i2c->requestFrom((uint8_t)MAX30105_ADDRESS, (uint8_t)1);
    if (_i2c->available()) {
      return _i2c->read();
    }
    return 0;
  }

public:
  MAX30105_Sensor() : _i2c(&Wire), _redValue(0), _irValue(0),
                      _dcRed(0), _dcIr(0), _acSqSumRed(0), _acSqSumIr(0),
                      _sampleCount(0), _lastCalculatedSpO2(98), _fingerPresent(false) {}

  bool begin(TwoWire &wirePort = Wire) {
    _i2c = &wirePort;

    // Check if device is responding on I2C address 0x57
    _i2c->beginTransmission(MAX30105_ADDRESS);
    if (_i2c->endTransmission() != 0) {
      return false;
    }

    uint8_t partId = readRegister(REG_PART_ID);
    // MAX30105 Part ID is 0x15, MAX30102 is 0x11
    if (partId != 0x15 && partId != 0x11) {
      // Still connected at 0x57, proceed
    }

    setup();
    return true;
  }

  void setup() {
    // Reset sensor
    writeRegister(REG_MODE_CONFIG, 0x40);
    delay(50);

    // FIFO Configuration:
    // Sample avg = 4, FIFO roll-over enabled, FIFO almost full = 17
    writeRegister(REG_FIFO_CONFIG, 0x5F);

    // Mode Configuration: SpO2 mode (Red + IR active)
    writeRegister(REG_MODE_CONFIG, 0x03);

    // SpO2 Configuration:
    // ADC range: 4096nA, Sample rate: 100 Hz, Pulse width: 411us (18-bit ADC resolution)
    writeRegister(REG_SPO2_CONFIG, 0x27);

    // LED Pulse Amplitude (current):
    // 0x24 = ~7.2mA for Red and IR (optimal for finger penetration without saturation)
    writeRegister(REG_LED1_PA, 0x24); // Red
    writeRegister(REG_LED2_PA, 0x24); // IR
    writeRegister(REG_LED3_PA, 0x00); // Green off

    // Reset FIFO pointers
    writeRegister(REG_FIFO_WR_PTR, 0x00);
    writeRegister(REG_OVF_COUNTER, 0x00);
    writeRegister(REG_FIFO_RD_PTR, 0x00);
  }

  // Poll available samples from FIFO
  bool readFIFO() {
    uint8_t wrPtr = readRegister(REG_FIFO_WR_PTR);
    uint8_t rdPtr = readRegister(REG_FIFO_RD_PTR);
    int numSamples = (wrPtr - rdPtr) & 0x1F;

    if (numSamples == 0) return false;

    // Read latest sample
    for (int i = 0; i < numSamples; i++) {
      _i2c->beginTransmission(MAX30105_ADDRESS);
      _i2c->write(REG_FIFO_DATA);
      _i2c->endTransmission(false);
      _i2c->requestFrom((uint8_t)MAX30105_ADDRESS, (uint8_t)6);

      if (_i2c->available() >= 6) {
        uint32_t r1 = _i2c->read();
        uint32_t r2 = _i2c->read();
        uint32_t r3 = _i2c->read();
        _redValue = ((r1 << 16) | (r2 << 8) | r3) & 0x03FFFF;

        uint32_t ir1 = _i2c->read();
        uint32_t ir2 = _i2c->read();
        uint32_t ir3 = _i2c->read();
        _irValue = ((ir1 << 16) | (ir2 << 8) | ir3) & 0x03FFFF;
      }
    }

    // Process new sample for SpO2
    processSample();
    return true;
  }

  uint32_t getRed() const { return _redValue; }
  uint32_t getIR() const { return _irValue; }
  bool isFingerDetected() const { return _fingerPresent; }
  int getSpO2() const { return _lastCalculatedSpO2; }

  // Read internal die temperature (accurate to 0.0625°C)
  float readDieTemperature() {
    writeRegister(REG_DIE_TEMP_CONFIG, 0x01); // Trigger temperature measurement
    unsigned long start = millis();
    while ((readRegister(REG_DIE_TEMP_CONFIG) & 0x01) && (millis() - start < 100)) {
      delay(5);
    }
    int8_t tempInt = (int8_t)readRegister(REG_DIE_TEMP_INT);
    uint8_t tempFrac = readRegister(REG_DIE_TEMP_FRAC);
    return (float)tempInt + ((float)tempFrac * 0.0625f);
  }

private:
  void processSample() {
    // Finger detection threshold (18-bit ADC: > 20000 means finger is placed on sensor)
    if (_irValue < 20000) {
      _fingerPresent = false;
      _sampleCount = 0;
      _acSqSumRed = 0;
      _acSqSumIr = 0;
      return;
    }

    _fingerPresent = true;

    // Initialize DC baselines if starting
    if (_dcRed == 0) _dcRed = _redValue;
    if (_dcIr == 0) _dcIr = _irValue;

    // Low-pass exponential moving average for DC component (tau ~ 0.95)
    _dcRed = 0.95f * _dcRed + 0.05f * (float)_redValue;
    _dcIr  = 0.95f * _dcIr  + 0.05f * (float)_irValue;

    // High-pass pulsatile AC component
    float acRed = (float)_redValue - _dcRed;
    float acIr  = (float)_irValue  - _dcIr;

    _acSqSumRed += acRed * acRed;
    _acSqSumIr  += acIr * acIr;
    _sampleCount++;

    // Calculate SpO2 every 100 samples (~1 second at 100Hz)
    if (_sampleCount >= 100) {
      if (_dcRed > 1000 && _dcIr > 1000 && _acSqSumIr > 0) {
        float rmsRed = sqrt(_acSqSumRed / (float)_sampleCount);
        float rmsIr  = sqrt(_acSqSumIr  / (float)_sampleCount);

        // Ratio of Ratios R = (AC_red / DC_red) / (AC_ir / DC_ir)
        float rRatio = (rmsRed / _dcRed) / (rmsIr / _dcIr);

        // Standard empirical calibration formula for human blood oxygenation:
        // SpO2 = 110 - 25 * R (or 104 - 17 * R)
        float calculatedSpo2 = 110.0f - (25.0f * rRatio);

        // Constrain to realistic human physiological limits [85%, 100%]
        int spo2Int = (int)round(constrain(calculatedSpo2, 85.0f, 100.0f));

        // Smooth output
        _lastCalculatedSpO2 = (int)round(0.7f * _lastCalculatedSpO2 + 0.3f * spo2Int);
      }

      // Reset accumulation for next window
      _sampleCount = 0;
      _acSqSumRed = 0;
      _acSqSumIr = 0;
    }
  }
};

#endif // MAX30105_H

