/* ==========================================================================
   SMART ECG - CORE LOGIC, WEB SERIAL & RENDER ENGINE
   ========================================================================== */

document.addEventListener('DOMContentLoaded', () => {
  // --- UI Elements ---
  const canvas = document.getElementById('ecg-canvas');
  const ctx = canvas.getContext('2d');
  const freezeOverlay = document.getElementById('freeze-overlay');
  
  const connectionBadge = document.getElementById('connection-badge');
  const leadsBadge = document.getElementById('leads-badge');
  
  const bpmValue = document.getElementById('bpm-value');
  const bpmStatus = document.getElementById('bpm-status');
  const heartIcon = document.getElementById('heart-icon');
  
  const nodeRA = document.getElementById('node-ra');
  const nodeLA = document.getElementById('node-la');
  const nodeRL = document.getElementById('node-rl');
  const leadsInstructions = document.getElementById('leads-instructions');
  
  const statTime = document.getElementById('stat-time');
  const statAvgBpm = document.getElementById('stat-avg-bpm');
  const statMax = document.getElementById('stat-max');
  const statMin = document.getElementById('stat-min');
  
  const baudSelect = document.getElementById('baud-select');
  const btnConnect = document.getElementById('btn-connect');
  const btnDemo = document.getElementById('btn-demo');
  
  const btnBleConnect = document.getElementById('btn-ble-connect');
  const btnBleDemo = document.getElementById('btn-ble-demo');
  const tabUsb = document.getElementById('tab-usb');
  const tabBle = document.getElementById('tab-ble');
  const panelUsb = document.getElementById('panel-usb');
  const panelBle = document.getElementById('panel-ble');
  
  const gainSlider = document.getElementById('gain-slider');
  const gainLabel = document.getElementById('gain-label');
  const speedSlider = document.getElementById('speed-slider');
  const speedLabel = document.getElementById('speed-label');
  
  const simBpmSlider = document.getElementById('sim-bpm-slider');
  const simBpmLabel = document.getElementById('sim-bpm-label');
  const simAmpSlider = document.getElementById('sim-amp-slider');
  const simAmpLabel = document.getElementById('sim-amp-label');
  const simulatorSettingsGroup = document.getElementById('simulator-settings-group');
  
  const modeSelect = document.getElementById('mode-select');
  const chkGrid = document.getElementById('chk-grid');
  const chkAutoscale = document.getElementById('chk-autoscale');
  
  const btnPlay = document.getElementById('btn-play');
  const btnReset = document.getElementById('btn-reset');
  const btnClearLogs = document.getElementById('btn-clear-logs');
  const logsConsole = document.getElementById('logs-console');

  // Recording & Clinical Report Elements
  const btnRecord = document.getElementById('btn-record');
  const recordText = document.getElementById('record-text');
  const reportModal = document.getElementById('report-modal');
  const btnCloseModal = document.getElementById('btn-close-modal');
  const btnCloseModalBottom = document.getElementById('btn-close-modal-bottom');
  const btnPrintReport = document.getElementById('btn-print-report');
  const btnDownloadCsvAgain = document.getElementById('btn-download-csv-again');
  
  const reportPatientName = document.getElementById('report-patient-name');
  const reportPatientAge = document.getElementById('report-patient-age');
  const reportDatetime = document.getElementById('report-datetime');
  const reportDuration = document.getElementById('report-duration');
  const reportAvgBpm = document.getElementById('report-avg-bpm');
  const reportRangeBpm = document.getElementById('report-range-bpm');
  const reportTotalBeats = document.getElementById('report-total-beats');
  const reportNormalSec = document.getElementById('report-normal-sec');
  const reportNormalPct = document.getElementById('report-normal-pct');
  const reportTachySec = document.getElementById('report-tachy-sec');
  const reportTachyPct = document.getElementById('report-tachy-pct');
  const reportBradySec = document.getElementById('report-brady-sec');
  const reportBradyPct = document.getElementById('report-brady-pct');
  const reportEctopicCount = document.getElementById('report-ectopic-count');
  const reportAssessmentText = document.getElementById('report-assessment-text');
  const reportGraphImg = document.getElementById('report-graph-img');

  // --- State Variables ---
  let serialPort = null;
  let serialReader = null;
  let isKeepReading = false;
  
  // Bluetooth BLE State
  let bleDevice = null;
  let bleCharacteristic = null;
  const BLE_SERVICE_UUID = '19b10000-e8f2-537e-4f6c-d104768a1214';
  const BLE_CHAR_UUID    = '19b10001-e8f2-537e-4f6c-d104768a1214';
  
  let isDemoMode = false;
  let demoIntervalId = null;
  let demoTime = 0;
  
  let isPaused = false;
  let sessionStartTime = null;
  let sessionTimerId = null;
  
  // Data Buffers
  const MAX_BUFF_LEN = 1500;
  let rawDataBuffer = [];
  let filteredDataBuffer = [];
  let displayPoints = []; // Rendered screen coordinate coordinates
  
  // Waveform display options
  let gain = parseFloat(gainSlider.value);
  let sweepSpeed = parseFloat(speedSlider.value);
  let showMedicalGrid = chkGrid.checked;
  let isAutoscaleEnabled = chkAutoscale.checked;
  let streamMode = modeSelect.value; // 'filtered', 'raw', 'both'
  
  // Simulator State Variables
  let simulatorBpm = parseInt(simBpmSlider.value);
  let simulatorAmplitude = parseFloat(simAmpSlider.value);
  let lastSentLeadsOff = null;
  
  // Stats & Analysis
  let minObservedVal = 99999;
  let maxObservedVal = -99999;
  let currentBpm = 0;
  let bpmHistory = [];
  let totalBeatsCount = 0;
  let lastHeartbeatTime = 0;
  
  // Lead connection states
  let isLeadsOff = false;

  // Recording State Variables
  let isRecording = false;
  let recordingStartTime = null;
  let recordedSamples = [];
  let recordingTimerId = null;

  // --- Initialization ---
  function init() {
    setupCanvas();
    window.addEventListener('resize', setupCanvas);
    setupEventListeners();
    resetStats();
    
    // Start main drawing loop
    requestAnimationFrame(renderLoop);
  }

  // --- Canvas Configuration (Retina-friendly resizing) ---
  function setupCanvas() {
    const rect = canvas.parentNode.getBoundingClientRect();
    // Device pixel ratio scaling for crisp drawings
    const dpr = window.devicePixelRatio || 1;
    canvas.width = rect.width * dpr;
    canvas.height = rect.height * dpr;
    ctx.scale(dpr, dpr);
    canvas.style.width = `${rect.width}px`;
    canvas.style.height = `${rect.height}px`;
  }

  // --- Register Controls Events ---
  function setupEventListeners() {
    // Sliders
    gainSlider.addEventListener('input', (e) => {
      gain = parseFloat(e.target.value);
      gainLabel.textContent = `${gain.toFixed(1)}x`;
    });
    
    speedSlider.addEventListener('input', (e) => {
      sweepSpeed = parseFloat(e.target.value);
      speedLabel.textContent = `${sweepSpeed.toFixed(1)}px`;
    });
    
    simBpmSlider.addEventListener('input', (e) => {
      simulatorBpm = parseInt(e.target.value);
      simBpmLabel.textContent = simulatorBpm;
      sendSerialCommand(`BPM:${simulatorBpm}`);
    });
    
    simAmpSlider.addEventListener('input', (e) => {
      simulatorAmplitude = parseFloat(e.target.value);
      simAmpLabel.textContent = `${simulatorAmplitude.toFixed(1)}V`;
      sendSerialCommand(`AMP:${simulatorAmplitude.toFixed(1)}`);
    });
    
    // Checkboxes & Selects
    chkGrid.addEventListener('change', (e) => {
      showMedicalGrid = e.target.checked;
    });
    
    chkAutoscale.addEventListener('change', (e) => {
      isAutoscaleEnabled = e.target.checked;
      gainSlider.disabled = isAutoscaleEnabled;
      logConsole(`Auto-gain scaling ${isAutoscaleEnabled ? 'Enabled' : 'Disabled'}.`, 'info');
    });
    
    modeSelect.addEventListener('change', (e) => {
      streamMode = e.target.value;
      logConsole(`Visualisation stream switched to: ${streamMode.toUpperCase()}`, 'info');
    });
    
    // Connection Buttons
    btnConnect.addEventListener('click', toggleSerialConnection);
    btnDemo.addEventListener('click', toggleDemoMode);

    // Recording & Report Listeners
    btnRecord.addEventListener('click', toggleRecording);
    btnCloseModal.addEventListener('click', () => reportModal.classList.remove('active'));
    btnCloseModalBottom.addEventListener('click', () => reportModal.classList.remove('active'));
    btnDownloadCsvAgain.addEventListener('click', downloadSessionCSV);
    btnPrintReport.addEventListener('click', () => {
      const pName = document.getElementById('report-patient-name').value || 'Patient';
      const d = new Date();
      const dateStr = d.getFullYear() + '-' + String(d.getMonth()+1).padStart(2,'0') + '-' + String(d.getDate()).padStart(2,'0') + '_' + String(d.getHours()).padStart(2,'0') + '-' + String(d.getMinutes()).padStart(2,'0');
      const safeName = pName.replace(/[^a-z0-9]/gi, '_'); // sanitize name
      
      const originalTitle = document.title;
      document.title = `ECG_Report_${safeName}_${dateStr}`;
      window.print();
      document.title = originalTitle;
    });
    
    // BLE Connection Buttons
    btnBleConnect.addEventListener('click', toggleBleConnection);
    btnBleDemo.addEventListener('click', toggleDemoMode);
    
    // Tab switching listeners
    tabUsb.addEventListener('click', () => switchConnectionTab('usb'));
    tabBle.addEventListener('click', () => switchConnectionTab('ble'));
    
    // Action Buttons
    btnPlay.addEventListener('click', togglePlayPause);
    btnReset.addEventListener('click', resetStats);
    btnClearLogs.addEventListener('click', () => {
      logsConsole.innerHTML = '';
      logConsole('Console cleared.', 'normal');
    });
  }

  // --- Session Timer ---
  function startSessionTimer() {
    if (sessionTimerId) clearInterval(sessionTimerId);
    sessionStartTime = Date.now();
    sessionTimerId = setInterval(() => {
      if (isPaused) return;
      const diffMs = Date.now() - sessionStartTime;
      const min = Math.floor(diffMs / 60000).toString().padStart(2, '0');
      const sec = Math.floor((diffMs % 60000) / 1000).toString().padStart(2, '0');
      statTime.textContent = `${min}:${sec}`;
    }, 1000);
  }

  function stopSessionTimer() {
    if (sessionTimerId) {
      clearInterval(sessionTimerId);
      sessionTimerId = null;
    }
  }

  // --- Reset Statistics ---
  function resetStats() {
    minObservedVal = 99999;
    maxObservedVal = -99999;
    bpmHistory = [];
    totalBeatsCount = 0;
    currentBpm = 0;
    
    statMin.textContent = '--';
    statMax.textContent = '--';
    statAvgBpm.textContent = '--';
    bpmValue.textContent = '--';
    bpmValue.style.color = 'inherit';
    bpmStatus.textContent = 'NO SIGNAL';
    bpmStatus.className = 'bpm-status-text';
    
    if (!isDemoMode && !serialPort && !bleDevice) {
      statTime.textContent = '00:00';
    } else {
      startSessionTimer();
    }
    logConsole('Session statistics and logs reset.', 'normal');
  }

  // --- Logging Console Helper ---
  function logConsole(message, type = 'normal') {
    const timestamp = new Date().toLocaleTimeString([], { hour12: false });
    const line = document.createElement('div');
    line.className = `log-line text-${type}`;
    line.textContent = `[${timestamp}] ${message}`;
    logsConsole.appendChild(line);
    
    // Auto scroll to bottom
    logsConsole.scrollTop = logsConsole.scrollHeight;
  }

  // --- Web Serial API Connection Handler ---
  async function toggleSerialConnection() {
    if (isDemoMode) {
      toggleDemoMode(); // Turn off demo mode first
    }
    
    if (serialPort) {
      // Disconnect
      logConsole('Disconnecting from serial port...', 'info');
      isKeepReading = false;
      
      if (serialReader) {
        try {
          await serialReader.cancel();
        } catch (e) {
          console.error(e);
        }
      }
      
      btnConnect.disabled = true;
      // Wait briefly for reading loop to terminate
      setTimeout(async () => {
        try {
          if (serialPort) {
            await serialPort.close();
          }
        } catch (err) {
          console.error('Error closing port:', err);
        }
        serialPort = null;
        serialReader = null;
        
        btnConnect.disabled = false;
        btnConnect.textContent = 'Connect Device';
        btnConnect.className = 'btn btn-primary';
        
        connectionBadge.className = 'badge badge-disconnected';
        connectionBadge.querySelector('.text').textContent = 'DISCONNECTED';
        
        updateLeadsUI(false, true); // Reset leads to unknown
        stopSessionTimer();
        logConsole('Serial port closed successfully.', 'success');
      }, 500);
      
    } else {
      // Connect
      if (!('serial' in navigator)) {
        logConsole('Web Serial API is not supported in this browser. Please use Google Chrome or Microsoft Edge.', 'error');
        alert('Web Serial is not supported. Use Chrome, Edge or Opera, or run in Demo Mode!');
        return;
      }
      
      logConsole('Requesting serial port authorization...', 'info');
      try {
        serialPort = await navigator.serial.requestPort();
        const baudRate = parseInt(baudSelect.value);
        logConsole(`Opening serial port at ${baudRate} Baud...`, 'info');
        
        await serialPort.open({ baudRate });
        isKeepReading = true;
        
        btnConnect.textContent = 'Disconnect';
        btnConnect.className = 'btn btn-danger';
        
        connectionBadge.className = 'badge badge-connected';
        connectionBadge.querySelector('.text').textContent = 'CONNECTED';
        
        resetStats();
        startSessionTimer();
        logConsole('Serial port opened successfully. Reading stream data...', 'success');
        
        // Start reading loop
        readSerialStream();
        
      } catch (err) {
        logConsole(`Connection failed: ${err.message}`, 'error');
        serialPort = null;
      }
    }
  }

  // --- Connection Tab Switcher ---
  function switchConnectionTab(type) {
    if (type === 'usb') {
      tabUsb.classList.add('active');
      tabBle.classList.remove('active');
      panelUsb.classList.add('active');
      panelBle.classList.remove('active');
      
      // If BLE is active, disconnect it
      if (bleDevice) {
        toggleBleConnection();
      }
    } else {
      tabBle.classList.add('active');
      tabUsb.classList.remove('active');
      panelBle.classList.add('active');
      panelUsb.classList.remove('active');
      
      // If USB is active, disconnect it
      if (serialPort) {
        toggleSerialConnection();
      }
    }
  }

  // --- Web Bluetooth BLE Connection Handler ---
  async function toggleBleConnection() {
    if (isDemoMode) {
      toggleDemoMode(); // Stop simulation first
    }
    
    if (bleDevice && bleDevice.gatt.connected) {
      // Disconnect BLE
      logConsole('Disconnecting from Bluetooth BLE Device...', 'info');
      try {
        if (bleCharacteristic) {
          await bleCharacteristic.stopNotifications();
        }
      } catch (err) {
        console.error('Error stopping notifications:', err);
      }
      
      if (bleDevice) {
        bleDevice.gatt.disconnect();
      }
      
      onBleDisconnected();
    } else {
      // Connect BLE
      if (!('bluetooth' in navigator)) {
        logConsole('Web Bluetooth API is not supported in this browser. Please use Chrome on Android, or a BLE browser like Bluefy on iOS.', 'error');
        alert('Web Bluetooth is not supported. Use Google Chrome, Microsoft Edge, or Bluefy (iOS) to connect wirelessly.');
        return;
      }
      
      logConsole('Requesting Bluetooth device pairing (Search name: Smart_ECG_Monitor)...', 'info');
      try {
        bleDevice = await navigator.bluetooth.requestDevice({
          filters: [{ name: 'Smart_ECG_Monitor' }],
          optionalServices: [BLE_SERVICE_UUID]
        });
        
        // Listen for sudden disconnections
        bleDevice.addEventListener('gattserverdisconnected', onBleDisconnected);
        
        logConsole('Connecting to GATT Server...', 'info');
        const server = await bleDevice.gatt.connect();
        
        logConsole('Retrieving Primary ECG Service...', 'info');
        const service = await server.getPrimaryService(BLE_SERVICE_UUID);
        
        logConsole('Retrieving ECG Characteristic...', 'info');
        bleCharacteristic = await service.getCharacteristic(BLE_CHAR_UUID);
        
        logConsole('Enabling BLE Notifications...', 'info');
        await bleCharacteristic.startNotifications();
        bleCharacteristic.addEventListener('characteristicvaluechanged', handleBleNotification);
        
        // Update UI states
        btnBleConnect.textContent = 'Disconnect';
        btnBleConnect.className = 'btn btn-danger';
        
        connectionBadge.className = 'badge badge-ble';
        connectionBadge.querySelector('.text').textContent = 'BLE CONNECTED';
        
        resetStats();
        startSessionTimer();
        logConsole('Bluetooth BLE connection established. Streaming live ECG...', 'success');
        
      } catch (err) {
        logConsole(`Bluetooth BLE connection failed: ${err.message}`, 'error');
        bleDevice = null;
        bleCharacteristic = null;
      }
    }
  }

  // Handle BLE Notification Data
  function handleBleNotification(event) {
    const value = event.target.value;
    const textDecoder = new TextDecoder('utf-8');
    const line = textDecoder.decode(value);
    parseIncomingLine(line);
  }

  // Handle BLE Disconnection cleanup
  function onBleDisconnected() {
    bleDevice = null;
    bleCharacteristic = null;
    
    btnBleConnect.textContent = 'Connect BLE';
    btnBleConnect.className = 'btn btn-primary';
    
    connectionBadge.className = 'badge badge-disconnected';
    connectionBadge.querySelector('.text').textContent = 'DISCONNECTED';
    
    updateLeadsUI(false, true); // Reset leads to unknown
    stopSessionTimer();
    logConsole('Bluetooth BLE connection closed.', 'warn');
  }

  // --- Serial Reading Loop ---
  async function readSerialStream() {
    const textDecoder = new TextDecoderStream();
    const readableStreamClosed = serialPort.readable.pipeTo(textDecoder.writable);
    serialReader = textDecoder.readable.getReader();
    
    let buffer = "";
    
    try {
      while (isKeepReading) {
        const { value, done } = await serialReader.read();
        if (done) {
          logConsole('Serial stream closed by hardware.', 'warn');
          break;
        }
        
        buffer += value;
        let lines = buffer.split('\n');
        buffer = lines.pop(); // Keep the last incomplete slice
        
        for (const line of lines) {
          if (line.trim()) {
            parseIncomingLine(line);
          }
        }
      }
    } catch (err) {
      logConsole(`Serial read error: ${err.message}`, 'error');
    } finally {
      serialReader.releaseLock();
      await readableStreamClosed.catch(() => {});
    }
  }

  // --- Send serial commands down to the hardware simulator ---
  async function sendSerialCommand(commandText) {
    if (serialPort && serialPort.writable) {
      try {
        const encoder = new TextEncoder();
        const writer = serialPort.writable.getWriter();
        await writer.write(encoder.encode(commandText + '\n'));
        writer.releaseLock();
        logConsole(`[COMMAND SENT] ${commandText}`, 'info');
      } catch (err) {
        logConsole(`Command send error: ${err.message}`, 'error');
      }
    }
  }

  // --- Robust Data Parser ---
  // Decodes multiple formats, prioritizing space-separated values
  function parseIncomingLine(line) {
    line = line.trim();
    if (!line) return;
    
    let raw = 0;
    let filtered = 0;
    let bpm = 0;
    let leadsOff = false;
    let parsedSuccess = false;
    
    // Check Option A: Space separated values (Mode 2)
    // Format: raw_val filtered_val bpm leads_off
    const spaceTokens = line.split(/\s+/);
    if (spaceTokens.length >= 2 && !isNaN(parseFloat(spaceTokens[0])) && !isNaN(parseFloat(spaceTokens[1]))) {
      raw = parseFloat(spaceTokens[0]);
      filtered = parseFloat(spaceTokens[1]);
      bpm = spaceTokens[2] ? parseFloat(spaceTokens[2]) : 0;
      leadsOff = spaceTokens[3] ? parseInt(spaceTokens[3]) === 1 : false;
      parsedSuccess = true;
    } 
    // Check Option B: Comma separated values (Alternative plotting format)
    else if (line.includes(',')) {
      const commaTokens = line.split(',');
      // Try to parse labeled fields e.g., Raw:123, Filtered:456
      let rawMatch = line.match(/Raw:([\d.-]+)/);
      let filtMatch = line.match(/Filtered:([\d.-]+)/);
      let discMatch = line.match(/Leads_Disconnected:(\d)/);
      
      if (rawMatch && filtMatch) {
        raw = parseFloat(rawMatch[1]);
        filtered = parseFloat(filtMatch[1]) / 10.0; // Scaled down in .ino
        leadsOff = discMatch ? parseInt(discMatch[1]) === 1 : false;
        parsedSuccess = true;
      } 
      // Raw numeric CSV e.g., 123,456
      else if (commaTokens.length >= 2 && !isNaN(parseFloat(commaTokens[0])) && !isNaN(parseFloat(commaTokens[1]))) {
        raw = parseFloat(commaTokens[0]);
        filtered = parseFloat(commaTokens[1]);
        parsedSuccess = true;
      }
    }
    // Check Option C: Leads_Disconnected message
    else if (line.includes("Leads_Disconnected")) {
      isLeadsOff = true;
      updateLeadsUI(true, false);
      return;
    }
    // Check Option D: Pure single value (Mode 3 & Mode 4)
    else if (!isNaN(parseFloat(line))) {
      filtered = parseFloat(line);
      raw = filtered; // Duplicate as raw
      parsedSuccess = true;
    }
    
    if (parsedSuccess) {
      processEcgSample(raw, filtered, bpm, leadsOff);
    }
  }

  // --- Process Samples & Trigger UI Alerts ---
  function processEcgSample(raw, filtered, bpm, leadsOff) {
    isLeadsOff = leadsOff;
    updateLeadsUI(leadsOff, false);
    
    if (leadsOff) {
      // Force filtered signal to flatline for visual cleanliness, but preserve raw ADC for troubleshooting
      filtered = 0;
      currentBpm = 0;
    } else {
      // Filtered samples tracking
      if (filtered < minObservedVal) {
        minObservedVal = filtered;
        statMin.textContent = minObservedVal.toFixed(1);
      }
      if (filtered > maxObservedVal) {
        maxObservedVal = filtered;
        statMax.textContent = maxObservedVal.toFixed(1);
      }
      
      // Heartbeat pulse triggers
      if (bpm > 0) {
        // If BPM has updated and differs significantly or periodically
        if (Math.abs(bpm - currentBpm) > 0.05 || (Date.now() - lastHeartbeatTime > 3000)) {
          handleHeartbeat(bpm);
        }
      }
    }
    
    // If recording is active, push metrics to recording buffer
    if (isRecording) {
      const elapsed = (Date.now() - recordingStartTime) / 1000;
      recordedSamples.push({
        time: parseFloat(elapsed.toFixed(3)),
        raw: raw,
        filtered: parseFloat(filtered.toFixed(2)),
        bpm: Math.round(bpm > 0 ? bpm : currentBpm)
      });
    }

    // Add data to buffers
    rawDataBuffer.push(raw);
    filteredDataBuffer.push(filtered);
    
    if (rawDataBuffer.length > MAX_BUFF_LEN) rawDataBuffer.shift();
    if (filteredDataBuffer.length > MAX_BUFF_LEN) filteredDataBuffer.shift();
  }

  // --- Heartbeat Event triggers ---
  function handleHeartbeat(bpmVal) {
    currentBpm = bpmVal;
    lastHeartbeatTime = Date.now();
    totalBeatsCount++;
    
    // Flash beating heart UI
    heartIcon.classList.add('beating');
    setTimeout(() => {
      heartIcon.classList.remove('beating');
    }, 150);
    
    // Update BPM Display
    bpmValue.textContent = Math.round(bpmVal);
    
    // Classification of Heart Rate (BPM Status)
    if (bpmVal < 60) {
      bpmStatus.textContent = 'BRADYCARDIA';
      bpmStatus.className = 'bpm-status-text bpm-status-bradycardia';
      bpmValue.style.color = 'var(--color-gold)';
    } else if (bpmVal > 100) {
      bpmStatus.textContent = 'TACHYCARDIA';
      bpmStatus.className = 'bpm-status-text bpm-status-magenta';
      bpmValue.style.color = 'var(--color-magenta)';
    } else {
      bpmStatus.textContent = 'NORMAL SINUS';
      bpmStatus.className = 'bpm-status-text bpm-status-normal';
      bpmValue.style.color = 'var(--color-teal)';
    }
    
    // Smooth moving average for sessions
    bpmHistory.push(bpmVal);
    if (bpmHistory.length > 30) bpmHistory.shift();
    const sum = bpmHistory.reduce((a, b) => a + b, 0);
    const avg = sum / bpmHistory.length;
    statAvgBpm.textContent = Math.round(avg).toString();
    
    logConsole(`Beat detected: ${Math.round(bpmVal)} BPM (${bpmStatus.textContent})`, 'success');
  }

  // --- Leads Diagnostics UI Controller ---
  function updateLeadsUI(leadsOff, isUnknown = false) {
    if (isUnknown) {
      leadsBadge.className = 'badge badge-leads-unknown';
      leadsBadge.querySelector('.text').textContent = 'LEADS UNKNOWN';
      nodeRA.className = 'electrode-node code-red unknown';
      nodeLA.className = 'electrode-node code-yellow unknown';
      nodeRL.className = 'electrode-node code-green unknown';
      leadsInstructions.textContent = 'Waiting for connection...';
      leadsInstructions.className = 'status-alert';
      return;
    }
    
    if (leadsOff) {
      leadsBadge.className = 'badge badge-leads-fail';
      leadsBadge.querySelector('.text').textContent = 'LEADS DETACHED';
      
      // Animate diagram nodes as disconnected alerts
      nodeRA.className = 'electrode-node code-red disconnected';
      nodeLA.className = 'electrode-node code-yellow disconnected';
      nodeRL.className = 'electrode-node code-green disconnected';
      
      leadsInstructions.innerHTML = '<span style="font-weight:700;">Electrodes Detached!</span><br>Verify skin contact:<br>• Red Pad (RA) &rarr; Right Arm<br>• Yellow Pad (LA) &rarr; Left Arm<br>• Green Pad (RL) &rarr; Right Leg';
      leadsInstructions.className = 'status-alert fail-alert';
    } else {
      leadsBadge.className = 'badge badge-leads-ok';
      leadsBadge.querySelector('.text').textContent = 'LEADS CONNECTED';
      
      // Set nodes to solid active states
      nodeRA.className = 'electrode-node code-red connected';
      nodeLA.className = 'electrode-node code-yellow connected';
      nodeRL.className = 'electrode-node code-green connected';
      
      leadsInstructions.textContent = 'All electrode contacts are normal. Signal transmitting.';
      leadsInstructions.className = 'status-alert success-alert';
    }
  }

  // --- Play/Pause (Freeze Plot) ---
  function togglePlayPause() {
    isPaused = !isPaused;
    if (isPaused) {
      btnPlay.innerHTML = `
        <svg viewBox="0 0 24 24" class="btn-icon"><path d="M8 5v14l11-7z" fill="currentColor"/></svg>
        <span class="btn-text">Resume Plot</span>
      `;
      btnPlay.className = 'btn btn-primary';
      freezeOverlay.classList.add('active');
      logConsole('Waveform rendering paused.', 'warn');
    } else {
      btnPlay.innerHTML = `
        <svg viewBox="0 0 24 24" class="btn-icon"><path d="M12 2C6.48 2 2 6.48 2 12s4.48 10 10 10 10-4.48 10-10S17.52 2 12 2zm-1 14H9V8h2v8zm4 0h-2V8h2v8z" fill="currentColor"/></svg>
        <span class="btn-text">Freeze Plot</span>
      `;
      btnPlay.className = 'btn btn-tertiary';
      freezeOverlay.classList.remove('active');
      logConsole('Waveform rendering resumed.', 'info');
    }
  }

  // --- Simulated ECG Generator (Demo Mode) ---
  function toggleDemoMode() {
    if (isDemoMode) {
      // Stop Demo Mode
      clearInterval(demoIntervalId);
      demoIntervalId = null;
      isDemoMode = false;
      
      btnDemo.textContent = 'Run Demo Mode';
      btnDemo.className = 'btn btn-secondary';
      
      connectionBadge.className = 'badge badge-disconnected';
      connectionBadge.querySelector('.text').textContent = 'DISCONNECTED';
      
      simulatorSettingsGroup.style.display = 'none'; // Hide simulator controls
      
      updateLeadsUI(false, true);
      stopSessionTimer();
      logConsole('Simulated Demo Mode stopped.', 'info');
      
    } else {
      // Start Demo Mode
      if (serialPort) {
        toggleSerialConnection(); // Close active serial port first
      }
      
      isDemoMode = true;
      resetStats();
      startSessionTimer();
      
      btnDemo.textContent = 'Stop Demo';
      btnDemo.className = 'btn btn-danger';
      
      connectionBadge.className = 'badge badge-demo';
      connectionBadge.querySelector('.text').textContent = 'DEMO STREAMING';
      
      simulatorSettingsGroup.style.display = 'flex'; // Show simulator controls
      
      logConsole('Simulated Demo Mode running at 100Hz ECG sample generation.', 'success');
      
      demoTime = 0;
      
      // Simulate 100Hz ECG sampling rate (10ms intervals)
      demoIntervalId = setInterval(() => {
        if (isPaused) return;
        
        generateDemoECGSample();
        demoTime += 0.01; // 10ms step
      }, 10);
    }
  }

  // Mathematical Model of a Physiological ECG Waveform
  function generateDemoECGSample() {
    let t = demoTime;
    let heartRate = simulatorBpm; // Dynamic heart rate from simulator settings slider (Exact value - no HRV)
    
    let period = 60 / heartRate; // Period in seconds
    let phase = (t % period) / period; // Phase from 0 to 1
    
    let val = 0.0;
    
    // 1. Baseline Wander (Low frequency breathing drift)
    const baseline = 0.8 * Math.sin(2 * Math.PI * (t / 4.5));
    val += baseline;
    
    // 2. High frequency mains noise (Simulate 50Hz electrical buzz, small amplitude)
    const buzz = 0.05 * Math.sin(2 * Math.PI * 50 * t);
    val += buzz;
    
    let isBeatCycle = false;
    
    // Synthesize components based on phase (0.0 to 1.0)
    // Standard ECG is P -> QRS -> T
    
    // P Wave (Atrial depolarization: small positive hump)
    if (phase >= 0.12 && phase <= 0.22) {
      const pPhase = (phase - 0.12) / 0.10;
      val += 1.8 * Math.sin(pPhase * Math.PI);
    }
    
    // QRS Complex (Ventricular depolarization: quick, sharp peak)
    // Q: small dip
    if (phase >= 0.27 && phase <= 0.29) {
      const qPhase = (phase - 0.27) / 0.02;
      val -= 2.0 * Math.sin(qPhase * Math.PI);
    }
    // R: large positive spike
    if (phase >= 0.29 && phase <= 0.32) {
      const rPhase = (phase - 0.29) / 0.03;
      val += 22.0 * Math.sin(rPhase * Math.PI);
      isBeatCycle = true;
    }
    // S: deep negative dip
    if (phase >= 0.32 && phase <= 0.35) {
      const sPhase = (phase - 0.32) / 0.03;
      val -= 5.5 * Math.sin(sPhase * Math.PI);
    }
    
    // T Wave (Ventricular repolarization: medium positive hump)
    if (phase >= 0.45 && phase <= 0.60) {
      const tPhase = (phase - 0.45) / 0.15;
      val += 4.5 * Math.sin(tPhase * Math.PI);
    }
    
    // Add tiny amount of stochastic muscle tremor noise
    val += (Math.random() - 0.5) * 0.15;
    
    // Scale signal by configured simulator amplitude factor
    val = val * (simulatorAmplitude / 1.2);
    
    // Scaling to make it fit ESP32 ADC values roughly [0, 4095] with centered signal
    let rawVal = Math.round((val + 10) * 120 + 1000); // Analog voltage simulation
    
    // Simulate DSP Filter logic (EMA Low-Pass + Baseline remover)
    // We already have a clean model, so let's simulate the filter output as:
    let filteredVal = val * 8.0; // Filtered output signal (DC removed, clean)
    
    // Inject periodic leads-off event for simulation demo (e.g. 5 seconds every 35 seconds)
    let simLeadsOff = false;
    const cycleTime = t % 35;
    if (cycleTime > 30 && cycleTime < 35) {
      simLeadsOff = true;
    }
    
    if (simLeadsOff !== lastSentLeadsOff) {
      lastSentLeadsOff = simLeadsOff;
      sendSerialCommand(`LEAD:${simLeadsOff ? 1 : 0}`);
    }
    
    let simulatedBpmOutput = 0;
    if (isBeatCycle && phase >= 0.30 && phase <= 0.31) {
      simulatedBpmOutput = heartRate;
    }
    
    processEcgSample(rawVal, filteredVal, simulatedBpmOutput, simLeadsOff);
  }

  // --- Real-Time Rendering Engine (60 FPS Canvas) ---
  function renderLoop() {
    if (!isPaused) {
      drawEcgChart();
    }
    requestAnimationFrame(renderLoop);
  }

  function drawEcgChart() {
    const width = canvas.width / window.devicePixelRatio;
    const height = canvas.height / window.devicePixelRatio;
    
    // Clear canvas - soft medical grid background
    ctx.fillStyle = '#fff5f5';
    ctx.fillRect(0, 0, width, height);
    
    // 1. Draw Medical Grid Paper
    if (showMedicalGrid) {
      drawGrid(width, height);
    }
    
    // Fetch data buffers
    const rawData = rawDataBuffer;
    const filteredData = filteredDataBuffer;
    
    if (filteredData.length < 2) return;
    
    // Determine which buffer to draw
    let activeBuffer = filteredData;
    if (streamMode === 'raw') {
      activeBuffer = rawData;
    }
    
    // 2. Perform Auto-scaling
    let displayMin = -50;
    let displayMax = 200;
    
    if (isAutoscaleEnabled) {
      // Find range of currently displayed window (based on viewport width / sweepSpeed)
      const samplesInView = Math.ceil(width / sweepSpeed);
      const startIdx = Math.max(0, activeBuffer.length - samplesInView);
      const visibleWindow = activeBuffer.slice(startIdx);
      
      if (visibleWindow.length > 5) {
        let min = 99999;
        let max = -99999;
        for (let i = 0; i < visibleWindow.length; i++) {
          if (visibleWindow[i] < min) min = visibleWindow[i];
          if (visibleWindow[i] > max) max = visibleWindow[i];
        }
        
        let range = max - min;
        if (range < 5.0) range = 5.0; // Avoid division by zero/extreme zoom on flatline
        
        // Add 15% margins
        displayMin = min - range * 0.15;
        displayMax = max + range * 0.15;
      }
    } else {
      // Manual scale using gain factor
      // Centers around the baseline
      const signalCenter = 0; // Filtered center is 0
      const halfSpan = 100 / gain;
      displayMin = signalCenter - halfSpan;
      displayMax = signalCenter + halfSpan;
    }
    
    // 3. Draw Plot Waveforms
    if (streamMode === 'both') {
      // Dual streams overlay: Draw raw in dark amber, filtered in deep teal
      drawWaveform(rawData, '#d97706', 1.2, width, height);
      drawWaveform(filteredData, 'var(--color-teal)', 2.2, width, height, true); // Fill under filtered
    } else if (streamMode === 'raw') {
      drawWaveform(rawData, '#d97706', 2.2, width, height, true);
    } else {
      drawWaveform(filteredData, 'var(--color-teal)', 2.4, width, height, true);
    }
  }

  // --- Draw Grid ---
  // Renders a realistic medical grid paper style (pink/red gridlines)
  function drawGrid(w, h) {
    const gridSpacing = 15; // Width of 1 small box (1mm)
    const largeGridSpacing = gridSpacing * 5; // Width of 1 large box (5mm)
    
    // Fine Lines (1mm) - very subtle pink
    ctx.strokeStyle = 'rgba(239, 68, 68, 0.08)';
    ctx.lineWidth = 0.5;
    ctx.beginPath();
    for (let x = 0; x < w; x += gridSpacing) {
      ctx.moveTo(x, 0);
      ctx.lineTo(x, h);
    }
    for (let y = 0; y < h; y += gridSpacing) {
      ctx.moveTo(0, y);
      ctx.lineTo(w, y);
    }
    ctx.stroke();
    
    // Thick Lines (5mm) - stronger pink
    ctx.strokeStyle = 'rgba(239, 68, 68, 0.22)';
    ctx.lineWidth = 1.0;
    ctx.beginPath();
    for (let x = 0; x < w; x += largeGridSpacing) {
      ctx.moveTo(x, 0);
      ctx.lineTo(x, h);
    }
    for (let y = 0; y < h; y += largeGridSpacing) {
      ctx.moveTo(0, y);
      ctx.lineTo(w, y);
    }
    ctx.stroke();
  }

  // --- Render Waveform Geometry ---
  function drawWaveform(buffer, strokeStyle, lineWidth, width, height, applyGlowAndFill = false) {
    // Autoscale parameters
    let min = -50;
    let max = 200;
    
    if (isAutoscaleEnabled) {
      const samplesInView = Math.ceil(width / sweepSpeed);
      const startIdx = Math.max(0, buffer.length - samplesInView);
      const visibleWindow = buffer.slice(startIdx);
      if (visibleWindow.length > 5) {
        let lMin = 99999;
        let lMax = -99999;
        for (let i = 0; i < visibleWindow.length; i++) {
          if (visibleWindow[i] < lMin) lMin = visibleWindow[i];
          if (visibleWindow[i] > lMax) lMax = visibleWindow[i];
        }
        let r = lMax - lMin;
        if (r < 5) r = 5;
        min = lMin - r * 0.15;
        max = lMax + r * 0.15;
      }
    } else {
      const signalCenter = (strokeStyle === '#d97706') ? 2048 : 0; // Raw centers around 2048, filtered around 0
      const halfSpan = (strokeStyle === '#d97706' ? 1200 : 100) / gain;
      min = signalCenter - halfSpan;
      max = signalCenter + halfSpan;
    }
    
    const range = max - min;
    
    // Determine plot point index mapping
    const samplesInView = Math.ceil(width / sweepSpeed);
    const startIdx = Math.max(0, buffer.length - samplesInView);
    const slice = buffer.slice(startIdx);
    
    ctx.lineWidth = lineWidth;
    ctx.strokeStyle = strokeStyle;
    ctx.lineJoin = 'round';
    ctx.lineCap = 'round';
    
    // Waveform line path
    ctx.beginPath();
    let isFirst = true;
    
    // Draw from left to right (scrolling effect where new samples enter on the right)
    for (let i = 0; i < slice.length; i++) {
      const x = width - (slice.length - 1 - i) * sweepSpeed;
      // Map signal amplitude to screen Y coordinate (invert so positive is up)
      const normVal = (slice[i] - min) / range;
      const y = height - (normVal * height);
      
      if (isFirst) {
        ctx.moveTo(x, y);
        isFirst = false;
      } else {
        ctx.lineTo(x, y);
      }
    }
    
    // Apply Glow effects for high-end aesthetics
    if (applyGlowAndFill) {
      ctx.shadowBlur = 10;
      ctx.shadowColor = strokeStyle;
    }
    ctx.stroke();
    
    // Clear shadow states immediately so they don't affect other canvas ops
    ctx.shadowBlur = 0;
    
    // Draw Gradient fill overlay under the line
    if (applyGlowAndFill && slice.length > 2) {
      ctx.beginPath();
      // Start from bottom left
      const xStart = width - (slice.length - 1) * sweepSpeed;
      ctx.moveTo(xStart, height);
      
      for (let i = 0; i < slice.length; i++) {
        const x = width - (slice.length - 1 - i) * sweepSpeed;
        const normVal = (slice[i] - min) / range;
        const y = height - (normVal * height);
        ctx.lineTo(x, y);
      }
      
      // End at bottom right
      ctx.lineTo(width, height);
      ctx.closePath();
      
      // Horizontal/Vertical gradient
      const fillGradient = ctx.createLinearGradient(0, 0, 0, height);
      fillGradient.addColorStop(0, 'rgba(13, 148, 136, 0.12)');
      fillGradient.addColorStop(1, 'rgba(13, 148, 136, 0.0)');
      
      ctx.fillStyle = fillGradient;
      ctx.fill();
    }
  }

  // --- Recording Control Loop ---
  function toggleRecording() {
    if (!isRecording) {
      // Start Recording
      recordedSamples = [];
      isRecording = true;
      recordingStartTime = Date.now();
      
      btnRecord.className = 'btn btn-danger';
      btnRecord.style.borderColor = 'rgba(219, 39, 119, 0.6)';
      
      let elapsed = 0;
      const targetSecs = document.getElementById('min-record-time') ? parseInt(document.getElementById('min-record-time').value) : 10;
      
      recordingTimerId = setInterval(() => {
        elapsed++;
        recordText.textContent = `Recording... (${elapsed}/${targetSecs}s)`;
        
        // Auto-stop when target duration is reached
        if (elapsed >= targetSecs) {
          toggleRecording();
        }
      }, 1000);
      
      logConsole(`Session auto-recording started for ${targetSecs} seconds.`, 'success');
    } else {
      // Stop Recording
      isRecording = false;
      clearInterval(recordingTimerId);
      recordingTimerId = null;
      
      btnRecord.className = 'btn btn-secondary';
      btnRecord.style.borderColor = 'rgba(255, 0, 127, 0.3)';
      recordText.textContent = 'Record Session';
      
      logConsole(`Session recording stopped. Collected ${recordedSamples.length} samples.`, 'info');
      
      if (recordedSamples.length > 50) {
        // downloadSessionCSV(); // Disabled automatic download per user request
        analyzeSessionData();
        
        // Capture graph snapshot for report
        if (reportGraphImg && canvas) {
          reportGraphImg.src = canvas.toDataURL('image/png');
        }
        
        reportModal.classList.add('active');
      } else {
        logConsole(`Error: No samples collected.`, 'error');
        alert(`Recording failed! No data collected.`);
      }
    }
  }

  // --- CSV File Downloader ---
  function downloadSessionCSV() {
    if (recordedSamples.length === 0) return;
    
    let csvContent = "Time (s),Raw,Filtered,BPM\n";
    for (const s of recordedSamples) {
      csvContent += `${s.time},${s.raw},${s.filtered},${s.bpm}\n`;
    }
    
    const blob = new Blob([csvContent], { type: 'text/csv;charset=utf-8;' });
    const url = URL.createObjectURL(blob);
    const link = document.createElement("a");
    const timestamp = new Date().toISOString().replace(/[:.]/g, '-');
    link.setAttribute("href", url);
    link.setAttribute("download", `ecg_session_${timestamp}.csv`);
    document.body.appendChild(link);
    link.click();
    document.body.removeChild(link);
    logConsole("ECG telemetry CSV log downloaded successfully.", "success");
  }

  // --- Clinical Analytics Calculations ---
  function analyzeSessionData() {
    if (recordedSamples.length === 0) return;
    
    const totalSec = recordedSamples[recordedSamples.length - 1].time;
    
    // Heart rate range calculation
    const validBeats = recordedSamples.filter(s => s.bpm > 0);
    let avgBpm = 0;
    let minBpm = 0;
    let maxBpm = 0;
    
    if (validBeats.length > 0) {
      const bpms = validBeats.map(s => s.bpm);
      const sumBpm = bpms.reduce((a, b) => a + b, 0);
      avgBpm = Math.round(sumBpm / bpms.length);
      minBpm = Math.min(...bpms);
      maxBpm = Math.max(...bpms);
    }
    
    // Rhythm distribution percentages
    let bradyCount = 0;
    let normalCount = 0;
    let tachyCount = 0;
    
    for (const s of recordedSamples) {
      if (s.bpm > 0) {
        if (s.bpm < 60) bradyCount++;
        else if (s.bpm > 100) tachyCount++;
        else normalCount++;
      }
    }
    
    const totalCount = bradyCount + normalCount + tachyCount || 1;
    const bradyPct = Math.round((bradyCount / totalCount) * 100);
    const tachyPct = Math.round((tachyCount / totalCount) * 100);
    const normalPct = Math.round((normalCount / totalCount) * 100);
    
    const bradySec = ((bradyCount / totalCount) * totalSec).toFixed(1);
    const tachySec = ((tachyCount / totalCount) * totalSec).toFixed(1);
    const normalSec = ((normalCount / totalCount) * totalSec).toFixed(1);
    
    // Ectopic Beat Detection using RR interval analysis
    // Detect samples that transitioned to positive BPM (heartbeats)
    const beatTimes = [];
    for (let i = 0; i < recordedSamples.length; i++) {
      if (recordedSamples[i].bpm > 0 && (i === 0 || recordedSamples[i-1].bpm === 0)) {
        beatTimes.push(recordedSamples[i].time);
      }
    }
    
    const rrIntervals = [];
    for (let j = 1; j < beatTimes.length; j++) {
      rrIntervals.push(beatTimes[j] - beatTimes[j-1]);
    }
    
    let ectopicCount = 0;
    if (rrIntervals.length >= 3) {
      const avgRR = rrIntervals.reduce((a, b) => a + b, 0) / rrIntervals.length;
      for (const rr of rrIntervals) {
        if (rr < avgRR * 0.75) {
          ectopicCount++;
        }
      }
    }
    
    // Assessment Statement Generation
    let assessment = "";
    if (validBeats.length === 0) {
      assessment = "No physiological heartbeat signals could be identified. Verify sensor skin contact and lead placement configuration.";
    } else {
      assessment = `Patient demonstrated an average heart rate of ${avgBpm} BPM during the ${totalSec.toFixed(1)}s session. `;
      if (normalPct > 85 && ectopicCount === 0) {
        assessment += "Rhythm is stable, demonstrating a consistent Normal Sinus Rhythm (NSR) with no observed ectopy or significant arrhythmic episodes.";
      } else {
        let conditions = [];
        if (tachyPct > 15) conditions.push(`sinus tachycardia episodes (${tachyPct}% of session)`);
        if (bradyPct > 15) conditions.push(`sinus bradycardia episodes (${bradyPct}% of session)`);
        if (ectopicCount > 0) conditions.push(`${ectopicCount} premature ventricular contractions/ectopic complexes`);
        
        assessment += `Observed clinical indicators: ${conditions.join(', ')}. `;
        if (ectopicCount > 2 || tachyPct > 40) {
          assessment += "RECOMMENDATION: Significant rhythm fluctuations or ectopy detected. Suggest 12-lead diagnostic ECG and clinical correlation by an attending cardiologist.";
        } else {
          assessment += "Rhythm shows transient physiological variability, but remains overall functional. Clinical correlation suggested if symptomatic.";
        }
      }
    }
    
    // Update Modal DOM values
    reportDatetime.textContent = new Date().toLocaleString();
    reportDuration.textContent = `${Math.floor(totalSec / 60)}m ${Math.round(totalSec % 60)}s`;
    reportAvgBpm.textContent = avgBpm > 0 ? `${avgBpm} BPM` : '--';
    reportRangeBpm.textContent = minBpm > 0 ? `${maxBpm} / ${minBpm} BPM` : '--';
    reportTotalBeats.textContent = beatTimes.length;
    reportNormalSec.textContent = `${normalSec}s`;
    reportNormalPct.textContent = `${normalPct}%`;
    reportTachySec.textContent = `${tachySec}s`;
    reportTachyPct.textContent = `${tachyPct}%`;
    reportBradySec.textContent = `${bradySec}s`;
    reportBradyPct.textContent = `${bradyPct}%`;
    
    reportEctopicCount.textContent = ectopicCount === 1 ? '1 Ectopic Beat Detected' : `${ectopicCount} Ectopic Beats Detected`;
    if (ectopicCount > 0) {
      reportEctopicCount.className = 'font-bold text-magenta';
    } else {
      reportEctopicCount.className = 'font-bold text-grey';
    }
    
    reportAssessmentText.textContent = assessment;
  }

  // --- Run Init ---
  init();
});
