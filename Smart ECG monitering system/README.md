# Smart ECG Monitoring System with ESP32 & 1.3" OLED

This project implements a real-time electrocardiogram (ECG) monitor using an **ESP32** microcontroller, an **AD8232 ECG sensor module**, and a **1.3-inch I2C OLED display** (driven by the **SH1106** or **SSD1306** controller). 

The system samples the analog voltage from the heart at **100 Hz**, applies digital signal processing (DSP) filters in real-time to remove noise, runs an adaptive thresholding algorithm to detect heartbeats (QRS peaks), calculates the heart rate (BPM), and draws a smooth, rolling ECG graph on the screen.

---

## Key Features
1. **Double-Stage Filtering**:
   - **High-Pass DC Blocker**: Eliminates low-frequency baseline drift caused by breathing or body movement.
   - **Low-Pass EMA Filter**: Smooths out high-frequency electrical interference (50/60 Hz mains humming and muscle tremors).
2. **Adaptive QRS Beat Detection**:
   - Automatically tracks peak heights to dynamically adjust the heartbeat threshold, ensuring stable BPM calculation even if electrode contact varies.
   - Uses a physiological refractory period (320ms) to prevent double-counting T-waves or noise.
3. **Optimized OLED Pipeline**:
   - Multi-rate loop execution: samples sensor data at exactly **100 Hz** for filter stability, but updates the screen at **~33 FPS** (30ms interval) to prevent I2C bus congestion.
   - Dynamic auto-scaling (autoscaling gain): scans the current 128-sample window and scales the waveform to fill the screen vertically, guaranteeing a readable plot.
   - Leads-off alarm: displays clear instructions to attach electrodes when pins `LO+` or `LO-` are active.

---

## Hardware Connection Guide

