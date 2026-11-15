<!-- workline
sources: [DomoticsCore-OTA/include/DomoticsCore/OTA.h, DomoticsCore-OTA/include/DomoticsCore/OTAEvents.h, DomoticsCore-OTA/include/DomoticsCore/OTAWebUI.h, DomoticsCore-OTA/include/DomoticsCore/Update_ESP32.h, DomoticsCore-OTA/include/DomoticsCore/Update_ESP8266.h, DomoticsCore-OTA/include/DomoticsCore/Update_HAL.h, DomoticsCore-OTA/include/DomoticsCore/Update_Stub.h, DomoticsCore-OTA/src/OTA.cpp, DomoticsCore-OTA/library.json, DomoticsCore-OTA/platformio.ini, DomoticsCore-OTA/test/test_ota_component/test_ota_component.cpp, DomoticsCore-OTA/test/test_ota_esp32/test_ota_esp32.cpp, DomoticsCore-OTA/test/test_ota_esp8266/test_ota_esp8266.cpp, DomoticsCore-OTA/test/test_ota_http/test_ota_http.cpp, DomoticsCore-OTA/test/test_ota_status_allocations/test_ota_status_allocations.cpp, DomoticsCore-OTA/test/test_ota_url_hash/test_ota_url_hash.cpp, DomoticsCore-OTA/examples/BasicOTA/src/main.cpp, DomoticsCore-OTA/examples/OTAWithWebUI/src/main.cpp]
checked: cff4019
verified: agent:documentalist
judged: bb7ea2c
-->
# DomoticsCore-OTA -- Project Context (AI Agent)

> **All development MUST comply with the [DomoticsCore Constitution](../../../.specify/memory/constitution.md).**

This document provides structured context for AI coding agents working on the DomoticsCore-OTA component. It covers identity, file inventory, class hierarchy, dependencies, coding conventions, platform caveats, and constitution compliance.

---

## Component Identity

| Field | Value |
|-------|-------|
| Library name | `DomoticsCore-OTA` |
| Version | **1.11.0** |
| `metadata.name` | `"OTA"` |
| `metadata.version` | `"1.11.0"` (must match `library.json`) |
| `metadata.category` | `"system"` |
| Type key | `"ota"` |
| Namespace | `DomoticsCore::Components` (core), `DomoticsCore::Components::WebUI` (provider), `DomoticsCore::OTAEvents` (events) |
| Platforms | `espressif32`, `espressif8266` |
| Frameworks | `arduino` |
| License | MIT |

---

## File Inventory

**Important**: Unlike most DomoticsCore components, OTA has a `.cpp` implementation file. This is not header-only.

```
DomoticsCore-OTA/
  include/
    DomoticsCore/
      OTA.h              -- OTAConfig struct, OTAComponent class declaration
      OTAEvents.h        -- Event topic constants (ota/start, ota/progress, etc.)
      OTAWebUI.h         -- OTAWebUI provider (header-only, CachingWebUIProvider)
      Update_HAL.h       -- Routing header: includes Update_ESP32.h, Update_ESP8266.h, or Update_Stub.h
      Update_ESP32.h     -- ESP32 HAL: direct flash write via ESP32 Update library
      Update_ESP8266.h   -- ESP8266 HAL: direct write using Update.runAsync(true)
      Update_Stub.h      -- No-op stub for native testing (all writes succeed, no flash)
  src/
    OTA.cpp              -- OTAComponent implementation (state machine, download, upload, SHA-256)
  examples/
    BasicOTA/            -- Minimal OTA with periodic URL checking, no WebUI
    OTAWithWebUI/        -- Full OTA with WebUI browser-based upload via Access Point
  test/
    test_ota_component/          -- 56 Unity tests: events, config, state, hashes, upload, lifecycle, integration
    test_ota_http/               -- 24 tests: the routes OTAWebUI registers, through the mocked async server
    test_ota_url_hash/           -- 6 tests: a URL install carrying (or required to carry) its SHA-256
    test_ota_status_allocations/ -- 2 tests: what a progress event allocates, counted at malloc
    test_ota_esp32/              -- 9 tests on an ESP32 board: the boot partition a bad image never reaches
    test_ota_esp8266/            -- 10 tests on an ESP8266 board: the eboot command a bad image never stages
  library.json           -- PlatformIO library manifest (v1.11.0)
  platformio.ini         -- native (host tests), esp32cam and esp8266dev (on-board suites) environments
  README.md              -- Component-level README with usage examples
```

