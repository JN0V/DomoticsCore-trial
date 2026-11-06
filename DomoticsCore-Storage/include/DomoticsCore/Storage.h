#pragma once

/**
 * @file Storage.h
 * 
 * @example DomoticsCore-Storage/examples/BasicStorage/src/main.cpp
 * @example DomoticsCore-Storage/examples/NamespaceDemo/src/main.cpp
 * @example DomoticsCore-Storage/examples/StorageWithWebUI/src/main.cpp
 * @brief Declares the DomoticsCore Storage component with HAL abstraction.
 * 
 * Uses HAL::PlatformStorage for multi-platform support:
 * - ESP32: Uses Preferences (NVS)
 * - ESP8266: Uses LittleFS + JSON
 * - Other: RAM-only storage
 */

#include "DomoticsCore/IComponent.h"
#include "DomoticsCore/Logger.h"
#include "DomoticsCore/Timer.h"
#include "DomoticsCore/StorageEvents.h"
#include "Storage_HAL.h"  // Hardware Abstraction Layer for Storage
#include <map>
#include <vector>

namespace DomoticsCore {
namespace Components {

// Storage value types
enum class StorageValueType {
    String,
    Integer,
    Float,
    Boolean,
    Blob,
    UInt64
};

// Storage entry structure
struct StorageEntry {
    String key;
    StorageValueType type;
    String stringValue;
    int32_t intValue;
    float floatValue;
    bool boolValue;
    uint64_t uint64Value;
    std::vector<uint8_t> blobValue;
    size_t size;

    StorageEntry() : type(StorageValueType::String), intValue(0), floatValue(0.0f), boolValue(false), uint64Value(0), size(0) {}
};

// Storage configuration
struct StorageConfig {
    String namespace_name = "domotics";
    String componentName = "";  // Optional: custom component name for multi-instance support
    bool readOnly = false;
    size_t maxEntries = 100;
};

// Key definition for registration
struct StorageKeyDef {
    String key;
    char type;  // 's'=string, 'b'=bool, 'i'=int, 'f'=float, 'u'=uint64
    String description;
    
    StorageKeyDef(const String& k, char t, const String& desc = "") 
        : key(k), type(t), description(desc) {}
};

/**
 * @class DomoticsCore::Components::StorageComponent
 * @brief Key-value storage manager with HAL abstraction for multi-platform support.
 *
 * Opens a storage namespace, provides typed getters/setters with write deduplication,
 * and periodic maintenance/status reporting. Uses HAL::PlatformStorage which maps to
 * Preferences (ESP32), LittleFS (ESP8266), or RAM-only storage (other platforms).
 */
class StorageComponent : public IComponent {
private:
    StorageConfig storageConfig;
    mutable HAL::PlatformStorage storage;  // HAL abstraction for multi-platform (mutable for const accessors)
    Utils::NonBlockingDelay statusTimer;
    Utils::NonBlockingDelay maintenanceTimer;
    /// @note Cache assumes sole-writer access. External modifications to the
    /// storage backend (e.g., direct NVS writes) will NOT be reflected in the
    /// cache until the entry is removed and re-read.
    /// @note The cache holds only values that exist in the backend. When a key
    /// is missing, the caller-supplied default is returned but not cached, so a
    /// later read with another default gets that default and a later put() of
    /// the same value still writes.
    std::map<String, StorageEntry> cache;
    std::vector<StorageKeyDef> registeredKeys;  // Keys registered by components
    bool isOpen;
    size_t entryCount;
    String nameStr_;  // Holds dynamic name when not a string literal
    
public:
    /**
     * Constructor
     * @param config Storage configuration
     */
    StorageComponent(const StorageConfig& config = StorageConfig())
        : storageConfig(config), statusTimer(300000), maintenanceTimer(300000), // 5 minutes each
          isOpen(false), entryCount(0) {
        // Initialize component metadata immediately for dependency resolution
        // Use componentName if provided, otherwise generate unique name for non-default namespaces
        if (!storageConfig.componentName.isEmpty()) {
            nameStr_ = storageConfig.componentName;
            metadata.name = nameStr_.c_str();
        } else {
            metadata.name = "Storage";
        }
        metadata.version = "1.7.0";  // x-release-please-version
        metadata.author = "DomoticsCore";
        metadata.description = "Key-value storage component for preferences and app data";
        metadata.category = "Storage";
        metadata.tags = {"storage", "preferences", "nvs", "settings", "config"};
    }
    
