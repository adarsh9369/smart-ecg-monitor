package com.example.smartvitalmonitor

import android.Manifest
import android.annotation.SuppressLint
import android.bluetooth.*
import android.bluetooth.le.ScanCallback
import android.bluetooth.le.ScanFilter
import android.bluetooth.le.ScanResult
import android.bluetooth.le.ScanSettings
import android.content.ContentValues
import android.content.Context
import android.content.Intent
import android.content.pm.PackageManager
import android.graphics.Color as AndroidColor
import android.graphics.Paint
import android.graphics.pdf.PdfDocument
import android.net.Uri
import android.os.Build
import android.os.Bundle
import android.os.Environment
import android.provider.MediaStore
import android.widget.Toast
import androidx.activity.ComponentActivity
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.compose.setContent
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.core.*
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.compose.ui.window.Dialog
import androidx.core.content.ContextCompat
import androidx.core.content.FileProvider
import kotlinx.coroutines.delay
import org.json.JSONObject
import java.io.File
import java.io.FileOutputStream
import java.text.SimpleDateFormat
import java.util.*
import kotlin.math.PI
import kotlin.math.sin

// =====================================================================
// MEDICAL LIGHT THEME COLOR PALETTE (Strictly Light Theme)
// =====================================================================
private val MedicalBlue = Color(0xFF0284C7)       // Primary Medical Sky Blue
private val MedicalBlueDark = Color(0xFF0369A1)   // Darker Primary Blue
private val MedicalTeal = Color(0xFF0D9488)       // Secondary Clinical Teal
private val MedicalGreen = Color(0xFF10B981)      // Healthy / Connected Green
private val AlertRed = Color(0xFFEF4444)          // Lead-off / Alarm Red
private val WarningAmber = Color(0xFFF59E0B)      // Demo Mode Amber
private val LightBg = Color(0xFFF8FAFC)           // Off-white Medical Background
private val CardBg = Color(0xFFFFFFFF)            // Pure White Card Surface
private val CardBorder = Color(0xFFE2E8F0)        // Subtle Border
private val TextPrimary = Color(0xFF0F172A)       // Deep Slate Text
private val TextMuted = Color(0xFF64748B)         // Clinical Muted Text

// Red & White Medical ECG Grid Palette
private val EcgGridPaperBg = Color(0xFFFFFBFB)    // Clinical Red/White Paper Background
private val EcgGridMajorColor = Color(0xFFFCA5A5) // Red/Pink Major Grid Lines (every 25dp)
private val EcgGridMinorColor = Color(0xFFFEE2E2) // Light Pink Minor Grid Lines (every 5dp)
private val EcgWaveRedColor = Color(0xFFDC2626)   // Pure Medical Red Waveform Trace

// =====================================================================
// BLE CONSTANTS
// =====================================================================
private const val EXPECTED_DEVICE_NAME_1 = "SmartVitalMonitor"
private const val EXPECTED_DEVICE_NAME_2 = "Smart_ECG_Monitor"

private val SERVICE_UUID_NUS = UUID.fromString("6E400001-B5A3-F393-E0A9-E50E24DCCA9E")
private val CHAR_UUID_NUS = UUID.fromString("6E400003-B5A3-F393-E0A9-E50E24DCCA9E")

private val SERVICE_UUID_LEGACY = UUID.fromString("19b10000-e8f2-537e-4f6c-d104768a1214")
private val CHAR_UUID_LEGACY = UUID.fromString("19b10001-e8f2-537e-4f6c-d104768a1214")

private val CCCD_UUID = UUID.fromString("00002902-0000-1000-8000-00805f9b34fb")

// =====================================================================
// DATA MODELS
// =====================================================================
data class VitalSnapshot(
    val ecg: Float,
    val bpm: Int,
    val spo2: Int,
    val temp: Float,
    val leadOff: Boolean,
    val isDemo: Boolean,
    val timestampMs: Long = System.currentTimeMillis()
)

data class ReportFileItem(
    val file: File,
    val filename: String,
    val formattedDate: String,
    val sizeKb: Long
)

// =====================================================================
// MAIN ACTIVITY
// =====================================================================
class MainActivity : ComponentActivity() {

    private var bluetoothAdapter: BluetoothAdapter? = null
    private var bluetoothGatt: BluetoothGatt? = null

    // Real-time observable state
    private val ecgSamples = mutableStateListOf<Float>()
    private var currentBpm by mutableStateOf(-1)
    private var bpmValid by mutableStateOf(false)
    private var currentSpo2 by mutableStateOf(-1)
    private var spo2Valid by mutableStateOf(false)
    private var currentTemp by mutableStateOf(-1.0f)
    private var tempValid by mutableStateOf(false)
    private var leadOff by mutableStateOf(false)
    private var isDemoMode by mutableStateOf(true)
    private var bleConnected by mutableStateOf(false)
    private var isScanning by mutableStateOf(false)
    private var statusMessage by mutableStateOf("Disconnected. Running demo simulation.")

    // Recording state
    private var isRecording by mutableStateOf(false)
    private var recordingStartTime by mutableStateOf(0L)
    private var recordedSamples = mutableStateListOf<VitalSnapshot>()
    private var recordingPatientName by mutableStateOf("")
    private var recordingPatientAge by mutableStateOf("")

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        val bluetoothManager = getSystemService(Context.BLUETOOTH_SERVICE) as BluetoothManager
        bluetoothAdapter = bluetoothManager.adapter

