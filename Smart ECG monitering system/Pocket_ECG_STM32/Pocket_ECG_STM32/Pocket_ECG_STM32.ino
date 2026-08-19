#include "ecg.h"              // ECG Fiter header file
#include "pulse_cal.h"        // Pulse calculation header file
#include "mv_hr.h"            // Heart rate average header file

#define ECG_PIN PA7           // ECG Signal Pin
#define LO_PLUS PA5           // Lead OFF Detection Pin
#define LO_MINUS PA6          // Lead OFF Detection Pin
#define SDN PA4               // AD8232 Shutdown Pin
#define BLE_STATUS_PIN PA1    // BLE Status Pin
#define BLUE_LED PC13         // Blue LED Pin
#define MY_SERIAL Serial2     // BLE Serial Port
const int sampling_rate_delay = 9400;    // Delay for achieved 100Hz sampling rate

MyfilterFilter filter_ecg;    // ECG Signal Filtering Object

void send_data(const String ecg_plot, const String hr_value);  // Send ECG and HR data to BLE Serial Port
void led_blink(void);                                          // Blink Blue LED

void setup() {
  Serial1.begin(38400);                    // Serial Monitor
  MY_SERIAL.begin(38400);                 // BLE Module Serial Port
  pinMode(LO_PLUS, INPUT);                // Lead OFF Detection Pin as Input
  pinMode(LO_MINUS, INPUT);               // Lead OFF Detection Pin as Input
  pinMode(BLE_STATUS_PIN, INPUT_PULLUP);  // BLE Status Pin as Input Pullup
  pinMode(SDN, OUTPUT);                   // AD8232 Shutdown Pin as Output
  pinMode(BLUE_LED, OUTPUT);              // Blue LED Pin as Output

  digitalWrite(SDN, HIGH);                // AD8232 Power ON
}

void loop() {
  if (digitalRead(BLE_STATUS_PIN) == HIGH) {      // Check BLE Connection
    led_blink();                                  // Blink Blue LED
    MY_SERIAL.println("Client Not Connected..");  // BLE Status print
    Serial1.println("Client Not Connected..");     // BLE Status print
  } else {
    digitalWrite(BLUE_LED, HIGH);                             // Blue LED OFF
    int raw_ecg = analogRead(ECG_PIN);                        // Read ECG Signal
    MyfilterFilter_put(&filter_ecg, raw_ecg);                 // ECG Signal Filtering
    float filter_ecg_wave = MyfilterFilter_get(&filter_ecg);  // Get Filtered ECG Signal
    hr_calculation(filter_ecg_wave);                          // Heart Rate Calculation
    int average_hr = hr_avg(heart_rate);                      // Heart Rate Average value
    send_data(String(filter_ecg_wave), String(average_hr));   // Send ECG and HR data to BLE Serial Port
    delayMicroseconds(sampling_rate_delay);                       // Delay for 100Hz Sampling Rate
  }
}

void send_data(String ecg_plot, String hr_value) {
  MY_SERIAL.print(ecg_plot);  // Send ECG Data
  MY_SERIAL.print(",");       // Send Comma
  MY_SERIAL.print(hr_value);  // Send HR Data
  MY_SERIAL.print('\n');      // Send New Line

  Serial1.print(ecg_plot);     // Print ECG Data
  Serial1.print(" ");          // Print Space
  Serial1.print(hr_value);     // Print HR Data
  Serial1.print('\n');           // Print New Line
}
void led_blink() {
  digitalWrite(BLUE_LED, HIGH);  // Blue LED OFF
  delay(950);                    // Delay 950ms
  digitalWrite(BLUE_LED, LOW);   // Blue LED ON
  delay(150);                    // Delay 150ms
}