    ComponentStatus begin() override {
        DLOG_I(LOG_STORAGE, "Initializing...");
        
        // Validate configuration
        if (storageConfig.namespace_name.isEmpty()) {
            DLOG_E(LOG_STORAGE, "Namespace cannot be empty");
            setStatus(ComponentStatus::ConfigError);
            return ComponentStatus::ConfigError;
        }
        
        if (storageConfig.namespace_name.length() > 15) {
            DLOG_E(LOG_STORAGE, "Namespace too long (max 15 chars): %s", storageConfig.namespace_name.c_str());
            setStatus(ComponentStatus::ConfigError);
            return ComponentStatus::ConfigError;
        }
        
        if (storageConfig.maxEntries < 1 || storageConfig.maxEntries > 500) {
            DLOG_E(LOG_STORAGE, "Invalid max_entries: %zu (must be 1-500)", storageConfig.maxEntries);
            setStatus(ComponentStatus::ConfigError);
            return ComponentStatus::ConfigError;
        }
        
        // Initialize preferences storage
        ComponentStatus status = initializeStorage();
        setStatus(status);

        // Emit storage ready event if successful
        if (status == ComponentStatus::Success && __dc_eventBus) {
            // BUG-30: publish the bytes, not the String. This one is a member
            // rather than a temporary, so the copied pointer stays valid while the
            // config does — and stops the moment setConfig() replaces it. Same
            // defect class, slower fuse. The sized overload copies the characters
            // themselves; length() + 1 carries the NUL.
            emit(StorageEvents::EVENT_READY,
                 storageConfig.namespace_name.c_str(),
                 storageConfig.namespace_name.length() + 1,
                 false);
        }

        return status;
    }
    
    void loop() override {
        if (getLastStatus() != ComponentStatus::Success) return;
        
        // Periodic status reporting
        if (statusTimer.isReady()) {
            updateStorageInfo();
            reportStorageStatus();
        }
        
        // Periodic maintenance
        if (maintenanceTimer.isReady()) {
            performMaintenance();
        }
    }
    
    ComponentStatus shutdown() override {
        DLOG_I(LOG_STORAGE, "Shutting down...");

        // F10: Shrink blobs and clear cache BEFORE storage.end() per spec
        for (auto& cached : cache) {
            cached.second.blobValue.clear();
            cached.second.blobValue.shrink_to_fit(); // Constitution XIV — release blob capacity
        }
        cache.clear();

        if (isOpen) {
            storage.end();
            isOpen = false;
        }

        setStatus(ComponentStatus::Success);
        return ComponentStatus::Success;
    }
    
    
    // Storage operations
    bool putString(const String& key, const String& value) {
        if (!writable()) return false;

        auto it = cache.find(key);
        if (it != cache.end() && it->second.type == StorageValueType::String && it->second.stringValue == value) return true;

        bool success = storage.putString(key.c_str(), value);
        if (success) {
            StorageEntry entry;
            entry.key = key;
            entry.type = StorageValueType::String;
            entry.stringValue = value;
            entry.size = value.length();
            cache[key] = entry;

            StorageEvents::StorageChangedEvent ev{};
            snprintf(ev.key, sizeof(ev.key), "%s", key.c_str());
            emit(StorageEvents::EVENT_CHANGED, ev);

            DLOG_D(LOG_STORAGE, "Stored string '%s' = '%s' (%d bytes)", key.c_str(), value.c_str(), value.length());
            return true;
        }
        return false;
    }
    
