/**
 * @file test_led_component.cpp
 * @brief Lifecycle and state management of LEDComponent.
 *
 * Covers what the two vectors owe each other: a configuration list the user
 * fills, and a runtime state list begin() derives from it. BUG-19 was the two
 * drifting apart; DC-5 was the runtime half never being released.
 */

#include <unity.h>
#include <cstring>
#include <DomoticsCore/Core.h>
#include <DomoticsCore/LED.h>
#include <DomoticsCore/Platform_Stub.h>

using namespace DomoticsCore;
using namespace DomoticsCore::Components;

void setUp(void) {}
void tearDown(void) {}

// Does `status` mention `fragment`? getLEDStatus() is the only window onto
// per-LED state from outside the component.
static bool statusMentions(const String& status, const char* fragment) {
    return strstr(status.c_str(), fragment) != nullptr;
}

// ============================================================================
// Metadata (M12)
// ============================================================================

void test_led_metadata_name_is_short(void) {
    LEDComponent led;
    TEST_ASSERT_EQUAL_STRING("LED", led.metadata.name);
}

void test_led_component_lookup_by_short_name(void) {
    Core core;
    core.addComponent(std::make_unique<LEDComponent>());
    core.begin();

    auto* ledComp = core.getComponent<LEDComponent>("LED");
    TEST_ASSERT_NOT_NULL(ledComp);
}

// ============================================================================
// begin() — pin validation
// ============================================================================

void test_begin_with_no_leds_succeeds(void) {
    LEDComponent led;
    TEST_ASSERT_EQUAL(ComponentStatus::Success, led.begin());
    TEST_ASSERT_EQUAL_size_t(0, led.getLEDCount());
}

void test_begin_accepts_valid_single_and_rgb_leds(void) {
    LEDComponent led;
    led.addSingleLED(2, "Status");
    led.addRGBLED(25, 26, 27, "Mood");

    TEST_ASSERT_EQUAL(ComponentStatus::Success, led.begin());
    TEST_ASSERT_EQUAL_size_t(2, led.getLEDCount());
}

void test_begin_rejects_negative_single_pin(void) {
    LEDComponent led;
    led.addSingleLED(-1, "Bad");

    TEST_ASSERT_EQUAL(ComponentStatus::ConfigError, led.begin());
}

void test_begin_rejects_incomplete_rgb_pins(void) {
    LEDComponent led;
    led.addRGBLED(25, -1, 27, "Bad");

    TEST_ASSERT_EQUAL(ComponentStatus::ConfigError, led.begin());
}

void test_failed_begin_leaves_no_drivable_state(void) {
    LEDComponent led;
    led.addSingleLED(-1, "Bad");
    led.begin();

    // The config is there, but nothing was initialized — no LED may be driven.
    TEST_ASSERT_EQUAL_size_t(1, led.getLEDCount());
    TEST_ASSERT_FALSE(led.setLED((size_t)0, LEDColor::White(), 128));
}

// ============================================================================
// Naming
// ============================================================================

void test_auto_generated_names(void) {
    LEDComponent led;
    led.addSingleLED(2);
    led.addSingleLED(4);
    led.addRGBLED(25, 26, 27);
    led.begin();

    auto names = led.getLEDNames();
    TEST_ASSERT_EQUAL_size_t(3, names.size());
    TEST_ASSERT_EQUAL_STRING("LED_0", names[0].c_str());
    TEST_ASSERT_EQUAL_STRING("LED_1", names[1].c_str());
    TEST_ASSERT_EQUAL_STRING("RGB_2", names[2].c_str());
}

void test_explicit_names_are_kept(void) {
    LEDComponent led;
    led.addSingleLED(2, "Status");
    led.begin();

    auto names = led.getLEDNames();
    TEST_ASSERT_EQUAL_STRING("Status", names[0].c_str());
}

// ============================================================================
// setLED
// ============================================================================