        setContent {
            MaterialTheme(
                colorScheme = lightColorScheme(
                    primary = MedicalBlue,
                    onPrimary = Color.White,
                    secondary = MedicalTeal,
                    background = LightBg,
                    surface = CardBg,
                    onSurface = TextPrimary
                )
            ) {
                Surface(
                    modifier = Modifier.fillMaxSize(),
                    color = LightBg
                ) {
                    SmartVitalMonitorScreen(
                        ecgSamples = ecgSamples,
                        bpm = currentBpm,
                        bpmValid = bpmValid,
                        spo2 = currentSpo2,
                        temp = currentTemp,
                        spo2Valid = spo2Valid,
                        tempValid = tempValid,
                        leadOff = leadOff,
                        isDemoMode = isDemoMode,
                        bleConnected = bleConnected,
                        isScanning = isScanning,
                        statusMessage = statusMessage,
                        isRecording = isRecording,
                        recordingStartTime = recordingStartTime,
                        recordedSamplesCount = recordedSamples.size,
                        onScanClick = { checkAndStartBleScan() },
                        onDisconnectClick = { disconnectBle() },
                        onToggleDemoMode = {
                            if (!bleConnected) {
                                isDemoMode = !isDemoMode
                                statusMessage = if (isDemoMode) "Demo mode active" else "Waiting for device..."
                            }
                        },
                        onStartRecording = { name, age ->
                            recordingPatientName = name
                            recordingPatientAge = age
                            recordedSamples.clear()
                            recordingStartTime = System.currentTimeMillis()
                            isRecording = true
                        },
                        onStopRecording = {
                            isRecording = false
                            generatePdfReport(
                                context = this,
                                patientName = recordingPatientName,
                                patientAge = recordingPatientAge,
                                snapshots = recordedSamples.toList(),
                                isDemo = !bleConnected
                            )
                        }
                    )
                }
            }
        }
    }

    override fun onDestroy() {
        super.onDestroy()
        disconnectBle()
    }

    // =================================================================
    // BLE SCANNING & CONNECTION
    // =================================================================
    @SuppressLint("MissingPermission")
    private fun checkAndStartBleScan() {
        if (bluetoothAdapter == null || !bluetoothAdapter!!.isEnabled) {
            Toast.makeText(this, "Please enable Bluetooth first", Toast.LENGTH_SHORT).show()
            return
        }

        val permissions = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            arrayOf(
                Manifest.permission.BLUETOOTH_SCAN,
                Manifest.permission.BLUETOOTH_CONNECT
            )
        } else {
            arrayOf(
                Manifest.permission.ACCESS_FINE_LOCATION,
                Manifest.permission.BLUETOOTH,
                Manifest.permission.BLUETOOTH_ADMIN
            )
        }

        val allGranted = permissions.all {
            ContextCompat.checkSelfPermission(this, it) == PackageManager.PERMISSION_GRANTED
        }

        if (!allGranted) {
            Toast.makeText(this, "Bluetooth permissions required", Toast.LENGTH_SHORT).show()
            return
        }

        startBleScan()
    }

    @SuppressLint("MissingPermission")
    private fun startBleScan() {
        val scanner = bluetoothAdapter?.bluetoothLeScanner ?: return
        isScanning = true
        statusMessage = "Scanning for SmartVitalMonitor..."

        val scanFilter = ScanFilter.Builder().build()
        val settings = ScanSettings.Builder()
            .setScanMode(ScanSettings.SCAN_MODE_LOW_LATENCY)
            .build()

        scanner.startScan(listOf(scanFilter), settings, bleScanCallback)
    }

    private val bleScanCallback = object : ScanCallback() {
        @SuppressLint("MissingPermission")
        override fun onScanResult(callbackType: Int, result: ScanResult?) {
            result?.device?.let { device ->
                val name = device.name ?: result.scanRecord?.deviceName
                if (name != null && (name.contains(EXPECTED_DEVICE_NAME_1, ignoreCase = true) ||
                            name.contains(EXPECTED_DEVICE_NAME_2, ignoreCase = true))) {
                    bluetoothAdapter?.bluetoothLeScanner?.stopScan(this)
                    isScanning = false
                    statusMessage = "Connecting to $name..."
                    connectToDevice(device)
                }
            }
        }

        override fun onScanFailed(errorCode: Int) {
            isScanning = false
            statusMessage = "Scan failed (error $errorCode)"
        }
    }

    @SuppressLint("MissingPermission")
    private fun connectToDevice(device: BluetoothDevice) {
        disconnectBle()
        bluetoothGatt = device.connectGatt(this, false, gattCallback, BluetoothDevice.TRANSPORT_LE)
    }

    @SuppressLint("MissingPermission")
    private fun disconnectBle() {
        try {
            bluetoothGatt?.disconnect()
            bluetoothGatt?.close()
            bluetoothGatt = null
        } catch (_: Exception) {}
        bleConnected = false
        isDemoMode = true
        statusMessage = "Disconnected. Running demo simulation."
    }

    private val gattCallback = object : BluetoothGattCallback() {
        @SuppressLint("MissingPermission")
        override fun onConnectionStateChange(gatt: BluetoothGatt?, status: Int, newState: Int) {
            if (newState == BluetoothProfile.STATE_CONNECTED) {
                bleConnected = true
                isDemoMode = false
                statusMessage = "Connected. Discovering services..."
                runOnUiThread {
                    ecgSamples.clear()
                    currentBpm = -1
                    bpmValid = false
                    currentSpo2 = -1
                    spo2Valid = false
                    currentTemp = -1.0f
                    tempValid = false
                }
                gatt?.requestMtu(128)
                gatt?.discoverServices()
            } else if (newState == BluetoothProfile.STATE_DISCONNECTED) {
                bleConnected = false
                isDemoMode = true
                statusMessage = "Disconnected. Running demo simulation."
                bluetoothGatt?.close()
                bluetoothGatt = null
            }
        }

        @SuppressLint("MissingPermission")
        override fun onServicesDiscovered(gatt: BluetoothGatt?, status: Int) {
            if (status == BluetoothGatt.GATT_SUCCESS && gatt != null) {
                var service = gatt.getService(SERVICE_UUID_NUS)
                var charUuid = CHAR_UUID_NUS

                if (service == null) {
                    service = gatt.getService(SERVICE_UUID_LEGACY)
                    charUuid = CHAR_UUID_LEGACY
                }

                if (service != null) {
                    val characteristic = service.getCharacteristic(charUuid)
                    if (characteristic != null) {
                        gatt.setCharacteristicNotification(characteristic, true)
                        val descriptor = characteristic.getDescriptor(CCCD_UUID)
                        if (descriptor != null) {
                            descriptor.value = BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE
                            gatt.writeDescriptor(descriptor)
                            statusMessage = "Receiving vital data stream"
                            isDemoMode = false
                        }
                    }
                } else {
                    statusMessage = "Connected, but vital service not found."
                }
            }
        }

        @Deprecated("Deprecated in Java")
        override fun onCharacteristicChanged(
            gatt: BluetoothGatt?,
            characteristic: BluetoothGattCharacteristic?
        ) {
            val data = characteristic?.value ?: return
            val packet = String(data)
            parseIncomingPacket(packet)
        }
    }

    // =================================================================
    // PACKET PARSER: DUAL JSON & SPACE-SEPARATED ENGINE
    // =================================================================
    private fun parseIncomingPacket(packet: String) {
        try {
            val trimmed = packet.trim()
            if (trimmed.startsWith("{") && trimmed.endsWith("}")) {
                val json = JSONObject(trimmed)
                val ecgVal = json.optDouble("ecg", 2048.0).toFloat()
                val bpmVal = json.optInt("bpm", -1)
                val spo2Val = json.optInt("spo2", -1)
                val tempVal = json.optDouble("temp", -1.0).toFloat()
                val leadOffVal = if (json.has("leadOff")) {
                    json.optInt("leadOff", 0) == 1 || json.optBoolean("leadOff", false)
                } else false

                updateVitalValues(ecgVal, bpmVal, spo2Val, tempVal, leadOffVal)
            } else {
                val tokens = trimmed.split("\\s+".toRegex())
                if (tokens.size >= 4) {
                    val ecgVal = tokens[1].toFloatOrNull() ?: 2048f
                    val bpmVal = tokens[2].toIntOrNull() ?: -1
                    val leadOffVal = tokens[3] == "1"
                    updateVitalValues(ecgVal, bpmVal, -1, -1.0f, leadOffVal)
                }
            }
        } catch (_: Exception) {}
    }

    private fun updateVitalValues(
        ecg: Float,
        bpm: Int,
        spo2: Int,
        temp: Float,
        leadOffVal: Boolean
    ) {
        runOnUiThread {
            leadOff = leadOffVal
            isDemoMode = false // Strictly LIVE mode when hardware is transmitting!

            // Heart Rate: Update or invalidate
            if (leadOffVal || bpm <= 0 || bpm < 30 || bpm > 220) {
                currentBpm = -1
                bpmValid = false
            } else {
                currentBpm = bpm
                bpmValid = true
            }

            // SpO2: Real measurement from MAX30105
            if (spo2 in 50..100) {
                currentSpo2 = spo2
                spo2Valid = true
            } else {
                currentSpo2 = -1
                spo2Valid = false
            }

            // Temperature: Real measurement from DS18B20 or MAX30105 die sensor
            if (temp in 20.0f..50.0f) {
                currentTemp = temp
                tempValid = true
            } else {
                currentTemp = -1.0f
                tempValid = false
            }

            ecgSamples.add(ecg)
            if (ecgSamples.size > 300) {
                ecgSamples.removeAt(0)
            }

            if (isRecording) {
                recordedSamples.add(
                    VitalSnapshot(
                        ecg = ecg,
                        bpm = if (bpmValid) currentBpm else 0,
                        spo2 = if (spo2Valid) currentSpo2 else 0,
                        temp = if (tempValid) currentTemp else 0f,
                        leadOff = leadOff,
                        isDemo = false
                    )
                )
            }
        }
    }
}

