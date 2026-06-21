# 💊 MediReminder – Your Smart Pill Manager

[![Arduino](https://img.shields.io/badge/Arduino-UNO-blue.svg)](https://www.arduino.cc/)
[![Platform](https://img.shields.io/badge/Platform-Arduino%20Mega-red.svg)]()
[![Display](https://img.shields.io/badge/Display-2.8"%20TFT%20Touch-green.svg)]()
[![License](https://img.shields.io/badge/License-MIT-yellow.svg)]()

> **Never miss a dose again!** A touchscreen medication reminder system built for Arduino Mega and 2.8" TFT Touch Shield.

---

## 🎬 Demo

<p align="center">
  <img src="docs/demo.gif" alt="MediReminder Demo" width="400">
  <br>
  <em>Watch MediReminder in action!</em>
</p>

> **📹 Video Demo:** [Click here to watch the full demo](https://www.youtube.com/watch?v=your-video-link)

---

## ✨ Features

| Feature | Description |
|---------|-------------|
| 💊 **5 Medications** | Add, edit, or delete up to 5 different pills |
| 🎨 **Color Coding** | Assign unique colors to each medication for easy identification |
| ⏰ **Up to 3 Daily Doses** | Set custom times for each medication |
| 📊 **Progress Tracking** | Visual bar shows daily progress for each pill |
| 🔔 **Smart Alarms** | Buzzer alerts exactly when it's time to take your medicine |
| 🗓️ **Jalali Calendar** | Persian (Shamsi) date display for local users |
| 💾 **EEPROM Storage** | All settings survive power cycles |
| 🖱️ **Touch Interface** | Full touch control with intuitive UI |

---

## 📸 Screenshots

### Main Dashboard
<p align="center">
  <img src="docs/screenshots/main_screen.jpg" alt="Main Screen" width="300">
  <br>
  <em>The main dashboard showing all medications and their status</em>
</p>

### Medication Settings
<p align="center">
  <img src="docs/screenshots/med_screen.jpg" alt="Medication Screen" width="300">
  <img src="docs/screenshots/keyboard.jpg" alt="Keyboard" width="300">
  <br>
  <em>Configure medication name and schedule with the built-in keyboard</em>
</p>

### Alarm & Schedule
<p align="center">
  <img src="docs/screenshots/alarm.jpg" alt="Alarm Screen" width="300">
  <img src="docs/screenshots/schedule.jpg" alt="Schedule Screen" width="300">
  <br>
  <em>Alarm notification and dose scheduling interface</em>
</p>

---

## 🛠️ Hardware Requirements

| Component | Quantity | Description |
|-----------|----------|-------------|
| Arduino Mega 2560 | 1 | Main controller |
| 2.8" TFT Touch Shield | 1 | Display + touch interface (MCUFRIEND compatible) |
| DS3231 RTC Module | 1 | Real-time clock for accurate timekeeping |
| Buzzer | 1 | Alarm notification |
| Micro SD Card (optional) | 1 | For storing calibration data |

### Wiring Diagram
<img width="1274" height="623" alt="image" src="https://github.com/user-attachments/assets/285c5a97-408d-4473-9f3a-8c3800d725e8" />
<div align="left">
  <strong>⚠️ Caution:</strong>
</div>

> The wiring diagram shown below is for **visual representation only**. The physical connections in your actual setup may differ depending on your specific hardware configuration.


<blockquote style="border-left: 4px solid #ffc107; background-color: #fff8e1; padding: 10px 15px; margin: 10px 0;">
  <strong>📌 NOTE:</strong> The image above is a schematic illustration created to give you a general idea of how the components are connected. However, if you are using a <strong>2.8" TFT Touch Shield</strong>, it is designed to be stacked directly onto the Arduino Mega (or Uno) via the GPIO headers, so there is <strong>no separate wiring required</strong> for the display itself!
</blockquote>

## 📚 Required Libraries

Install these libraries via Arduino Library Manager or manually:

- `MCUFRIEND_kbv` – TFT display driver
- `Adafruit_GFX` – Graphics library
- `TouchScreen` – Touch input handling
- `RTClib` – RTC DS3231 communication
- `Wire` – I2C communication (built-in)
- `EEPROM` – Data storage (built-in)

---

## 🚀 Installation

### Step 1: Clone the Repository
```bash
git clone https://github.com/your-username/medireminder.git
cd medireminder