    bool putInt(const String& key, int32_t value) {
        if (!writable()) return false;

        auto it = cache.find(key);
        if (it != cache.end() && it->second.type == StorageValueType::Integer && it->second.intValue == value) return true;

        bool success = storage.putInt(key.c_str(), value);
        if (success) {
            StorageEntry entry;
            entry.key = key;
            entry.type = StorageValueType::Integer;
            entry.intValue = value;
            entry.size = sizeof(int32_t);
            cache[key] = entry;

            StorageEvents::StorageChangedEvent ev{};
            snprintf(ev.key, sizeof(ev.key), "%s", key.c_str());
            emit(StorageEvents::EVENT_CHANGED, ev);

            DLOG_D(LOG_STORAGE, "Stored int '%s' = %d", key.c_str(), value);
            return true;
        }
        return false;
    }
    
    bool putFloat(const String& key, float value) {
        if (!writable()) return false;

        auto it = cache.find(key);
        if (it != cache.end() && it->second.type == StorageValueType::Float && it->second.floatValue == value) return true;

        bool success = storage.putFloat(key.c_str(), value);
        if (success) {
            StorageEntry entry;
            entry.key = key;
            entry.type = StorageValueType::Float;
            entry.floatValue = value;
            entry.size = sizeof(float);
            cache[key] = entry;

            StorageEvents::StorageChangedEvent ev{};
            snprintf(ev.key, sizeof(ev.key), "%s", key.c_str());
            emit(StorageEvents::EVENT_CHANGED, ev);

            DLOG_D(LOG_STORAGE, "Stored float '%s' = %.2f", key.c_str(), value);
            return true;
        }
        return false;
    }
    
    bool putBool(const String& key, bool value) {
        if (!writable()) return false;

        auto it = cache.find(key);
        if (it != cache.end() && it->second.type == StorageValueType::Boolean && it->second.boolValue == value) return true;

        bool success = storage.putBool(key.c_str(), value);
        if (success) {
            StorageEntry entry;
            entry.key = key;
            entry.type = StorageValueType::Boolean;
            entry.boolValue = value;
            entry.size = sizeof(bool);
            cache[key] = entry;

            StorageEvents::StorageChangedEvent ev{};
            snprintf(ev.key, sizeof(ev.key), "%s", key.c_str());
            emit(StorageEvents::EVENT_CHANGED, ev);

            DLOG_D(LOG_STORAGE, "Stored bool '%s' = %s", key.c_str(), value ? "true" : "false");
            return true;
        }
        return false;
    }
    
    bool putULong64(const String& key, uint64_t value) {
        if (!writable()) return false;

        auto it = cache.find(key);
        if (it != cache.end() && it->second.type == StorageValueType::UInt64 && it->second.uint64Value == value) return true;

        bool success = storage.putULong64(key.c_str(), value);
        if (success) {
            StorageEntry entry;
            entry.key = key;
            entry.type = StorageValueType::UInt64;
            entry.uint64Value = value;
            entry.size = sizeof(uint64_t);
            cache[key] = entry;

            StorageEvents::StorageChangedEvent ev{};
            snprintf(ev.key, sizeof(ev.key), "%s", key.c_str());
            emit(StorageEvents::EVENT_CHANGED, ev);

            DLOG_D(LOG_STORAGE, "Stored uint64 '%s' = %llu", key.c_str(), (unsigned long long)value);
            return true;
        }
        return false;
    }
    