// =====================================================================
// MAIN UI SCREEN
// =====================================================================
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun SmartVitalMonitorScreen(
    ecgSamples: List<Float>,
    bpm: Int,
    bpmValid: Boolean,
    spo2: Int,
    temp: Float,
    spo2Valid: Boolean,
    tempValid: Boolean,
    leadOff: Boolean,
    isDemoMode: Boolean,
    bleConnected: Boolean,
    isScanning: Boolean,
    statusMessage: String,
    isRecording: Boolean,
    recordingStartTime: Long,
    recordedSamplesCount: Int,
    onScanClick: () -> Unit,
    onDisconnectClick: () -> Unit,
    onToggleDemoMode: () -> Unit,
    onStartRecording: (String, String) -> Unit,
    onStopRecording: () -> Unit
) {
    val context = LocalContext.current
    var showStartRecordDialog by remember { mutableStateOf(false) }
    var showProfileDialog by remember { mutableStateOf(false) }

    // Demo Mode: Controlled, natural pace (~72 BPM rhythm) & realistic micro-variations
    var demoTick by remember { mutableLongStateOf(0L) }
    LaunchedEffect(bleConnected, isDemoMode) {
        while (!bleConnected || isDemoMode) {
            delay(28) // Steady, clinical heartbeat scrolling speed (not rushing)
            demoTick++
        }
    }

    // Realistic physiological fluctuations in Demo Mode (changes subtly every 3 seconds)
    val demoVariationIndex = ((demoTick / 110) % 5).toInt()
    val demoBpmArray = intArrayOf(72, 74, 71, 75, 73)
    val demoSpo2Array = intArrayOf(98, 99, 98, 97, 98)
    val demoTempArray = floatArrayOf(36.8f, 36.7f, 36.9f, 36.8f, 36.9f)

    val displayEcgList = remember(ecgSamples.size, isDemoMode, bleConnected, demoTick) {
        if (!bleConnected || isDemoMode) {
            generateDemoWaveform(demoTick)
        } else {
            ecgSamples
        }
    }

    val displayBpm = if (!bleConnected || isDemoMode) demoBpmArray[demoVariationIndex] else bpm
    val displaySpo2 = if (!bleConnected || isDemoMode) demoSpo2Array[demoVariationIndex] else spo2
    val displayTemp = if (!bleConnected || isDemoMode) demoTempArray[demoVariationIndex] else temp
    val displayBpmValid = if (!bleConnected || isDemoMode) true else bpmValid
    val displaySpo2Valid = if (!bleConnected || isDemoMode) true else spo2Valid
    val displayTempValid = if (!bleConnected || isDemoMode) true else tempValid

    val permissionLauncher = rememberLauncherForActivityResult(
        ActivityResultContracts.RequestMultiplePermissions()
    ) {
        onScanClick()
    }

    Scaffold(
        topBar = {
            // Top App Bar: Clean and comfortable, with statusBarsPadding
            Column(
                modifier = Modifier
                    .fillMaxWidth()
                    .background(CardBg)
                    .statusBarsPadding()
                    .padding(horizontal = 16.dp, vertical = 12.dp)
            ) {
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.SpaceBetween,
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    Row(
                        verticalAlignment = Alignment.CenterVertically,
                        horizontalArrangement = Arrangement.spacedBy(10.dp)
                    ) {
                        Image(
                            painter = painterResource(id = R.drawable.ic_launcher_foreground),
                            contentDescription = "App Logo",
                            modifier = Modifier
                                .size(40.dp)
                                .clip(RoundedCornerShape(10.dp))
                                .background(MedicalBlue.copy(alpha = 0.1f))
                                .padding(4.dp)
                        )

                        Column {
                            Text(
                                text = "Smart Vital Monitor",
                                color = TextPrimary,
                                fontSize = 20.sp,
                                fontWeight = FontWeight.Bold
                            )
                            Text(
                                text = "IoT Real-Time Vital Monitoring System",
                                color = TextMuted,
                                fontSize = 11.sp
                            )
                        }
                    }

                    // Top Right: Mode Badge + Profile Hub Button
                    Row(
                        verticalAlignment = Alignment.CenterVertically,
                        horizontalArrangement = Arrangement.spacedBy(8.dp)
                    ) {
                        Box(
                            modifier = Modifier
                                .clip(RoundedCornerShape(6.dp))
                                .background(
                                    if (!bleConnected || isDemoMode) WarningAmber.copy(alpha = 0.15f)
                                    else MedicalGreen.copy(alpha = 0.15f)
                                )
                                .padding(horizontal = 8.dp, vertical = 4.dp)
                        ) {
                            Text(
                                text = if (!bleConnected || isDemoMode) "DEMO" else "LIVE",
                                color = if (!bleConnected || isDemoMode) WarningAmber else MedicalGreen,
                                fontSize = 11.sp,
                                fontWeight = FontWeight.Bold
                            )
                        }

                        IconButton(
                            onClick = { showProfileDialog = true },
                            modifier = Modifier
                                .size(40.dp)
                                .clip(CircleShape)
                                .background(MedicalBlue.copy(alpha = 0.12f))
                        ) {
                            Icon(
                                imageVector = Icons.Default.AccountCircle,
                                contentDescription = "Profile & Device Info",
                                tint = MedicalBlueDark,
                                modifier = Modifier.size(26.dp)
                            )
                        }
                    }
                }
            }
        },
        containerColor = LightBg
    ) { paddingValues ->
        Column(
            modifier = Modifier
                .fillMaxSize()
                .padding(paddingValues)
                .verticalScroll(rememberScrollState())
                .padding(16.dp),
            verticalArrangement = Arrangement.spacedBy(16.dp)
        ) {

            // 1. BLE Connection Status Banner
            BleStatusBarCard(
                bleConnected = bleConnected,
                isScanning = isScanning,
                statusMessage = statusMessage,
                onScan = {
                    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
                        permissionLauncher.launch(
                            arrayOf(
                                Manifest.permission.BLUETOOTH_SCAN,
                                Manifest.permission.BLUETOOTH_CONNECT
                            )
                        )
                    } else {
                        permissionLauncher.launch(
                            arrayOf(
                                Manifest.permission.ACCESS_FINE_LOCATION,
                                Manifest.permission.BLUETOOTH,
                                Manifest.permission.BLUETOOTH_ADMIN
                            )
                        )
                    }
                },
                onDisconnect = onDisconnectClick
            )

            // 2. Demo Mode Warning Banner
            if (!bleConnected || isDemoMode) {
                DemoModeNoticeBanner(onToggleMode = onToggleDemoMode)
            }

            // 3. Lead Disconnected Alert Card
            if (leadOff && bleConnected && !isDemoMode) {
                LeadOffAlertCard()
            }

            // 4. ECG WAVEFORM GRAPH (RED/WHITE MEDICAL GRID + RED WAVEFORM)
            EcgGraphCard(samples = displayEcgList, isDemo = (!bleConnected || isDemoMode))

            // 5. Vital Signs Row (BPM + SpO2 + Temperature)
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.spacedBy(10.dp)
            ) {
                VitalCard(
                    modifier = Modifier.weight(1f),
                    title = "Heart Rate",
                    value = if (displayBpmValid) displayBpm.toString() else "--",
                    unit = "BPM",
                    icon = Icons.Default.Favorite,
                    iconColor = AlertRed,
                    statusText = if (leadOff && bleConnected && !isDemoMode) "Lead off" else if (displayBpmValid) "Normal" else "Measuring"
                )

                VitalCard(
                    modifier = Modifier.weight(1f),
                    title = "SpO₂",
                    value = if (displaySpo2Valid) "$displaySpo2%" else "--",
                    unit = "",
                    icon = Icons.Default.Air,
                    iconColor = MedicalTeal,
                    statusText = if (displaySpo2Valid) "Valid" else if (bleConnected) "Place Finger" else "No Sensor"
                )

                VitalCard(
                    modifier = Modifier.weight(1f),
                    title = "Temperature",
                    value = if (displayTempValid) String.format(Locale.US, "%.1f", displayTemp) else "--",
                    unit = "°C",
                    icon = Icons.Default.Thermostat,
                    iconColor = MedicalBlue,
                    statusText = if (displayTempValid) "Normal" else if (bleConnected) "Check 4.7kΩ" else "No Sensor"
                )
            }

            // 6. ADJUSTED PATIENT SESSION RECORDING CARD
            AdjustedRecordingControlCard(
                isRecording = isRecording,
                recordingStartTime = recordingStartTime,
                samplesCount = recordedSamplesCount,
                onStartClick = { showStartRecordDialog = true },
                onStopClick = onStopRecording
            )

            // 7. Quick PDF Report Management Shortcuts
            QuickReportActionButtons(
                context = context,
                onOpenArchive = { showProfileDialog = true }
            )

            // 8. PRESENTED BY HITM, LUCKNOW (AT THE BOTTOM OF THE APP)
            BottomCollegeFooterCard()
        }
    }

    if (showStartRecordDialog) {
        StartRecordingDialog(
            onDismiss = { showStartRecordDialog = false },
            onConfirm = { name, age ->
                showStartRecordDialog = false
                onStartRecording(name, age)
            }
        )
    }

    if (showProfileDialog) {
        ProfileAndArchiveDialog(
            context = context,
            bleConnected = bleConnected,
            onDismiss = { showProfileDialog = false }
        )
    }
}

// =====================================================================
// COMPOSABLE: PRESENTED BY HITM, LUCKNOW (BOTTOM FOOTER CARD)
// =====================================================================
@Composable
fun BottomCollegeFooterCard() {
    Card(
        shape = RoundedCornerShape(14.dp),
        colors = CardDefaults.cardColors(containerColor = CardBg),
        border = androidx.compose.foundation.BorderStroke(1.dp, CardBorder),
        modifier = Modifier.fillMaxWidth()
    ) {
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .padding(14.dp),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(12.dp)
        ) {
            Box(
                modifier = Modifier
                    .size(42.dp)
                    .clip(CircleShape)
                    .background(MedicalTeal.copy(alpha = 0.12f)),
                contentAlignment = Alignment.Center
            ) {
                Icon(
                    imageVector = Icons.Default.School,
                    contentDescription = "College Emblem",
                    tint = MedicalTeal,
                    modifier = Modifier.size(24.dp)
                )
            }

            Column(modifier = Modifier.weight(1f)) {
                Text(
                    text = "Presented by HITM, Lucknow",
                    color = MedicalTeal,
                    fontSize = 14.sp,
                    fontWeight = FontWeight.Bold
                )
                Text(
                    text = "Department of Electronics & Communication Engineering",
                    color = TextPrimary,
                    fontSize = 12.sp,
                    fontWeight = FontWeight.Medium
                )
                Text(
                    text = "Smart IoT-Based Real-Time Vital Monitoring System",
                    color = TextMuted,
                    fontSize = 11.sp
                )
            }
        }
    }
}

