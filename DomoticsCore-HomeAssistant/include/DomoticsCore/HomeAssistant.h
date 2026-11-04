#pragma once

/**
 * @file HomeAssistant.h
 * @brief Home Assistant MQTT Discovery component.
 * 
 * @example DomoticsCore-HomeAssistant/examples/BasicHA/src/main.cpp
 * @example DomoticsCore-HomeAssistant/examples/HAWithWebUI/src/main.cpp
 */

#include <DomoticsCore/IComponent.h>
#include <DomoticsCore/ComponentRegistry.h>  // to resolve the MQTT component
#include <DomoticsCore/Logger.h>
#include <DomoticsCore/MQTT.h>  // For event structures (MQTTPublishEvent, MQTTSubscribeEvent, MQTTMessageEvent)
#include <DomoticsCore/MQTTEvents.h>  // For MQTT event names
#include "HAEvents.h"  // For HA event names
#include "HAEntity.h"
#include "HASensor.h"
#include "HABinarySensor.h"
#include "HASwitch.h"
#include "HALight.h"
#include "HAButton.h"
#include "HAAlarmControlPanel.h"
#include <vector>
#include <memory>

namespace DomoticsCore {
namespace Components {
namespace HomeAssistant {

// HAEntity's topic buffer is sized to the event field every topic crosses; the
// refusal below compares against the field, so the two must not drift apart.
static_assert(HA_TOPIC_BUF_SIZE == DomoticsCore::Components::MQTT_EVENT_TOPIC_SIZE,
              "the topic buffer and the event field it crosses must be one size");

namespace HA {
constexpr size_t MAX_NODE_ID         = 33;   // 32 chars + null (MQTT client ID limit)
constexpr size_t MAX_DEVICE_NAME     = 65;   // 64 chars + null (HA device registry)
constexpr size_t MAX_MANUFACTURER    = 33;   // 32 chars + null
constexpr size_t MAX_MODEL           = 33;   // 32 chars + null
constexpr size_t MAX_SW_VERSION      = 17;   // 16 chars + null (semver with pre-release)
constexpr size_t MAX_DISCOVERY_PREFIX = 33;   // 32 chars + null
constexpr size_t MAX_AVAIL_TOPIC     = 129;  // 128 chars + null (generated topic)
constexpr size_t MAX_CONFIG_URL      = 129;  // 128 chars + null (http://IP:port)
constexpr size_t MAX_SUGGESTED_AREA  = 33;   // 32 chars + null

inline void setField(char* dest, const char* src, size_t maxLen) {
    if (!src) { dest[0] = '\0'; return; }
    size_t srcLen = strlen(src);
    if (srcLen >= maxLen) {
        DLOG_W("HA", "Field truncated: '%.*s...' (max %zu)", (int)(maxLen - 1), src, maxLen - 1);
    }
    strncpy(dest, src, maxLen - 1);
    dest[maxLen - 1] = '\0';
}
} // namespace HA

/**
 * @brief Event for entity added to Home Assistant
 */
struct HAEntityAddedEvent {
    char id[64];           // Entity ID
    char component[32];    // Component type (sensor, switch, etc.)
};

/**
 * @brief Configuration for Home Assistant component
 */
struct HAConfig {
    // Device identity (populated by System.h from SystemConfig)
    char nodeId[HA::MAX_NODE_ID];
    char deviceName[HA::MAX_DEVICE_NAME];
    char manufacturer[HA::MAX_MANUFACTURER];
    char model[HA::MAX_MODEL];
    char swVersion[HA::MAX_SW_VERSION];

    // Home Assistant specific settings
    bool retainDiscovery = true;
    char discoveryPrefix[HA::MAX_DISCOVERY_PREFIX];
    char availabilityTopic[HA::MAX_AVAIL_TOPIC];
    char configUrl[HA::MAX_CONFIG_URL];
    char suggestedArea[HA::MAX_SUGGESTED_AREA];

    HAConfig() : retainDiscovery(true) {
        HA::setField(nodeId, "myDeviceId", sizeof(nodeId));
        HA::setField(deviceName, "My Device", sizeof(deviceName));
        HA::setField(manufacturer, "DomoticsCore", sizeof(manufacturer));
        HA::setField(model, "MyDeviceModel", sizeof(model));
        HA::setField(swVersion, "1.0.0", sizeof(swVersion));
        HA::setField(discoveryPrefix, "homeassistant", sizeof(discoveryPrefix));
        availabilityTopic[0] = '\0';
        configUrl[0] = '\0';
        suggestedArea[0] = '\0';
    }
};

/**
 * @brief Home Assistant MQTT Discovery Component
 * 
 * Provides automatic entity registration and state management for Home Assistant.
 * Supports sensors, switches, lights, buttons, and more via MQTT discovery protocol.
 */
class HomeAssistantComponent : public IComponent {
public:
    /**
     * @brief Construct HomeAssistant component
     * @param config HA configuration
     */
    HomeAssistantComponent(const HAConfig& config = HAConfig())
        : config(config) {
        // Initialize component metadata immediately for dependency resolution
        metadata.name = "HomeAssistant";
        metadata.version = "2.7.0";  // x-release-please-version
        metadata.author = "DomoticsCore";
        metadata.description = "Home Assistant MQTT Discovery integration";
        availabilityTopicNamed = (this->config.availabilityTopic[0] != '\0');
        if (this->config.availabilityTopic[0] == '\0') {
            int written = snprintf(this->config.availabilityTopic, HA::MAX_AVAIL_TOPIC,
                                   "%s/%s/availability", this->config.discoveryPrefix, this->config.nodeId);
            if (written >= (int)HA::MAX_AVAIL_TOPIC) {
                DLOG_W("HA", "availabilityTopic truncated (%d chars, max %zu)",
                       written, HA::MAX_AVAIL_TOPIC - 1);
            }
        }
    }
    
    ~HomeAssistantComponent() override = default;
    