### Line Counts

| File | Lines | Notes |
|------|-------|-------|
| `OTA.h` | 181 | Config struct (13 fields) + class declaration |
| `OTAEvents.h` | 33 | Six event topic constants |
| `OTAWebUI.h` | 563 | Full WebUI provider with route registration |
| `Update_HAL.h` | 37 | Platform routing only |
| `Update_ESP32.h` | 83 | ESP32 HAL: direct flash write, no buffering |
| `Update_ESP8266.h` | 106 | ESP8266 HAL: `Update.runAsync(true)`, no buffering |
| `Update_Stub.h` | 84 | Stub HAL: all writes succeed silently |
| `OTA.cpp` | 761 | Core logic -- approaching 800-line constitution limit |
| **Total** | **1848** | All source files combined |

---

## Key Classes and Types

### `OTAConfig` (struct)

Plain configuration struct with 13 fields. No methods. Passed by value to constructor and `setConfig()`.

### `OTAComponent` (class)

- Inherits: `IComponent`
- State enum: `Idle`, `Checking`, `Downloading`, `Applying`, `RebootPending`, `Error` (**Note (C15)**: `Applying` is defined in the enum but never entered at runtime -- the code transitions directly from `Downloading` to `RebootPending` or `Idle`)
- Transport callbacks: `ManifestFetcher`, `Downloader` (both `std::function`, nullable)
- Internal structs: `ManifestInfo` (parsed manifest), `UploadSession` (upload state tracking)
- Key pattern: deferred execution -- `triggerImmediateCheck()` and `triggerUpdateFromUrl()` set pending flags; actual work happens in `loop()`

### `OTAWebUI` (class)

- Inherits: `CachingWebUIProvider`
- Holds non-owning pointers to `OTAComponent*` and `WebUIComponent*`
- Requires explicit `init(WebUIComponent*)` call after construction (route registration needs server access)
- Uses `LazyState<OTAState>` for change detection (tracks `state`, `progress`, `bytes`)
- Registers both standard WebUI contexts and custom REST/upload routes
- `handleWebUIRequest` accepts both `ota_unified` and `ota_manager` as context IDs
- Internal `UploadState` struct tracks active upload metadata (filename, total, success/error)
- `getWebUIVersion()` returns `ota->metadata.version`, falling back to `"1.4.0"` only when constructed without an `OTAComponent`

### `OTAEvents` (namespace)

Six `static constexpr const char*` constants. No classes or functions. All six are emitted by `OTA.cpp` since BUG-21 closed the gap: `EVENT_START` when a transfer opens and `EVENT_END` when it finishes, before verification, on the download and upload paths alike. (`EVENT_COMPLETE` was removed in v2.1.0, DC-6.) Each translation unit holds its own copy, which costs nothing: the EventBus keys subscriptions by the string's value, never by its address, and `inline` variables are C++17 while the `esp32dev` build is `gnu++14`.

---

## Dependencies

### Build Dependencies (`library.json`)

| Dependency | Version | Purpose |
|------------|---------|---------|
| ArduinoJson | >= 7.0.0 | JSON manifest parsing, status event serialization |

### Runtime Dependencies (implicit via includes)

| Dependency | Headers Used | Purpose |
|------------|-------------|---------|
| DomoticsCore-Core | `IComponent.h`, `Logger.h`, `Events.h`, `Platform_HAL.h` | Base component interface, logging macros, EventBus, HAL platform utilities |
| DomoticsCore-WebUI | `IWebUIProvider.h`, `WebUI.h`, `BaseWebUIComponents.h` | WebUI provider interface, route registration, field types (only for OTAWebUI) |

OTA does **not** depend on DomoticsCore-Wifi, DomoticsCore-MQTT, or any other component. Network transport is fully delegated to application-injected callbacks.

### Test Dependencies

| Dependency | Purpose |
|------------|---------|
| Unity | Test framework (PlatformIO native) |
| DomoticsCore-Core | Provides `Core`, `IComponent`, stubs |

### Build Configuration

`platformio.ini` has three environments:

- `native` runs the host suites (`test_ota_esp32` and `test_ota_esp8266` are ignored there). It builds with `gnu++17` and `CORE_DEBUG_LEVEL=3`, puts `tests/mocks/libraries` and the Core, OTA and WebUI headers on the include path, links Core, WebUI and `ArduinoJson@^7.0.0`, and `lib_ignore`s the real ESPAsyncWebServer and AsyncTCP so the mocked async server is found instead (`test_ota_http` compiles `OTAWebUI`). It also runs the tests with glibc's tcache disabled, so heap assertions do not depend on earlier tests.
- `esp32cam` runs `test_ota_esp32` on a board, with the `default.csv` partition table that gives OTA its two app slots.
- `esp8266dev` (`nodemcuv2`) runs `test_ota_esp8266` on a board.

---

## Conventions and Patterns

### Deferred Execution

All control methods (`triggerImmediateCheck`, `triggerUpdateFromUrl`) set `pending*` flags. The actual network and flash operations run inside `loop()`. This avoids blocking the caller and ensures operations execute in the main task context.

### Get-Override-Set for Config Updates

Both the component and WebUI use the pattern:
```cpp
OTAConfig cfg = ota->getConfig();
cfg.fieldName = newValue;
ota->setConfig(cfg);
```
This avoids exposing individual setters and keeps the config struct as the single source of truth.

### Event Publishing

All events are emitted as serialized JSON bytes, NUL included, via the sized `emit(topic, data, size, sticky)`; a payload under 192 bytes is serialized on the stack. The `publishStatusEvent` helper automatically injects `state`, `progress`, and `lastResult` into every event payload.

### Progress Throttling

Upload progress events are throttled to once per second (`lastProgressPublishMillis`) to prevent EventBus queue overflow. Log messages are throttled to every 10% or every 256 KB.

### Platform HAL Isolation

All `#ifdef` platform branching is confined to `Update_HAL.h` and the platform-specific `Update_ESP32.h` / `Update_ESP8266.h` / `Update_Stub.h` headers. Business logic in `OTA.cpp` calls `HAL::OTAUpdate::*` and `HAL::Platform::*` functions without any conditional compilation.

---

## Testing

### Unit Tests

| Suite | Tests | Runs on | Coverage |
|-------|-------|---------|----------|
| `test_ota_component` | 56 | `native` | Event constants, creation, config, state machine, triggers, SHA-256 on the download and upload paths (a mismatch never commits), the size cap, start/end/completed event order, upload states and progress, lifecycle, providers, non-blocking loop, Core integration, check interval |
| `test_ota_http` | 24 | `native` | The upload and REST routes `OTAWebUI` registers: CSRF and credentials checks, state reset, abort on a dropped client, announced size |
| `test_ota_url_hash` | 6 | `native` | A URL install carrying the SHA-256 its image must match, and `requireDownloadHash` |
| `test_ota_status_allocations` | 2 | `native` | Allocations of a progress event, counted at `malloc` |
| `test_ota_esp32` | 9 | `esp32cam` | SEC-2 and SEC-7 on silicon: a mismatched image never switches the boot partition |
| `test_ota_esp8266` | 10 | `esp8266dev` | SEC-2 and SEC-7 on silicon: a mismatched image never stages the eboot command |

The host suites use the `Update_Stub.h` HAL: no real firmware flashing occurs.

### Examples

| Example | Description | Key Concept |
|---------|-------------|-------------|
| `BasicOTA` | Minimal OTA with periodic URL checking, no WebUI. WiFi STA mode. | `OTAComponent` standalone, `checkIntervalMs` auto-check |
| `OTAWithWebUI` | Full OTA with WebUI browser upload, WiFi AP mode. | `OTAWebUI` provider, `init()` post-begin, `registerProviderWithComponent` |

---

## Platform HAL Details

### ESP32 (`Update_ESP32.h`)

ESP32 can write directly to flash from async context (no yield panic). Uses the standard ESP32 `Update` library.

### ESP8266 (`Update_ESP8266.h`)

On ESP8266, the OTA HAL uses `Update.runAsync(true)` to enable direct flash writes from AsyncWebServer callbacks without `__yield` panic. This eliminates the need for buffered writes. When `size == 0`, the HAL calculates available sketch space via `ESP.getFreeSketchSpace()`.

Both ESP32 and ESP8266 now perform direct writes via `HAL::OTAUpdate::write()` and immediate finalization via `HAL::OTAUpdate::end()`.

### Native Stub (`Update_Stub.h`)

Used for native unit tests. All writes succeed silently; `begin()` always returns `true`, `write()` accumulates byte count, `end()` returns `true`. `errorString()` returns `"Update not supported on this platform"`.