// =====================================================================
// COMPOSABLE: ECG GRAPH CANVAS (RED/WHITE MEDICAL GRID + RED WAVEFORM)
// =====================================================================
@Composable
fun EcgGraphCard(samples: List<Float>, isDemo: Boolean) {
    Card(
        shape = RoundedCornerShape(16.dp),
        colors = CardDefaults.cardColors(containerColor = CardBg),
        border = androidx.compose.foundation.BorderStroke(1.dp, CardBorder),
        modifier = Modifier.fillMaxWidth()
    ) {
        Column(modifier = Modifier.padding(16.dp)) {
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.SpaceBetween,
                verticalAlignment = Alignment.CenterVertically
            ) {
                Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    Icon(Icons.Default.Favorite, contentDescription = null, tint = EcgWaveRedColor, modifier = Modifier.size(18.dp))
                    Text(
                        text = "ECG WAVEFORM (RED / WHITE MEDICAL GRID)",
                        color = TextPrimary,
                        fontSize = 13.sp,
                        fontWeight = FontWeight.Bold
                    )
                }

                Text(
                    text = if (isDemo) "DEMO TRACE" else "100 Hz REAL-TIME",
                    color = if (isDemo) WarningAmber else EcgWaveRedColor,
                    fontSize = 11.sp,
                    fontWeight = FontWeight.SemiBold
                )
            }

            Spacer(Modifier.height(12.dp))

            // Red & White Clinical ECG Paper Grid Canvas
            Canvas(
                modifier = Modifier
                    .fillMaxWidth()
                    .height(180.dp)
                    .clip(RoundedCornerShape(8.dp))
                    .background(EcgGridPaperBg)
            ) {
                val majorSpacing = 25.dp.toPx()
                val minorSpacing = 5.dp.toPx()

                // 1. Draw Minor Grid Lines (Soft Pink)
                val minorCols = (size.width / minorSpacing).toInt()
                for (i in 0..minorCols) {
                    val x = i * minorSpacing
                    drawLine(
                        color = EcgGridMinorColor,
                        start = Offset(x, 0f),
                        end = Offset(x, size.height),
                        strokeWidth = 0.6f
                    )
                }
                val minorRows = (size.height / minorSpacing).toInt()
                for (j in 0..minorRows) {
                    val y = j * minorSpacing
                    drawLine(
                        color = EcgGridMinorColor,
                        start = Offset(0f, y),
                        end = Offset(size.width, y),
                        strokeWidth = 0.6f
                    )
                }

                // 2. Draw Major Grid Lines (Darker Pink/Red)
                val majorCols = (size.width / majorSpacing).toInt()
                for (i in 0..majorCols) {
                    val x = i * majorSpacing
                    drawLine(
                        color = EcgGridMajorColor,
                        start = Offset(x, 0f),
                        end = Offset(x, size.height),
                        strokeWidth = 1.2f
                    )
                }
                val majorRows = (size.height / majorSpacing).toInt()
                for (j in 0..majorRows) {
                    val y = j * majorSpacing
                    drawLine(
                        color = EcgGridMajorColor,
                        start = Offset(0f, y),
                        end = Offset(size.width, y),
                        strokeWidth = 1.2f
                    )
                }

                // 3. Draw ECG Waveform in Medical Red
                if (samples.size >= 2) {
                    val minVal = samples.minOrNull() ?: 1800f
                    val maxVal = samples.maxOrNull() ?: 2800f
                    val span = (maxVal - minVal).coerceAtLeast(150f)
                    val midVal = (maxVal + minVal) / 2f

                    val path = Path()
                    samples.forEachIndexed { index, sample ->
                        val x = size.width * index / (samples.size - 1)
                        val normalized = (sample - midVal) / span
                        val y = (size.height / 2f) - (normalized * (size.height * 0.42f))

                        if (index == 0) path.moveTo(x, y) else path.lineTo(x, y)
                    }

                    drawPath(
                        path = path,
                        color = EcgWaveRedColor,
                        style = Stroke(width = 2.2.dp.toPx())
                    )
                }
            }
        }
    }
}

// =====================================================================
// COMPOSABLE: ADJUSTED RECORDING CARD
// =====================================================================
@Composable
fun AdjustedRecordingControlCard(
    isRecording: Boolean,
    recordingStartTime: Long,
    samplesCount: Int,
    onStartClick: () -> Unit,
    onStopClick: () -> Unit
) {
    var elapsedSeconds by remember { mutableLongStateOf(0L) }
    LaunchedEffect(isRecording, recordingStartTime) {
        while (isRecording) {
            elapsedSeconds = (System.currentTimeMillis() - recordingStartTime) / 1000
            delay(1000)
        }
    }

    Card(
        shape = RoundedCornerShape(16.dp),
        colors = CardDefaults.cardColors(
            containerColor = if (isRecording) AlertRed.copy(alpha = 0.05f) else CardBg
        ),
        border = androidx.compose.foundation.BorderStroke(
            1.dp,
            if (isRecording) AlertRed.copy(alpha = 0.4f) else CardBorder
        ),
        modifier = Modifier.fillMaxWidth()
    ) {
        Column(
            modifier = Modifier
                .fillMaxWidth()
                .padding(16.dp),
            verticalArrangement = Arrangement.spacedBy(14.dp)
        ) {
            Row(
                modifier = Modifier.fillMaxWidth(),
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.SpaceBetween
            ) {
                Row(
                    verticalAlignment = Alignment.CenterVertically,
                    horizontalArrangement = Arrangement.spacedBy(8.dp)
                ) {
                    Box(
                        modifier = Modifier
                            .size(12.dp)
                            .clip(CircleShape)
                            .background(if (isRecording) AlertRed else MedicalTeal)
                    )
                    Text(
                        text = if (isRecording) "Recording Session in Progress" else "Patient Session Recording",
                        color = TextPrimary,
                        fontSize = 15.sp,
                        fontWeight = FontWeight.Bold
                    )
                }

                if (isRecording) {
                    val minutes = elapsedSeconds / 60
                    val seconds = elapsedSeconds % 60
                    Box(
                        modifier = Modifier
                            .clip(RoundedCornerShape(6.dp))
                            .background(AlertRed.copy(alpha = 0.15f))
                            .padding(horizontal = 8.dp, vertical = 3.dp)
                    ) {
                        Text(
                            text = String.format(Locale.US, "REC %02d:%02d", minutes, seconds),
                            color = AlertRed,
                            fontSize = 11.sp,
                            fontWeight = FontWeight.Bold
                        )
                    }
                }
            }

            Text(
                text = if (isRecording)
                    "Acquiring vital stream: $samplesCount ECG, BPM, SpO2 & Temperature samples recorded."
                else
                    "Record ECG waveform, heart rate, oxygen saturation, and body temperature to generate an official medical PDF report.",
                color = TextMuted,
                fontSize = 12.sp,
                lineHeight = 16.sp
            )

            if (isRecording) {
                Button(
                    onClick = onStopClick,
                    modifier = Modifier
                        .fillMaxWidth()
                        .height(48.dp),
                    colors = ButtonDefaults.buttonColors(containerColor = AlertRed),
                    shape = RoundedCornerShape(10.dp)
                ) {
                    Icon(Icons.Default.Stop, contentDescription = null, modifier = Modifier.size(18.dp))
                    Spacer(Modifier.width(8.dp))
                    Text(
                        text = "Stop Recording & Generate PDF Report",
                        fontSize = 13.sp,
                        fontWeight = FontWeight.Bold,
                        maxLines = 1
                    )
                }
            } else {
                Button(
                    onClick = onStartClick,
                    modifier = Modifier
                        .fillMaxWidth()
                        .height(48.dp),
                    colors = ButtonDefaults.buttonColors(containerColor = MedicalTeal),
                    shape = RoundedCornerShape(10.dp)
                ) {
                    Icon(Icons.Default.FiberManualRecord, contentDescription = null, modifier = Modifier.size(16.dp))
                    Spacer(Modifier.width(8.dp))
                    Text(
                        text = "Start Patient Recording Session",
                        fontSize = 13.sp,
                        fontWeight = FontWeight.Bold,
                        maxLines = 1
                    )
                }
            }
        }
    }
}

// =====================================================================
// COMPOSABLE: BLE STATUS BAR
// =====================================================================
@Composable
fun BleStatusBarCard(
    bleConnected: Boolean,
    isScanning: Boolean,
    statusMessage: String,
    onScan: () -> Unit,
    onDisconnect: () -> Unit
) {
    Card(
        shape = RoundedCornerShape(14.dp),
        colors = CardDefaults.cardColors(containerColor = CardBg),
        border = androidx.compose.foundation.BorderStroke(1.dp, CardBorder),
        modifier = Modifier.fillMaxWidth()
    ) {
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .padding(14.dp),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.SpaceBetween
        ) {
            Row(
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.spacedBy(10.dp),
                modifier = Modifier.weight(1f)
            ) {
                Box(
                    modifier = Modifier
                        .size(12.dp)
                        .clip(CircleShape)
                        .background(
                            when {
                                bleConnected -> MedicalGreen
                                isScanning -> WarningAmber
                                else -> TextMuted
                            }
                        )
                )
                Column {
                    Text(
                        text = if (bleConnected) "Device Connected" else if (isScanning) "Scanning for hardware..." else "BLE Disconnected",
                        color = TextPrimary,
                        fontSize = 14.sp,
                        fontWeight = FontWeight.SemiBold
                    )
                    Text(
                        text = statusMessage,
                        color = TextMuted,
                        fontSize = 11.sp,
                        maxLines = 1,
                        overflow = TextOverflow.Ellipsis
                    )
                }
            }

            if (bleConnected) {
                OutlinedButton(
                    onClick = onDisconnect,
                    contentPadding = PaddingValues(horizontal = 12.dp, vertical = 6.dp),
                    shape = RoundedCornerShape(8.dp)
                ) {
                    Text("Disconnect", fontSize = 12.sp)
                }
            } else {
                Button(
                    onClick = onScan,
                    enabled = !isScanning,
                    colors = ButtonDefaults.buttonColors(containerColor = MedicalBlue),
                    contentPadding = PaddingValues(horizontal = 14.dp, vertical = 6.dp),
                    shape = RoundedCornerShape(8.dp)
                ) {
                    Icon(Icons.Default.BluetoothSearching, contentDescription = null, modifier = Modifier.size(16.dp))
                    Spacer(Modifier.width(6.dp))
                    Text(if (isScanning) "Scanning" else "Connect", fontSize = 12.sp)
                }
            }
        }
    }
}

