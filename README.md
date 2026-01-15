# ESP32 Crash Logger with MPU6050

This project implements a crash detection and logging system using an ESP32 microcontroller and an MPU6050 accelerometer/gyroscope. It hosts a local web server (Access Point mode) that allows you to view real-time status and analyze crash data immediately after an impact.

## Features
- **Crash Detection**: Automatically detects impacts > 1.5G.
- **Data Logging**: Captures 50 samples before and 50 samples after the crash (1 second total window).
- **Web Interface**: View crash statistics, severity ratings, and an interactive chart of the G-forces.
- **Scoring System**: "Gamified" crash score based on impact intensity and duration.

## Hardware Requirements
1.  **ESP32 Development Board**
2.  **MPU6050 Accelerometer/Gyroscope Module**
3.  Jumper Wires

### Wiring
| MPU6050 Pin | ESP32 Pin |
|-------------|-----------|
| VCC         | 3.3V      |
| GND         | GND       |
| SCL         | GPIO 22   |
| SDA         | GPIO 21   |

*(Note: You can change the pin definitions in `Crash_Test_Dummy.ino` if needed)*

## Installation

### 1. Arduino IDE Setup
Ensure you have the **ESP32 Board Manager** installed in Arduino IDE.

### 2. Required Libraries
Install the following libraries via the Arduino Library Manager:
- **Adafruit MPU6050** (and its dependency **Adafruit Unified Sensor**)
- **ESPAsyncWebServer** https://github.com/me-no-dev/ESPAsyncWebServer
- **AsyncTCP** https://github.com/me-no-dev/AsyncTCP
- **ArduinoJson**

*Note: ESPAsyncWebServer and AsyncTCP typically need to be installed manually (download ZIP from GitHub and use "Sketch > Include Library > Add .ZIP Library").*

### 3. Flash the Code
1.  Open `Crash_Test_Dummy.ino`.
2.  Select your ESP32 board and COM port.
3.  Upload the sketch.

## Usage

1.  **Power On**: Connect the ESP32 to power.
2.  **Connect WiFi**: On your phone or laptop, connect to the WiFi network:
    *   **SSID**: `ESP32_CrashLogger`
    *   **Password**: `password123`
3.  **Open Dashboard**: Open a web browser and navigate to:
    *   `http://192.168.4.1`
4.  **Arm the System**: Click **"Start Recording"** (or it may auto-arm).
5.  **CRASH IT!**: Simulate a crash (shake or tap the sensor).
6.  **View Results**: The page will automatically update with the crash score and graph.

## Development
The `web_src` folder contains the separated HTML, CSS, and JS files used to develop the web interface. These are **already embedded** into the `Crash_Test_Dummy.ino` file. If you modify the files in `web_src`, you must manually update the `HTML_CONTENT` string in the `.ino` file to reflect your changes.