    // IComponent interface
    ComponentStatus begin() override {
        DLOG_I(LOG_HA, "Initializing Home Assistant integration");
        DLOG_I(LOG_HA, "Node ID: %s", config.nodeId);
        DLOG_I(LOG_HA, "Discovery prefix: %s", config.discoveryPrefix);

        // Before anything advertises a topic, make it the one the broker corrects.
        reconcileAvailabilityWithWill();
        
        // Subscribe to MQTT events via EventBus
        on<bool>(DomoticsCore::MQTTEvents::EVENT_CONNECTED, [this](const bool&) {
            DLOG_I(LOG_HA, "MQTT connected (via EventBus), publishing availability");
            // A new session may carry a will moved since begin(): follow it.
            reconcileAvailabilityWithWill();
            mqttConnected = true;
            setAvailable(true);
            subscribeToCommands();
            
            if (stats.entityCount > 0) {
                DLOG_I(LOG_HA, "Publishing HA discovery after MQTT connect");
                publishDiscovery();
            } else {
                DLOG_W(LOG_HA, "No entities registered yet; skipping discovery on connect");
            }
        });
        
        on<bool>(DomoticsCore::MQTTEvents::EVENT_DISCONNECTED, [this](const bool&) {
            DLOG_W(LOG_HA, "MQTT disconnected (via EventBus)");
            mqttConnected = false;
        });
        
        // Subscribe to incoming MQTT messages.
        //
        // MEM-2: ev.topic and ev.payload are already char[], and every message the
        // shared client receives arrives here — before findEntity has decided the
        // message concerns HomeAssistant at all. Wrapping them in Strings asked the
        // allocator for the topic on every single message (38 characters and up,
        // where the small-string buffer stops at 10 on ESP8266 and 14 on ESP32), to parse text the
        // component was handed as characters. They are passed through as they are.
        on<DomoticsCore::Components::MQTTMessageEvent>(DomoticsCore::MQTTEvents::EVENT_MESSAGE, [this](const DomoticsCore::Components::MQTTMessageEvent& ev) {
            handleCommand(ev.topic, ev.payload);
        });
        
        // Note: Initial MQTT state will be signaled via mqtt/connected event
        
        return ComponentStatus::Success;
    }
    
    void loop() override {
        // Discovery leaves one document per loop, then the states an outage
        // held a few per loop: the event queue never holds the whole burst.
        if (!mqttConnected) return;
        HAL::Platform::LockGuard guard(storeLock);
        if (discoveryPending) {
            publishNextDiscovery();
            return;
        }
        if (pendingPublishes.empty()) return;

        size_t sent = 0;
        while (sent < FLUSH_PER_LOOP && !pendingPublishes.empty()) {
            const PendingPublish& held = pendingPublishes.front();
            // A topic that cannot leave never will — a prefix or node id changed
            // under the store — so it is dropped rather than kept, or it would
            // hold every value behind it for the life of the process.
            if (!heldTopicFits(held.entity, held.attributes)) {
                heldBytes -= static_cast<uint16_t>(held.payload.length());
                pendingPublishes.erase(pendingPublishes.begin());
                stats.statesRefused++;
                continue;
            }
            // Erase only what went out: a refused publish keeps its slot, or the
            // store would lose the very value it exists to keep.
            if (!sendToBroker(held.entity, held.payload, held.attributes)) break;
            heldBytes -= static_cast<uint16_t>(held.payload.length());
            pendingPublishes.erase(pendingPublishes.begin());
            ++sent;
        }
        // Empty is where this costs nothing: erasing keeps the buffer.
        if (pendingPublishes.empty()) pendingPublishes.shrink_to_fit();
    }
    
    /**
     * @brief Check if component is ready (MQTT connected and availability published)
     * @return true if ready for state publishing
     */
    bool isReady() const {
        return availabilityPublished && mqttConnected;
    }
    
    ComponentStatus shutdown() override {
        DLOG_I(LOG_HA, "Shutting down");
        // Over a dropped link nothing can be delivered: queuing it would only
        // waste EventBus space.
        if (mqttConnected) {
            setAvailable(false);
            removeDiscovery();
        }
        // Nothing will drain it after this, and the entities it points at are
        // being withdrawn from Home Assistant anyway.
        HAL::Platform::LockGuard guard(storeLock);
        if (!pendingPublishes.empty()) {
            DLOG_W(LOG_HA, "Discarding %u held payloads at shutdown",
                   (unsigned)pendingPublishes.size());
        }
        pendingPublishes.clear();
        pendingPublishes.shrink_to_fit();
        heldBytes = 0;
        return ComponentStatus::Success;
    }
    
    // ========== Entity Management ==========

    /**
     * @brief The registered entity with this id, to set its discovery fields after add*(); nullptr if none.
     * Set them before the connect that publishes discovery; after it, call republishEntity(id).
     */
    HAEntity* entity(const String& id) { return findEntity(id); }

    /**
     * @brief Add a sensor entity
     */
    void addSensor(const String& id, const String& name, const String& unit = "", 
                   const String& deviceClass = "", const String& icon = "", const String& stateClass = "") {
        auto sensor = std::make_unique<HASensor>(id, name, unit, deviceClass, icon);
        if (!stateClass.isEmpty()) {
            sensor->stateClass = stateClass;
        }
        warnIfDuplicateId(id);
        entities.push_back(std::move(sensor));
        stats.entityCount++;
        DLOG_I(LOG_HA, "Added sensor: %s", id.c_str());
        {
            HAEntityAddedEvent ev{};
            snprintf(ev.id, sizeof(ev.id), "%s", id.c_str());
            snprintf(ev.component, sizeof(ev.component), "sensor");
            emit(DomoticsCore::HAEvents::EVENT_ENTITY_ADDED, ev);
        }
        if (mqttConnected) {
            republishEntity(id);
        }
    }
    