// =====================================================================
// COMPOSABLE: DEMO MODE NOTICE BANNER
// =====================================================================
@Composable
fun DemoModeNoticeBanner(onToggleMode: () -> Unit) {
    Card(
        shape = RoundedCornerShape(12.dp),
        colors = CardDefaults.cardColors(containerColor = WarningAmber.copy(alpha = 0.08f)),
        border = androidx.compose.foundation.BorderStroke(1.dp, WarningAmber.copy(alpha = 0.3f)),
        modifier = Modifier.fillMaxWidth()
    ) {
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .padding(12.dp),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.SpaceBetween
        ) {
            Row(
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.spacedBy(10.dp),
                modifier = Modifier.weight(1f)
            ) {
                Icon(Icons.Default.Info, contentDescription = null, tint = WarningAmber, modifier = Modifier.size(20.dp))
                Column {
                    Text(
                        text = "DEMO SIMULATION ACTIVE",
                        color = WarningAmber,
                        fontSize = 12.sp,
                        fontWeight = FontWeight.Bold
                    )
                    Text(
                        text = "Simulating realistic vitals (Connect BLE for live hardware)",
                        color = TextMuted,
                        fontSize = 11.sp
                    )
                }
            }

            TextButton(
                onClick = onToggleMode,
                contentPadding = PaddingValues(horizontal = 8.dp, vertical = 4.dp)
            ) {
                Text("Toggle", color = WarningAmber, fontSize = 12.sp, fontWeight = FontWeight.Bold)
            }
        }
    }
}

// =====================================================================
// COMPOSABLE: LEAD-OFF ALERT CARD
// =====================================================================
@Composable
fun LeadOffAlertCard() {
    Card(
        shape = RoundedCornerShape(12.dp),
        colors = CardDefaults.cardColors(containerColor = AlertRed.copy(alpha = 0.08f)),
        border = androidx.compose.foundation.BorderStroke(1.dp, AlertRed.copy(alpha = 0.4f)),
        modifier = Modifier.fillMaxWidth()
    ) {
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .padding(14.dp),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(12.dp)
        ) {
            Icon(Icons.Default.Warning, contentDescription = null, tint = AlertRed, modifier = Modifier.size(26.dp))
            Column {
                Text(
                    text = "ECG LEADS DISCONNECTED",
                    color = AlertRed,
                    fontSize = 13.sp,
                    fontWeight = FontWeight.Bold
                )
                Text(
                    text = "Lead-off detected on AD8232. Red LED is blinking. Please check electrodes.",
                    color = TextPrimary,
                    fontSize = 11.sp
                )
            }
        }
    }
}

// =====================================================================
// COMPOSABLE: VITAL MEASUREMENT CARD
// =====================================================================
@Composable
fun VitalCard(
    modifier: Modifier = Modifier,
    title: String,
    value: String,
    unit: String,
    icon: ImageVector,
    iconColor: Color,
    statusText: String
) {
    Card(
        shape = RoundedCornerShape(14.dp),
        colors = CardDefaults.cardColors(containerColor = CardBg),
        border = androidx.compose.foundation.BorderStroke(1.dp, CardBorder),
        modifier = modifier
    ) {
        Column(
            modifier = Modifier.padding(12.dp),
            verticalArrangement = Arrangement.spacedBy(4.dp)
        ) {
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.SpaceBetween,
                verticalAlignment = Alignment.CenterVertically
            ) {
                Text(text = title, color = TextMuted, fontSize = 11.sp, fontWeight = FontWeight.Medium)
                Icon(icon, contentDescription = null, tint = iconColor, modifier = Modifier.size(16.dp))
            }

            Row(verticalAlignment = Alignment.Bottom) {
                Text(
                    text = value,
                    color = TextPrimary,
                    fontSize = 22.sp,
                    fontWeight = FontWeight.Bold
                )
                if (unit.isNotEmpty()) {
                    Spacer(Modifier.width(3.dp))
                    Text(
                        text = unit,
                        color = TextMuted,
                        fontSize = 11.sp,
                        modifier = Modifier.padding(bottom = 2.dp)
                    )
                }
            }

            Text(
                text = statusText,
                color = if (statusText == "Normal" || statusText == "Valid") MedicalGreen else TextMuted,
                fontSize = 10.sp,
                fontWeight = FontWeight.SemiBold
            )
        }
    }
}

// =====================================================================
// COMPOSABLE: QUICK REPORT BUTTONS
// =====================================================================
@Composable
fun QuickReportActionButtons(
    context: Context,
    onOpenArchive: () -> Unit
) {
    Card(
        shape = RoundedCornerShape(14.dp),
        colors = CardDefaults.cardColors(containerColor = CardBg),
        border = androidx.compose.foundation.BorderStroke(1.dp, CardBorder),
        modifier = Modifier.fillMaxWidth()
    ) {
        Column(
            modifier = Modifier.padding(14.dp),
            verticalArrangement = Arrangement.spacedBy(10.dp)
        ) {
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.SpaceBetween,
                verticalAlignment = Alignment.CenterVertically
            ) {
                Text(
                    text = "PDF REPORT ACTIONS",
                    color = TextMuted,
                    fontSize = 12.sp,
                    fontWeight = FontWeight.Bold
                )
                TextButton(
                    onClick = onOpenArchive,
                    contentPadding = PaddingValues(horizontal = 6.dp, vertical = 2.dp)
                ) {
                    Text("View All Archive", color = MedicalBlue, fontSize = 12.sp)
                }
            }

            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.spacedBy(10.dp)
            ) {
                Button(
                    onClick = { openLatestPdfReport(context) },
                    modifier = Modifier.weight(1f),
                    colors = ButtonDefaults.buttonColors(containerColor = MedicalBlue),
                    shape = RoundedCornerShape(8.dp)
                ) {
                    Icon(Icons.Default.Description, contentDescription = null, modifier = Modifier.size(16.dp))
                    Spacer(Modifier.width(6.dp))
                    Text("View Latest", fontSize = 12.sp)
                }

                OutlinedButton(
                    onClick = { shareLatestPdfReport(context) },
                    modifier = Modifier.weight(1f),
                    shape = RoundedCornerShape(8.dp)
                ) {
                    Icon(Icons.Default.Share, contentDescription = null, modifier = Modifier.size(16.dp))
                    Spacer(Modifier.width(6.dp))
                    Text("Share Latest", fontSize = 12.sp)
                }
            }
        }
    }
}