    bool putBlob(const String& key, const uint8_t* data, size_t length) {
        if (!writable()) return false;
        if (data == nullptr && length > 0) {
            DLOG_E(LOG_STORAGE, "putBlob: null data with length %zu", length);
            return false;
        }

        size_t written = storage.putBytes(key.c_str(), data, length);
        if (written == length) {
            StorageEntry entry;
            entry.key = key;
            entry.type = StorageValueType::Blob;
            entry.blobValue.assign(data, data + length);
            entry.size = length;
            cache[key] = entry;

            StorageEvents::StorageChangedEvent ev{};
            snprintf(ev.key, sizeof(ev.key), "%s", key.c_str());
            emit(StorageEvents::EVENT_CHANGED, ev);

            DLOG_D(LOG_STORAGE, "Stored blob '%s' (%zu bytes)", key.c_str(), length);
            return true;
        }
        return false;
    }
    
    String getString(const String& key, const String& defaultValue = "") {
        if (!isOpen) {
            DLOG_E(LOG_STORAGE, "Not open");
            return defaultValue;
        }
        // BUG-15: Check cache first
        auto it = cache.find(key);
        if (it != cache.end() && it->second.type == StorageValueType::String) {
            DLOG_D(LOG_STORAGE, "Cache hit string '%s'", key.c_str());
            return it->second.stringValue;
        }
        String value = storage.getString(key.c_str(), defaultValue);
        // Cache the HAL result for future reads
        if (storage.isKey(key.c_str())) {  // cache only stored values, never the caller default
            StorageEntry entry;
            entry.key = key;
            entry.type = StorageValueType::String;
            entry.stringValue = value;
            entry.size = value.length();
            cache[key] = entry;
        }
        DLOG_D(LOG_STORAGE, "Retrieved string '%s' = '%s'", key.c_str(), value.c_str());
        return value;
    }
    
    int32_t getInt(const String& key, int32_t defaultValue = 0) {
        if (!isOpen) {
            DLOG_E(LOG_STORAGE, "Not open");
            return defaultValue;
        }
        // BUG-15: Check cache first
        auto it = cache.find(key);
        if (it != cache.end() && it->second.type == StorageValueType::Integer) {
            DLOG_D(LOG_STORAGE, "Cache hit int '%s'", key.c_str());
            return it->second.intValue;
        }
        int32_t value = storage.getInt(key.c_str(), defaultValue);
        if (storage.isKey(key.c_str())) {  // cache only stored values, never the caller default
            StorageEntry entry;
            entry.key = key;
            entry.type = StorageValueType::Integer;
            entry.intValue = value;
            entry.size = sizeof(int32_t);
            cache[key] = entry;
        }
        DLOG_D(LOG_STORAGE, "Retrieved int '%s' = %d", key.c_str(), value);
        return value;
    }
    
    float getFloat(const String& key, float defaultValue = 0.0f) {
        if (!isOpen) {
            DLOG_E(LOG_STORAGE, "Not open");
            return defaultValue;
        }
        // BUG-15: Check cache first
        auto it = cache.find(key);
        if (it != cache.end() && it->second.type == StorageValueType::Float) {
            DLOG_D(LOG_STORAGE, "Cache hit float '%s'", key.c_str());
            return it->second.floatValue;
        }
        float value = storage.getFloat(key.c_str(), defaultValue);
        if (storage.isKey(key.c_str())) {  // cache only stored values, never the caller default
            StorageEntry entry;
            entry.key = key;
            entry.type = StorageValueType::Float;
            entry.floatValue = value;
            entry.size = sizeof(float);
            cache[key] = entry;
        }
        DLOG_D(LOG_STORAGE, "Retrieved float '%s' = %.2f", key.c_str(), value);
        return value;
    }
    
    bool getBool(const String& key, bool defaultValue = false) {
        if (!isOpen) {
            DLOG_E(LOG_STORAGE, "Not open");
            return defaultValue;
        }
        // BUG-15: Check cache first
        auto it = cache.find(key);
        if (it != cache.end() && it->second.type == StorageValueType::Boolean) {
            DLOG_D(LOG_STORAGE, "Cache hit bool '%s'", key.c_str());
            return it->second.boolValue;
        }
        bool value = storage.getBool(key.c_str(), defaultValue);
        if (storage.isKey(key.c_str())) {  // cache only stored values, never the caller default
            StorageEntry entry;
            entry.key = key;
            entry.type = StorageValueType::Boolean;
            entry.boolValue = value;
            entry.size = sizeof(bool);
            cache[key] = entry;
        }
        DLOG_D(LOG_STORAGE, "Retrieved bool '%s' = %s", key.c_str(), value ? "true" : "false");
        return value;
    }
    