    /**
     * @brief Add a binary sensor entity
     */
    void addBinarySensor(const String& id, const String& name,
                         const String& deviceClass = "", const String& icon = "") {
        auto sensor = std::make_unique<HABinarySensor>(id, name, deviceClass, icon);
        warnIfDuplicateId(id);
        entities.push_back(std::move(sensor));
        stats.entityCount++;
        DLOG_I(LOG_HA, "Added binary sensor: %s", id.c_str());
        {
            HAEntityAddedEvent ev{};
            snprintf(ev.id, sizeof(ev.id), "%s", id.c_str());
            snprintf(ev.component, sizeof(ev.component), "binary_sensor");
            emit(DomoticsCore::HAEvents::EVENT_ENTITY_ADDED, ev);
        }
        if (mqttConnected) {
            republishEntity(id);
        }
    }

    /**
     * @brief Add a switch entity
     */
    void addSwitch(const String& id, const String& name, const String& icon = "",
                   bool autoPublishState = true, bool optimistic = false) {
        auto sw = std::make_unique<HASwitch>(id, name, icon);
        sw->autoPublishState = autoPublishState;
        sw->optimistic = optimistic;
        warnIfDuplicateId(id);
        entities.push_back(std::move(sw));
        stats.entityCount++;
        DLOG_I(LOG_HA, "Added switch: %s", id.c_str());
        {
            HAEntityAddedEvent ev{};
            snprintf(ev.id, sizeof(ev.id), "%s", id.c_str());
            snprintf(ev.component, sizeof(ev.component), "switch");
            emit(DomoticsCore::HAEvents::EVENT_ENTITY_ADDED, ev);
        }
        if (mqttConnected) {
            republishEntity(id);
        }
    }

    /**
     * @brief Add a light entity
     */
    void addLight(const String& id, const String& name) {
        auto light = std::make_unique<HALight>(id, name);
        warnIfDuplicateId(id);
        entities.push_back(std::move(light));
        stats.entityCount++;
        DLOG_I(LOG_HA, "Added light: %s", id.c_str());
        {
            HAEntityAddedEvent ev{};
            snprintf(ev.id, sizeof(ev.id), "%s", id.c_str());
            snprintf(ev.component, sizeof(ev.component), "light");
            emit(DomoticsCore::HAEvents::EVENT_ENTITY_ADDED, ev);
        }
        if (mqttConnected) {
            republishEntity(id);
        }
    }

    /**
     * @brief Add a button entity
     */
    void addButton(const String& id, const String& name, const String& icon = "") {
        auto button = std::make_unique<HAButton>(id, name, icon);
        warnIfDuplicateId(id);
        entities.push_back(std::move(button));
        stats.entityCount++;
        DLOG_I(LOG_HA, "Added button: %s", id.c_str());
        {
            HAEntityAddedEvent ev{};
            snprintf(ev.id, sizeof(ev.id), "%s", id.c_str());
            snprintf(ev.component, sizeof(ev.component), "button");
            emit(DomoticsCore::HAEvents::EVENT_ENTITY_ADDED, ev);
        }
        if (mqttConnected) {
            republishEntity(id);
        }
    }

    /**
     * @brief Add an alarm control panel entity
     */
    void addAlarmControlPanel(
        const String& id, const String& name,
        const String& icon = "mdi:shield-home",
        AlarmFeature features = AlarmFeature::ArmAway,
        const String& code = "",
        bool codeArmRequired = false,
        bool codeDisarmRequired = false,
        bool codeTriggerRequired = false) {
        auto panel = std::make_unique<HAAlarmControlPanel>(id, name, icon);
        panel->supportedFeatures = features;
        panel->code = code;
        panel->codeArmRequired = codeArmRequired;
        panel->codeDisarmRequired = codeDisarmRequired;
        panel->codeTriggerRequired = codeTriggerRequired;
        warnIfDuplicateId(id);
        entities.push_back(std::move(panel));
        stats.entityCount++;
        DLOG_I(LOG_HA, "Added alarm_control_panel: %s", id.c_str());
        {
            HAEntityAddedEvent ev{};
            snprintf(ev.id, sizeof(ev.id), "%s", id.c_str());
            snprintf(ev.component, sizeof(ev.component), "alarm_control_panel");
            emit(DomoticsCore::HAEvents::EVENT_ENTITY_ADDED, ev);
        }
        if (mqttConnected) { republishEntity(id); }
    }

    // ========== State Publishing ==========

    /**
     * @brief Publish entity state (string), or hold it until the broker is back.
     *
     * While the link is down the payload is stored, and loop() drains the store
     * after the reconnection. Safe from any task: the store takes a lock.
     */
    void publishState(const String& id, const String& state) {
        HAEntity* entity = findEntity(id);
        if (!entity) {
            DLOG_W(LOG_HA, "Entity not found: %s", id.c_str());
            return;
        }

        publishOrHold(entity, state, false);
    }
    
    /**
     * @brief Publish entity state (numeric)
     */
    void publishState(const String& id, float value) {
        publishState(id, String(value, 2));
    }
    
    /**
     * @brief Publish entity state (boolean)
     */
    void publishState(const String& id, bool state) {
        publishState(id, String(state ? HAPayload::ON : HAPayload::OFF));
    }

    /**
     * @brief Publish entity state (const char*) — prevents implicit bool conversion
     */
    void publishState(const String& id, const char* state) {
        publishState(id, String(state));
    }

    /**
     * @brief Publish entity state with JSON (for lights with brightness).
     *
     * Held through an outage like the string form.
     */
    void publishStateJson(const String& id, const JsonDocument& doc) {
        HAEntity* entity = findEntity(id);
        if (!entity) return;
        
        String payload;
        serializeJson(doc, payload);
        publishOrHold(entity, payload, false);
    }
    
    /**
     * @brief Publish entity attributes (additional metadata). Always retained.
     *
     * Held through an outage like a state.
     */
    void publishAttributes(const String& id, const JsonDocument& attributes) {
        HAEntity* entity = findEntity(id);
        if (!entity) return;
        
        String payload;
        serializeJson(attributes, payload);
        publishOrHold(entity, payload, true);
    }
    