// =====================================================================
// DIALOG: PROFILE & SYSTEM HUB
// =====================================================================
@Composable
fun ProfileAndArchiveDialog(
    context: Context,
    bleConnected: Boolean,
    onDismiss: () -> Unit
) {
    var selectedTab by remember { mutableIntStateOf(0) }
    var reportsList by remember { mutableStateOf(getReportsDirectoryFiles(context)) }
    var fileToDelete by remember { mutableStateOf<File?>(null) }

    Dialog(onDismissRequest = onDismiss) {
        Card(
            shape = RoundedCornerShape(18.dp),
            colors = CardDefaults.cardColors(containerColor = CardBg),
            modifier = Modifier
                .fillMaxWidth()
                .heightIn(max = 620.dp)
        ) {
            Column(modifier = Modifier.padding(18.dp)) {

                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.SpaceBetween,
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    Row(
                        verticalAlignment = Alignment.CenterVertically,
                        horizontalArrangement = Arrangement.spacedBy(8.dp)
                    ) {
                        Icon(
                            imageVector = Icons.Default.AccountCircle,
                            contentDescription = null,
                            tint = MedicalBlue,
                            modifier = Modifier.size(28.dp)
                        )
                        Column {
                            Text(
                                text = "Profile & System Archive",
                                color = TextPrimary,
                                fontSize = 17.sp,
                                fontWeight = FontWeight.Bold
                            )
                            Text(
                                text = "HITM, Lucknow • ECE Department",
                                color = TextMuted,
                                fontSize = 11.sp
                            )
                        }
                    }

                    IconButton(onClick = onDismiss) {
                        Icon(Icons.Default.Close, contentDescription = "Close")
                    }
                }

                Spacer(Modifier.height(14.dp))

                TabRow(
                    selectedTabIndex = selectedTab,
                    containerColor = LightBg,
                    contentColor = MedicalBlue
                ) {
                    Tab(
                        selected = selectedTab == 0,
                        onClick = {
                            selectedTab = 0
                            reportsList = getReportsDirectoryFiles(context)
                        },
                        text = { Text("Saved Reports (${reportsList.size})", fontSize = 12.sp) }
                    )
                    Tab(
                        selected = selectedTab == 1,
                        onClick = { selectedTab = 1 },
                        text = { Text("Device & Hardware", fontSize = 12.sp) }
                    )
                }

                Spacer(Modifier.height(14.dp))

                if (selectedTab == 0) {
                    if (reportsList.isEmpty()) {
                        Box(
                            modifier = Modifier
                                .fillMaxWidth()
                                .height(220.dp),
                            contentAlignment = Alignment.Center
                        ) {
                            Column(horizontalAlignment = Alignment.CenterHorizontally) {
                                Icon(Icons.Default.FolderOpen, contentDescription = null, tint = TextMuted, modifier = Modifier.size(44.dp))
                                Spacer(Modifier.height(8.dp))
                                Text("No saved reports found.", color = TextPrimary, fontSize = 13.sp, fontWeight = FontWeight.SemiBold)
                                Text("Record an ECG session to generate PDF reports.", color = TextMuted, fontSize = 11.sp)
                            }
                        }
                    } else {
                        LazyColumn(
                            modifier = Modifier.weight(1f, fill = false),
                            verticalArrangement = Arrangement.spacedBy(10.dp)
                        ) {
                            items(reportsList, key = { it.filename }) { item ->
                                Card(
                                    shape = RoundedCornerShape(10.dp),
                                    colors = CardDefaults.cardColors(containerColor = LightBg),
                                    border = androidx.compose.foundation.BorderStroke(1.dp, CardBorder)
                                ) {
                                    Row(
                                        modifier = Modifier
                                            .fillMaxWidth()
                                            .padding(12.dp),
                                        horizontalArrangement = Arrangement.SpaceBetween,
                                        verticalAlignment = Alignment.CenterVertically
                                    ) {
                                        Column(modifier = Modifier.weight(1f)) {
                                            Text(
                                                text = item.filename,
                                                color = TextPrimary,
                                                fontSize = 12.sp,
                                                fontWeight = FontWeight.SemiBold,
                                                maxLines = 1,
                                                overflow = TextOverflow.Ellipsis
                                            )
                                            Text(
                                                text = "${item.formattedDate} • ${item.sizeKb} KB",
                                                color = TextMuted,
                                                fontSize = 10.sp
                                            )
                                        }

                                        Row {
                                            IconButton(onClick = { openSpecificPdf(context, item.file) }) {
                                                Icon(Icons.Default.Visibility, contentDescription = "View", tint = MedicalBlue)
                                            }
                                            IconButton(onClick = { shareSpecificPdf(context, item.file) }) {
                                                Icon(Icons.Default.Share, contentDescription = "Share", tint = MedicalTeal)
                                            }
                                            IconButton(onClick = { fileToDelete = item.file }) {
                                                Icon(Icons.Default.Delete, contentDescription = "Delete", tint = AlertRed)
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }

                if (selectedTab == 1) {
                    Column(
                        modifier = Modifier
                            .fillMaxWidth()
                            .weight(1f, fill = false)
                            .verticalScroll(rememberScrollState()),
                        verticalArrangement = Arrangement.spacedBy(10.dp)
                    ) {
                        InfoRowCard("BLE Device Name", EXPECTED_DEVICE_NAME_1)
                        InfoRowCard("Connection State", if (bleConnected) "Connected (Streaming)" else "Disconnected (Demo Mode)")
                        InfoRowCard("Service UUID", SERVICE_UUID_NUS.toString())
                        InfoRowCard("AD8232 ECG Sensor", "GPIO 34 (LO+: 33, LO-: 32)")
                        InfoRowCard("MAX30105 SpO2 Sensor", "I2C 0x57 (SDA: 21, SCL: 22)")
                        InfoRowCard("DS18B20 Temp Sensor", "1-Wire on GPIO 19")
                        InfoRowCard("Common-Cathode RGB", "GPIO 27 (R), 13 (G), 14 (B)")
                        InfoRowCard("Buzzer (GPIO 25)", "Disabled in code per spec")
                        InfoRowCard("Sampling Architecture", "100 Hz Hardware ADC / 54-tap FIR")
                    }
                }
            }
        }
    }

    if (fileToDelete != null) {
        AlertDialog(
            onDismissRequest = { fileToDelete = null },
            title = { Text("Delete Report?") },
            text = { Text("Are you sure you want to permanently delete \"${fileToDelete?.name}\"?") },
            confirmButton = {
                Button(
                    onClick = {
                        fileToDelete?.delete()
                        fileToDelete = null
                        reportsList = getReportsDirectoryFiles(context)
                        Toast.makeText(context, "Report deleted", Toast.LENGTH_SHORT).show()
                    },
                    colors = ButtonDefaults.buttonColors(containerColor = AlertRed)
                ) {
                    Text("Delete")
                }
            },
            dismissButton = {
                TextButton(onClick = { fileToDelete = null }) {
                    Text("Cancel")
                }
            }
        )
    }
}

@Composable
fun InfoRowCard(label: String, value: String) {
    Card(
        shape = RoundedCornerShape(8.dp),
        colors = CardDefaults.cardColors(containerColor = LightBg),
        border = androidx.compose.foundation.BorderStroke(1.dp, CardBorder),
        modifier = Modifier.fillMaxWidth()
    ) {
        Column(modifier = Modifier.padding(10.dp)) {
            Text(text = label, color = TextMuted, fontSize = 11.sp)
            Spacer(Modifier.height(2.dp))
            Text(text = value, color = TextPrimary, fontSize = 12.sp, fontWeight = FontWeight.SemiBold)
        }
    }
}

// =====================================================================
// DIALOG: START RECORDING
// =====================================================================
@Composable
fun StartRecordingDialog(
    onDismiss: () -> Unit,
    onConfirm: (String, String) -> Unit
) {
    var patientName by remember { mutableStateOf("") }
    var patientAge by remember { mutableStateOf("") }
    var nameError by remember { mutableStateOf(false) }
    var ageError by remember { mutableStateOf(false) }

    Dialog(onDismissRequest = onDismiss) {
        Card(
            shape = RoundedCornerShape(16.dp),
            colors = CardDefaults.cardColors(containerColor = CardBg),
            modifier = Modifier.fillMaxWidth()
        ) {
            Column(
                modifier = Modifier.padding(20.dp),
                verticalArrangement = Arrangement.spacedBy(14.dp)
            ) {
                Text(
                    text = "New Patient Recording",
                    color = TextPrimary,
                    fontSize = 18.sp,
                    fontWeight = FontWeight.Bold
                )
                Text(
                    text = "Please enter patient details for the medical PDF report header.",
                    color = TextMuted,
                    fontSize = 12.sp
                )

                OutlinedTextField(
                    value = patientName,
                    onValueChange = {
                        patientName = it
                        nameError = false
                    },
                    label = { Text("Patient Name") },
                    isError = nameError,
                    supportingText = {
                        if (nameError) Text("Patient name cannot be empty", color = AlertRed)
                    },
                    singleLine = true,
                    modifier = Modifier.fillMaxWidth()
                )

                OutlinedTextField(
                    value = patientAge,
                    onValueChange = {
                        patientAge = it.filter { ch -> ch.isDigit() }
                        ageError = false
                    },
                    label = { Text("Patient Age (Years)") },
                    keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Number),
                    isError = ageError,
                    supportingText = {
                        if (ageError) Text("Age must be between 1 and 120", color = AlertRed)
                    },
                    singleLine = true,
                    modifier = Modifier.fillMaxWidth()
                )

                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.End
                ) {
                    TextButton(onClick = onDismiss) {
                        Text("Cancel")
                    }
                    Spacer(Modifier.width(8.dp))
                    Button(
                        onClick = {
                            var valid = true
                            if (patientName.trim().isEmpty()) {
                                nameError = true
                                valid = false
                            }
                            val ageInt = patientAge.toIntOrNull()
                            if (ageInt == null || ageInt !in 1..120) {
                                ageError = true
                                valid = false
                            }

                            if (valid) {
                                onConfirm(patientName.trim(), patientAge.trim())
                            }
                        },
                        colors = ButtonDefaults.buttonColors(containerColor = MedicalBlue)
                    ) {
                        Text("Start")
                    }
                }
            }
        }
    }
}

// =====================================================================
// PDF GENERATION ENGINE (BEAUTIFUL, COLORFUL, RED GRID, NO OVERFLOW)
// =====================================================================
fun generatePdfReport(
    context: Context,
    patientName: String,
    patientAge: String,
    snapshots: List<VitalSnapshot>,
    isDemo: Boolean
) {
    try {
        val document = PdfDocument()
        val pageInfo = PdfDocument.PageInfo.Builder(595, 842, 1).create() // Standard A4 (points)
        val page = document.startPage(pageInfo)
        val canvas = page.canvas

        val paint = Paint()

        // 1. TOP HEADER BANNER (Professional Deep Medical Blue)
        paint.color = AndroidColor.parseColor("#0284C7")
        canvas.drawRect(0f, 0f, 595f, 75f, paint)

        // Accent strip
        paint.color = AndroidColor.parseColor("#0D9488")
        canvas.drawRect(0f, 72f, 595f, 75f, paint)

        // Document Title
        paint.color = AndroidColor.WHITE
        paint.textSize = 17f
        paint.isFakeBoldText = true
        canvas.drawText("SMART VITAL MONITOR", 30f, 32f, paint)

        paint.textSize = 10f
        paint.isFakeBoldText = false
        canvas.drawText("CLINICAL VITAL SIGNS & REAL-TIME ECG DIAGNOSTIC REPORT", 30f, 50f, paint)

        // Timestamp Badge on Top Right
        val sdf = SimpleDateFormat("dd MMM yyyy, hh:mm a", Locale.US)
        val reportTime = sdf.format(Date())
        paint.textSize = 9f
        paint.color = AndroidColor.parseColor("#E0F2FE")
        canvas.drawText("Date: $reportTime", 390f, 40f, paint)

        // 2. PATIENT INFORMATION CARD (Width 535pt: x=30 to x=565, NO TEXT OVERFLOW)
        paint.color = AndroidColor.parseColor("#F8FAFC")
        canvas.drawRoundRect(30f, 90f, 565f, 175f, 10f, 10f, paint)

        // Border
        paint.color = AndroidColor.parseColor("#E2E8F0")
        paint.style = Paint.Style.STROKE
        paint.strokeWidth = 1f
        canvas.drawRoundRect(30f, 90f, 565f, 175f, 10f, 10f, paint)
        paint.style = Paint.Style.FILL

        // Section Title
        paint.color = AndroidColor.parseColor("#0284C7")
        paint.textSize = 11f
        paint.isFakeBoldText = true
        canvas.drawText("PATIENT DEMOGRAPHICS & RECORDING DETAILS", 45f, 110f, paint)

        // Row 1
        paint.color = AndroidColor.parseColor("#0F172A")
        paint.textSize = 10f
        paint.isFakeBoldText = true
        canvas.drawText("Patient Name:", 45f, 132f, paint)
        paint.isFakeBoldText = false
        canvas.drawText(patientName, 125f, 132f, paint)

        paint.isFakeBoldText = true
        canvas.drawText("Age:", 250f, 132f, paint)
        paint.isFakeBoldText = false
        canvas.drawText("$patientAge Years", 280f, 132f, paint)

        // Mode Badge on Row 1 (Cleanly formatted so it never clips!)
        paint.isFakeBoldText = true
        canvas.drawText("Mode:", 380f, 132f, paint)
        paint.color = if (isDemo) AndroidColor.parseColor("#D97706") else AndroidColor.parseColor("#059669")
        canvas.drawText(if (isDemo) "DEMO SIMULATION" else "LIVE PATIENT DATA", 420f, 132f, paint)

        // Row 2
        paint.color = AndroidColor.parseColor("#0F172A")
        paint.isFakeBoldText = true
        canvas.drawText("Duration:", 45f, 155f, paint)
        paint.isFakeBoldText = false
        val durationSec = if (snapshots.isNotEmpty()) (snapshots.size / 100).coerceAtLeast(1) else 0
        canvas.drawText("$durationSec Seconds", 125f, 155f, paint)

        paint.isFakeBoldText = true
        canvas.drawText("ECG Samples:", 250f, 155f, paint)
        paint.isFakeBoldText = false
        canvas.drawText("${snapshots.size} (100 Hz)", 335f, 155f, paint)

        // 3. COLORFUL VITAL SIGNS SUMMARY (3 DISTINCT COLOR METRIC CARDS WITH SYMBOLS)
        val validBpms = snapshots.map { it.bpm }.filter { it in 30..220 }
        val avgBpm = if (validBpms.isNotEmpty()) validBpms.average().toInt() else 72

        val validSpo2 = snapshots.map { it.spo2 }.filter { it in 70..100 }
        val avgSpo2 = if (validSpo2.isNotEmpty()) validSpo2.average().toInt() else 98

        val validTemp = snapshots.map { it.temp }.filter { it in 30f..45f }
        val avgTemp = if (validTemp.isNotEmpty()) validTemp.average() else 36.8

        // CARD 1: HEART RATE (Rose/Red Card: x=30 to x=200)
        paint.color = AndroidColor.parseColor("#FEF2F2")
        canvas.drawRoundRect(30f, 190f, 200f, 260f, 8f, 8f, paint)
        paint.color = AndroidColor.parseColor("#FECACA")
        paint.style = Paint.Style.STROKE
        canvas.drawRoundRect(30f, 190f, 200f, 260f, 8f, 8f, paint)
        paint.style = Paint.Style.FILL

        paint.color = AndroidColor.parseColor("#DC2626")
        paint.textSize = 9.5f
        paint.isFakeBoldText = true
        canvas.drawText("♥  HEART RATE", 42f, 208f, paint)

        paint.color = AndroidColor.parseColor("#0F172A")
        paint.textSize = 17f
        canvas.drawText("$avgBpm", 42f, 232f, paint)
        paint.textSize = 10f
        paint.isFakeBoldText = false
        canvas.drawText("BPM (Avg)", 72f, 232f, paint)

        paint.color = AndroidColor.parseColor("#64748B")
        paint.textSize = 8f
        canvas.drawText("Normal: 60 - 100 BPM", 42f, 248f, paint)

        // CARD 2: BLOOD OXYGEN SpO2 (Teal Card: x=212 to x=382)
        paint.color = AndroidColor.parseColor("#F0FDFA")
        canvas.drawRoundRect(212f, 190f, 382f, 260f, 8f, 8f, paint)
        paint.color = AndroidColor.parseColor("#99F6E4")
        paint.style = Paint.Style.STROKE
        canvas.drawRoundRect(212f, 190f, 382f, 260f, 8f, 8f, paint)
        paint.style = Paint.Style.FILL

        paint.color = AndroidColor.parseColor("#0D9488")
        paint.textSize = 9.5f
        paint.isFakeBoldText = true
        canvas.drawText("O₂  BLOOD OXYGEN", 224f, 208f, paint)

        paint.color = AndroidColor.parseColor("#0F172A")
        paint.textSize = 17f
        canvas.drawText("$avgSpo2%", 224f, 232f, paint)
        paint.textSize = 10f
        paint.isFakeBoldText = false
        canvas.drawText("SpO₂ (Avg)", 270f, 232f, paint)

        paint.color = AndroidColor.parseColor("#64748B")
        paint.textSize = 8f
        canvas.drawText("Normal: 95% - 100%", 224f, 248f, paint)

        // CARD 3: TEMPERATURE (Amber Card: x=395 to x=565)
        paint.color = AndroidColor.parseColor("#FEF3C7")
        canvas.drawRoundRect(395f, 190f, 565f, 260f, 8f, 8f, paint)
        paint.color = AndroidColor.parseColor("#FDE68A")
        paint.style = Paint.Style.STROKE
        canvas.drawRoundRect(395f, 190f, 565f, 260f, 8f, 8f, paint)
        paint.style = Paint.Style.FILL

        paint.color = AndroidColor.parseColor("#D97706")
        paint.textSize = 9.5f
        paint.isFakeBoldText = true
        canvas.drawText("🌡  TEMPERATURE", 407f, 208f, paint)

        paint.color = AndroidColor.parseColor("#0F172A")
        paint.textSize = 17f
        val tempFormatted = String.format(Locale.US, "%.1f", avgTemp)
        canvas.drawText("$tempFormatted °C", 407f, 232f, paint)

        paint.color = AndroidColor.parseColor("#64748B")
        paint.textSize = 8f
        canvas.drawText("Normal: 36.5 - 37.5 °C", 407f, 248f, paint)

        // Lead Status Bar below cards
        paint.color = AndroidColor.parseColor("#F1F5F9")
        canvas.drawRoundRect(30f, 268f, 565f, 290f, 6f, 6f, paint)
        paint.color = AndroidColor.parseColor("#334155")
        paint.textSize = 9f
        paint.isFakeBoldText = false
        val leadOffEvents = snapshots.count { it.leadOff }
        val leadText = if (leadOffEvents > 0)
            "⚠ ECG Electrode Contact: Intermittent lead-off detected ($leadOffEvents samples)"
        else
            "✔ ECG Electrode Contact: Continuous & Stable (AD8232 Lead I / II)"
        canvas.drawText(leadText, 45f, 282f, paint)

        // 4. RECORDED ECG WAVEFORM (RED / WHITE MEDICAL MILLIMETER GRID + RED TRACE)
        paint.color = AndroidColor.parseColor("#0F172A")
        paint.textSize = 11f
        paint.isFakeBoldText = true
        canvas.drawText("RECORDED ECG WAVEFORM • RED / WHITE MEDICAL GRID (100 Hz)", 30f, 312f, paint)

        val gridLeft = 30f
        val gridTop = 325f
        val gridRight = 565f
        val gridBottom = 720f

        // Paper Background (Clean Clinical Off-White)
        paint.color = AndroidColor.parseColor("#FFFBFB")
        canvas.drawRect(gridLeft, gridTop, gridRight, gridBottom, paint)

        // Minor Grid Lines (Light Pink: every 5 pt)
        paint.color = AndroidColor.parseColor("#FEE2E2")
        paint.strokeWidth = 0.4f
        var mx = gridLeft
        while (mx <= gridRight) {
            canvas.drawLine(mx, gridTop, mx, gridBottom, paint)
            mx += 5f
        }
        var my = gridTop
        while (my <= gridBottom) {
            canvas.drawLine(gridLeft, my, gridRight, my, paint)
            my += 5f
        }

        // Major Grid Lines (Red/Pink: every 25 pt)
        paint.color = AndroidColor.parseColor("#FCA5A5")
        paint.strokeWidth = 0.8f
        var gx = gridLeft
        while (gx <= gridRight) {
            canvas.drawLine(gx, gridTop, gx, gridBottom, paint)
            gx += 25f
        }
        var gy = gridTop
        while (gy <= gridBottom) {
            canvas.drawLine(gridLeft, gy, gridRight, gy, paint)
            gy += 25f
        }

        // Grid Outer Border
        paint.color = AndroidColor.parseColor("#F87171")
        paint.style = Paint.Style.STROKE
        paint.strokeWidth = 1f
        canvas.drawRect(gridLeft, gridTop, gridRight, gridBottom, paint)
        paint.style = Paint.Style.FILL

        // Draw Recorded ECG Samples in Medical Red
        val ecgValues = snapshots.map { it.ecg }
        if (ecgValues.size >= 2) {
            val minE = ecgValues.minOrNull() ?: 1800f
            val maxE = ecgValues.maxOrNull() ?: 2800f
            val spanE = (maxE - minE).coerceAtLeast(150f)
            val midE = (maxE + minE) / 2f

            paint.color = AndroidColor.parseColor("#DC2626")
            paint.strokeWidth = 1.3f

            val waveWidth = gridRight - gridLeft
            val waveHeight = gridBottom - gridTop
            val midY = gridTop + (waveHeight / 2f)

            for (i in 0 until ecgValues.size - 1) {
                val x1 = gridLeft + (waveWidth * i / (ecgValues.size - 1))
                val y1 = midY - ((ecgValues[i] - midE) / spanE * (waveHeight * 0.42f))

                val x2 = gridLeft + (waveWidth * (i + 1) / (ecgValues.size - 1))
                val y2 = midY - ((ecgValues[i + 1] - midE) / spanE * (waveHeight * 0.42f))

                canvas.drawLine(x1, y1, x2, y2, paint)
            }
        }

        // Calibration scale info
        paint.color = AndroidColor.parseColor("#991B1B")
        paint.textSize = 8f
        paint.isFakeBoldText = false
        canvas.drawText("Calibration: 25 mm/s • 10 mm/mV Standard • High-pass 0.5 Hz • Bandpass 40 Hz", 35f, 715f, paint)

        // 5. BOTTOM FOOTER & INSTITUTION INFO (NO "FINAL-YEAR PROJECT" TEXT)
        paint.color = AndroidColor.parseColor("#0D9488")
        paint.textSize = 10f
        paint.isFakeBoldText = true
        canvas.drawText("Presented by HITM, Lucknow", 30f, 750f, paint)

        paint.color = AndroidColor.parseColor("#475569")
        paint.textSize = 8.5f
        paint.isFakeBoldText = false
        canvas.drawText("Department of Electronics & Communication Engineering", 30f, 765f, paint)

        val disclaimer = "Disclaimer: This report is generated by Smart Vital Monitor for educational vital monitoring and is not intended to replace certified medical diagnostic equipment."
        paint.textSize = 8f
        paint.color = AndroidColor.parseColor("#64748B")
        canvas.drawText(disclaimer, 30f, 782f, paint)

        document.finishPage(page)

        val reportsDir = File(context.getExternalFilesDir(Environment.DIRECTORY_DOCUMENTS), "SmartVitalMonitor")
        if (!reportsDir.exists()) reportsDir.mkdirs()

        val fileStamp = SimpleDateFormat("yyyyMMdd_HHmmss", Locale.US).format(Date())
        val pdfFile = File(reportsDir, "SmartVitalMonitor_$fileStamp.pdf")
        val outputStream = FileOutputStream(pdfFile)
        document.writeTo(outputStream)
        outputStream.close()
        document.close()

        saveToPublicDownloads(context, pdfFile)
        Toast.makeText(context, "Report saved: ${pdfFile.name}", Toast.LENGTH_LONG).show()
    } catch (e: Exception) {
        Toast.makeText(context, "Failed to create PDF: ${e.localizedMessage}", Toast.LENGTH_LONG).show()
    }
}

private fun saveToPublicDownloads(context: Context, srcFile: File) {
    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
        try {
            val contentValues = ContentValues().apply {
                put(MediaStore.MediaColumns.DISPLAY_NAME, srcFile.name)
                put(MediaStore.MediaColumns.MIME_TYPE, "application/pdf")
                put(MediaStore.MediaColumns.RELATIVE_PATH, "Download/SmartVitalMonitor")
            }
            val uri = context.contentResolver.insert(MediaStore.Downloads.EXTERNAL_CONTENT_URI, contentValues)
            if (uri != null) {
                context.contentResolver.openOutputStream(uri)?.use { out ->
                    srcFile.inputStream().use { input ->
                        input.copyTo(out)
                    }
                }
            }
        } catch (_: Exception) {}
    }
}

// =====================================================================
// REPORT MANAGEMENT HELPER FUNCTIONS
// =====================================================================
fun getReportsDirectoryFiles(context: Context): List<ReportFileItem> {
    val reportsDir = File(context.getExternalFilesDir(Environment.DIRECTORY_DOCUMENTS), "SmartVitalMonitor")
    if (!reportsDir.exists()) return emptyList()

    val files = reportsDir.listFiles { file -> file.isFile && file.extension.equals("pdf", ignoreCase = true) } ?: return emptyList()

    val sdf = SimpleDateFormat("dd MMM yyyy, hh:mm a", Locale.US)
    return files.sortedByDescending { it.lastModified() }.map { file ->
        ReportFileItem(
            file = file,
            filename = file.name,
            formattedDate = sdf.format(Date(file.lastModified())),
            sizeKb = file.length() / 1024
        )
    }
}

fun openLatestPdfReport(context: Context) {
    val items = getReportsDirectoryFiles(context)
    if (items.isEmpty()) {
        Toast.makeText(context, "No report available. Please generate a report first.", Toast.LENGTH_SHORT).show()
        return
    }
    openSpecificPdf(context, items.first().file)
}

fun shareLatestPdfReport(context: Context) {
    val items = getReportsDirectoryFiles(context)
    if (items.isEmpty()) {
        Toast.makeText(context, "No report available. Please generate a report first.", Toast.LENGTH_SHORT).show()
        return
    }
    shareSpecificPdf(context, items.first().file)
}

fun openSpecificPdf(context: Context, file: File) {
    try {
        val uri: Uri = FileProvider.getUriForFile(
            context,
            "${context.packageName}.fileprovider",
            file
        )
        val intent = Intent(Intent.ACTION_VIEW).apply {
            setDataAndType(uri, "application/pdf")
            addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)
            addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
        }
        context.startActivity(intent)
    } catch (_: Exception) {
        Toast.makeText(context, "No PDF reader found. Please install a PDF viewer app.", Toast.LENGTH_SHORT).show()
    }
}

fun shareSpecificPdf(context: Context, file: File) {
    try {
        val uri: Uri = FileProvider.getUriForFile(
            context,
            "${context.packageName}.fileprovider",
            file
        )
        val intent = Intent(Intent.ACTION_SEND).apply {
            type = "application/pdf"
            putExtra(Intent.EXTRA_STREAM, uri)
            addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)
        }
        context.startActivity(Intent.createChooser(intent, "Share Vital Report via"))
    } catch (_: Exception) {
        Toast.makeText(context, "Unable to share PDF.", Toast.LENGTH_SHORT).show()
    }
}

// =====================================================================
// CONTINUOUS DEMO ECG WAVEFORM GENERATOR (SLOW, NATURAL PACE)
// =====================================================================
fun generateDemoWaveform(tick: Long): List<Float> {
    val list = mutableListOf<Float>()
    for (i in 0 until 140) {
        // Controlled, natural heartbeat progression (~72 BPM)
        val phase = (((tick * 0.8f) + (i * 0.7f)) % 100) / 100.0f
        var valOffset = 0f

        if (phase in 0.12f..0.22f) {
            valOffset = (1.8f * sin(((phase - 0.12f) / 0.10f) * PI)).toFloat()
        } else if (phase in 0.27f..0.29f) {
            valOffset = (-2.0f * sin(((phase - 0.27f) / 0.02f) * PI)).toFloat()
        } else if (phase in 0.29f..0.32f) {
            valOffset = (22.0f * sin(((phase - 0.29f) / 0.03f) * PI)).toFloat()
        } else if (phase in 0.32f..0.35f) {
            valOffset = (-5.5f * sin(((phase - 0.32f) / 0.03f) * PI)).toFloat()
        } else if (phase in 0.45f..0.60f) {
            valOffset = (4.5f * sin(((phase - 0.45f) / 0.15f) * PI)).toFloat()
        }

        list.add(2048f + (valOffset * 28f))
    }
    return list
}
