#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <ArduinoJson.h>

// --- WiFi Configuration ---
const char* ssid = "ESP32_CrashLogger";
const char* password = "password123"; // You can change this

// --- MPU6050 Configuration ---
Adafruit_MPU6050 mpu;
#define SCL_PIN 22 // Adjust if your board uses different pins
#define SDA_PIN 21 // Adjust if your board uses different pins

// --- Web Server and WebSocket ---
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

// --- Crash Detection Settings ---
// Adjust these values based on testing for your specific setup
const float CRASH_THRESHOLD_G = 2.0; // Acceleration magnitude in G's to trigger a crash
const int PRE_CRASH_SAMPLES = 250;   // Number of samples to keep in circular buffer
const int MAX_SAMPLES = 500;         // Max total samples (1 second @ 500Hz)
const int SAMPLE_RATE_MS = 2;        // 500Hz
const float SILENCE_THRESHOLD_G = 1.5; // G-force below this is considered "silence"
const int SILENCE_DURATION_MS = 1000; // Stop recording after 1s of silence
const int SILENCE_SAMPLE_COUNT = SILENCE_DURATION_MS / SAMPLE_RATE_MS;

// --- Data Buffering ---
struct SensorData {
    float ax, ay, az;
    float gx, gy, gz;
};

// Global buffers (Heap allocation) to avoid stack overflow
SensorData preCrashBuffer[PRE_CRASH_SAMPLES]; // Circular buffer for pre-impact data
SensorData recordingBuffer[MAX_SAMPLES];      // Linear buffer for the full crash event
int preCrashIndex = 0;
int recordingIndex = 0;
int silenceCounter = 0;
bool preCrashFull = false;

// --- State Management ---
enum AppState {
    IDLE,
    ARMED,
    RECORDING,  // New state for active recording
    PROCESSING  // State while sending data
};
AppState currentState = IDLE;

// --- Function Prototypes ---
void handleWebSocketEvent(AsyncWebSocket* server, AsyncWebSocketClient* client, AwsEventType type, void* arg, uint8_t* data, size_t len);
void notifyClients(String message, String type);
void readMPUData(SensorData& currentData);
void sendCrashDataToClients(SensorData* crashData, int numSamples);
void printCrashDataToSerial(SensorData* crashData, int numSamples);