    // ========== Availability ==========
    
    /**
     * @brief Set device availability status
     */
    void setAvailable(bool available) {
        String payload = available ? HAPayload::AVAILABLE : HAPayload::NOT_AVAILABLE;
        DLOG_I(LOG_HA, "Publishing availability:");
        DLOG_I(LOG_HA, "  Topic: %s", config.availabilityTopic);
        DLOG_I(LOG_HA, "  Payload: %s", payload.c_str());
        
        if (!topicFitsTheWire((int)strlen(config.availabilityTopic), "availability")) return;
        mqttPublish(config.availabilityTopic, payload, 0, true);
        DLOG_I(LOG_HA, "  Availability published");
        availabilityPublished = available;
    }
    
    // ========== Discovery ==========
    
    /**
     * @brief Publish discovery messages for all entities
     *
     * Asynchronous: the documents leave one per loop() while the broker is
     * connected, and EVENT_DISCOVERY_PUBLISHED fires after the last one. A call
     * during a pass restarts it from the first entity.
     */
    void publishDiscovery() {
        HAL::Platform::LockGuard guard(storeLock);
        DLOG_I(LOG_HA, "Publishing discovery for %zu entities", entities.size());
        discoveryNext = 0;
        discoveryPending = true;
        stats.discoveryCount++;
    }

    /** @brief True while a discovery pass still has documents to send. */
    bool isDiscoveryPending() const { return discoveryPending; }
    
    /**
     * @brief Remove discovery messages (makes entities disappear from HA)
     */
    void removeDiscovery() {
        DLOG_I(LOG_HA, "Removing discovery for all entities");
        {
            // A pass still running would publish the documents again, retained.
            HAL::Platform::LockGuard guard(storeLock);
            discoveryPending = false;
        }
        
        for (const auto& entity : entities) {
            char topic[HA_TOPIC_BUF_SIZE];
            if (!topicFitsTheWire(entity->getDiscoveryTopic(topic, sizeof(topic), config.nodeId,
                                                            config.discoveryPrefix), entity->id.c_str())) continue;
            mqttPublish(topic, "", 0, config.retainDiscovery);  // Empty payload removes entity
        }
    }
    
    /**
     * @brief Republish single entity discovery
     */
    void republishEntity(const String& id) {
        HAEntity* entity = findEntity(id);
        if (!entity) return;
        
        JsonDocument deviceDoc;
        JsonObject device = deviceDoc.to<JsonObject>();
        buildDeviceInfo(device);
        
        publishEntityDiscovery(entity, device);
    }
    
    // ========== Configuration ==========
    
    void setConfig(const HAConfig& cfg) {
        config = cfg;
        // "Named" is decided by what the caller HANDED us, before this function
        // generates a topic of its own. The WebUI clears the field so the topic
        // follows a new nodeId; generating one here and then calling it named
        // would push it onto the will and bounce a live session on every
        // settings save. A topic this component adopted from the will is not a
        // named one either, or SystemPersistence's getConfig()/setConfig()
        // round trip would make every boot look like an application choice.
        const bool cameInNamed = (config.availabilityTopic[0] != '\0') &&
                                 strcmp(config.availabilityTopic, adoptedFromWill_) != 0;
        availabilityTopicNamed = cameInNamed;
        if (config.availabilityTopic[0] == '\0') {
            int written = snprintf(config.availabilityTopic, HA::MAX_AVAIL_TOPIC,
                                   "%s/%s/availability", config.discoveryPrefix, config.nodeId);
            if (written >= (int)HA::MAX_AVAIL_TOPIC) {
                DLOG_W("HA", "availabilityTopic truncated (%d chars, max %zu)",
                       written, HA::MAX_AVAIL_TOPIC - 1);
            }
        }
        // Persistence and the WebUI both land here after begin(), so the two
        // topics have to be reconciled again or they drift apart.
        if (__dc_registry) reconcileAvailabilityWithWill();
        // The command filter was built from the prefix at connect time. A prefix
        // changed here moves where commands arrive, so the subscription follows
        // it now rather than at the next reconnection.
        if (mqttConnected) subscribeToCommands();
    }
    
    /**
     * @brief Get current HomeAssistant configuration
     * @return Current HAConfig
     */
    const HAConfig& getConfig() const { return config; }
    
    void setDeviceInfo(const char* name, const char* model,
                       const char* manufacturer, const char* swVersion) {
        HA::setField(config.deviceName, name, sizeof(config.deviceName));
        HA::setField(config.model, model, sizeof(config.model));
        HA::setField(config.manufacturer, manufacturer, sizeof(config.manufacturer));
        HA::setField(config.swVersion, swVersion, sizeof(config.swVersion));
    }
    
    // ========== Statistics ==========
    
    struct HAStatistics {
        uint32_t entityCount = 0;
        uint32_t discoveryCount = 0;
        uint32_t discoveryRefused = 0;  ///< Configs never handed to MQTT: over the event field (BUG-38)
        uint32_t stateUpdates = 0;
        uint32_t statesRefused = 0;     ///< States and attributes never held: over the event field, or no heap
        uint32_t commandsReceived = 0;
    };
    
    const HAStatistics& getStatistics() const { return stats; }

    /**
     * @brief States and attributes a broker outage is holding.
     *
     * One slot per entity and topic, so it never exceeds twice the entity
     * count however long the outage lasts, and never more than MAX_HELD_BYTES
     * of payload. Non-zero during an outage and for the first loops after a
     * reconnection, which drains four per loop(); zero once it has drained.
     */
    size_t getPendingPublishCount() { 
        HAL::Platform::LockGuard guard(storeLock);
        return pendingPublishes.size();
    }
    