Please wire the components as shown in the table below. Make sure to connect the AD8232 to the **3.3V** supply (connecting it to 5V can damage the sensor or the ESP32's 3.3V-logic ADC).

| Component | Pin Label | ESP32 GPIO | Description |
| :--- | :--- | :--- | :--- |
| **AD8232 ECG** | GND | GND | Common Ground |
| **AD8232 ECG** | 3.3V | 3.3V | Power Supply (3.3V) |
| **AD8232 ECG** | OUTPUT | GPIO 34 (ADC1_CH6) | Analog Signal Input |
| **AD8232 ECG** | LO- | GPIO 32 | Lead-Off Detect Negative |
| **AD8232 ECG** | LO+ | GPIO 33 | Lead-Off Detect Positive |
| **1.3" OLED** | GND | GND | Common Ground |
| **1.3" OLED** | VCC | 3.3V or 5V (Check display) | Power Supply |
| **1.3" OLED** | SDA | GPIO 21 | I2C Data line |
| **1.3" OLED** | SCL | GPIO 22 | I2C Clock line |
| **Buzzer Alarm** | Positive (+) | GPIO 25 | Active Buzzer Signal Pin (Optional/Disabled) |
| **Buzzer Alarm** | Negative (-) | GND | Common Ground |


### Electrode Placement (Standard 3-Lead Configuration)
Attach the biomedical electrode pads to your skin in the following arrangement (Triangle of Einthoven):
- **Red Pad (RA)**: Right Arm (or near right collarbone).
- **Yellow Pad (LA)**: Left Arm (or near left collarbone).
- **Green Pad (RL)**: Right Leg (or lower left abdomen, serving as the ground reference).

---

## Installation & Setup

1. **Software Requirements**:
   - Install the **Arduino IDE** (or VS Code with PlatformIO).
   - In Arduino IDE, navigate to **Tools > Board** and select your **ESP32 Dev Module** (or appropriate board).
2. **Library Installation**:
   - Go to **Sketch > Include Library > Manage Libraries**.
   - Search for **U8g2** (by oliver) and install the latest version.
3. **Upload the Code**:
   - Open [Smart_ECG_Monitor.ino](file:///e:/files/Smart%20ECG%20monitering%20system/Smart_ECG_Monitor/Smart_ECG_Monitor.ino).
   - Under `DISPLAY CONFIGURATION`, check if your OLED uses the **SH1106** controller (standard for 1.3-inch displays) or **SSD1306** (standard for 0.96-inch displays). Comment/uncomment the appropriate constructor accordingly.
   - Connect the ESP32 to your computer using a USB cable, select the correct Port, and click **Upload**.

---

## Calibration and Troubleshooting

### 1. The screen is blank or shifted:
- **Shifted drawing**: If your screen has a 2-pixel vertical or horizontal offset, it means you are using the wrong driver. Toggle the commented constructors for `U8G2_SH1106_128X64_NONAME_F_HW_I2C` and `U8G2_SSD1306_128X64_NONAME_F_HW_I2C` near the top of the file.
- **No display**: Check the I2C wires. Ensure SDA is on GPIO 21 and SCL on GPIO 22. If your display module operates at I2C address `0x3D` instead of `0x3C`, call `u8g2.setI2CAddress(0x3D * 2)` before calling `u8g2.begin()` in `setup()`.

### 2. The waveform has too much noise or is flatlining:
- Ensure that the electrode pads are making solid, wet contact with your skin. Shave body hair or clean skin with alcohol if necessary.
- **Baseline Wander**: If the wave wanders up/down when moving, increase the filtering strength of the High-Pass Filter by decreasing `alpha` in `hpFilter()` (e.g. from `0.98` to `0.95`).
- **50/60 Hz Electrical Noise**: Keep the sensor wires away from laptop chargers, extension cords, or other AC power lines. You can increase low-pass smoothing by reducing `beta` in `lpFilter()` (e.g. from `0.18` to `0.10`).

### 3. Debugging on Serial Plotter:
The code prints both the raw analog signal and the filtered ECG signal to the Serial connection. 
- Open **Tools > Serial Plotter** in Arduino IDE.
- Set the baud rate to **115200**.
- You will see the filtered, centered ECG signal overlaid with the raw signal. This is very useful for checking if the signal levels are correct before mounting the screen.

---

## Real-Time Web Visualization Suite

The project now includes **Smart ECG Monitoring**, a premium, high-performance web visualization suite that connects directly to the ESP32 serial interface.

### 1. Launching the Web Visualizer
- Navigate to the [ECG_Visualizer](file:///e:/files/Smart%20ECG%20monitering%20system/ECG_Visualizer) directory.
- Open [index.html](file:///e:/files/Smart%20ECG%20monitering%20system/ECG_Visualizer/index.html) in **Google Chrome**, **Microsoft Edge**, or **Opera** (Firefox/Safari do not support Web Serial).
- To verify the interface immediately without any hardware connected, click the **Run Demo Mode** button to load a simulated, high-fidelity physiological ECG wave generator.

### 2. Configurable Serial Format (ESP32 Integration)
To support different external serial analyzers or visualizers, the Arduino code in [Smart_ECG_Monitor.ino](file:///e:/files/Smart%20ECG%20monitering%20system/Smart_ECG_Monitor/Smart_ECG_Monitor.ino) contains:
- **Mode 1**: Standard Arduino IDE Serial Plotter format (`"Raw:<raw>,Filtered:<filtered>"`).
- **Mode 2** *(Default/Recommended)*: Space-Separated Values (`"<raw> <filtered> <bpm> <leadsOff>"`). This is the optimized format parsed by the Smart ECG Web Visualizer and other advanced serial plotting tools.
- **Mode 3**: Space-Prefixed Filtered Value (`" <filtered>"`). Useful if your serial analyzer expects a single numerical value after a space.
- **Mode 4**: Filtered Value Only (`"<filtered>"`). Sends only the numerical value without prefixes.

### 3. Connecting the Web App to the ESP32
- Make sure to select `Mode 2` for `SERIAL_FORMAT` in your ESP32 code and upload it.
- Ensure the Arduino IDE Serial Monitor/Plotter is **closed** to release the COM port.
- In the Smart ECG Web Visualizer connection dashboard, select `115200` baud.
- Click **Connect Device**, choose your ESP32 COM port from the browser prompt, and click **Connect**.

---

## Smart ECG Monitor Firmware

The project includes the production sensor monitor firmware located under [Smart_ECG_Monitor.ino](file:///e:/files/Smart%20ECG%20monitering%20system/Smart_ECG_Monitor/Smart_ECG_Monitor.ino).

### Flashing and Testing:
1. Connect the AD8232 sensor, OLED, and RGB status LEDs to the ESP32 according to the wiring guide.
2. Open [Smart_ECG_Monitor.ino](file:///e:/files/Smart%20ECG%20monitering%20system/Smart_ECG_Monitor/Smart_ECG_Monitor.ino) in the Arduino IDE.
3. Choose the ESP32 Dev Module board and compile/upload the code.
4. Open the Web Serial Visualizer (`index.html`) or pair via Bluetooth BLE (device name `Smart_ECG_Monitor`) to stream, monitor, and analyze the physiological ECG waveforms in real time!


