<!-- workline
sources: [DomoticsCore-System/include/DomoticsCore/System.h, DomoticsCore-System/include/DomoticsCore/SystemPersistence.h]
checked: 500862d
verified: agent:documentalist
judged-in-parts: 33c09cb
-->
# Component Configuration Pattern -- Storage and WebUI Integration

Part of the [Component Configuration Pattern](component-configuration-pattern.md).

## 3. Storage Integration (in SystemPersistence.h)

### Pattern: Get → Override → Set

```cpp
// ====================================================================
// 6.X. Load MyComponent configuration from Storage
// ====================================================================
#if __has_include(<DomoticsCore/MyComponent.h>) && __has_include(<DomoticsCore/Storage.h>)
if (config.enableMyComponent && config.enableStorage) {
    auto* storageComp = core.getComponent<Components::StorageComponent>("Storage");
    auto* myComp = core.getComponent<Components::MyComponent>("MyComponent");
    if (storageComp && myComp) {
        // 1. GET current config (preserves defaults)
        Components::MyComponentConfig cfg = myComp->getConfig();
        
        // 2. OVERRIDE only fields from Storage (keeps defaults if key missing)
        cfg.enabled = storageComp->getBool("my_enabled", cfg.enabled);
        cfg.updateInterval = storageComp->getInt("my_interval", cfg.updateInterval);
        cfg.serverUrl = storageComp->getString("my_server", cfg.serverUrl);
        cfg.threshold = storageComp->getFloat("my_threshold", cfg.threshold);
        
        DLOG_I(LOG_SYSTEM, "Loading MyComponent config from Storage: interval=%d", 
               cfg.updateInterval);
        
        // 3. SET the merged config
        myComp->setConfig(cfg);
    }
}
#endif
```

**Critical Steps:**
1. **GET** current config → Preserves constructor defaults and non-persisted fields
2. **OVERRIDE** from Storage → Uses Storage value OR falls back to current value
3. **SET** merged config → Applies changes intelligently

**Why This Pattern?**
- ✅ Preserves non-persisted fields (e.g., `port`, `maxClients`)
- ✅ Graceful degradation if Storage empty/missing
- ✅ Constructor defaults always available
- ✅ No data loss on partial updates

---

## 4. WebUI Integration

Save config when user modifies settings:

```cpp
void onConfigChanged(const MyComponentConfig& cfg) {
    auto* storage = getCore()->getComponent<StorageComponent>("Storage");
    if (storage) {
        storage->putBool("my_enabled", cfg.enabled);
        storage->putInt("my_interval", cfg.updateInterval);
        storage->putString("my_server", cfg.serverUrl);
        storage->putFloat("my_threshold", cfg.threshold);
        
        DLOG_I("MyComponent", "Config saved to Storage");
    }
    
    // Apply immediately
    setConfig(cfg);
}
```


## 8. Anti-Patterns to Avoid

### ❌ Creating Empty Config

```cpp
// BAD: Loses all defaults
MyComponentConfig cfg;
cfg.field1 = storage->getString("field1", "");
myComp->setConfig(cfg);  // All other fields now have wrong values!
```

### ✅ Get → Override → Set

```cpp
// GOOD: Preserves defaults
MyComponentConfig cfg = myComp->getConfig();
cfg.field1 = storage->getString("field1", cfg.field1);
myComp->setConfig(cfg);  // All fields correct
```

---

## 9. Migration Checklist

For each component needing update:

- [ ] Add `getConfig()` method
- [ ] Verify `setConfig()` is smart (detects changes, minimal restart)
- [ ] Add a load function in `SystemPersistence.h`, called from `loadAllConfigs`
- [ ] Test with empty Storage (should use defaults)
- [ ] Test with partial Storage (should merge correctly)
- [ ] Test with full Storage (should override everything)
- [ ] Update component documentation