    /**
     * @brief Check if MQTT is connected
     * @return true if MQTT connection is active
     */
    bool isMQTTConnected() const {
        return mqttConnected;
    }
    
private:
    HAConfig config;
    std::vector<std::unique_ptr<HAEntity>> entities;
    HAStatistics stats;
    bool availabilityPublished = false;  // Track if initial availability sent
    // Whether the application named availabilityTopic, or it was generated or
    // adopted. Not inferable from "the field is non-empty": begin() fills it.
    bool availabilityTopicNamed = false;
    char adoptedFromWill_[HA::MAX_AVAIL_TOPIC] = {0};
    bool mqttConnected = false;  // Track MQTT connection state via EventBus
    char commandTopicFilter[HA_TOPIC_BUF_SIZE] = {};  // Stored to keep pointer valid for EventBus

    // What a broker outage stopped, one slot per entity and topic. Held here
    // rather than on HAEntity: empty while the link is up, it costs the
    // component one vector instead of a String on every entity.
    struct PendingPublish {
        HAEntity* entity;   // stable — entities are append-only unique_ptr
        String payload;
        bool attributes;    // the attributes topic rather than the state topic
    };
    std::vector<PendingPublish> pendingPublishes;
    uint16_t heldBytes = 0;
    // The web server's task publishes settings, so a consumer can reach a state
    // publish from somewhere other than the loop. Taken around every touch of
    // the store; the EventBus releases its own before dispatching a handler, so
    // this one is only ever taken first and the two cannot deadlock.
    HAL::Platform::RecursiveLock storeLock;
    // Small enough that a reconnection's discovery documents and the states
    // behind them never fill the event queue at once.
    static constexpr size_t FLUSH_PER_LOOP = 4;
    // Each document is an 830-byte event: sent all at once from the connect
    // handler, twenty of them outgrow an ESP8266's heap before any is written.
    bool discoveryPending = false;
    size_t discoveryNext = 0;

    void publishNextDiscovery() {
        if (discoveryNext < entities.size()) {
            JsonDocument deviceDoc;
            JsonObject device = deviceDoc.to<JsonObject>();
            buildDeviceInfo(device);
            publishEntityDiscovery(entities[discoveryNext++].get(), device);
        }
        if (discoveryNext < entities.size()) return;
        discoveryPending = false;
        emit(DomoticsCore::HAEvents::EVENT_DISCOVERY_PUBLISHED, (int)entities.size());
    }
    // A slot count is not a memory bound: each one may hold 699 characters, and
    // an outage can hold two per entity. The budget is what the store is
    // allowed on the smaller heap, and a hold over it is refused aloud.
    static constexpr uint16_t MAX_HELD_BYTES = 2048;

    // Publish now, or hold until the broker is back.
    void publishOrHold(HAEntity* entity, const String& payload, bool attributes) {
        if (mqttConnected) {
            // This value is newer than anything the outage held for the same
            // topic, and the drain may not have reached that slot yet: leaving
            // it would republish the older one over this one, retained.
            dropHeldSlot(entity, attributes);
            sendToBroker(entity, payload, attributes);
            return;
        }
        holdForReconnect(entity, payload, attributes);
    }

    void dropHeldSlot(HAEntity* entity, bool attributes) {
        HAL::Platform::LockGuard guard(storeLock);
        for (auto it = pendingPublishes.begin(); it != pendingPublishes.end(); ++it) {
            if (it->entity == entity && it->attributes == attributes) {
                heldBytes -= static_cast<uint16_t>(it->payload.length());
                pendingPublishes.erase(it);
                return;
            }
        }
    }

    // A topic that does not fit today will not fit on the next loop either, so
    // the held store checks it on the way in and the flush drops rather than
    // keeps: a kept slot would stall every value behind it for good.
    bool heldTopicFits(HAEntity* entity, bool attributes) {
        char topic[HA_TOPIC_BUF_SIZE];
        const int needed = attributes
            ? entity->getAttributesTopic(topic, sizeof(topic), config.nodeId, config.discoveryPrefix)
            : entity->getStateTopic(topic, sizeof(topic), config.nodeId, config.discoveryPrefix);
        return topicFitsTheWire(needed, entity->id.c_str());
    }

    bool sendToBroker(HAEntity* entity, const String& payload, bool attributes) {
        char topic[HA_TOPIC_BUF_SIZE];
        if (attributes) {
            if (!topicFitsTheWire(entity->getAttributesTopic(topic, sizeof(topic), config.nodeId,
                                                             config.discoveryPrefix), entity->id.c_str())) {
                stats.statesRefused++;
                return false;
            }
            return mqttPublish(topic, payload, 0, true);
        }
        if (!topicFitsTheWire(entity->getStateTopic(topic, sizeof(topic), config.nodeId,
                                                    config.discoveryPrefix), entity->id.c_str())) {
            stats.statesRefused++;
            return false;
        }
        DLOG_D(LOG_HA, "Publishing state: %s = %s", entity->id.c_str(), payload.c_str());
        if (!mqttPublish(topic, payload, 0, entity->retained)) return false;
        stats.stateUpdates++;
        return true;
    }

    uint16_t heldSlotBytes(HAEntity* entity, bool attributes) {
        HAL::Platform::LockGuard guard(storeLock);
        for (const auto& held : pendingPublishes) {
            if (held.entity == entity && held.attributes == attributes) {
                return static_cast<uint16_t>(held.payload.length());
            }
        }
        return 0;
    }

