/**
 * @file test_ha_reenable_discovery.cpp
 * @brief A shutdown()/begin() cycle (WebUI switch) while MQTT stays connected republishes discovery and availability.
 */

#include <unity.h>
#include "../ha_test_support.h"

void test_reenable_while_connected_republishes_discovery() {
    Core core;
    HAConfig config;
    HA::setField(config.nodeId, "test_node", sizeof(config.nodeId));
    auto ha = std::make_unique<HomeAssistantComponent>(config);
    HomeAssistantComponent* haPtr = ha.get();
    ha->addSwitch("relay", "Relay");
    core.addComponent(std::move(ha));
    core.begin();
    simulateMqttConnect(core);

    haPtr->shutdown();
    for (int i = 0; i < 5; i++) core.loop();

    int discovery = 0;
    int availability = 0;
    core.on<MQTTPublishEvent>(DomoticsCore::MQTTEvents::EVENT_PUBLISH,
        [&](const MQTTPublishEvent& ev) {
            if (strstr(ev.topic, "/config") && ev.payload[0] != '\0') discovery++;
            if (strstr(ev.topic, "availability") && strcmp(ev.payload, "online") == 0) availability++;
        });

    haPtr->begin();
    for (int i = 0; i < 5; i++) core.loop();

    TEST_ASSERT_TRUE_MESSAGE(discovery >= 1, "no discovery after re-enable");
    TEST_ASSERT_TRUE_MESSAGE(availability >= 1, "no availability after re-enable");
    TEST_ASSERT_TRUE(haPtr->isMQTTConnected());

    core.shutdown();
}

int main(int argc, char** argv) {
    UNITY_BEGIN();
    RUN_TEST(test_reenable_while_connected_republishes_discovery);
    return UNITY_END();
}
