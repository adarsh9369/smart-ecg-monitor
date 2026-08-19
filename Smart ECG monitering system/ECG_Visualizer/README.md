# Smart ECG Visualizer Dashboard

This is a premium, real-time, browser-based monitoring application for visualizing ECG data from the ESP32 via the Web Serial API.

## Features
- **Zero Installation**: Built using vanilla HTML, CSS, and JS. Open `index.html` in a web browser to run it immediately. No Node.js, `npm install`, or python servers required.
- **Web Serial Interface**: Directly reads telemetry from the ESP32 serial COM port.
- **Double-Buffered Render Engine**: Custom HTML5 Canvas plotting scrolling grid lines and glowing ECG waves with visual gradients at a smooth 60 FPS.
- **Heart Beat & BPM Diagnostics**: Visual pulsating heart icon synced with heart rate averages and physiological status alerts (Normal Sinus, Bradycardia, Tachycardia).
- **Leads Placement Diagnostics**: Torso mapping silhouette displaying real-time electrode attachment status.
- **Demo Mode**: Built-in mathematical ECG wave synthesizer simulating cardiac waveforms (P-QRS-T) to preview features without hardware.

## How to Run

1. **Launch**:
   - Double-click [index.html](file:///d:/files/Smart%20ECG%20monitering%20system/ECG_Visualizer/index.html) to open it in a compatible browser.
   - **Compatible Browsers**: Google Chrome, Microsoft Edge, or Opera (Web Serial API is not supported in Safari or Firefox).

2. **Demo Mode (No Hardware)**:
   - Click **Run Demo Mode** in the connection panel.
   - You will see a simulated physiological ECG signal scrolling on the screen, active BPM beats, statistics accumulating, and diagnostic alerts.
   - Press **Stop Demo** to end the simulation.

3. **Connecting the ESP32**:
   - Connect the ESP32 to your PC using a USB cable.
   - Ensure the Arduino IDE Serial Monitor or Plotter is **closed** (only one application can capture a COM port at a time).
   - Select your Baud Rate (default: **115200**).
   - Click **Connect Device**.
   - A browser pop-up will ask you to select the serial COM port matching your ESP32. Select it and click **Connect**.
   - If electrodes are attached, you will see your live ECG waveform scrolling on the grid!

## Control Dashboard Quick Guide

- **Vertical Gain Slider**: Manually adjust the vertical signal amplification (zoom) when Autoscale is disabled.
- **Sweep Speed Slider**: Changes the horizontal scrolling speed of the grid (number of pixels between samples).
- **Signal Stream Select**: Choose what to display:
  - **Filtered Wave**: DSP clean ECG (default).
  - **Raw ADC Stream**: Unfiltered analog voltage readings.
  - **Dual Stream Overlay**: Both raw (amber) and filtered (teal) waves superimposed.
- **Autoscale Gain Checkbox**: Enabled by default. Automatically scales the graph bounds based on the current window's min and max amplitudes.
- **Freeze Plot Button**: Pauses rendering immediately so you can inspect QRS complexes or capture screenshots.