    uint64_t getULong64(const String& key, uint64_t defaultValue = 0) {
        if (!isOpen) {
            DLOG_E(LOG_STORAGE, "Not open");
            return defaultValue;
        }
        // BUG-15: Check cache first
        auto it = cache.find(key);
        if (it != cache.end() && it->second.type == StorageValueType::UInt64) {
            DLOG_D(LOG_STORAGE, "Cache hit uint64 '%s'", key.c_str());
            return it->second.uint64Value;
        }
        uint64_t value = storage.getULong64(key.c_str(), defaultValue);
        if (storage.isKey(key.c_str())) {  // cache only stored values, never the caller default
            StorageEntry entry;
            entry.key = key;
            entry.type = StorageValueType::UInt64;
            entry.uint64Value = value;
            entry.size = sizeof(uint64_t);
            cache[key] = entry;
        }
        DLOG_D(LOG_STORAGE, "Retrieved uint64 '%s' = %llu", key.c_str(), (unsigned long long)value);
        return value;
    }
    
    size_t getBlob(const String& key, uint8_t* buffer, size_t maxLength) {
        if (!isOpen) {
            DLOG_E(LOG_STORAGE, "Not open");
            return 0;
        }
        // BUG-15: Check cache first
        auto it = cache.find(key);
        if (it != cache.end() && it->second.type == StorageValueType::Blob) {
            size_t copyLen = it->second.blobValue.size();
            if (copyLen > maxLength) copyLen = maxLength;
            if (buffer && copyLen > 0) {
                memcpy(buffer, it->second.blobValue.data(), copyLen);
            }
            DLOG_D(LOG_STORAGE, "Cache hit blob '%s' (%zu bytes)", key.c_str(), copyLen);
            return copyLen;
        }

        size_t length = storage.getBytesLength(key.c_str());
        if (length == 0) {
            DLOG_D(LOG_STORAGE, "Blob '%s' not found", key.c_str());
            return 0;
        }

        if (length > maxLength) {
            DLOG_W(LOG_STORAGE, "Blob '%s' too large (%zu > %zu)", key.c_str(), length, maxLength);
            length = maxLength;
        }

        size_t read = storage.getBytes(key.c_str(), buffer, length);
        // Cache for future reads
        if (read > 0 && buffer) {
            StorageEntry entry;
            entry.key = key;
            entry.type = StorageValueType::Blob;
            entry.blobValue.assign(buffer, buffer + read);
            entry.size = read;
            cache[key] = entry;
        }
        DLOG_D(LOG_STORAGE, "Retrieved blob '%s' (%zu bytes)", key.c_str(), read);
        return read;
    }
    
    bool remove(const String& key) {
        if (!writable()) return false;
        
        bool success = storage.remove(key.c_str());
        if (success) {
            auto it = cache.find(key);
            if (it != cache.end()) {
                it->second.blobValue.clear();
                it->second.blobValue.shrink_to_fit(); // Constitution XIV
                cache.erase(it);
            }

            StorageEvents::StorageChangedEvent ev{};
            snprintf(ev.key, sizeof(ev.key), "%s", key.c_str());
            emit(StorageEvents::EVENT_CHANGED, ev);

            DLOG_I(LOG_STORAGE, "Removed key: %s", key.c_str());
        } else {
            DLOG_E(LOG_STORAGE, "Failed to remove key: %s", key.c_str());
        }
        return success;
    }
    
