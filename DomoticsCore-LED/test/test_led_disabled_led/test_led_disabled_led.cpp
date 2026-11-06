/**
 * @file test_led_disabled_led.cpp
 * @brief A disabled LED must stay dark across update ticks.
 *
 * The stub HAL drops analogWrite(), so this test installs its own recorder
 * for the PWM writes: the stub header is included first with its
 * analogWrite renamed out of the way, then a recording analogWrite takes
 * its place in the same namespace.
 */

#define analogWrite stubDroppedAnalogWriteForDisabledLedTest
#include <DomoticsCore/Platform_Stub.h>
#undef analogWrite

namespace DomoticsCore {
namespace HAL {
namespace Platform {
inline int lastPwmWrittenForDisabledLedTest[64];
inline void analogWrite(uint8_t pin, int value) {
    if (pin < 64) lastPwmWrittenForDisabledLedTest[pin] = value;
}
} // namespace Platform
} // namespace HAL
} // namespace DomoticsCore

#include <DomoticsCore/Logger.h>
#include <DomoticsCore/LED.h>
#include <unity.h>

using namespace DomoticsCore;
using namespace DomoticsCore::Components;

static const int kPin = 2;

void setUp(void) {
    for (auto& v : HAL::Platform::lastPwmWrittenForDisabledLedTest) v = -1;
    HAL::Platform::setMillisForTest(1000);
}
void tearDown(void) {
    HAL::Platform::resetMillisForTest();
}

// Disabled stays dark:
// Given an LED lit red at brightness 200,
// When the application calls enableLED(0, false) and the update loop runs
// for one 50 ms tick,
// Then the pin stays at 0.
void test_disabled_stays_dark(void) {
    LEDComponent led;
    led.addSingleLED(kPin, "Status");
    TEST_ASSERT_EQUAL(ComponentStatus::Success, led.begin());
    led.setLED((size_t)0, LEDColor::Red(), 200);

    led.enableLED((size_t)0, false);
    HAL::Platform::advanceMillisForTest(60);
    led.loop();

    TEST_ASSERT_EQUAL_INT(0, HAL::Platform::lastPwmWrittenForDisabledLedTest[kPin]);
}

// Disabled by name:
// Given the same LED,
// When the application disables it by name "Status",
// Then the pin stays at 0 after several ticks.
void test_disabled_by_name(void) {
    LEDComponent led;
    led.addSingleLED(kPin, "Status");
    TEST_ASSERT_EQUAL(ComponentStatus::Success, led.begin());
    led.setLED((size_t)0, LEDColor::Red(), 200);

    TEST_ASSERT_TRUE(led.enableLED(String("Status"), false));
    for (int i = 0; i < 4; i++) {
        HAL::Platform::advanceMillisForTest(60);
        led.loop();
    }

    TEST_ASSERT_EQUAL_INT(0, HAL::Platform::lastPwmWrittenForDisabledLedTest[kPin]);
}

int main(int argc, char** argv) {
    UNITY_BEGIN();
    RUN_TEST(test_disabled_stays_dark);
    RUN_TEST(test_disabled_by_name);
    return UNITY_END();
}
