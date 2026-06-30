
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

> 


https://github.com/user-attachments/assets/fb996bd7-2a97-43c6-b0e2-5a1661329b5b

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
   <img width="422" height="324" alt="5906761655272541935 (1)" src="https://github.com/user-attachments/assets/fe775d42-b44c-478c-a66d-57967ff06f94" />
  <br>
  <em>The main dashboard showing all medications and their status</em
</p>

### Medication Settings
<p align="center">
  <img width="422" height="324" alt="5906761655272541936" src="https://github.com/user-attachments/assets/e10c08b0-a153-467c-a703-abac1948e185" />
  <img width="422" height="324" alt="5906761655272541937" src="https://github.com/user-attachments/assets/14093a92-685d-4671-bb49-e10a8b025b9d" />
  <br>
  <em>Configure medication name and schedule with the built-in keyboard</em>
</p>

### Alarm & Schedule
<p align="center">
  <img width="422" height="324" alt="5906761655272541939" src="https://github.com/user-attachments/assets/74a77833-72e7-496e-9e18-cd13dcb3350e" />
  <img width="422" height="324" alt="5906761655272541938" src="https://github.com/user-attachments/assets/f632d59d-841d-4695-a5f7-ca7ebbc6c337" />

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
git clone https://github.com/siinnnaaa/medireminder.git cd medireminder