    bool clear() {
        if (!writable()) return false;
        
        bool success = storage.clear();
        if (success) {
            for (auto& cached : cache) {
                cached.second.blobValue.clear();
                cached.second.blobValue.shrink_to_fit(); // Constitution XIV
            }
            cache.clear();

            StorageEvents::StorageChangedEvent ev{};
            snprintf(ev.key, sizeof(ev.key), "*");
            emit(StorageEvents::EVENT_CHANGED, ev);

            DLOG_I(LOG_STORAGE, "Cleared all entries");
        } else {
            DLOG_E(LOG_STORAGE, "Failed to clear");
        }
        return success;
    }
    
    bool exists(const String& key) {
        if (!isOpen) return false;
        return storage.isKey(key.c_str());
    }
    
    // Storage information
    bool isOpenStorage() const { return isOpen; }
    size_t getEntryCount() const {
        if (!isOpen) return 0;
        // F1: Query maxEntries from HAL to avoid constant mismatch
        // freeEntries() returns FREE entries, not used entries.
        return storage.maxEntries() - storage.freeEntries();
    }
    size_t getFreeEntries() const {
        if (!isOpen) return 0;
        return storage.freeEntries();
    }
    
    String getNamespace() const {
        return storageConfig.namespace_name;
    }
    
    String getStorageInfo() {
        char buf[256];
        int pos = snprintf(buf, sizeof(buf),
                 "Storage: HAL PlatformStorage\n"
                 "Namespace: %s\n"
                 "Open: %s\n"
                 "Read-only: %s",
                 storageConfig.namespace_name.c_str(),
                 isOpen ? "Yes" : "No",
                 storageConfig.readOnly ? "Yes" : "No");
        if (pos < 0) pos = 0;
        if ((size_t)pos >= sizeof(buf)) pos = sizeof(buf) - 1;
        if (isOpen) {
            size_t storedCount = getStoredKeyCount();
            snprintf(buf + pos, sizeof(buf) - pos,
                     "\nRegistered keys: %lu\nStored values: %lu",
                     (unsigned long)registeredKeys.size(),
                     (unsigned long)storedCount);
        }
        return String(buf);
    }
    
    std::vector<String> getKeys() {
        std::vector<String> keys;
        if (!isOpen) return keys;
        
        // Return registered keys that exist in storage
        for (const auto& kd : registeredKeys) {
            if (exists(kd.key)) {
                keys.push_back(kd.key);
            }
        }
        
        return keys;
    }
    
    /**
     * @brief Register storage keys for a component
     * @param componentName Name of the component registering keys
     * @param keys Vector of key definitions
     */
    void registerKeys(const String& componentName, const std::vector<StorageKeyDef>& keys) {
        for (const auto& key : keys) {
            registeredKeys.push_back(key);
        }
        DLOG_D(LOG_STORAGE, "Registered %zu keys for %s", keys.size(), componentName.c_str());
    }
    
    /**
     * @brief Get number of registered keys that exist in storage
     */
    size_t getStoredKeyCount() {
        size_t count = 0;
        for (const auto& kd : registeredKeys) {
            if (exists(kd.key)) count++;
        }
        return count;
    }
    