void test_set_led_by_index(void) {
    LEDComponent led;
    led.addSingleLED(2, "Status");
    led.begin();

    TEST_ASSERT_TRUE(led.setLED((size_t)0, LEDColor::Red(), 200));

    String status = led.getLEDStatus(0);
    TEST_ASSERT_TRUE(statusMentions(status, "Color: RGB(255,0,0)"));
    TEST_ASSERT_TRUE(statusMentions(status, "Brightness: 200"));
}

void test_set_led_rejects_out_of_range_index(void) {
    LEDComponent led;
    led.addSingleLED(2, "Status");
    led.begin();

    TEST_ASSERT_FALSE(led.setLED((size_t)1, LEDColor::Red(), 200));
}

void test_set_led_by_name(void) {
    LEDComponent led;
    led.addSingleLED(2, "Status");
    led.addSingleLED(4, "Alarm");
    led.begin();

    TEST_ASSERT_TRUE(led.setLED(String("Alarm"), LEDColor::Blue(), 90));
    TEST_ASSERT_TRUE(statusMentions(led.getLEDStatus(1), "Brightness: 90"));
    // The other LED is untouched
    TEST_ASSERT_TRUE(statusMentions(led.getLEDStatus(0), "Brightness: 0"));
}

void test_set_led_by_unknown_name_fails(void) {
    LEDComponent led;
    led.addSingleLED(2, "Status");
    led.begin();

    TEST_ASSERT_FALSE(led.setLED(String("Nope"), LEDColor::Blue(), 90));
}

void test_set_led_clears_any_running_effect(void) {
    LEDComponent led;
    led.addSingleLED(2, "Status");
    led.begin();

    led.setLEDEffect((size_t)0, LEDEffect::Blink, 500);
    TEST_ASSERT_TRUE(statusMentions(led.getLEDStatus(0), "Effect: Blink"));

    led.setLED((size_t)0, LEDColor::Red(), 200);
    TEST_ASSERT_TRUE(statusMentions(led.getLEDStatus(0), "Effect: Solid"));
}

// ============================================================================
// setLEDEffect
// ============================================================================

void test_effect_defaults_brightness_to_configured_max(void) {
    LEDComponent led;
    led.addSingleLED(2, "Status", 180);
    led.begin();

    // Brightness was never set: an effect on a dark LED would show nothing.
    TEST_ASSERT_TRUE(led.setLEDEffect((size_t)0, LEDEffect::Fade, 500));
    TEST_ASSERT_TRUE(statusMentions(led.getLEDStatus(0), "Brightness: 180"));
}

void test_effect_defaults_color_to_white(void) {
    LEDComponent led;
    led.addSingleLED(2, "Status");
    led.begin();

    led.setLEDEffect((size_t)0, LEDEffect::Fade, 500);
    TEST_ASSERT_TRUE(statusMentions(led.getLEDStatus(0), "Color: RGB(255,255,255)"));
}

void test_effect_keeps_an_explicit_color_and_brightness(void) {
    LEDComponent led;
    led.addRGBLED(25, 26, 27, "Mood");
    led.begin();

    led.setLED((size_t)0, LEDColor::Green(), 64);
    led.setLEDEffect((size_t)0, LEDEffect::Breathing, 2000);

    String status = led.getLEDStatus(0);
    TEST_ASSERT_TRUE(statusMentions(status, "Color: RGB(0,255,0)"));
    TEST_ASSERT_TRUE(statusMentions(status, "Brightness: 64"));
    TEST_ASSERT_TRUE(statusMentions(status, "Effect: Breathing"));
}

void test_effect_re_enables_a_disabled_led(void) {
    LEDComponent led;
    led.addSingleLED(2, "Status");
    led.begin();

    led.enableLED((size_t)0, false);
    TEST_ASSERT_TRUE(statusMentions(led.getLEDStatus(0), "Disabled"));

    led.setLEDEffect((size_t)0, LEDEffect::Pulse, 500);
    TEST_ASSERT_TRUE(statusMentions(led.getLEDStatus(0), "Enabled"));
}

