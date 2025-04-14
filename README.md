# 🏠 ESP32-C3 Smart Home System (Light Control + Intruder Alert)

This is a dual-function smart home system built on **one ESP32-C3 device**, using **ESP RainMaker**. It combines:

1. 💡 Smart Light Control
2. 🚨 Intruder Alert System

The project supports **cloud-based OTA firmware updates** and **Google Home voice control** via ESP RainMaker integration.

---

## ⚙️ Features

### 💡 Smart Light Control
- Control a light (GPIO 2) using the `"Light"` switch in the RainMaker app
- Real-time status sync with cloud and Google Home
- Voice command supported via Google Assistant

### 🚨 Intruder Alert System
- Detects motion using an **IR sensor** on GPIO 3 (active low)
- When triggered:
  - Turns on alert LED (GPIO 4)
  - Activates buzzer (GPIO 5) with repeated beeps using PWM
  - Pushes notification to RainMaker app: `"Intruder Alert 🚨"`
- Reset alert via `"Intruder Alert Reset"` switch in the app or with Google Assistant

### 🌐 Cloud & Voice Integration
- OTA firmware updates supported via ESP RainMaker
- Google Home voice control:
  - "Hey Google, turn on the light"
  - "Hey Google, turn on intruder alert reset"

---

## 🔌 Hardware Overview

| Component        | GPIO | Function                |
|------------------|------|-------------------------|
| Light LED        | 2    | Controlled via app/voice|
| IR Sensor        | 3    | Intruder detection      |
| Alert LED        | 4    | Indicates intrusion     |
| Buzzer (PWM)     | 5    | Audible alarm           |

---

## 📲 Device Structure in RainMaker

This single device exposes **two virtual switches**:

| RainMaker Device         | Description                           |
|--------------------------|---------------------------------------|
| `"Light"`                | Controls GPIO 2 (LED)                 |
| `"Intruder Alert Reset"` | Resets alert (turns off buzzer/LED)  |

> Push notification is triggered automatically on motion detection.

---

## 🚀 Usage Flow

1. Light can be toggled anytime via app or voice.
2. When motion is detected:
   - IR sensor goes LOW
   - LED + Buzzer activate
   - Notification pushed to app
3. User resets alert by toggling `"Intruder Alert Reset"` OFF
4. System returns to idle and monitors for next motion

---

## 🛠 Build Instructions

```bash
idf.py set-target esp32c3
idf.py build
idf.py -p /dev/ttyUSBx flash monitor