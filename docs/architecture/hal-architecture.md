<!-- workline
sources: [DomoticsCore-Core/include/DomoticsCore/Platform_HAL.h, DomoticsCore-Core/include/DomoticsCore/Filesystem_HAL.h, DomoticsCore-Wifi/include/DomoticsCore/Wifi_HAL.h, DomoticsCore-Wifi/include/DomoticsCore/WiFiServer_HAL.h, DomoticsCore-WebUI/include/DomoticsCore/WebUI_HAL.h, DomoticsCore-MQTT/include/DomoticsCore/MQTT_HAL.h, DomoticsCore-NTP/include/DomoticsCore/NTP_HAL.h, DomoticsCore-Storage/include/DomoticsCore/Storage_HAL.h, DomoticsCore-OTA/include/DomoticsCore/Update_HAL.h]
checked: ae5715e
judged: 1e9326b
verified: agent:documentalist
judged-in-parts: 33c09cb
-->
# HAL Architecture: Routing Header + Platform Files

## Overview

DomoticsCore uses a Hardware Abstraction Layer (HAL) architecture that separates platform-specific code into dedicated files per platform. This enables:

- Clean separation of concerns
- Easy addition of new platforms (ESP32-C3, ESP32-H2, etc.)
- Better testability
- Reduced `#if` block complexity in individual files

## File Structure Pattern

```
ComponentName_HAL.h       → Routing header (platform detection + include)
ComponentName_ESP32.h     → ESP32 implementation
ComponentName_ESP8266.h   → ESP8266 implementation
ComponentName_ESP32C3.h   → Future: ESP32-C3
ComponentName_ESP32H2.h   → Future: ESP32-H2
ComponentName_Stub.h      → Fallback for unsupported platforms
```

## Routing Header Example

The actual pattern used in `Platform_HAL.h` (the root routing header):

```cpp
// Platform_HAL.h (actual)
#if DOMOTICS_PLATFORM_ESP32
    #include "Platform_ESP32.h"
#elif DOMOTICS_PLATFORM_ESP8266
    #include "Platform_ESP8266.h"
#else
    #include "Platform_Stub.h"  // Native testing / unknown platforms
#endif
```

The same pattern is used by all component HAL files. Example for a component HAL:

```cpp
// Storage_HAL.h
#pragma once
#include "DomoticsCore/Platform_HAL.h"

#if DOMOTICS_PLATFORM_ESP32
    #include "Storage_ESP32.h"
#elif DOMOTICS_PLATFORM_ESP8266
    #include "Storage_ESP8266.h"
#else
    #include "Storage_Stub.h"
#endif
```

**Note:** ESP32-C3 is handled by the `DOMOTICS_PLATFORM_ESP32` branch (detected via `defined(ESP32)`). There are currently no separate ESP32-C3 or ESP32-H2 HAL files -- they share the ESP32 implementation.

## Platform Implementation Guidelines

Each `*_Platform.h` file MUST:

1. **Be self-contained**: Include all necessary platform-specific headers
2. **Follow the same namespace**: Platform-level functions use `DomoticsCore::Platform`, with `DomoticsCore::HAL` as a backward-compatible wrapper API
3. **Implement the same interface**: All platforms expose identical functions
4. **Stay under 800 lines**: Per constitution file size limits
5. **Use inline functions**: For header-only compatibility

### Platform Detection Macros

`Platform_HAL.h` defines feature-availability macros that other code can check at compile-time:

| Macro | Description |
|-------|-------------|
| `DOMOTICS_PLATFORM_ESP32` | ESP32 detected (includes ESP32-C3) |
| `DOMOTICS_PLATFORM_ESP8266` | ESP8266 detected |
| `DOMOTICS_HAS_WIFI` | Platform supports WiFi |
| `DOMOTICS_HAS_PREFERENCES` | Platform supports NVS Preferences |
| `DOMOTICS_HAS_FREERTOS` | Platform has FreeRTOS |
| `DOMOTICS_SUPPORTS_FULL_FRAMEWORK()` | RAM >= 80KB (enough for EventBus, WebUI, JSON) |

## Adding a New Platform

1. Create `ComponentName_NewPlatform.h` with platform-specific implementation
2. Add platform detection to `Platform_HAL.h`
3. Update routing headers to include the new platform file
4. Add tests for the new platform

## Current HAL Components

| HAL File | Component | Location | ESP32 | ESP8266 | Stub |
|----------|-----------|----------|-------|---------|------|
| `Platform_HAL.h` | Core | `DomoticsCore-Core/include/DomoticsCore/` | ✅ | ✅ | ✅ |
| `Filesystem_HAL.h` | Core | `DomoticsCore-Core/include/DomoticsCore/` | ✅ | ✅ | ✅ |
| `Wifi_HAL.h` | Wifi | `DomoticsCore-Wifi/include/DomoticsCore/` | ✅ | ✅ | ✅ |
| `WiFiServer_HAL.h` | Wifi | `DomoticsCore-Wifi/include/DomoticsCore/` | ✅ | ✅ | ✅ |
| `WebUI_HAL.h` | WebUI | `DomoticsCore-WebUI/include/DomoticsCore/` | ✅ | ✅ | ✅ |
| `MQTT_HAL.h` | MQTT | `DomoticsCore-MQTT/include/DomoticsCore/` | ✅ | ✅ | ✅ |
| `NTP_HAL.h` | NTP | `DomoticsCore-NTP/include/DomoticsCore/` | ✅ | ✅ | ✅ |
| `Storage_HAL.h` | Storage | `DomoticsCore-Storage/include/DomoticsCore/` | ✅ | ✅ | ✅ |
| `Update_HAL.h` | OTA | `DomoticsCore-OTA/include/DomoticsCore/` | ✅ | ✅ | ✅ |

**Note:** Each HAL routing header is located within its component's include directory, not centralized. The `Platform_HAL.h` in Core serves as the root platform detection header that all other HAL files depend on.

## Constitution Compliance

This architecture follows:
- **Principle IX (HAL)**: Hardware code in `*_HAL` files only
- **Principle VII (File Size)**: Each file < 800 lines
- **Principle I (SOLID/SRP)**: One platform per file