    /**
     * @brief Dump all registered configuration keys and their values
     * @return Formatted string with all keys and values
     */
    String dumpContents() {
        if (!isOpen) return "Storage: Not open\n";

        char headerBuf[128];
        snprintf(headerBuf, sizeof(headerBuf), "Storage Contents (namespace: %s):\n", storageConfig.namespace_name.c_str());
        String result = headerBuf;
        result += "──────────────────────────────────────\n";

        if (registeredKeys.empty()) {
            result += "  (no keys registered)\n";
        } else {
            int found = 0;
            char entryBuf[256];
            for (const auto& kd : registeredKeys) {
                if (exists(kd.key)) {
                    found++;
                    if (kd.type == 'b') {
                        snprintf(entryBuf, sizeof(entryBuf), "  %s = %s\n", kd.key.c_str(), getBool(kd.key, false) ? "true" : "false");
                    } else if (kd.type == 'i') {
                        snprintf(entryBuf, sizeof(entryBuf), "  %s = %d\n", kd.key.c_str(), getInt(kd.key, 0));
                    } else if (kd.type == 'f') {
                        snprintf(entryBuf, sizeof(entryBuf), "  %s = %.2f\n", kd.key.c_str(), getFloat(kd.key, 0.0f));
                    } else if (kd.type == 'u') {
                        snprintf(entryBuf, sizeof(entryBuf), "  %s = %llu\n", kd.key.c_str(), (unsigned long long)getULong64(kd.key, 0));
                    } else {
                        String val = getString(kd.key, "");
                        if (kd.key.indexOf("pass") >= 0 && val.length() > 0) {
                            snprintf(entryBuf, sizeof(entryBuf), "  %s = ****\n", kd.key.c_str());
                        } else {
                            snprintf(entryBuf, sizeof(entryBuf), "  %s = \"%s\"\n", kd.key.c_str(), val.c_str());
                        }
                    }
                    result += entryBuf;
                }
            }

            if (found == 0) {
                result += "  (no stored values found)\n";
            }

            result += "──────────────────────────────────────\n";
            snprintf(entryBuf, sizeof(entryBuf), "Registered: %lu keys, Found: %d stored\n", (unsigned long)registeredKeys.size(), found);
            result += entryBuf;
        }

        return result;
    }

private:
    // Write gate for every platform: only ESP32's Preferences refuses writes on its own.
    bool writable() const {
        if (!isOpen) {
            DLOG_E(LOG_STORAGE, "Not open");
            return false;
        }
        if (storageConfig.readOnly) {
            DLOG_W(LOG_STORAGE, "Read-only namespace: write refused");
            return false;
        }
        return true;
    }

    ComponentStatus initializeStorage() {
        DLOG_I(LOG_STORAGE, "Initializing storage via HAL...");
        
        // Open preferences with namespace
        bool success = storage.begin(storageConfig.namespace_name.c_str(), storageConfig.readOnly);
        
        if (success) {
            isOpen = true;
            updateStorageInfo();
            DLOG_I(LOG_STORAGE, "Storage opened successfully (namespace: %s)", 
                   storageConfig.namespace_name.c_str());
            return ComponentStatus::Success;
        } else {
            DLOG_E(LOG_STORAGE, "Failed to open preferences");
            return ComponentStatus::HardwareError;
        }
    }

    void updateStorageInfo() {
        if (!isOpen) {
            entryCount = 0;
            return;
        }
        
        // Update entry count from cache
        entryCount = cache.size();
        
        DLOG_D(LOG_STORAGE, "Info updated: %zu entries cached", entryCount);
    }

    void reportStorageStatus() {
        if (!isOpen) {
            DLOG_W(LOG_STORAGE, "Not open");
            return;
        }
        
        DLOG_I(LOG_STORAGE, "=== Status ===");
        DLOG_I(LOG_STORAGE, "%s", getStorageInfo().c_str());
        
        // Check storage usage
        float usagePercent = (float)entryCount / (float)storageConfig.maxEntries * 100.0f;
        if (usagePercent > 90.0f) {
            DLOG_W(LOG_STORAGE, "Usage high: %.1f%%", usagePercent);
        }
    }

    void performMaintenance() {
        if (!isOpen) return;
        
        DLOG_D(LOG_STORAGE, "Performing maintenance...");
        
        // Update storage information
        updateStorageInfo();
        
        // Log cache statistics
        DLOG_D(LOG_STORAGE, "Cache contains %zu entries", cache.size());
        
        // Check for storage health
        if (entryCount >= storageConfig.maxEntries) {
            DLOG_W(LOG_STORAGE, "At maximum capacity (%zu entries)", entryCount);
        }
    }
};

} // namespace Components
} // namespace DomoticsCore
