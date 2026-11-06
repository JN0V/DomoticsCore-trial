/**
 * @file test_ha_reenable.cpp
 * @brief HomeAssistant component: a shutdown() and begin() cycle (the WebUI enable switch) must not stack MQTT subscriptions.
 */

#include <unity.h>
#include "../ha_test_support.h"

static void sendSwitchCommand(Core& core, const char* payload) {
    MQTTMessageEvent msg{};
    String topic = "homeassistant/switch/test_node/relay/set";
    strncpy(msg.topic, topic.c_str(), MQTT_EVENT_TOPIC_SIZE - 1);
    strncpy(msg.payload, payload, MQTT_EVENT_PAYLOAD_SIZE - 1);
    core.emit<MQTTMessageEvent>(DomoticsCore::MQTTEvents::EVENT_MESSAGE, msg);
    for (int i = 0; i < 5; i++) core.loop();
}

// Scenario: Re-enable once
// Given a device with MQTT and one switch `relay`,
// When I turn HomeAssistant off and on in the WebUI and toggle `relay` from Home Assistant once,
// Then the command is handled once.
void test_re_enable_once() {
    Core core;
    HAConfig config;
    HA::setField(config.nodeId, "test_node", sizeof(config.nodeId));
    auto ha = std::make_unique<HomeAssistantComponent>(config);
    HomeAssistantComponent* haPtr = ha.get();
    ha->addSwitch("relay", "Relay");
    core.addComponent(std::move(ha));
    core.begin();

    int commandEvents = 0;
    core.getEventBus().subscribe(String(HAEvents::EVENT_COMMAND), [&](const void*) {
        commandEvents++;
    }, nullptr);

    simulateMqttConnect(core);
    haPtr->shutdown();
    haPtr->begin();

    sendSwitchCommand(core, "ON");

    TEST_ASSERT_EQUAL_UINT32(1, haPtr->getStatistics().commandsReceived);
    TEST_ASSERT_EQUAL_INT(1, commandEvents);

    core.shutdown();
}

// Scenario: Disabled ignores commands
// Given HomeAssistant turned off in the WebUI,
// When I toggle `relay` from Home Assistant,
// Then nothing is handled and no `ha/command` is emitted.
void test_disabled_ignores_commands() {
    Core core;
    HAConfig config;
    HA::setField(config.nodeId, "test_node", sizeof(config.nodeId));
    auto ha = std::make_unique<HomeAssistantComponent>(config);
    HomeAssistantComponent* haPtr = ha.get();
    ha->addSwitch("relay", "Relay");
    core.addComponent(std::move(ha));
    core.begin();

    int commandEvents = 0;
    core.getEventBus().subscribe(String(HAEvents::EVENT_COMMAND), [&](const void*) {
        commandEvents++;
    }, nullptr);

    simulateMqttConnect(core);
    haPtr->shutdown();

    sendSwitchCommand(core, "ON");

    TEST_ASSERT_EQUAL_UINT32(0, haPtr->getStatistics().commandsReceived);
    TEST_ASSERT_EQUAL_INT(0, commandEvents);

    core.shutdown();
}

// Scenario: Repeated cycles
// Given several off/on cycles,
// When I send one command,
// Then it is still handled once.
void test_repeated_cycles() {
    Core core;
    HAConfig config;
    HA::setField(config.nodeId, "test_node", sizeof(config.nodeId));
    auto ha = std::make_unique<HomeAssistantComponent>(config);
    HomeAssistantComponent* haPtr = ha.get();
    ha->addSwitch("relay", "Relay");
    core.addComponent(std::move(ha));
    core.begin();

    int commandEvents = 0;
    core.getEventBus().subscribe(String(HAEvents::EVENT_COMMAND), [&](const void*) {
        commandEvents++;
    }, nullptr);

    simulateMqttConnect(core);
    for (int i = 0; i < 3; i++) {
        haPtr->shutdown();
        haPtr->begin();
    }

    sendSwitchCommand(core, "ON");

    TEST_ASSERT_EQUAL_UINT32(1, haPtr->getStatistics().commandsReceived);
    TEST_ASSERT_EQUAL_INT(1, commandEvents);

    core.shutdown();
}

int main(int argc, char** argv) {
    UNITY_BEGIN();
    RUN_TEST(test_re_enable_once);
    RUN_TEST(test_disabled_ignores_commands);
    RUN_TEST(test_repeated_cycles);
    return UNITY_END();
}
