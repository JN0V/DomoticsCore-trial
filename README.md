<!-- workline
sources: [library.json, library.properties, DomoticsCore-Core/library.json, DomoticsCore-HomeAssistant/library.json, DomoticsCore-LED/library.json, DomoticsCore-MQTT/library.json, DomoticsCore-NTP/library.json, DomoticsCore-OTA/library.json, DomoticsCore-RemoteConsole/library.json, DomoticsCore-Storage/library.json, DomoticsCore-System/library.json, DomoticsCore-SystemInfo/library.json, DomoticsCore-WebUI/library.json, DomoticsCore-Wifi/library.json, DomoticsCore-System/include/DomoticsCore/System.h, DomoticsCore-System/include/DomoticsCore/SystemConfig.h, DomoticsCore-Core/include/DomoticsCore/Core.h, DomoticsCore-Core/include/DomoticsCore/IComponent.h, tools/check_versions.py, release-please-config.json]
checked: d54dae8
judged-in-parts: 02495d2
-->

# DomoticsCore

[![version](https://img.shields.io/badge/dynamic/json?url=https%3A%2F%2Fraw.githubusercontent.com%2FJN0V%2FDomoticsCore%2Fmain%2Flibrary.json&query=%24.version&label=version&color=blue)](https://github.com/JN0V/DomoticsCore/releases/latest)
[![License](https://img.shields.io/badge/license-MIT-green.svg)](LICENSE)
[![Platform](https://img.shields.io/badge/platform-ESP32%20|%20ESP32--C3%20|%20ESP8266-orange.svg)](https://platformio.org/)

**Production-ready ESP32 framework for IoT applications** with modular architecture, automatic error handling, and visual status indicators.

> **🎉 Version 2.0.0 Released!** Breaking change: HomeAssistant callbacks removed in favor of EventBus `ha/command` events. Virtual dispatch for all HA entities, complete code remediation roadmap done. See [CHANGELOG.md](CHANGELOG.md) and [Documentation Index](docs/README.md).

## ✨ What Makes DomoticsCore Different

- **🔌 Truly Modular**: Only include what you need - from a 300KB minimal core to full-featured IoT system
- **🚨 Visual Debugging**: LED status indicators work even when system fails - perfect for headless devices
- **🛡️ Production Ready**: Comprehensive error handling, component health monitoring, and graceful degradation
- **🎯 Developer Friendly**: Header-only design (no linking issues), automatic dependency resolution, extensive examples
- **📡 EventBus Architecture**: Decoupled components communicate via EventBus - no tight coupling, easy testing
- **🔧 IoT Complete**: WiFi, MQTT, Home Assistant, OTA, WebUI, Storage - everything integrated and tested

> **Note:** Components are mostly header-only (`.h` files) for zero overhead and simple integration. This is a standard C++ pattern, not ESP32-specific. See [Architecture Guide](docs/architecture.md#header-only-design) for details.

## 📷 Screenshots

WebUI and Home Assistant integration (from the [WaterMeter showcase](https://github.com/JN0V/WaterMeter)):

All screenshots are anonymized (WiFi SSID, passwords, and internal IP addresses are redacted).

![WebUI Dashboard](screenshots/webui-dashboard.png)

![WebUI Settings](screenshots/webui-settings.png)

![WebUI Components](screenshots/webui-components.png)

![WebUI OTA / SystemInfo / HomeAssistant](screenshots/webui-ota-systeminfo-ha.png)

![Home Assistant Device](screenshots/homeassistant-device.png)

## 🚀 Quick Start (3 Minutes)

Two ways to start, the full system or a minimal core, with their code and LED states: see [Quick Start](docs/readme/quick-start.md).

## 📦 Installation

### PlatformIO Registry (Recommended)

DomoticsCore is available on the PlatformIO Registry:

- https://registry.platformio.org/libraries/jn0v/DomoticsCore

Add to your `platformio.ini`:

```ini
[env:esp32dev]
platform = espressif32@7.1.3
board = esp32dev
framework = arduino

lib_deps =
    jn0v/DomoticsCore@^2.5.0
```

### PlatformIO (GitHub)

Add to your `platformio.ini`:

```ini
[env:esp32dev]
platform = espressif32@7.1.3
board = esp32dev
framework = arduino

lib_deps =
    https://github.com/JN0V/DomoticsCore.git#v2.5.0
```

Installing only some components, or a local checkout: see [Installing from a checkout](docs/readme/installation.md).

### Upgrading

Every release entry in [CHANGELOG.md](CHANGELOG.md) opens with what a sketch
sees — removed symbols, changed defaults, new behaviour — before the list of
fixes. Read the entries between the version you run and the one you take.

## 🧩 Available Components

| Component | Version | Description | Size | Status |
|-----------|---------|-------------|------|--------|
| **Core** | ![version](https://img.shields.io/badge/dynamic/json?url=https%3A%2F%2Fraw.githubusercontent.com%2FJN0V%2FDomoticsCore%2Fmain%2FDomoticsCore-Core%2Flibrary.json&query=%24.version&label=v&color=blue) | Framework, registry, event bus, MemoryManager, HeapTracker | ~50KB | ✅ Stable |
| **System** | ![version](https://img.shields.io/badge/dynamic/json?url=https%3A%2F%2Fraw.githubusercontent.com%2FJN0V%2FDomoticsCore%2Fmain%2FDomoticsCore-System%2Flibrary.json&query=%24.version&label=v&color=blue) | High-level orchestration (batteries included) | ~100KB | ✅ Stable |
| **WiFi** | ![version](https://img.shields.io/badge/dynamic/json?url=https%3A%2F%2Fraw.githubusercontent.com%2FJN0V%2FDomoticsCore%2Fmain%2FDomoticsCore-Wifi%2Flibrary.json&query=%24.version&label=v&color=blue) | Network connectivity with AP fallback | ~40KB | ✅ Stable |
| **LED** | ![version](https://img.shields.io/badge/dynamic/json?url=https%3A%2F%2Fraw.githubusercontent.com%2FJN0V%2FDomoticsCore%2Fmain%2FDomoticsCore-LED%2Flibrary.json&query=%24.version&label=v&color=blue) | Visual status indicators (6 effects) | ~20KB | ✅ Stable |
| **Storage** | ![version](https://img.shields.io/badge/dynamic/json?url=https%3A%2F%2Fraw.githubusercontent.com%2FJN0V%2FDomoticsCore%2Fmain%2FDomoticsCore-Storage%2Flibrary.json&query=%24.version&label=v&color=blue) | NVS / LittleFS persistent data | ~30KB | ✅ Stable |
| **RemoteConsole** | ![version](https://img.shields.io/badge/dynamic/json?url=https%3A%2F%2Fraw.githubusercontent.com%2FJN0V%2FDomoticsCore%2Fmain%2FDomoticsCore-RemoteConsole%2Flibrary.json&query=%24.version&label=v&color=blue) | Telnet debugging console with WebUI integration | ~25KB | ✅ Stable |
| **WebUI** | ![version](https://img.shields.io/badge/dynamic/json?url=https%3A%2F%2Fraw.githubusercontent.com%2FJN0V%2FDomoticsCore%2Fmain%2FDomoticsCore-WebUI%2Flibrary.json&query=%24.version&label=v&color=blue) | Web interface with WebSocket + SSE dual-mode | ~150KB | ✅ Stable |
| **MQTT** | ![version](https://img.shields.io/badge/dynamic/json?url=https%3A%2F%2Fraw.githubusercontent.com%2FJN0V%2FDomoticsCore%2Fmain%2FDomoticsCore-MQTT%2Flibrary.json&query=%24.version&label=v&color=blue) | Message broker with auto-reconnect | ~40KB | ✅ Stable |
| **NTP** | ![version](https://img.shields.io/badge/dynamic/json?url=https%3A%2F%2Fraw.githubusercontent.com%2FJN0V%2FDomoticsCore%2Fmain%2FDomoticsCore-NTP%2Flibrary.json&query=%24.version&label=v&color=blue) | Time synchronization | ~15KB | ✅ Stable |
| **OTA** | ![version](https://img.shields.io/badge/dynamic/json?url=https%3A%2F%2Fraw.githubusercontent.com%2FJN0V%2FDomoticsCore%2Fmain%2FDomoticsCore-OTA%2Flibrary.json&query=%24.version&label=v&color=blue) | Over-the-air updates | ~30KB | ✅ Stable |
| **HomeAssistant** | ![version](https://img.shields.io/badge/dynamic/json?url=https%3A%2F%2Fraw.githubusercontent.com%2FJN0V%2FDomoticsCore%2Fmain%2FDomoticsCore-HomeAssistant%2Flibrary.json&query=%24.version&label=v&color=blue) | Auto-discovery integration | ~20KB | ✅ Stable |
| **SystemInfo** | ![version](https://img.shields.io/badge/dynamic/json?url=https%3A%2F%2Fraw.githubusercontent.com%2FJN0V%2FDomoticsCore%2Fmain%2FDomoticsCore-SystemInfo%2Flibrary.json&query=%24.version&label=v&color=blue) | Real-time monitoring with charts | ~25KB | ✅ Stable |

**Total with everything:** ~545KB flash, ~50KB RAM

## Versioning

DomoticsCore uses [Semantic Versioning](https://semver.org/) with **per-component versions** and a **root framework version**:

How the root and component versions move, and the tool that checks them: see [Versioning](docs/readme/versioning.md).

## 📖 Documentation

- **[Getting Started Guide](docs/getting-started.md)** - Comprehensive tutorial
- **[Architecture Guide](docs/architecture.md)** - Design decisions and patterns
- **[EventBus Reference](docs/reference/eventbus-architecture.md)** - Complete EventBus documentation
- **[CHANGELOG.md](CHANGELOG.md)** - Version history
- **[Complete Documentation Index](docs/README.md)** - All guides and references
- **Component READMEs** - See each `DomoticsCore-*/README.md`
- **Examples** - 19 working examples in component directories
- **[Platform Compatibility](docs/readme/platforms.md)** - Supported platforms and HAL headers
- **[Project Structure](docs/readme/project-structure.md)** - Layout of the monorepo and of each component
- **[Key Features Deep Dive](docs/readme/key-features.md)** - Error recovery, dependencies, LED states, Event Bus, chunked responses
- **[Roadmap](docs/readme/roadmap.md)** - Completed, current and planned work
- **[Acknowledgments](docs/readme/acknowledgments.md)** - Libraries DomoticsCore builds on

### Key Documentation

- **LED Effects**: [DomoticsCore-LED/README.md](DomoticsCore-LED/README.md)
- **Event Bus**: [DomoticsCore-Core/README.md](DomoticsCore-Core/README.md#event-bus)
- **WebUI Development**: [docs/guides/webui-developer.md](docs/guides/webui-developer.md)
- **Custom Components**: [docs/guides/custom-components.md](docs/guides/custom-components.md)
- **Storage API**: [DomoticsCore-Storage/README.md](DomoticsCore-Storage/README.md)
- **Observing a device in production** (crashes, reboots, memory): [docs/reliability/observing-a-device.md](docs/reliability/observing-a-device.md)

## 💡 Examples

### Full-Featured Application

**Location:** [`DomoticsCore-System/examples/FullStack/`](DomoticsCore-System/examples/FullStack/)

Complete IoT device with:
- ✅ WiFi with AP fallback
- ✅ LED status indicators
- ✅ Telnet console (port 23)
- ✅ Web interface (port 80)
- ✅ MQTT with Home Assistant discovery
- ✅ OTA updates
- ✅ NTP time sync
- ✅ Persistent storage
- ✅ Custom sensor integration

**Binary:** ~900KB flash, ~50KB RAM

Minimal core, LED patterns and component development examples: see [More examples](docs/readme/examples.md).

### Event Bus Communication

**Location:** [`DomoticsCore-Core/examples/03-EventBusBasics/`](DomoticsCore-Core/examples/03-EventBusBasics/)

Inter-component messaging:
- Publish/subscribe pattern
- Sticky events
- Type-safe payloads
- Event coordination

### All Examples

See [`examples/README.md`](examples/README.md) for the complete list of 19 working examples across all components.

## 🤝 Contributing

Contributions welcome! Please:

1. Fork the repository
2. Create a feature branch (`git checkout -b feature/amazing-feature`)
3. Follow existing code style
4. Add tests if applicable
5. Update documentation
6. Submit a pull request

See [Architecture Guide](docs/architecture.md) for design patterns.

## 📄 License

MIT License - see [LICENSE](LICENSE) file for details.

## 📞 Support

- **Issues**: [GitHub Issues](https://github.com/JN0V/DomoticsCore/issues)
- **Discussions**: [GitHub Discussions](https://github.com/JN0V/DomoticsCore/discussions)
- **Documentation**: See `docs/` folder and component READMEs

---

**Made with ❤️ for the ESP32 community**