    void holdForReconnect(HAEntity* entity, const String& payload, bool attributes) {
        HAL::Platform::LockGuard guard(storeLock);
        // The event field is the ceiling at send time, so refuse it here too
        // rather than hold bytes that could never leave.
        if (payload.length() >= MQTT_EVENT_PAYLOAD_SIZE) {
            DLOG_W(LOG_HA, "Held payload for '%s' is %u bytes, over the %u-byte event field: dropped",
                   entity->id.c_str(), (unsigned)payload.length(),
                   (unsigned)(MQTT_EVENT_PAYLOAD_SIZE - 1));
            stats.statesRefused++;
            return;
        }

        if (!heldTopicFits(entity, attributes)) {
            stats.statesRefused++;
            return;
        }

        // Against the budget, counting what this payload would replace rather
        // than add: a slot that is overwritten costs only its difference.
        const uint16_t replacing = heldSlotBytes(entity, attributes);
        if (heldBytes - replacing + payload.length() > MAX_HELD_BYTES) {
            DLOG_W(LOG_HA, "Held payloads are at %u B of %u: dropping the one for '%s'",
                   (unsigned)heldBytes, (unsigned)MAX_HELD_BYTES, entity->id.c_str());
            stats.statesRefused++;
            return;
        }

        // A String whose allocation fails invalidates to empty, and an empty
        // payload on a retained topic is a message of its own. Copy first, and
        // take the slot only if the copy is whole.
        String copy(payload);
        if (copy.length() != payload.length()) {
            DLOG_W(LOG_HA, "Out of memory holding a payload for '%s': dropped", entity->id.c_str());
            stats.statesRefused++;
            return;
        }
        // One slot per entity and topic, overwritten: a state is
        // last-value-wins, so only the newest is worth sending when the link
        // returns. The insertion order is kept, so entities come back in the
        // order the application produced them.
        for (auto& held : pendingPublishes) {
            if (held.entity == entity && held.attributes == attributes) {
                heldBytes -= static_cast<uint16_t>(held.payload.length());
                held.payload = std::move(copy);
                heldBytes += static_cast<uint16_t>(held.payload.length());
                return;
            }
        }
        heldBytes += static_cast<uint16_t>(copy.length());
        pendingPublishes.push_back(PendingPublish{entity, std::move(copy), attributes});
        DLOG_D(LOG_HA, "MQTT down, holding %s for '%s'",
               attributes ? "attributes" : "state", entity->id.c_str());
    }

    /**
     * @brief Find entity by ID
     */
    HAEntity* findEntity(const String& id) {
        for (const auto& entity : entities) {
            if (entity->id == id) {
                return entity.get();
            }
        }
        return nullptr;
    }

    /**
     * @brief Find entity by ID given as a range of characters
     *
     * MEM-2: the command path knows the id as a slice of the topic buffer, and
     * building a String from it to call the overload above is the allocation this
     * lot removes. The comparison is on the full extent deliberately — the id is
     * copied into the event's 64-byte field afterwards, and looking up the
     * truncated copy instead would stop matching an entity registered with a
     * longer id, which is a behaviour change and not a cost one.
     */
    HAEntity* findEntity(const char* id, size_t len) {
        for (const auto& entity : entities) {
            if ((size_t)entity->id.length() == len && strncmp(entity->id.c_str(), id, len) == 0) {
                return entity.get();
            }
        }
        return nullptr;
    }
    
    // OBS-5: two entities with one id publish two configs for one unique_id, and
    // Home Assistant keeps whichever arrived first.
    void warnIfDuplicateId(const String& id) {
        if (findEntity(id)) {
            DLOG_W(LOG_HA, "Entity id '%s' already registered; Home Assistant keeps the first config", id.c_str());
        }
    }

    // Home Assistant watches one topic per device, the one `avty_t` names, and
    // only the broker can write `offline` to it. Whichever of availabilityTopic
    // and MQTTConfig::lwtTopic the application named, the other follows.
    void reconcileAvailabilityWithWill() {
        // Injected at addComponent(), so the initialisation order is irrelevant.
        // The downcast is the framework's accepted one: the name selects the type.
        IComponent* found = __dc_registry ? __dc_registry->getComponent("MQTT") : nullptr;
        MQTTComponent* mqtt = static_cast<MQTTComponent*>(found);
        if (!mqtt) {
            // Every other exit from this function says why; this one used to be
            // the silent one, and it is the likeliest in a sketch.
            DLOG_W(LOG_HA, "No MQTT component: '%s' has no Last Will behind it",
                   config.availabilityTopic);
            return;
        }

        MQTTConfig cfg = mqtt->getConfig();
        if (!cfg.enableLWT) {
            DLOG_W(LOG_HA, "MQTT Last Will is off: '%s' will never report offline and "
                           "every entity stays available after this device drops off",
                   config.availabilityTopic);
            return;
        }
        // The broker writes the payload, not this component, and the documents
        // leave Home Assistant on its default "offline". Anything else lands on
        // the right topic and still never marks the device unavailable.
        // setAvailable() publishes "online" retained, so a transient will is
        // seen only by whoever is subscribed at that instant and the retained
        // "online" outlives the device.
        if (!cfg.lwtRetain) {
            DLOG_W(LOG_HA, "MQTT will was not retained: forcing it, or the retained "
                           "'online' on '%s' would outlive this device",
                   config.availabilityTopic);
            cfg.lwtRetain = true;
            mqtt->setConfig(cfg);
        }
        if (cfg.lwtMessage != HAPayload::NOT_AVAILABLE) {
            DLOG_W(LOG_HA, "MQTT will payload is '%s', not 'offline': Home Assistant "
                           "will not read it as unavailable",
                   cfg.lwtMessage.c_str());
        }

        if (!availabilityTopicNamed) {
            // The generated topic has no will behind it; adopt the one that has.
            // An empty will topic would blank availabilityTopic, drop avty_t from
            // every document and publish availability to no topic at all.
            if (cfg.lwtTopic.isEmpty()) {
                DLOG_W(LOG_HA, "MQTT has a Last Will and no topic for it: availability "
                               "keeps '%s' and nothing corrects it",
                       config.availabilityTopic);
                return;
            }
            if (cfg.lwtTopic.length() >= HA::MAX_AVAIL_TOPIC) {
                DLOG_W(LOG_HA, "LWT topic is %u chars, over the %u-char availability field: "
                               "availability keeps '%s' and no will corrects it",
                       (unsigned)cfg.lwtTopic.length(), (unsigned)(HA::MAX_AVAIL_TOPIC - 1),
                       config.availabilityTopic);
                return;
            }
            HA::setField(config.availabilityTopic, cfg.lwtTopic.c_str(), HA::MAX_AVAIL_TOPIC);
            HA::setField(adoptedFromWill_, cfg.lwtTopic.c_str(), HA::MAX_AVAIL_TOPIC);
            return;
        }

        if (cfg.lwtTopic == config.availabilityTopic) return;

        // The application named the availability topic: move the will onto it.
        // The will is sent in CONNECT, so a session opened before this has the
        // old one and has to be reopened.
        DLOG_I(LOG_HA, "Moving the MQTT Last Will from '%s' to '%s'",
               cfg.lwtTopic.c_str(), config.availabilityTopic);
        cfg.lwtTopic = config.availabilityTopic;
        mqtt->setConfig(cfg);
        if (mqtt->isConnected()) {
            DLOG_I(LOG_HA, "Reopening the session so the broker holds the new will");
            // Reopened here rather than left to MQTTComponent::loop(): that
            // reconnection is gated on enabled && autoReconnect AND on a backoff
            // timer, so the device would sit off the broker until the timer next
            // fires — and for ever when autoReconnect is off.
            mqtt->disconnect();
            if (!mqtt->connect()) {
                DLOG_W(LOG_HA, "Could not reopen the MQTT session: the broker still "
                               "holds the old will until the next reconnection");
            }
        }
    }