void test_effect_by_name_and_unknown_name(void) {
    LEDComponent led;
    led.addSingleLED(2, "Status");
    led.begin();

    TEST_ASSERT_TRUE(led.setLEDEffect(String("Status"), LEDEffect::Rainbow, 500));
    TEST_ASSERT_TRUE(statusMentions(led.getLEDStatus(0), "Effect: Rainbow"));
    TEST_ASSERT_FALSE(led.setLEDEffect(String("Nope"), LEDEffect::Rainbow, 500));
}

void test_effect_rejects_out_of_range_index(void) {
    LEDComponent led;
    led.addSingleLED(2, "Status");
    led.begin();

    TEST_ASSERT_FALSE(led.setLEDEffect((size_t)5, LEDEffect::Blink, 500));
}

// ============================================================================
// enableLED
// ============================================================================

void test_enable_led_by_index_and_name(void) {
    LEDComponent led;
    led.addSingleLED(2, "Status");
    led.begin();

    TEST_ASSERT_TRUE(led.enableLED((size_t)0, false));
    TEST_ASSERT_TRUE(statusMentions(led.getLEDStatus(0), "Disabled"));

    TEST_ASSERT_TRUE(led.enableLED(String("Status"), true));
    TEST_ASSERT_TRUE(statusMentions(led.getLEDStatus(0), "Enabled"));
}

void test_enable_led_rejects_bad_index_and_name(void) {
    LEDComponent led;
    led.addSingleLED(2, "Status");
    led.begin();

    TEST_ASSERT_FALSE(led.enableLED((size_t)9, true));
    TEST_ASSERT_FALSE(led.enableLED(String("Nope"), true));
}

void test_disabled_led_status_hides_the_details(void) {
    LEDComponent led;
    led.addSingleLED(2, "Status");
    led.begin();
    led.setLED((size_t)0, LEDColor::Red(), 200);
    led.enableLED((size_t)0, false);

    String status = led.getLEDStatus(0);
    TEST_ASSERT_TRUE(statusMentions(status, "LED 'Status': Disabled"));
    TEST_ASSERT_FALSE(statusMentions(status, "Brightness"));
}

void test_status_of_out_of_range_index(void) {
    LEDComponent led;
    led.addSingleLED(2, "Status");
    led.begin();

    TEST_ASSERT_EQUAL_STRING("Invalid index", led.getLEDStatus(4).c_str());
}

// ============================================================================
// BUG-19 — addLED() after begin()
// ============================================================================

void test_led_added_after_begin_is_drivable(void) {
    LEDComponent led;
    led.addSingleLED(2, "Status");
    led.begin();

    TEST_ASSERT_TRUE(led.addSingleLED(4, "Late"));
    TEST_ASSERT_EQUAL_size_t(2, led.getLEDCount());

    // Before the fix the state vector stayed at size 1: the LED was listed by
    // getLEDCount() and getLEDNames() but every setter refused its index.
    TEST_ASSERT_TRUE(led.setLED((size_t)1, LEDColor::Cyan(), 120));
    TEST_ASSERT_TRUE(statusMentions(led.getLEDStatus(1), "LED 'Late': Enabled"));
    TEST_ASSERT_TRUE(statusMentions(led.getLEDStatus(1), "Brightness: 120"));
}

void test_rgb_led_added_after_begin_is_drivable(void) {
    LEDComponent led;
    led.addSingleLED(2, "Status");
    led.begin();

    TEST_ASSERT_TRUE(led.addRGBLED(25, 26, 27, "Mood"));
    TEST_ASSERT_TRUE(led.setLEDEffect((size_t)1, LEDEffect::Rainbow, 1000));
    TEST_ASSERT_TRUE(statusMentions(led.getLEDStatus(1), "Effect: Rainbow"));
}

void test_led_added_after_begin_with_bad_pin_is_refused(void) {
    LEDComponent led;
    led.addSingleLED(2, "Status");
    led.begin();

    // begin() already ran its validation pass; refusing here is the only place
    // left to report the mistake.
    TEST_ASSERT_FALSE(led.addSingleLED(-1, "Bad"));
    TEST_ASSERT_FALSE(led.addRGBLED(25, 26, -1, "Bad"));
    TEST_ASSERT_EQUAL_size_t(1, led.getLEDCount());
}

