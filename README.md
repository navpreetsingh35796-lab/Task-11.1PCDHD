# Smart Assisted Living & Elderly Care System (Dual-Node IoT)

A dual-node IoT prototype built for **SIT210 (Embedded Systems Development – Task 11.1P)** that helps elderly residents live safely and independently at home while giving caregivers real-time monitoring and remote control over a local MQTT network.

---

## System Overview

The system splits edge automation and caregiver monitoring across two communicating nodes over a 2.4 GHz local Wi-Fi network:

1. **Node 1 — Edge Sensing & Actuation (`Arduino Nano 33 IoT`):** Reads room sensors, runs low-latency local automation (night-light hysteresis, bed-exit proximity detection, plant watering alerts, and an emergency alarm latch), and publishes JSON telemetry every `1.5 seconds`.
2. **Node 2 — Caregiver Hub (`Raspberry Pi 4`):** Hosts a local **Mosquitto MQTT broker** (`port 1883`) and runs a Python **Tkinter** dashboard (`caregiver_hub.py`) that displays live sensor states, triggers visual emergency alerts, and sends remote override commands back to the edge node.

---

## Hardware & Pin Mapping

| Component | Type | Arduino Nano 33 IoT Pin | Notes |
| :--- | :--- | :--- | :--- |
| **BH1750 Ambient Light Sensor** | I2C Sensor | `SDA` → `A4`, `SCL` → `A5` | Auto night-light ON `< 40 lx`, OFF `> 65 lx` (hysteresis) |
| **PIR Motion Sensor** | Digital Input | `OUT` → `D2` | Detects general room movement |
| **HC-SR04 Ultrasonic Sensor** | Digital I/O | `TRIG` → `D3`, `ECHO` → `D4` | `3.3V` powered; filters `< 2.5 cm`, 2-hit confirmation `< 20 cm` |
| **Soil Moisture Sensor** | Analog Input | `AO` → `A0` | Triggers plant alert when raw reading `> 600` |
| **Emergency Push Button** | Digital Input | `D5` (`INPUT_PULLUP`) | Latching toggle for emergency alert |
| **Pathway Night-Light LED** | Digital Output | `D6` (`220Ω` to `GND`) | Controlled automatically by lux or remotely by caregiver |
| **Plant Moisture Alert LED** | Digital Output | `D7` (`220Ω` to `GND`) | Lights up when soil is dry |
| **Piezo Buzzer** | Digital Output | `D8` | Sounds an audible alert when emergency button is pressed |
| **Emergency Alert LED** | Digital Output | `D9` (`220Ω` to `GND`) | Latches ON until toggled or reset remotely via MQTT |

---

## MQTT Communication Architecture

* **Broker:** Local Mosquitto MQTT Broker running on Raspberry Pi 4 (`port 1883`)
* **Telemetry Topic (`assisted_living/telemetry`):** Published by the Arduino every `1.5s` in JSON format:
  ```json
  {
    "lux": 242.5,
    "dark": false,
    "pathway_led": false,
    "pir": false,
    "dist": 31.2,
    "activity": false,
    "soil": 1018,
    "plant_dry": true,
    "emergency": false
  }