    /**
     * @brief Publish MQTT message via EventBus
     */
    // A topic longer than the event field arrives cut at the broker: Home
    // Assistant never reads a config there, and two ids sharing a prefix cut to
    // the same state topic and overwrite each other.
    bool topicFitsTheWire(int needed, const char* entityId) {
        // A negative return leaves the buffer indeterminate, so it is a refusal
        // like any other rather than a length to compare.
        if (needed >= 0 && needed < (int)MQTT_EVENT_TOPIC_SIZE) return true;
        DLOG_W(LOG_HA, "Topic for '%s' needs %d chars, over the %u-char event field: not published",
               entityId, needed, (unsigned)(MQTT_EVENT_TOPIC_SIZE - 1));
        return false;
    }

    bool mqttPublish(const char* topic, const String& payload, uint8_t qos = 0, bool retain = false) {
        using namespace DomoticsCore::Components;
        MQTTPublishEvent ev{};
        // OBS-5: the event's payload field is the cap. A cut JSON would sit
        // retained on the broker and be rejected by Home Assistant silently
        // at every restart, so it is refused here, aloud.
        if (payload.length() >= MQTT_EVENT_PAYLOAD_SIZE) {
            DLOG_W(LOG_HA, "Payload for '%s' is %u bytes, over the %u-byte event field: not published", topic,
                   (unsigned)payload.length(), (unsigned)(MQTT_EVENT_PAYLOAD_SIZE - 1));
            return false;
        }

        // Copy strings into fixed-size buffers
        strncpy(ev.topic, topic, MQTT_EVENT_TOPIC_SIZE - 1);
        ev.topic[MQTT_EVENT_TOPIC_SIZE - 1] = '\0';
        strncpy(ev.payload, payload.c_str(), MQTT_EVENT_PAYLOAD_SIZE - 1);
        ev.payload[MQTT_EVENT_PAYLOAD_SIZE - 1] = '\0';
        ev.qos = qos;
        ev.retain = retain;

        emit(DomoticsCore::MQTTEvents::EVENT_PUBLISH, ev);
        return true;
    }
    
    /**
     * @brief Build device information JSON
     */
    void buildDeviceInfo(JsonObject& device) {
        JsonArray identifiers = device["ids"].to<JsonArray>();
        identifiers.add((const char*)config.nodeId);

        device["name"] = (const char*)config.deviceName;
        device["mdl"] = (const char*)config.model;
        device["mf"] = (const char*)config.manufacturer;
        device["sw"] = (const char*)config.swVersion;

        if (config.configUrl[0] != '\0') {
            device["cu"] = (const char*)config.configUrl;
        }

        if (config.suggestedArea[0] != '\0') {
            device["sa"] = (const char*)config.suggestedArea;
        }
    }
    
    /**
     * @brief Publish discovery message for a single entity
     */
    void publishEntityDiscovery(HAEntity* entity, const JsonObject& device) {
        if (!entity->entityCategoryIsValid()) {
            DLOG_W(LOG_HA, "Entity '%s': entity_category '%s' is not one Home Assistant accepts; left out",
                   entity->id.c_str(), entity->entityCategory.c_str());
        }
        JsonDocument doc;
        entity->buildDiscoveryPayload(doc, config.nodeId, config.discoveryPrefix, 
                                     device, config.availabilityTopic);
        
        String payload;
        serializeJson(doc, payload);
        char topic[HA_TOPIC_BUF_SIZE];
        char stateTopic[HA_TOPIC_BUF_SIZE];
        if (!topicFitsTheWire(entity->getDiscoveryTopic(topic, sizeof(topic), config.nodeId,
                                                        config.discoveryPrefix), entity->id.c_str())
            || !topicFitsTheWire(entity->getStateTopic(stateTopic, sizeof(stateTopic), config.nodeId,
                                                       config.discoveryPrefix), entity->id.c_str())) {
            stats.discoveryRefused++;
            return;
        }

        DLOG_I(LOG_HA, "Publishing discovery for '%s':", entity->id.c_str());
        DLOG_I(LOG_HA, "  Topic: %s", topic);
        DLOG_I(LOG_HA, "  Payload size: %d bytes", payload.length());
        DLOG_D(LOG_HA, "  Payload: %s", payload.c_str());
        
        if (!mqttPublish(topic, payload, 0, config.retainDiscovery)) {
            stats.discoveryRefused++;  // BUG-38: refused, warned about above, and counted
            return;
        }
        // Handed to the EventBus, not published: mqttPublish() emits, and MQTT
        // consumes it later. This component never learns the outcome, so saying
        // "published" claimed something it cannot know — and did so even while
        // the rate limiter was silently discarding these very messages (BUG-29).
        DLOG_I(LOG_HA, "  Discovery queued for publish");
    }
    