void test_led_added_before_begin_is_validated_by_begin(void) {
    LEDComponent led;
    // Unchanged behaviour: pins added before begin() are accepted here and
    // rejected there, so a bad pin still yields ConfigError.
    TEST_ASSERT_TRUE(led.addSingleLED(-1, "Bad"));
    TEST_ASSERT_EQUAL(ComponentStatus::ConfigError, led.begin());
}

void test_names_stay_aligned_with_late_additions(void) {
    LEDComponent led;
    led.addSingleLED(2, "Status");
    led.begin();
    led.addSingleLED(4);

    auto names = led.getLEDNames();
    TEST_ASSERT_EQUAL_size_t(2, names.size());
    TEST_ASSERT_EQUAL_STRING("LED_1", names[1].c_str());
}

// ============================================================================
// DC-5 — shutdown() releases the runtime state, keeps the configuration
// ============================================================================

void test_shutdown_releases_runtime_state(void) {
    LEDComponent led;
    led.addSingleLED(2, "Status");
    led.begin();
    led.setLED((size_t)0, LEDColor::Red(), 200);

    TEST_ASSERT_EQUAL(ComponentStatus::Success, led.shutdown());

    // State is gone: no LED may be driven while the component is down.
    TEST_ASSERT_FALSE(led.setLED((size_t)0, LEDColor::Red(), 200));
    TEST_ASSERT_FALSE(led.setLEDEffect((size_t)0, LEDEffect::Blink, 500));
    TEST_ASSERT_EQUAL_STRING("Invalid index", led.getLEDStatus(0).c_str());
}

void test_shutdown_keeps_the_configuration(void) {
    LEDComponent led;
    led.addSingleLED(2, "Status");
    led.begin();
    led.shutdown();

    // The user's registration survives — shutdown() is reversible.
    TEST_ASSERT_EQUAL_size_t(1, led.getLEDCount());
    TEST_ASSERT_EQUAL_STRING("Status", led.getLEDNames()[0].c_str());
}

void test_restart_brings_the_same_leds_back(void) {
    LEDComponent led;
    led.addSingleLED(2, "Status");
    led.begin();
    led.shutdown();

    TEST_ASSERT_EQUAL(ComponentStatus::Success, led.begin());
    TEST_ASSERT_TRUE(led.setLED((size_t)0, LEDColor::Red(), 200));
}

void test_restart_resets_the_runtime_state(void) {
    LEDComponent led;
    led.addSingleLED(2, "Status");
    led.begin();
    led.setLED((size_t)0, LEDColor::Red(), 200);
    led.setLEDEffect((size_t)0, LEDEffect::Blink, 500);

    led.begin();

    String status = led.getLEDStatus(0);
    TEST_ASSERT_TRUE(statusMentions(status, "Brightness: 0"));
    TEST_ASSERT_TRUE(statusMentions(status, "Effect: Solid"));
}

void test_add_after_shutdown_is_deferred_to_the_next_begin(void) {
    LEDComponent led;
    led.addSingleLED(2, "Status");
    led.begin();
    led.shutdown();

    TEST_ASSERT_TRUE(led.addSingleLED(4, "Late"));
    TEST_ASSERT_FALSE(led.setLED((size_t)1, LEDColor::Cyan(), 120));  // still down

    led.begin();
    TEST_ASSERT_TRUE(led.setLED((size_t)1, LEDColor::Cyan(), 120));
}

// ============================================================================
// Effect names
// ============================================================================

void test_effect_names(void) {
    LEDComponent led;
    TEST_ASSERT_EQUAL_STRING("Solid", led.getEffectName(LEDEffect::Solid).c_str());
    TEST_ASSERT_EQUAL_STRING("Blink", led.getEffectName(LEDEffect::Blink).c_str());
    TEST_ASSERT_EQUAL_STRING("Fade", led.getEffectName(LEDEffect::Fade).c_str());
    TEST_ASSERT_EQUAL_STRING("Pulse", led.getEffectName(LEDEffect::Pulse).c_str());
    TEST_ASSERT_EQUAL_STRING("Rainbow", led.getEffectName(LEDEffect::Rainbow).c_str());
    TEST_ASSERT_EQUAL_STRING("Breathing", led.getEffectName(LEDEffect::Breathing).c_str());
}