// --- Embedded Web Content ---
const char* HTML_CONTENT = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>ESP32 Crash Logger</title>
    <style>
        body {
            font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, 'Helvetica Neue', Arial, sans-serif;
            background-color: #121212;
            color: #e0e0e0;
            margin: 0;
            padding: 20px;
            display: flex;
            justify-content: center;
            align-items: center;
            min-height: 100vh;
            box-sizing: border-box;
        }

        .container {
            width: 90%;
            max-width: 800px;
            text-align: center;
        }

        h1 {
            color: #ffffff;
            border-bottom: 2px solid #444;
            padding-bottom: 10px;
            margin-bottom: 20px;
        }

        .status-container {
            background-color: #1e1e1e;
            border-radius: 8px;
            padding: 15px;
            margin-bottom: 20px;
            font-size: 1.2em;
            box-shadow: 0 4px 8px rgba(0, 0, 0, 0.2);
        }

        #status {
            font-weight: bold;
            color: #f39c12;
        }

        .controls button {
            background-color: #3498db;
            color: white;
            border: none;
            padding: 12px 24px;
            font-size: 1em;
            border-radius: 5px;
            cursor: pointer;
            transition: background-color 0.3s, transform 0.1s;
            margin: 5px;
        }

        .controls button:hover {
            background-color: #2980b9;
        }

        .controls button:active {
            transform: scale(0.98);
        }

        .controls button:disabled {
            background-color: #555;
            cursor: not-allowed;
        }

        .chart-container {
            background-color: #1e1e1e;
            padding: 20px;
            border-radius: 8px;
            box-shadow: 0 4px 8px rgba(0, 0, 0, 0.2);
            margin-top: 20px;
        }

        .stats-container {
            background: linear-gradient(135deg, #1e1e1e 0%, #2a2a2a 100%);
            padding: 20px;
            border-radius: 8px;
            box-shadow: 0 4px 12px rgba(0, 0, 0, 0.3);
            margin-top: 20px;
            display: none;
        }

        .stats-container.visible {
            display: block;
            animation: slideIn 0.5s ease-out;
        }

        @keyframes slideIn {
            from {
                opacity: 0;
                transform: translateY(-20px);
            }
            to {
                opacity: 1;
                transform: translateY(0);
            }
        }

        .stats-header {
            font-size: 1.5em;
            font-weight: bold;
            margin-bottom: 15px;
            text-align: center;
            color: #ff6b6b;
        }

        .severity-badge {
            display: inline-block;
            padding: 10px 20px;
            border-radius: 20px;
            font-size: 1.2em;
            font-weight: bold;
            margin: 10px 0;
            text-align: center;
        }

        .stats-grid {
            display: grid;
            grid-template-columns: repeat(auto-fit, minmax(150px, 1fr));
            gap: 15px;
            margin-top: 15px;
        }

        .stat-item {
            background-color: #2a2a2a;
            padding: 15px;
            border-radius: 8px;
            text-align: center;
            border: 2px solid #444;
        }

        .stat-label {
            font-size: 0.9em;
            color: #aaa;
            margin-bottom: 5px;
        }

        .stat-value {
            font-size: 1.8em;
            font-weight: bold;
            color: #fff;
        }

        .stat-unit {
            font-size: 0.8em;
            color: #888;
        }
    </style>
</head>
<body>
    <div class="container">
        <h1>ESP32 Crash Logger</h1>
        <div class="status-container">
            <p>Status: <span id="status">Connecting...</span></p>
        </div>
        <div class="controls">
            <button id="startButton">Start Recording</button>
            <button id="stopButton" disabled>Stop Recording</button>
        </div>
        <div id="statsContainer" class="stats-container">
            <div class="stats-header">💥 CRASH DETECTED! 💥</div>
            <div style="text-align: center;">
                <div id="severityBadge" class="severity-badge"></div>
            </div>
            <div class="stats-grid">
                <div class="stat-item">
                    <div class="stat-label">Peak G-Force</div>
                    <div class="stat-value" id="peakG">0.0</div>
                    <div class="stat-unit">G's</div>
                </div>
                <div class="stat-item">
                    <div class="stat-label">Crash Score</div>
                    <div class="stat-value" id="crashScore">0</div>
                    <div class="stat-unit">points</div>
                </div>
                <div class="stat-item">
                    <div class="stat-label">Impact Duration</div>
                    <div class="stat-value" id="impactDuration">0</div>
                    <div class="stat-unit">ms</div>
                </div>
                <div class="stat-item">
                    <div class="stat-label">Total Samples</div>
                    <div class="stat-value" id="totalSamples">0</div>
                    <div class="stat-unit">readings</div>
                </div>
            </div>
        </div>
        <div class="chart-container">
            <canvas id="crashChart"></canvas>
        </div>
    </div>
    <script>
        document.addEventListener('DOMContentLoaded', () => {
            const statusSpan = document.getElementById('status');
            const startButton = document.getElementById('startButton');
            const stopButton = document.getElementById('stopButton');
            const crashChartCanvas = document.getElementById('crashChart');

            let ws;

            function connectWebSocket() {
                // Determine WebSocket URL based on current host
                const wsProtocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
                ws = new WebSocket(`${wsProtocol}//${window.location.host}/ws`);

                ws.onopen = () => {
                    console.log('WebSocket connected');
                    statusSpan.textContent = 'Connected, awaiting commands.';
                    startButton.disabled = false;
                    stopButton.disabled = true;
                };

                ws.onmessage = (event) => {
                    const message = JSON.parse(event.data);
                    console.log('Message from ESP32:', message);

                    if (message.type === 'status') {
                        statusSpan.textContent = message.data;
                        if (message.data.includes('Armed')) {
                            startButton.disabled = true;
                            stopButton.disabled = false;
                        } else if (message.data.includes('Idle') || message.data.includes('Connected')) {
                            startButton.disabled = false;
                            stopButton.disabled = true;
                            // Don't hide stats here so they persist after a crash
                        }
                    } else if (message.type === 'crashData') {
                        calculateAndDisplayStats(message.data);
                        drawSimpleChart(crashChartCanvas, message.data);
                        startButton.disabled = false; // Re-enable after crash data is displayed
                        stopButton.disabled = true;
                    }
                };

                ws.onclose = () => {
                    console.log('WebSocket disconnected, attempting to reconnect...');
                    statusSpan.textContent = 'Disconnected, reconnecting...';
                    startButton.disabled = true;
                    stopButton.disabled = true;
                    setTimeout(connectWebSocket, 3000); // Attempt to reconnect every 3 seconds
                };

                ws.onerror = (error) => {
                    console.error('WebSocket Error:', error);
                    statusSpan.textContent = 'Connection error.';
                    ws.close();
                };
            }

            function sendCommand(command) {
                if (ws && ws.readyState === WebSocket.OPEN) {
                    ws.send(command);
                } else {
                    console.warn('WebSocket not open. Cannot send command:', command);
                    statusSpan.textContent = 'Not connected. Please refresh.';
                }
            }

            startButton.addEventListener('click', () => {
                sendCommand('START');
                document.getElementById('statsContainer').classList.remove('visible');
            });
            stopButton.addEventListener('click', () => sendCommand('STOP'));

            function calculateAndDisplayStats(data) {
                // Calculate peak G-force from acceleration data
                let peakG = 0;
                let highGCount = 0;
                const GRAVITY = 9.80665; // m/s^2
                const HIGH_G_THRESHOLD = 2.0; // G's
                
                for (let i = 0; i < data.ax.length; i++) {
                    const totalAccel = Math.sqrt(
                        data.ax[i] * data.ax[i] +
                        data.ay[i] * data.ay[i] +
                        data.az[i] * data.az[i]
                    );
                    const gForce = totalAccel / GRAVITY;
                    if (gForce > peakG) peakG = gForce;
                    if (gForce > HIGH_G_THRESHOLD) highGCount++;
                }
                
                // Calculate impact duration (samples above threshold * sample rate)
                const SAMPLE_RATE_MS = 2;
                const impactDuration = highGCount * SAMPLE_RATE_MS;
                
                // Calculate crash score (peak G * duration factor)
                const crashScore = Math.round(peakG * 100 + impactDuration);
                
                // Determine severity rating
                let severity, severityColor, severityEmoji;
                if (peakG < 3) {
                    severity = "Gentle Bump";
                    severityColor = "#4CAF50";
                    severityEmoji = "😊";
                } else if (peakG < 5) {
                    severity = "Fender Bender";
                    severityColor = "#FFC107";
                    severityEmoji = "😬";
                } else if (peakG < 8) {
                    severity = "Ouch!";
                    severityColor = "#FF9800";
                    severityEmoji = "😵";
                } else if (peakG < 12) {
                    severity = "Serious Impact!";
                    severityColor = "#FF5722";
                    severityEmoji = "🤕";
                } else if (peakG < 20) {
                    severity = "CATASTROPHIC!";
                    severityColor = "#F44336";
                    severityEmoji = "💀";
                } else {
                    severity = "OBLITERATED!!!";
                    severityColor = "#9C27B0";
                    severityEmoji = "💥";
                }
                
                // Update the UI
                document.getElementById('peakG').textContent = peakG.toFixed(2);
                document.getElementById('crashScore').textContent = crashScore;
                document.getElementById('impactDuration').textContent = impactDuration;
                document.getElementById('totalSamples').textContent = data.ax.length;
                
                const severityBadge = document.getElementById('severityBadge');
                severityBadge.textContent = `${severityEmoji} ${severity} ${severityEmoji}`;
                severityBadge.style.backgroundColor = severityColor;
                severityBadge.style.color = '#fff';
                
                // Show the stats container with animation
                document.getElementById('statsContainer').classList.add('visible');
            }

            function drawSimpleChart(canvas, data) {
                 // Set canvas internal dimensions to match display size for sharp rendering
                canvas.width = canvas.clientWidth;
                canvas.height = canvas.clientHeight;
                
                const ctx = canvas.getContext('2d');
                const width = canvas.width;
                const height = canvas.height;
                ctx.clearRect(0, 0, width, height);

                // Find min/max for scaling
                let allValues = [...data.ax, ...data.ay, ...data.az];
                let minVal = Math.min(...allValues);
                let maxVal = Math.max(...allValues);
                
                // Add padding and ensure non-zero range
                let range = maxVal - minVal;
                if (range === 0) range = 1;
                minVal -= range * 0.1;
                maxVal += range * 0.1;
                
                // Recalculate range after padding
                range = maxVal - minVal;

                function mapY(val) {
                    return height - ((val - minVal) / range * height);
                }
                
                function mapX(i, total) {
                    return (i / (total - 1)) * width;
                }

                // Draw Background Grid
                ctx.strokeStyle = '#333';
                ctx.lineWidth = 1;
                ctx.beginPath();
                // Horizontal lines
                for(let i=0; i<=4; i++) {
                     let y = height * (i/4);
                     ctx.moveTo(0, y);
                     ctx.lineTo(width, y);
                }
                ctx.stroke();

                // Draw Zero Line if visible
                if (minVal < 0 && maxVal > 0) {
                    ctx.strokeStyle = '#666';
                    ctx.beginPath();
                    let zeroY = mapY(0);
                    ctx.moveTo(0, zeroY);
                    ctx.lineTo(width, zeroY);
                    ctx.stroke();
                }

                function plotLine(dataset, color) {
                    if (!dataset || dataset.length === 0) return;
                    ctx.strokeStyle = color;
                    ctx.lineWidth = 2;
                    ctx.beginPath();
                    ctx.moveTo(mapX(0, dataset.length), mapY(dataset[0]));
                    for (let i = 1; i < dataset.length; i++) {
                        ctx.lineTo(mapX(i, dataset.length), mapY(dataset[i]));
                    }
                    ctx.stroke();
                }

                plotLine(data.ax, 'rgb(255, 99, 132)'); // X - Red
                plotLine(data.ay, 'rgb(54, 162, 235)'); // Y - Blue
                plotLine(data.az, 'rgb(75, 192, 192)'); // Z - Green
                
                // Legend
                ctx.font = 'bold 14px sans-serif';
                ctx.fillStyle = 'rgb(255, 99, 132)'; ctx.fillText('X', 20, 30);
                ctx.fillStyle = 'rgb(54, 162, 235)'; ctx.fillText('Y', 50, 30);
                ctx.fillStyle = 'rgb(75, 192, 192)'; ctx.fillText('Z', 80, 30);
                
                // Y-Axis Labels (Min/Max)
                ctx.fillStyle = '#aaa';
                ctx.font = '12px sans-serif';
                ctx.fillText(maxVal.toFixed(1), 5, 15);
                ctx.fillText(minVal.toFixed(1), 5, height - 5);
            }

            connectWebSocket(); // Initial WebSocket connection
        });
    </script>
</body>
</html>
)rawliteral";

