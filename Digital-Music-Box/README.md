# Digital Music Box Project

### **Bill of Materials (BOM)**

| Component | Specific Note | Purpose / Interface |
| :--- | :--- | :--- |
| **Arduino Uno R3** | ATmega328P Microcontroller | Main System Controller; runs Embedded C firmware. |
| **DFPlayer Mini** | With Header Pins | Audio Decoding; communicates via **UART/SoftwareSerial**. |
| **I2C LCD 16x2** | 4-Pin Version (Inclusion of 0x27 module) | System Status Display; utilizes **I2C Protocol**. |
| **Micro SD Card** | 8GB or 16GB (FAT32) | Non-volatile storage for MP3/WAV tracks. |
| **Speaker** | 3W 4-Ohm or 8-Ohm | Audio output transducer. |
| **WS2812B LED Strip** | 10–20 NeoPixels | Visual feedback via individually addressable digital signal. |
| **10kOmega Potentiometer** | Linear Taper | Manual volume control via **Analog-to-Digital Conversion (ADC)**. |
| **Toggle Switch** | KCD1-5Pack Automotive Grade | Primary hardware power disconnect. |
| **Momentary Buttons** | 3x Tactile Switches | Track navigation (Next/Prev) using **GPIO with Pull-ups**. |
| **1000muF Capacitor** | Electrolytic | **Power Stability**: Decouples voltage spikes during audio surges. |
| **1000Omega Resistor** | 1/4 Watt Metal Film | **Signal Integrity**: Current limiting for the DFPlayer RX line. |
| **USB-B Cable** | Modified/Spliced | Custom power interface with integrated hardware switch. |
| **Breadboard & Jumpers** | Half-size (400 pts) | Circuit prototyping and peripheral interconnects. |
| **Enclosure Materials** | Balsa Wood, Foam Board, Dried Moss | Physical housing and "Passages Log" aesthetic decor. |

## 🛠 Technical Challenges & Engineering Solutions

### **1. Power Surge Mitigation**
During initial testing, I identified audible "pops" and system instability when the audio module initiated playback. I resolved this by integrating a **1000muF decoupling capacitor** across the power rails to smooth out voltage spikes caused by the speaker's transient current draw.

### **2. Signal Integrity (UART)**
To protect the DFPlayer Mini and ensure reliable data transmission, I implemented a **1000$\Omega$ resistor** on the RX line. This current-limiting measure ensures the $5\text{V}$ Arduino logic does not damage the $3.3\text{V}$ sensitive audio module.

### **3. Custom Hardware Power Toggle**
Instead of relying on software-level sleep modes, I physically **spliced a USB-B cable** to integrate a manual toggle switch. This provides a hard power-cut for the entire system, demonstrating hands-on experience with cable modification and low-voltage electrical work.

## 📸 Visual Documentation & Proof of Concept

* **System Demonstration:** [Link to Video / YouTube] (Demonstrates I2C display updates, track navigation, and volume scaling via ADC).
* **Hardware Assembly:** [Link to Photo] (Shows the internal circuit layout, including the 1000µF capacitor and spliced USB-B power cable).
* **BOM Documentation:** [Link to Excel/PDF Materials List] (Detailed tracking of all components used).

* ## 🚀 Future Iterations
* **PCB Fabrication:** Transition the current breadboard prototype into a custom **Printed Circuit Board (PCB)** layout using **Altium** or **KiCad** to reduce signal noise and improve portability.
* **Power Management:** Incorporate a dedicated 3.3V/5V regulator circuit for more efficient power distribution.