    /**
     * @brief Subscribe to all command topics
     */
    void subscribeToCommands() {
        // Subscribe to wildcard command topics for all entity types via EventBus
        snprintf(commandTopicFilter, sizeof(commandTopicFilter), "%s/+/%s/+/set",
                 config.discoveryPrefix, config.nodeId);

        using namespace DomoticsCore::Components;
        MQTTSubscribeEvent ev{};
        strncpy(ev.topic, commandTopicFilter, MQTT_EVENT_TOPIC_SIZE - 1);
        ev.topic[MQTT_EVENT_TOPIC_SIZE - 1] = '\0';
        ev.qos = 0;

        emit(DomoticsCore::MQTTEvents::EVENT_SUBSCRIBE, ev);

        DLOG_D(LOG_HA, "Subscribed to commands via EventBus: %s", commandTopicFilter);
        // Message handling is done via "mqtt/message" event listener in begin()
    }
    
    /**
     * @brief Handle incoming command
     *
     * MEM-2: takes the topic and payload as the characters MQTT delivered them as.
     * Everything up to and including the entity lookup — the work done for every
     * message on the shared client, most of which are not ours — now runs without
     * touching the allocator. The one String that survives is the temporary bound
     * to HAEntity::handleCommand(const String&), built after the message has been
     * accepted; that virtual keeps its signature, so a subclass written against it
     * still compiles and still runs.
     *
     * Private, so the parameter change is not a change to any published API.
     */
    void handleCommand(const char* topic, const char* payload) {
        // Every message the shared client receives arrives here. A topic outside
        // the discovery prefix belongs to the application, not to this component:
        // it is dropped before the parse, and says nothing at any level.
        const size_t prefixLen = strlen(config.discoveryPrefix);
        if (prefixLen > 0 && (strncmp(topic, config.discoveryPrefix, prefixLen) != 0
                              || topic[prefixLen] != '/')) {
            return;
        }

        DLOG_I(LOG_HA, "Received MQTT command - Topic: %s, Payload: %s", topic, payload);

        // Extract entity ID from topic
        // Format: homeassistant/{component}/{node_id}/{entity_id}/set
        const char* lastSlash = strrchr(topic, '/');
        if (!lastSlash) {
            DLOG_E(LOG_HA, "Invalid topic format - no trailing slash");
            return;
        }

        // Walk back from the last slash rather than from the end: the id is the
        // segment between the last two. The loop stops at `topic` itself, so a
        // topic whose only slash is its first character falls through to the
        // missing-id branch, as it did before.
        const char* secondLastSlash = nullptr;
        for (const char* p = lastSlash; p != topic; ) {
            --p;
            if (*p == '/') { secondLastSlash = p; break; }
        }
        if (!secondLastSlash) {
            DLOG_E(LOG_HA, "Invalid topic format - missing entity ID");
            return;
        }

        const char* idStart = secondLastSlash + 1;
        const size_t idLen = (size_t)(lastSlash - idStart);

        // Sized to the event field it will fill, so the truncation point is the
        // one strncpy() applied here before — 63 characters plus the terminator.
        // idLen keeps the true length, which the logs and the warning below both
        // need: they report the id as it was delivered, not the copy that was
        // stored, which is what the String version did and what an operator
        // reading them has to be able to match against the broker's traffic.
        // Hence %.*s over idStart everywhere the id is printed.
        char entityId[sizeof(HAEvents::HACommandEvent::entityId)];
        const size_t idCopy = idLen < sizeof(entityId) ? idLen : sizeof(entityId) - 1;
        memcpy(entityId, idStart, idCopy);
        entityId[idCopy] = '\0';

        DLOG_I(LOG_HA, "Extracted entity ID: '%.*s', looking up entity...", (int)idLen, idStart);
        HAEntity* entity = findEntity(idStart, idLen);
        if (!entity) {
            DLOG_W(LOG_HA, "Command for unknown entity: %.*s", (int)idLen, idStart);
            return;
        }

        stats.commandsReceived++;
        DLOG_D(LOG_HA, "Command for %.*s: %s", (int)idLen, idStart, payload);

        // An entity refuses a command it cannot apply (a button's wrong payload, a light's garbage).
        bool valid = entity->handleCommand(payload);
        if (!valid) return;

        HAEvents::HACommandEvent ev{};
        strncpy(ev.entityId, entityId, sizeof(ev.entityId) - 1);
        strncpy(ev.component, entity->component.c_str(), sizeof(ev.component) - 1);
        strncpy(ev.command, payload, sizeof(ev.command) - 1);

        // Log warning if entity ID or payload was truncated. idLen is already
        // known; the payload is measured once here rather than on the discard
        // path, where nothing needs its length.
        const size_t payloadLen = strlen(payload);
        if (idLen >= sizeof(ev.entityId)) {
            DLOG_W(LOG_HA, "Entity ID truncated: %.*s (%zu > %zu)",
                   (int)idLen, idStart, idLen, sizeof(ev.entityId) - 1);
        }
        if (payloadLen >= sizeof(ev.command)) {
            DLOG_W(LOG_HA, "Command payload truncated for entity %.*s (%zu > %zu)",
                   (int)idLen, idStart, payloadLen, sizeof(ev.command) - 1);
        }

        entity->describeCommand(ev);
        emit(HAEvents::EVENT_COMMAND, ev);

        // entity->id rather than the local buffer: publishState builds its own Strings anyway.
        if (entity->echoesCommandAsState()) {
            publishState(entity->id, payload);
        }
    }
};

} // namespace HomeAssistant
} // namespace Components
} // namespace DomoticsCore