void setup() {
    Serial.begin(115200);

    // --- MPU6050 Setup ---
    Wire.begin(SDA_PIN, SCL_PIN); // Initialize I2C communication with custom pins
    Serial.println("Initializing MPU6050...");
    if (!mpu.begin()) {
        Serial.println("Failed to find MPU6050 chip");
        while (1) {
            delay(10);
        }
    }
    Serial.println("MPU6050 Found!");

    mpu.setAccelerometerRange(MPU6050_RANGE_16_G);
    Serial.print("Accelerometer range set to: ");
    switch (mpu.getAccelerometerRange()) {
        case MPU6050_RANGE_2_G: Serial.println("+-2G"); break;
        case MPU6050_RANGE_4_G: Serial.println("+-4G"); break;
        case MPU6050_RANGE_8_G: Serial.println("+-8G"); break;
        case MPU6050_RANGE_16_G: Serial.println("+-16G"); break;
    }
    mpu.setGyroRange(MPU6050_RANGE_500_DEG);
    Serial.print("Gyro range set to: ");
    switch (mpu.getGyroRange()) {
        case MPU6050_RANGE_250_DEG: Serial.println("+-250 deg/s"); break;
        case MPU6050_RANGE_500_DEG: Serial.println("+-500 deg/s"); break;
        case MPU6050_RANGE_1000_DEG: Serial.println("+-1000 deg/s"); break;
        case MPU6050_RANGE_2000_DEG: Serial.println("+-2000 deg/s"); break;
    }

    mpu.setFilterBandwidth(MPU6050_BAND_260_HZ);
    Serial.print("Filter bandwidth set to: ");
    switch (mpu.getFilterBandwidth()) {
        case MPU6050_BAND_260_HZ: Serial.println("260 Hz"); break;
        case MPU6050_BAND_184_HZ: Serial.println("184 Hz"); break;
        case MPU6050_BAND_94_HZ: Serial.println("94 Hz"); break;
        case MPU6050_BAND_44_HZ: Serial.println("44 Hz"); break;
        case MPU6050_BAND_21_HZ: Serial.println("21 Hz"); break;
        case MPU6050_BAND_10_HZ: Serial.println("10 Hz"); break;
        case MPU6050_BAND_5_HZ: Serial.println("5 Hz"); break;
    }
    Serial.println("");
    delay(100);

    // --- WiFi Access Point Setup ---
    Serial.print("Setting up AP (Access Point)...");
    WiFi.softAP(ssid, password);
    IPAddress IP = WiFi.softAPIP();
    Serial.print("AP IP address: ");
    Serial.println(IP);

    // --- Web Server Setup ---
    ws.onEvent(handleWebSocketEvent);
    server.addHandler(&ws);

    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
        request->send_P(200, "text/html", HTML_CONTENT);
    });

    server.onNotFound([](AsyncWebServerRequest *request){
        request->send(404, "text/plain", "Not Found");
    });

    server.begin();
    Serial.println("HTTP server started");
    notifyClients("Idle. Connect to WiFi: ESP32_CrashLogger", "status");
}

