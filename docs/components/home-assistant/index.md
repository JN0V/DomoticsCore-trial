<!-- workline
sources: [DomoticsCore-HomeAssistant/library.json, DomoticsCore-HomeAssistant/include/DomoticsCore/HAAlarmControlPanel.h, DomoticsCore-HomeAssistant/include/DomoticsCore/HABinarySensor.h, DomoticsCore-HomeAssistant/include/DomoticsCore/HAButton.h, DomoticsCore-HomeAssistant/include/DomoticsCore/HAEntity.h, DomoticsCore-HomeAssistant/include/DomoticsCore/HAEvents.h, DomoticsCore-HomeAssistant/include/DomoticsCore/HALight.h, DomoticsCore-HomeAssistant/include/DomoticsCore/HASensor.h, DomoticsCore-HomeAssistant/include/DomoticsCore/HASwitch.h, DomoticsCore-HomeAssistant/include/DomoticsCore/HomeAssistant.h, DomoticsCore-HomeAssistant/include/DomoticsCore/HomeAssistantWebUI.h]
checked: ae5715e
judged: 6a979a6
judged-in-parts: bb7ea2c
-->

# DomoticsCore-HomeAssistant -- Documentation Index

> Component documentation for DomoticsCore-HomeAssistant (version: see `library.json`)

---

## Overview

DomoticsCore-HomeAssistant is the Home Assistant MQTT Discovery integration component for the DomoticsCore IoT framework. It provides automatic entity registration, state management, and command handling for ESP32/ESP8266 devices via the HA MQTT Discovery protocol.

**Architecture:** Header-only library, EventBus-driven (no direct MQTT client reference).

**v2.0.0 Breaking Change:** Per-entity callbacks removed. All commands are now delivered via the `ha/command` EventBus event carrying an `HACommandEvent` struct.

---

## Supported Entity Types

| Entity | HA Component | Direction | Command Handling |
|--------|-------------|-----------|-----------------|
| Sensor | `sensor` | Device -> HA | None (read-only) |
| BinarySensor | `binary_sensor` | Device -> HA | None (read-only) |
| Switch | `switch` | Bidirectional | `handleCommand()` sets `state`, always valid |
| Light | `light` | Bidirectional | `handleCommand()` parses JSON or ON/OFF, rejects garbage |
| Button | `button` | HA -> Device | `handleCommand()` accepts only `payloadPress` |
| AlarmControlPanel | `alarm_control_panel` | Bidirectional | `handleCommand()` parses `COMMAND [CODE]` format |

---

## Documentation Files

| Document | Description |
|----------|-------------|
| [README.md](./README.md) | Component overview, quick start, migration guide from v1.x to v2.0.0 |
| [Technical Reference](./technical-reference.md) | Full API surface, MQTT topics, discovery payloads, command handling flow |
| [Project Context](./project-context.md) | AI-oriented context: class inventory, dependencies, conventions, constitution compliance |

---

## Quick Reference

### Key Namespaces

- `DomoticsCore::Components::HomeAssistant` -- main component, entities, HAConfig, HA utilities
- `DomoticsCore::HAEvents` -- event constants (`EVENT_COMMAND`, `EVENT_DISCOVERY_PUBLISHED`, `EVENT_ENTITY_ADDED`) and `HACommandEvent` struct
- `DomoticsCore::Components::WebUI` -- optional `HomeAssistantWebUI` provider

### Key Header Files

| Header | Content |
|--------|---------|
| `HomeAssistant.h` | `HomeAssistantComponent`, `HAConfig`, `HA::setField()`, `HAStatistics` |
| `HAEntity.h` | Base entity class, `HA_TOPIC_BUF_SIZE`, topic generation methods |
| `HAEvents.h` | `HACommandEvent` struct, event topic constants |
| `HASensor.h` | `HASensor` -- read-only sensor |
| `HABinarySensor.h` | `HABinarySensor` -- on/off sensor |
| `HASwitch.h` | `HASwitch` -- controllable toggle |
| `HALight.h` | `HALight` -- brightness-capable light |
| `HAButton.h` | `HAButton` -- trigger-only action |
| `HAAlarmControlPanel.h` | `HAAlarmControlPanel`, `AlarmFeature`, `AlarmPanelState`, `AlarmPanelCommand` |
| `HomeAssistantWebUI.h` | Optional WebUI provider (4 UI contexts) |

### EventBus Events

| Event Topic | Payload | Direction |
|-------------|---------|-----------|
| `ha/command` | `HACommandEvent` | HA -> Consumer (via component) |
| `ha/discovery_published` | `int` (entity count) | Component -> Observers |
| `ha/entity_added` | `HAEntityAddedEvent` | Component -> Observers |
| `mqtt/publish` | `MQTTPublishEvent` | Component -> MQTT |
| `mqtt/subscribe` | `MQTTSubscribeEvent` | Component -> MQTT |

### Dependencies

| Dependency | Version | Purpose |
|------------|---------|---------|
| DomoticsCore-Core | >= 1.4.0 | IComponent, EventBus, Logger |
| DomoticsCore-MQTT | >= 1.4.0 | MQTT event structures |
| ArduinoJson | ^7.0.0 | JSON serialization |
| DomoticsCore-WebUI | optional | Required only for `HomeAssistantWebUI` |

---

## Related Resources

- [Alarm Control Panel Deep-Dive](../../deep-dive-ha-alarm-control-panel.md)
- [Component Source](../../../DomoticsCore-HomeAssistant/)
- [DomoticsCore Constitution](../../../.specify/memory/constitution.md)
- [BasicHA Example](../../../DomoticsCore-HomeAssistant/examples/BasicHA/src/main.cpp)
- [HAWithWebUI Example](../../../DomoticsCore-HomeAssistant/examples/HAWithWebUI/src/main.cpp)

---

## Source Tree

```
DomoticsCore-HomeAssistant/
  library.json                                  # PlatformIO metadata and version
  README.md                                     # Component README
  SPECIFICATIONS.md                             # Original design specs (v0.1.0, partially outdated)
  include/
    DomoticsCore/
      HomeAssistant.h                           # Main component
      HAEntity.h                                # Base entity class
      HASensor.h                                # Sensor entity
      HABinarySensor.h                          # Binary sensor entity
      HASwitch.h                                # Switch entity
      HALight.h                                 # Light entity
      HAButton.h                                # Button entity
      HAAlarmControlPanel.h                     # Alarm control panel entity
      HAEvents.h                                # EventBus event constants + HACommandEvent
      HomeAssistantWebUI.h                      # WebUI provider (optional)
  examples/
    BasicHA/src/main.cpp                        # Minimal HA integration + alarm demo
    HAWithWebUI/src/main.cpp                    # Full-featured with WebUI
```
