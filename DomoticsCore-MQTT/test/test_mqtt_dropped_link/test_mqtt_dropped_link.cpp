#include <unity.h>
#include <DomoticsCore/MQTT.h>
#include <string>
#include <type_traits>

using namespace DomoticsCore;
using namespace DomoticsCore::Components;

namespace {

// lostCount is new in MQTTStatistics: read it when present, else a sentinel,
// so the test builds on the base and fails by its assertion.
template <typename T>
auto readLost(const T& s, int) -> decltype(static_cast<uint32_t>(s.lostCount)) {
    return static_cast<uint32_t>(s.lostCount);
}
template <typename T>
uint32_t readLost(const T&, long) { return 0xFFFFFFFFu; }

std::string order;
int disconnectedCount = 0;

MQTTConfig makeConfig() {
    MQTTConfig cfg;
    cfg.broker = "broker.local";
    cfg.clientId = "test-client";
    cfg.reconnectDelay = 10;
    return cfg;
}

void attach(MQTTComponent& mqtt) {
    order.clear();
    disconnectedCount = 0;
    mqtt.on<bool>(MQTTEvents::EVENT_CONNECTED, [](const bool&) { order += "C"; });
    mqtt.on<bool>(MQTTEvents::EVENT_DISCONNECTED, [](const bool&) {
        order += "D";
        disconnectedCount++;
    });
}

void dropLink() { HAL::MQTT::stubSetBrokerLink(false); }
void restoreLink() { HAL::MQTT::stubSetBrokerLink(true); }

} // namespace

void setUp() {
    HAL::WiFiHAL::stubSetConnected(true);
    restoreLink();
}
void tearDown() {}

// Scenario: Silent reset
// Given a connected MQTT component,
// When resetReconnect() is called,
// Then subscribers get mqtt/disconnected before any later mqtt/connected.
void test_silent_reset() {
    MQTTComponent mqtt(makeConfig());
    attach(mqtt);
    mqtt.begin();
    TEST_ASSERT_TRUE(mqtt.connect());
    TEST_ASSERT_TRUE(mqtt.getState() == MQTTState::Connected);

    mqtt.resetReconnect();
    TEST_ASSERT_EQUAL_INT(1, disconnectedCount);
    TEST_ASSERT_TRUE(mqtt.getState() == MQTTState::Disconnected);

    TEST_ASSERT_TRUE(mqtt.connect());
    size_t d = order.find('D');
    size_t c = order.rfind('C');
    TEST_ASSERT_TRUE(d != std::string::npos);
    TEST_ASSERT_TRUE(c != std::string::npos);
    TEST_ASSERT_TRUE(d < c);
}

// Scenario: Link flap
// Given a connected component whose link drops and returns twice,
// When I read the statistics,
// Then they show two losses.
void test_link_flap() {
    MQTTComponent mqtt(makeConfig());
    attach(mqtt);
    mqtt.begin();
    TEST_ASSERT_TRUE(mqtt.connect());

    for (int i = 0; i < 2; i++) {
        dropLink();
        mqtt.loop();
        restoreLink();
        HAL::Platform::stubAdvanceMillis(100000);
        mqtt.loop();
        if (!mqtt.isConnected()) mqtt.connect();
        TEST_ASSERT_TRUE(mqtt.isConnected());
    }
    TEST_ASSERT_EQUAL_UINT32(2, readLost(mqtt.getStatistics(), 0));
}

// Scenario: Frozen uptime
// Given a dropped link,
// When I read the statistics during the outage,
// Then the uptime no longer reads as live.
void test_frozen_uptime() {
    MQTTComponent mqtt(makeConfig());
    attach(mqtt);
    mqtt.begin();
    TEST_ASSERT_TRUE(mqtt.connect());
    HAL::Platform::stubAdvanceMillis(5000);
    mqtt.loop();
    TEST_ASSERT_TRUE(mqtt.getStatistics().uptime > 0);

    dropLink();
    mqtt.loop();
    TEST_ASSERT_EQUAL_UINT32(0, mqtt.getStatistics().uptime);
    HAL::Platform::stubAdvanceMillis(5000);
    TEST_ASSERT_EQUAL_UINT32(0, mqtt.getStatistics().uptime);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_silent_reset);
    RUN_TEST(test_link_flap);
    RUN_TEST(test_frozen_uptime);
    return UNITY_END();
}