void loop() {
    static unsigned long lastSampleTime = 0;
    unsigned long currentTime = millis();

    // Handle Serial Commands (USB)
    if (Serial.available() > 0) {
        String command = Serial.readStringUntil('\n');
        command.trim();
        command.toUpperCase();
        
        if (command == "START") {
            if (currentState != ARMED) {
                currentState = ARMED;
                preCrashIndex = 0;
                preCrashFull = false;
                Serial.println(">>> System Armed via Serial! Monitoring for crashes...");
                notifyClients("Armed. Monitoring for crashes.", "status");
            } else {
                Serial.println(">>> System already armed.");
            }
        } else if (command == "STOP") {
            if (currentState == ARMED) {
                currentState = IDLE;
                Serial.println(">>> System Disarmed via Serial.");
                notifyClients("Idle. Ready to arm.", "status");
            } else {
                Serial.println(">>> System already idle.");
            }
        } else if (command == "STATUS") {
            Serial.print(">>> Current State: ");
            if (currentState == IDLE) Serial.println("IDLE");
            else if (currentState == ARMED) Serial.println("ARMED");
            else if (currentState == RECORDING) Serial.println("RECORDING");
        } else if (command == "HELP") {
            Serial.println(">>> Available Commands:");
            Serial.println("    START  - Arm the crash detection system");
            Serial.println("    STOP   - Disarm the system");
            Serial.println("    STATUS - Show current system state");
            Serial.println("    HELP   - Show this help message");
        } else if (command.length() > 0) {
            Serial.println(">>> Unknown command. Type HELP for available commands.");
        }
    }

    if (currentTime - lastSampleTime >= SAMPLE_RATE_MS) {
        lastSampleTime = currentTime;
        SensorData currentReading;
        readMPUData(currentReading);

        if (currentState == ARMED) {
            // Fill circular pre-crash buffer
            preCrashBuffer[preCrashIndex] = currentReading;
            preCrashIndex = (preCrashIndex + 1) % PRE_CRASH_SAMPLES;
            if (preCrashIndex == 0) preCrashFull = true;

            // Check for Crash Trigger
            float totalAccel = sqrt(pow(currentReading.ax, 2) + pow(currentReading.ay, 2) + pow(currentReading.az, 2));
            float totalAccelG = totalAccel / SENSORS_GRAVITY_STANDARD;

            if (totalAccelG >= CRASH_THRESHOLD_G) {
                currentState = RECORDING;
                Serial.println(">>> CRASH STARTED! Recording...");
                notifyClients("CRASH DETECTED! Recording...", "status");

                // Copy pre-crash buffer to main recording buffer
                int startIdx = preCrashFull ? preCrashIndex : 0;
                int count = 0;
                
                // If buffer isn't full yet, only copy what we have (though typically it will be full if armed for >0.5s)
                int itemsToCopy = preCrashFull ? PRE_CRASH_SAMPLES : preCrashIndex;
                
                for (int i = 0; i < itemsToCopy; i++) {
                    recordingBuffer[count++] = preCrashBuffer[(startIdx + i) % PRE_CRASH_SAMPLES];
                }
                
                recordingIndex = count; // Start recording new data after the pre-crash data
                silenceCounter = 0;
                
                // Store the trigger sample too
                if (recordingIndex < MAX_SAMPLES) {
                    recordingBuffer[recordingIndex++] = currentReading;
                }
            }
        } 
        else if (currentState == RECORDING) {
            // Continually record data
            if (recordingIndex < MAX_SAMPLES) {
                recordingBuffer[recordingIndex] = currentReading;
                
                // Silence Detection
                float totalAccel = sqrt(pow(currentReading.ax, 2) + pow(currentReading.ay, 2) + pow(currentReading.az, 2));
                float totalAccelG = totalAccel / SENSORS_GRAVITY_STANDARD;

                // Reset silence counter if we see significant movement
                if (totalAccelG > SILENCE_THRESHOLD_G) {
                    silenceCounter = 0;
                } else {
                    silenceCounter++;
                }

                recordingIndex++;
                
                // Stop Logic: Buffer Full OR Long Silence
                bool stopRecording = false;
                if (recordingIndex >= MAX_SAMPLES) {
                    Serial.println(">>> Buffer Full! stopping.");
                    stopRecording = true;
                } else if (silenceCounter >= SILENCE_SAMPLE_COUNT) {
                    Serial.println(">>> Silence detected! stopping.");
                    stopRecording = true;
                }

                if (stopRecording) {
                    currentState = PROCESSING;
                    notifyClients("Processing Data...", "status");
                    Serial.println(">>> Sending Data...");
                    
                    sendCrashDataToClients(recordingBuffer, recordingIndex);
                    printCrashDataToSerial(recordingBuffer, recordingIndex);
                    
                    currentState = IDLE;
                    notifyClients("Idle. Ready to arm.", "status");
                    Serial.println(">>> System reset to IDLE.");
                }
            }
        }
    }
    ws.cleanupClients();
}

