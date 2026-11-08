<!-- workline
sources: [DomoticsCore-Core/library.json, DomoticsCore-HomeAssistant/library.json, DomoticsCore-LED/library.json, DomoticsCore-MQTT/library.json, DomoticsCore-NTP/library.json, DomoticsCore-OTA/library.json, DomoticsCore-RemoteConsole/library.json, DomoticsCore-Storage/library.json, DomoticsCore-System/library.json, DomoticsCore-SystemInfo/library.json, DomoticsCore-WebUI/library.json, DomoticsCore-Wifi/library.json, DomoticsCore-Core/include/DomoticsCore/IComponent.h, DomoticsCore-Core/include/DomoticsCore/StringParse.h, DomoticsCore-Core/include/DomoticsCore/Testing/HeapTracker_ESP32.h]
checked: 4cdb3e6
judged: 33c09cb
verified: agent:documentalist
-->
# DomoticsCore-Core -- Project Context (AI Agent Reference)

> **All development MUST comply with the [DomoticsCore Constitution](../../../.specify/memory/constitution.md).**

This document provides the context an AI coding agent needs to understand, navigate, and safely modify the DomoticsCore-Core component.

---

## Pages

| Page | What it holds |
|------|---------------|
| [File Inventory](project-context-files.md) | every header, source and other file, and its purpose |
| [Key Classes](project-context-classes.md) | each class and its one responsibility |
| [Conventions and Pitfalls](project-context-conventions.md) | the rules to follow and the traps to avoid |
| [Constitution Compliance](project-context-constitution.md) | the constitution principles that bear most on Core |
| [Modification Patterns and Testing](project-context-modifying.md) | how to add an event, a method, a HAL function or a component; testing notes |

---

## 1. Component Identity

| Field | Value |
|-------|-------|
| **Name** | DomoticsCore-Core |
| **Version** | 1.14.0 (see `library.json`) |
| **Role** | Foundation library -- runtime engine, component model, EventBus, logging, timers, memory management |
| **Namespace** | `DomoticsCore` (top-level), `DomoticsCore::Components`, `DomoticsCore::Utils`, `DomoticsCore::Testing` |
| **License** | MIT |
| **Platforms** | espressif32, espressif8266 (Arduino framework) |
| **Repository** | `https://github.com/JN0V/DomoticsCore.git` |
| **Author** | JN0V |

---

## 4. Dependencies

**DomoticsCore-Core has ZERO library dependencies.** It is the foundational layer. It requires only the Arduino-ESP32 or Arduino-ESP8266 toolchain.

---

## 5. Dependents (Components That Depend on Core)

Every DomoticsCore component depends on Core. Based on `library.json` dependency declarations and `#include` usage:

| Component | Declared in library.json | Notes |
|-----------|------------------------|-------|
| DomoticsCore-MQTT | Yes (`>=1.0.0`) | Uses IComponent, EventBus, Logger |
| DomoticsCore-HomeAssistant | Yes (`>=1.4.0`) | Uses IComponent, EventBus, Core injection |
| DomoticsCore-LED | Yes | Uses IComponent, NonBlockingDelay |
| DomoticsCore-NTP | Yes (`>=1.0.0`) | Uses IComponent, EventBus |
| DomoticsCore-System | Yes (`>=1.0.0`) | Uses IComponent, EventBus, ComponentRegistry |
| DomoticsCore-RemoteConsole | Yes (`>=1.13.0`) | Uses IComponent, LoggerCallbacks, EventBus |
| DomoticsCore-Wifi | Implicit (via includes) | Uses IComponent, EventBus, Logger |
| DomoticsCore-Storage | Yes (`>=1.4.0`) | Uses IComponent, EventBus, Logger |
| DomoticsCore-OTA | Yes (`>=1.4.0`) | Uses IComponent, EventBus, Logger |
| DomoticsCore-WebUI | Implicit (via includes) | Uses IComponent, ComponentRegistry, IWebUIProvider |
| DomoticsCore-SystemInfo | Yes (`>=1.4.0`) | Uses IComponent, EventBus, MemoryManager |

**Impact of changes to Core:** Any breaking change to Core's public API affects ALL components. Exercise extreme caution.

