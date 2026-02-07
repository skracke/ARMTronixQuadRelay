ESP32 Quad Relay Controller (Home Assistant Ready)

A robust and secure firmware for ESP32-based relay boards (optimized for the Armtronix Quad Relay Board). This project enables control of four relays via MQTT with full support for Home Assistant Discovery and secure TLS encryption (mTLS).

✨ Features

MQTT with TLS/mTLS: Support for CA certificates, client certificates, and private keys for secure communication.

Home Assistant Discovery: Relays automatically appear in Home Assistant as Switch, Light, Cover (Garage), Lock, Valve, or Fan.

WiFiManager: Easy configuration via a built-in access point interface. No hard-coding of WiFi credentials required.

Dynamic Device Classes: Configure each relay individually (e.g., Relay 1 as a light, Relay 2 as a garage door).

Physical Reset/Portal Trigger: Hold the physical button (GPIO 0) for 3 seconds to manually start the configuration portal.

Secure Storage: Certificates and settings are stored in SPIFFS (internal flash) and are protected from accidental overwrites.

🛠 Hardware Requirements

Processor: ESP32 (DevKit or Armtronix Quad Relay Board).

Relay Pins: GPIO 14, 13, 12, 4 (Armtronix standard).

Config Button: GPIO 0.

🚀 Installation

Install the following libraries in your Arduino IDE:

PubSubClient

WiFiManager

ArduinoJson (v7 or later)

Upload the code to your ESP32.

On first boot (or by holding the S1 button):

Connect to the WiFi network created by the device (e.g., QuadRelay_XXXXXX).

Navigate to 192.168.4.1 in your web browser.

Click Configure WiFi.

Enter your WiFi credentials, MQTT server address, and paste your certificates.

Save and reboot.

🏠 Home Assistant Integration

Thanks to MQTT Discovery, no YAML configuration is needed. The device automatically creates entities based on the Device Class selected in the portal:

switch / outlet / light: Standard ON/OFF logic.

garage / cover: Supports OPEN/CLOSE commands and reports state as open/closed.

lock: Supports LOCK/UNLOCK commands and reports state as LOCKED/UNLOCKED.

valve: Optimized for water or gas valves.

🔒 Security & Certificates

In the configuration portal, you can paste your PEM-formatted certificates.

Validation: The firmware requires at least 100 characters to validate that a new certificate has actually been entered before overwriting existing data.

Clearing Data: A dedicated checkbox is available if you need to wipe all stored certificates.

Fallback: If no certificates are provided, the device uses setInsecure() (recommended for testing only).

📄 License

This project is open-source. See the LICENSE file for more information.
