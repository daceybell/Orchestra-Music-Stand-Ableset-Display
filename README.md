# Orchestra Music Stand Ableset Display
 * ESP32-C6 Music Stand Display | Connect to Ableset | Song Title &amp; Measure/Bar Number Display
 * AbleSet stage display — Waveshare ESP32-C6 LCD 1.47"
-----------------------------------------------------
* Created by Dacey Bell for the First West Orchestra
* West Monroe, Louisiana
* https://daceybell.weebly.com/
* daceybell@duck.com
-----------------------------------------------------
* Connects to a WiFi network (2.4 GHz), subscribes to AbleSet's OSC feed, and shows current song and current measure.
* Board:    ESP32-C6 Dev Module (ST7789, 172x320)
* Tools menu: Board = "ESP32C6 Dev Module", USB CDC On Boot = Enabled
-----------------------------------------------------
### Dependencies
 
* UPDATE: user_wifi_setup.h to add SSID, password and Ableset IP address.
* NOTE: Leave IP add blank "" to auto-connect to Ableset (details in file) // Can have multiple SSIDs, Password & Ableset IP addresses
-----------------------------------------------------
### Hardware / Board Support
 
* ESP32 Board Package (v2.x or v3.x) — `ESP32-C6 Dev Module`
* Waveshare ESP32-C6 Board:
*  US Amazon link: https://a.co/d/08C632xC
-----------------------------------------------------
### Required Libraries

Install via the Arduino Library Manager (`Ctrl+Shift+I` / `Cmd+Shift+I`):
1. **Arduino_GFX_Library** (by Moon On Our Nation)
2. **Adafruit GFX Library** (by Adafruit)