### Common HAL Interface

All three implementations expose the same function set in `DomoticsCore::HAL::OTAUpdate`:

| Function | Signature | Purpose |
|----------|-----------|---------|
| `begin` | `bool begin(size_t)` | Initialize flash partition |
| `write` | `size_t write(uint8_t*, size_t)` | Write firmware chunk |
| `end` | `bool end(bool)` | Finalize flash write |
| `abort` | `void abort()` | Cancel in-progress update |
| `errorString` | `String errorString()` | Last error description |
| `hasError` | `bool hasError()` | Error flag |
| `getBytesWritten` | `size_t getBytesWritten()` | Total bytes committed to flash |

---

## Constitution Compliance Notes

| Principle | Status | Notes |
|-----------|--------|-------|
| SOLID / SRP | Compliant | `OTAComponent` handles update logic; `OTAWebUI` handles presentation; transport is injected |
| HAL Isolation (IX) | Compliant | All `#ifdef` confined to `Update_HAL.h` and platform headers |
| Non-Blocking (X) | Compliant | Deferred execution via pending flags; `loop()` processes work; `delay()` only used in 100ms pre-reboot pause |
| File Size (VII) | Warning | `OTA.cpp` is 761 lines, within the 800-line hard limit but above the 500-line target. `OTAWebUI.h` is 563 lines. Monitor growth. |
| EventBus (VI) | Compliant | All status and progress updates published via `publishStatusEvent()`, which emits the serialized JSON bytes |
| Memory (XIV) | Compliant | Upload progress throttled; no String concatenation in hot paths; PROGMEM used for HTML |
| Versioning (XV) | Compliant | `library.json` version `1.11.0` matches `metadata.version` in constructor |
| Anti-Patterns (XIII) | Compliant | No singletons; centralized event constants in `OTAEvents.h`; dependencies declared |

### Potential Improvement Areas

- `OTA.cpp` line count (761) should be monitored. If new features are added, consider extracting manifest fetching or version comparison into a separate utility.
- `OTAWebUI.h` contains both context building and route registration. If it grows further, route registration could be extracted into a helper.
- Firmware signature verification is not implemented. The `signaturePublicKey` field that used to advertise it was removed in v2.1.0 (DC-7): it was never read, so it promised a check that did not happen. SHA-256 integrity verification *is* performed on the download path, and the image is committed to flash only after the digest matches (SEC-2). Note the ordering is the security property: `HAL::OTAUpdate::end(true)` is the point of no return — it switches the ESP32 boot partition and stages the ESP8266 eboot copy — and no Arduino core lets the application undo it, so `abort()` is only meaningful *before* `end()`. See the contract in `Update_HAL.h`. The **upload** path verifies on the same terms since SEC-7: `acceptUploadChunk()` hashes what it writes and `finalizeUpload()` checks the digest before `end(true)`. The expected hash is optional per upload (`X-Firmware-SHA256` header or `?sha256=`), because every caller before SEC-7 supplied none; set `OTAConfig::requireUploadHash` to refuse uploads without one, which also rejects the built-in browser form since it cannot send a digest.
- ~~**(C14)**~~ **Resolved (BUG-21).** `EVENT_START` and `EVENT_END` are emitted at the points this entry named: the beginning of a download or upload, and the end of the transfer before verification.
- **(C15)** `State::Applying` is defined in the enum and checked in `isBusy()` / `stateToString()` but is never entered via `transition()`. Either introduce a transition to `Applying` after download completes (before `finalizeUpdateOperation`), or remove the state from the enum to match actual runtime behavior.
- `setConfig()` logs three of the ten `OTAConfig` fields (`updateUrl`, `autoReboot`, `enableWebUIUpload`); a runtime change to `maxDownloadSize`, `requireUploadHash`, `requireDownloadHash` or `uploadIdleTimeoutSec` leaves no trace in the log. The ESP8266 log buffer (128 bytes) is the constraint a fuller line has to fit.
- The four `toInt()` calls in `OTA.cpp:685-690` parse a version string from the
  update manifest, so `"1.2x"` reads as a minor of 2 and a letter reads as 0.
  The suites that feed a manifest all give the well-formed `"9.9.9"`, and no
  consequence has been demonstrated: a malformed manifest version compares
  wrong, it does not flash anything. Recorded here rather than filed, since the
  digits-only rule that covers every settings field does not reach a manifest.
