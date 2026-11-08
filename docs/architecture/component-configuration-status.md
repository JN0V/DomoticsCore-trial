<!-- workline
sources: [DomoticsCore-Wifi/include/DomoticsCore/Wifi.h, DomoticsCore-WebUI/include/DomoticsCore/WebUI.h, DomoticsCore-WebUI/include/DomoticsCore/WebUI/WebUIConfig.h, DomoticsCore-NTP/include/DomoticsCore/NTP.h, DomoticsCore-MQTT/include/DomoticsCore/MQTT.h, DomoticsCore-HomeAssistant/include/DomoticsCore/HomeAssistant.h, DomoticsCore-SystemInfo/include/DomoticsCore/SystemInfo.h, DomoticsCore-System/include/DomoticsCore/SystemPersistence.h]
checked: 500862d
verified: agent:documentalist
judged: 33c09cb
judged-in-parts: 33c09cb
-->
# Component Configuration Pattern -- Current Component Status

Part of the [Component Configuration Pattern](component-configuration-pattern.md).

## ✅ Components with Uniform Pattern

| Component | Config Struct | getConfig() | setConfig() | Storage Integration |
|-----------|---------------|-------------|-------------|---------------------|
| **WiFi** | `WifiConfig` | ✅ | ✅ | ✅ Section 6.1 |
| **WebUI** | `WebUIConfig` | ✅ | ✅ | ✅ Section 6.2 |
| **NTP** | `NTPConfig` | ✅ | ✅ | ✅ Section 6.3 |
| **MQTT** | `MQTTConfig` | ✅ | ✅ | ✅ Section 6.4 |
| **HomeAssistant** | `HAConfig` | ✅ | ✅ | ✅ Section 6.5 |
| **SystemInfo** | `SystemInfoConfig` | ✅ | ⚠️ | ⚠️ Read-only (populated from SystemConfig) |

The components that do not follow it yet: [Components Needing Full Pattern](component-configuration-status-needed.md).