// ============================================================================
// Disabled LED stays dark; enabling drives the stored color again (#47)
// ============================================================================

void test_disabled_led_stays_dark_and_reenabling_restores_color(void) {
    HAL::Platform::setMillisForTest(1000);
    LEDComponent led;
    led.addSingleLED(2, "Status");
    TEST_ASSERT_EQUAL(ComponentStatus::Success, led.begin());
    led.setLED((size_t)0, LEDColor::Red(), 200);
    HAL::Platform::advanceMillisForTest(60);
    led.loop();
    TEST_ASSERT_EQUAL_INT(200, HAL::Platform::lastAnalogWriteValue(2));

    led.enableLED((size_t)0, false);
    HAL::Platform::advanceMillisForTest(60);
    led.loop();
    TEST_ASSERT_EQUAL_INT(0, HAL::Platform::lastAnalogWriteValue(2));

    led.enableLED((size_t)0, true);
    HAL::Platform::advanceMillisForTest(60);
    led.loop();
    TEST_ASSERT_EQUAL_INT(200, HAL::Platform::lastAnalogWriteValue(2));
    HAL::Platform::resetMillisForTest();
}

int main(int argc, char** argv) {
    UNITY_BEGIN();

    RUN_TEST(test_led_metadata_name_is_short);
    RUN_TEST(test_led_component_lookup_by_short_name);

    RUN_TEST(test_begin_with_no_leds_succeeds);
    RUN_TEST(test_begin_accepts_valid_single_and_rgb_leds);
    RUN_TEST(test_begin_rejects_negative_single_pin);
    RUN_TEST(test_begin_rejects_incomplete_rgb_pins);
    RUN_TEST(test_failed_begin_leaves_no_drivable_state);

    RUN_TEST(test_auto_generated_names);
    RUN_TEST(test_explicit_names_are_kept);

    RUN_TEST(test_set_led_by_index);
    RUN_TEST(test_set_led_rejects_out_of_range_index);
    RUN_TEST(test_set_led_by_name);
    RUN_TEST(test_set_led_by_unknown_name_fails);
    RUN_TEST(test_set_led_clears_any_running_effect);

    RUN_TEST(test_effect_defaults_brightness_to_configured_max);
    RUN_TEST(test_effect_defaults_color_to_white);
    RUN_TEST(test_effect_keeps_an_explicit_color_and_brightness);
    RUN_TEST(test_effect_re_enables_a_disabled_led);
    RUN_TEST(test_effect_by_name_and_unknown_name);
    RUN_TEST(test_effect_rejects_out_of_range_index);

    RUN_TEST(test_enable_led_by_index_and_name);
    RUN_TEST(test_enable_led_rejects_bad_index_and_name);
    RUN_TEST(test_disabled_led_status_hides_the_details);
    RUN_TEST(test_status_of_out_of_range_index);

    RUN_TEST(test_led_added_after_begin_is_drivable);
    RUN_TEST(test_rgb_led_added_after_begin_is_drivable);
    RUN_TEST(test_led_added_after_begin_with_bad_pin_is_refused);
    RUN_TEST(test_led_added_before_begin_is_validated_by_begin);
    RUN_TEST(test_names_stay_aligned_with_late_additions);

    RUN_TEST(test_shutdown_releases_runtime_state);
    RUN_TEST(test_shutdown_keeps_the_configuration);
    RUN_TEST(test_restart_brings_the_same_leds_back);
    RUN_TEST(test_restart_resets_the_runtime_state);
    RUN_TEST(test_add_after_shutdown_is_deferred_to_the_next_begin);

    RUN_TEST(test_effect_names);
    RUN_TEST(test_disabled_led_stays_dark_and_reenabling_restores_color);

    return UNITY_END();
}
