<!-- workline
sources: [DomoticsCore-System/include/DomoticsCore/System.h, DomoticsCore-System/include/DomoticsCore/SystemConfig.h, DomoticsCore-System/examples/Minimal/platformio.ini, DomoticsCore-System/examples/Standard/platformio.ini, DomoticsCore-System/examples/FullStack/platformio.ini, DomoticsCore-MQTT/include/DomoticsCore/MQTT.h, DomoticsCore-WebUI/include/DomoticsCore/WebUI.h, DomoticsCore-WebUI/include/DomoticsCore/WebUI/WebUIConfig.h, DomoticsCore-NTP/include/DomoticsCore/NTP.h, examples/README.md]
checked: d54dae8
verified: agent:documentalist
-->
# Getting Started with DomoticsCore

**Quick guide to start building IoT applications for ESP32, ESP32-C3, and ESP8266**

---

## Quick Start

### Installation from PlatformIO Registry (Recommended)

**Step 1: Add to platformio.ini**
```ini
[env:esp32dev]
platform = espressif32@7.1.3
board = esp32dev
framework = arduino

lib_deps =
    jn0v/DomoticsCore@^2.5.0
```

**Step 2: Write your code**
```cpp
#include <DomoticsCore/System.h>

using namespace DomoticsCore;
System* domotics = nullptr;

void setup() {
    Serial.begin(115200);

    SystemConfig config = SystemConfig::fullStack();
    config.deviceName = "MyDevice";
    config.wifiSSID = "YOUR_WIFI";
    config.wifiPassword = "YOUR_PASSWORD";

    domotics = new System(config);

    if (!domotics->begin()) {
        while (1) {
            domotics->loop();  // Keep LED error animation running
            yield();
        }
    }
}

void loop() {
    domotics->loop();
}
```

**Step 3: Build**
```bash
pio run -t upload -t monitor
```

That's it! WiFi, LED status, telnet console, error recovery - all automatic.

---

### Installation from GitHub (Development/Testing)

```ini
[env:esp32dev]
platform = espressif32@7.1.3
board = esp32dev
framework = arduino
board_build.partitions = min_spiffs.csv

lib_deps =
    https://github.com/JN0V/DomoticsCore.git#v2.5.0
```

### From a Local Checkout

```ini
lib_deps =
    symlink://../DomoticsCore
```

`symlink://` compiles the library where it lives, so an edit in the checkout
is seen by the next build; `file://` would copy the whole repository into
`.pio/libdeps` and never refresh it. Delete the project's `.pio` once when
switching between a registry version and a checkout.

### ESP32-C3 Configuration

```ini
[env:esp32c3]
platform = espressif32@7.1.3
board = esp32-c3-devkitm-1
framework = arduino

lib_deps =
    jn0v/DomoticsCore@^2.5.0
```

ESP32-C3 is fully supported with USB CDC serial.

---

## What You Get Automatically

### WiFi Connection
- Connects on `begin()`
- Handles timeouts
- AP fallback mode when STA credentials are empty
- Logs connection status

### LED Status (Automatic Patterns)
- **Fast Blink (200ms)** - Booting
- **Slow Blink (1s)** - WiFi connecting
- **Heartbeat (2s)** - WiFi connected
- **Breathing (3s)** - System ready (normal operation)
- **Fast Blink (300ms)** - Error

### Remote Console (Telnet)
Built-in commands:
- `status` - System status
- `wifi` - WiFi info
- `help` - Show all commands
- `level <0-4>` - Change log level
- `heap` - Memory usage
- `bootdiag` - Boot diagnostics
- `reboot` - Restart

Plus YOUR custom commands!

---

## Examples

### Start Here

**[`DomoticsCore-System/examples/Minimal/`](../DomoticsCore-System/examples/Minimal/)**

The simplest way to get started - minimal system with WiFi, LED, and console.

### Standard Setup

**[`DomoticsCore-System/examples/Standard/`](../DomoticsCore-System/examples/Standard/)**

Standard setup with common features enabled.

### Full-Featured

**[`DomoticsCore-System/examples/FullStack/`](../DomoticsCore-System/examples/FullStack/)**

Production-ready example with all features (WebUI, MQTT, HA, OTA, NTP, etc.).

### Component-Specific Examples