void handleWebSocketEvent(AsyncWebSocket* server, AsyncWebSocketClient* client, AwsEventType type, void* arg, uint8_t* data, size_t len) {
    if (type == WS_EVT_CONNECT) {
        Serial.printf("WebSocket client #%u connected from %s\n", client->id(), client->remoteIP().toString().c_str());
        notifyClients(String(currentState == ARMED ? "Armed. Monitoring for crashes." : "Idle. Ready to arm."), "status");
    } else if (type == WS_EVT_DISCONNECT) {
        Serial.printf("WebSocket client #%u disconnected\n", client->id());
    } else if (type == WS_EVT_DATA) {
        AwsFrameInfo* info = (AwsFrameInfo*)arg;
        if (info->final && info->index == 0 && info->len == len) {
            // This is a complete message in a single frame
            if (info->opcode == WS_TEXT) {
                // Null-terminate the data
                data[len] = 0;
                String msg = String((char*)data);
                msg.trim(); // Remove any whitespace
                Serial.printf("Received: %s\n", msg.c_str());
                
                if (msg == "START") {
                    if (currentState != ARMED) {
                        currentState = ARMED;
                        preCrashIndex = 0; // Reset buffer on arming
                        preCrashFull = false;
                        Serial.println("System Armed!");
                        notifyClients("Armed. Monitoring for crashes.", "status");
                    } else {
                        Serial.println("Already armed, ignoring duplicate START");
                    }
                } else if (msg == "STOP") {
                    if (currentState == ARMED) {
                        currentState = IDLE;
                        Serial.println("System Disarmed.");
                        notifyClients("Idle. Ready to arm.", "status");
                    } else {
                        Serial.println("Already idle, ignoring duplicate STOP");
                    }
                }
            }
        }
    }
}

