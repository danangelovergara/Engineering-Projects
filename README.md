# Dan Angelo Vergara | Computer Engineering Portfolio
**Hardware-Software Integration | Digital Signal Processing | Embedded Systems**

Welcome to my engineering project hub. This repository serves as a documented record of my design, prototyping, and testing work, ranging from real-time computer vision systems to analog circuit analysis.

---

## 🛠 Core Technical Stack
**Languages:** C++, Python, MATLAB, Embedded C (Arduino)

# 📂 Project Directory

### 1. [Autonomous Object Tracking Turret](#turret)
**C++ | OpenCV | Win32 Serial API | Trigonometric Modeling**
A 2-axis tracking system that uses computer vision to identify targets and translates coordinates into physical motion with parallax compensation.

### 2. [Integrated Digital Audio System (Music Box)](#music-box)
**Embedded C | I2C & UART | Hardware Power Management**
A custom audio player featuring real-time LCD feedback, hardware-level volume control, and power stabilization via decoupling capacitors.

---

<a name="turret"></a>
## 🎯 Project 1: Autonomous Tracking Turret
* **The Engineering Challenge:** Synchronizing high-level C++ logic with physical actuators while compensating for camera-to-laser axis offset.
* **Key Feature:** Implemented **parallax error compensation** logic using `atan2f()` to ensure accuracy across variable distances.
* **Status:** [Link to Code/Documentation] | [Watch Demo Video]

<a name="music-box"></a>
## 🎵 Project 2: Integrated Digital Audio System
* **The Engineering Challenge:** Managing multiple communication protocols (I2C for LCD, UART for MP3) while ensuring power stability.
* **Hardware Safety:** Integrated a **1000µF capacitor** for voltage smoothing and a **1000Ω resistor** for serial line protection.
* **Status:** [Link to Code/Documentation] | [View Bill of Materials (BOM)]