Each component has its own examples directory:
- `DomoticsCore-Core/examples/` - Core, EventBus basics
- `DomoticsCore-Wifi/examples/` - WiFi connection patterns
- `DomoticsCore-WebUI/examples/` - Web interface
- `DomoticsCore-MQTT/examples/` - MQTT with WebUI
- `DomoticsCore-LED/examples/` - LED effects
- `DomoticsCore-Storage/examples/` - Persistent storage
- `DomoticsCore-RemoteConsole/examples/` - Telnet console with WebUI
- See [`examples/README.md`](../examples/README.md) for the full list

---

## Common Use Cases

### Temperature Monitoring

```cpp
#include <DHT.h>

DHT dht(4, DHT22);

void setup() {
    // ... system setup ...
    dht.begin();

    domotics->registerCommand("temp", [](const String& args) {
        float temp = dht.readTemperature();
        return String("Temperature: ") + String(temp, 1) + "°C\n";
    });
}
```

### Relay Control

```cpp
#define RELAY_PIN 5

void setup() {
    // ... system setup ...
    pinMode(RELAY_PIN, OUTPUT);

    domotics->registerCommand("relay", [](const String& args) {
        if (args == "on") {
            digitalWrite(RELAY_PIN, HIGH);
            return "Relay ON\n";
        } else if (args == "off") {
            digitalWrite(RELAY_PIN, LOW);
            return "Relay OFF\n";
        }
        return "Usage: relay on|off\n";
    });
}
```

### Periodic Sensor Reading

```cpp
void loop() {
    domotics->loop();

    static unsigned long lastRead = 0;
    if (millis() - lastRead > 10000) {  // Every 10 seconds
        float temp = readTemperature();
        Serial.printf("Temperature: %.1f°C\n", temp);

        if (temp > 25.0) {
            digitalWrite(RELAY_PIN, HIGH);  // Turn on cooling
        }

        lastRead = millis();
    }
}
```

---

## Adding More Components

The System handles the basics. Add more as needed:

### MQTT

```cpp
#include <DomoticsCore/MQTT.h>

// After system.begin():
MQTTConfig mqttConfig;
mqttConfig.broker = "192.168.1.100";
auto mqtt = std::make_unique<MQTTComponent>(mqttConfig);
domotics->getCore().addComponent(std::move(mqtt));
```

### Web UI

```cpp
#include <DomoticsCore/WebUI.h>

WebUIConfig webConfig;
webConfig.port = 8080;
auto webui = std::make_unique<WebUIComponent>(webConfig);
domotics->getCore().addComponent(std::move(webui));
```

### NTP Time Sync

```cpp
#include <DomoticsCore/NTP.h>

NTPConfig ntpConfig;
ntpConfig.server = "pool.ntp.org";
auto ntp = std::make_unique<NTPComponent>(ntpConfig);
domotics->getCore().addComponent(std::move(ntp));
```

---

## Troubleshooting

### WiFi Won't Connect

- Check SSID and password
- Ensure 2.4GHz network (ESP32 doesn't support 5GHz)
- LED will blink fast on error
- Check serial monitor for details

### Can't Connect via Telnet

- Get IP from serial monitor
- Try: `telnet <ip> 23` (space between IP and port)
- Ensure port 23 not blocked by firewall

### LED Not Working

- Check GPIO pin (default: 2)
- Some boards use inverted logic
- Set `config.ledActiveHigh = false` if needed

---

## Documentation

- **[Main README](../README.md)** - Project overview and features
- **[Architecture Guide](architecture.md)** - Design decisions and patterns
- **[Documentation Index](README.md)** - All guides and references
- **[CHANGELOG.md](../CHANGELOG.md)** - Version history
- **Component READMEs** - See each `DomoticsCore-*/README.md`

---

## Learning Path

1. **Start**: `DomoticsCore-System/examples/Minimal/`
2. **Understand**: Read [System README](../DomoticsCore-System/README.md)
3. **Customize**: Add your sensors/actuators
4. **Extend**: Add MQTT, WebUI, etc.
5. **Advanced**: Explore individual components and [architecture docs](architecture.md)

---

## Tips

### Development
- Use `CORE_DEBUG_LEVEL=4` for development
- Use `CORE_DEBUG_LEVEL=2` for production
- Connect via telnet for live debugging
- Use `level` command to change log level at runtime

### Production
- Set strong WiFi password
- Change default device name
- Enable OTA for remote updates
- Add watchdog timer
- Test error recovery

### Best Practices
- Keep `loop()` non-blocking
- Use timers for periodic tasks
- Handle sensor errors gracefully
- Log important events
- Document custom commands
