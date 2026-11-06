/**
 * @file test_ha_reenable_link.cpp
 * @brief The MQTT link followed while the component is off decides what begin() does.
 */

#include <unity.h>
#include "../ha_test_support.h"

struct Counts { int discovery = 0; int availability = 0; };

static void watch(Core& core, Counts& c) {
    core.on<MQTTPublishEvent>(DomoticsCore::MQTTEvents::EVENT_PUBLISH,
        [&c](const MQTTPublishEvent& ev) {
            if (strstr(ev.topic, "/config") && ev.payload[0] != '\0') c.discovery++;
            if (strstr(ev.topic, "availability") && strcmp(ev.payload, "online") == 0) c.availability++;
        });
}

static HomeAssistantComponent* setup(Core& core) {
    HAConfig config;
    HA::setField(config.nodeId, "test_node", sizeof(config.nodeId));
    auto ha = std::make_unique<HomeAssistantComponent>(config);
    HomeAssistantComponent* p = ha.get();
    ha->addSwitch("relay", "Relay");
    core.addComponent(std::move(ha));
    core.begin();
    simulateMqttConnect(core);
    return p;
}

void test_drop_while_disabled_is_seen_as_disconnected() {
    Core core;
    HomeAssistantComponent* ha = setup(core);
    ha->shutdown();
    core.emit<bool>(DomoticsCore::MQTTEvents::EVENT_DISCONNECTED, true);
    for (int i = 0; i < 5; i++) core.loop();

    Counts c;
    watch(core, c);
    ha->begin();
    for (int i = 0; i < 5; i++) core.loop();

    TEST_ASSERT_FALSE(ha->isMQTTConnected());
    TEST_ASSERT_EQUAL(0, c.discovery);
    TEST_ASSERT_EQUAL(0, c.availability);
    core.shutdown();
}

void test_drop_and_reconnect_while_disabled_is_seen_as_connected() {
    Core core;
    HomeAssistantComponent* ha = setup(core);
    ha->shutdown();
    core.emit<bool>(DomoticsCore::MQTTEvents::EVENT_DISCONNECTED, true);
    for (int i = 0; i < 5; i++) core.loop();
    core.emit<bool>(DomoticsCore::MQTTEvents::EVENT_CONNECTED, true);
    for (int i = 0; i < 5; i++) core.loop();

    Counts c;
    watch(core, c);
    ha->begin();
    for (int i = 0; i < 5; i++) core.loop();

    TEST_ASSERT_TRUE(ha->isMQTTConnected());
    TEST_ASSERT_TRUE(c.discovery >= 1);
    TEST_ASSERT_TRUE(c.availability >= 1);
    core.shutdown();
}

void test_connect_while_disabled_after_down_shutdown_is_seen_as_connected() {
    Core core;
    HomeAssistantComponent* ha = setup(core);
    core.emit<bool>(DomoticsCore::MQTTEvents::EVENT_DISCONNECTED, true);
    for (int i = 0; i < 5; i++) core.loop();
    ha->shutdown();
    core.emit<bool>(DomoticsCore::MQTTEvents::EVENT_CONNECTED, true);
    for (int i = 0; i < 5; i++) core.loop();

    Counts c;
    watch(core, c);
    ha->begin();
    for (int i = 0; i < 5; i++) core.loop();

    TEST_ASSERT_TRUE(ha->isMQTTConnected());
    TEST_ASSERT_TRUE(c.discovery >= 1);
    core.shutdown();
}

int main(int argc, char** argv) {
    UNITY_BEGIN();
    RUN_TEST(test_drop_while_disabled_is_seen_as_disconnected);
    RUN_TEST(test_drop_and_reconnect_while_disabled_is_seen_as_connected);
    RUN_TEST(test_connect_while_disabled_after_down_shutdown_is_seen_as_connected);
    return UNITY_END();
}
