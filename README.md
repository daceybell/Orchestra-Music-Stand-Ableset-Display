# Orchestra Music Stand AbleSet Display

An ESP32-C6 stage display for live playback telemetry. It connects to 2.4 GHz Wi-Fi networks, subscribes to AbleSet's OSC feed over UDP, and renders the current song title (2-line word wrapped) and current measure number in real time.

---

Designed and developed by **Dacey Bell** for the First West Orchestra in West Monroe, Louisiana.
* **Website:** [firstwest.cc](https://firstwest.cc/)
* **Website:** [daceybell.weebly.com](https://daceybell.weebly.com/)
* **Contact:** [daceybell@duck.com](mailto:daceybell@duck.com)
---
### Hardware & Specifications
* Microcontroller: Waveshare ESP32-C6 LCD 1.47" (ST7789, 172x320 IPS)
* Purchase Link: [Amazon (US)](https://a.co/d/08C632xC)
* Target Board: `ESP32-C6 Dev Module`
---
### Power Integration & Hardware Assembly
The display is integrated into a Vekkia music stand light:
* Power is tapped directly from the **5V rail after the light's power switch**.
* Turning on the music stand light powers up the ESP32-C6 automatically.
* Upon boot, the board scans for known Wi-Fi networks, connects, acquires the AbleSet OSC feed, and begins displaying telemetry.
---
### Dependencies & Requirements
### Board Package
* Espressif ESP32 Board Package (`v2.x` or `v3.x`)
### Required Arduino Libraries
Install these via the Arduino Library Manager (`Ctrl+Shift+I` / `Cmd+Shift+I`):
1. **Arduino_GFX_Library** (by *Moon On Our Nation*) — Screen driver & SPI bus communication.
2. **Adafruit GFX Library** (by *Adafruit*) — Base graphics primitives and font headers (`FreeSansBold`).

*(Note: Native OSC parsing is embedded directly in the project code—no external OSC library is required.)*

---
### Network & Failover Behavior

* **Multi-Network Support:** Configured for automatic roaming across multiple Wi-Fi networks (SSIDs, passwords, and static AbleSet IPs).
* **Automatic Connection:** Upon boot, the board scans for known networks and connects to the first available match in your configuration list.
* **AbleSet Hunt & Fallback (30s Timeout):** If Wi-Fi connects successfully but AbleSet OSC telemetry is not detected within **30 seconds**, the display disconnects and moves to the next configured network in the list to hunt for AbleSet.
* **Offline Recovery Mode:** If no configured networks are reachable after scanning and cycling through the entire network list **twice**, the display enters an offline recovery state and presents a **"Retry"** screen.
* **Manual Reconnect:** Press the physical **`BOOT` button** (GPIO 9) on the ESP32-C6 board at any time to re-trigger a full Wi-Fi scan and reconnect cycle.
  
---
### Configuration & Setup

* Open `user_wifi_setup.h` and configure your network details:
   ```cpp

   static const WifiNetwork WIFI_NETWORKS[] = {
     { "SSID_NAME", "WIFI_PASSWORD", "STATIC_ABLESET_IP" },
   };