void notifyClients(String message, String type) {
    if (ws.count() > 0) {
        DynamicJsonDocument doc(256);
        doc["type"] = type;
        doc["data"] = message;
        String jsonMessage;
        serializeJson(doc, jsonMessage);
        ws.textAll(jsonMessage);
    }
}

void readMPUData(SensorData& currentData) {
    sensors_event_t a, g, temp;
    mpu.getEvent(&a, &g, &temp);

    currentData.ax = a.acceleration.x;
    currentData.ay = a.acceleration.y;
    currentData.az = a.acceleration.z;
    currentData.gx = g.gyro.x;
    currentData.gy = g.gyro.y;
    currentData.gz = g.gyro.z;
}

void sendCrashDataToClients(SensorData* crashData, int numSamples) {
    if (ws.count() == 0) return;
    // Optimized payload: Round to 2 decimal places to save ~50% message size
    // 500 samples * 3 axes * ~6 chars = ~9KB. Very safe.
    DynamicJsonDocument doc(32000); 
    doc["type"] = "crashData";
    JsonObject dataObj = doc.createNestedObject("data");

    JsonArray ax_array = dataObj.createNestedArray("ax");
    JsonArray ay_array = dataObj.createNestedArray("ay");
    JsonArray az_array = dataObj.createNestedArray("az");

    for (int i = 0; i < numSamples; ++i) {
        // Rounding to 2 decimal places reduces JSON string length significantly
        ax_array.add(round(crashData[i].ax * 100.0) / 100.0);
        ay_array.add(round(crashData[i].ay * 100.0) / 100.0);
        az_array.add(round(crashData[i].az * 100.0) / 100.0);
    }

    String jsonMessage;
    serializeJson(doc, jsonMessage);
    
    Serial.print(">>> JSON Generated. Size: ");
    Serial.print(jsonMessage.length());
    Serial.println(" bytes.");
    
    ws.textAll(jsonMessage);
    Serial.println(">>> Data sent to clients.");
}

void printCrashDataToSerial(SensorData* crashData, int numSamples) {
    Serial.println("\n========== CRASH DATA ==========");
    Serial.print("Total samples: ");
    Serial.println(numSamples);
    Serial.println("\nSample | Accel X | Accel Y | Accel Z | Gyro X | Gyro Y | Gyro Z");
    Serial.println("-------|---------|---------|---------|--------|--------|--------");
    
    for (int i = 0; i < numSamples; i++) {
        char buffer[100];
        sprintf(buffer, "%6d | %7.2f | %7.2f | %7.2f | %6.2f | %6.2f | %6.2f",
                i,
                crashData[i].ax,
                crashData[i].ay,
                crashData[i].az,
                crashData[i].gx,
                crashData[i].gy,
                crashData[i].gz);
        Serial.println(buffer);
        
        // Mark the crash point (between pre and post samples)
        if (i == PRE_CRASH_SAMPLES - 1) {
            Serial.println("------- CRASH TRIGGER -------");
        }
    }
    Serial.println("================================\n");
}
