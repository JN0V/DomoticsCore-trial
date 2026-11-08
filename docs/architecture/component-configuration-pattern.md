<!-- workline
sources: [DomoticsCore-NTP/include/DomoticsCore/NTP.h, DomoticsCore-System/include/DomoticsCore/System.h, DomoticsCore-System/include/DomoticsCore/SystemPersistence.h]
checked: 500862d
verified: agent:documentalist
judged-in-parts: 33c09cb
-->

# Component Configuration Pattern

## Standard Pattern for All Components

Every component with persistent configuration MUST follow this pattern for consistency and maintainability.

---

## Pages

| Page | What it holds |
|------|---------------|
| [Configuration Structure and Component Implementation](component-configuration-implementation.md) | sections 1, 2 and 5: the config struct, the constructor, `getConfig()`, `setConfig()`, a complete example |
| [Storage and WebUI Integration](component-configuration-storage.md) | sections 3, 4, 8 and 9: get, override, set; saving from the WebUI; anti-patterns; migration checklist |
| [Current Component Status](component-configuration-status.md) | section 6: the components that follow the pattern |
| [Components Needing Full Pattern](component-configuration-status-needed.md) | section 6: the components that do not follow it yet |

---

## 7. Benefits of This Pattern

1. **Consistency**: All components work the same way
2. **Safety**: Defaults always preserved
3. **Flexibility**: Partial config updates supported
4. **Testability**: Easy to mock and test
5. **User-Friendly**: Graceful degradation if Storage empty
6. **Performance**: Only restarts when necessary

---

## 10. References

- **Implementation Example**: `DomoticsCore-NTP/include/DomoticsCore/NTP.h`
- **Storage Integration**: `DomoticsCore-System/include/DomoticsCore/System.h` (sections 6.2-6.5)
- **Custom Components Guide**: `docs/guides/custom-components.md`
