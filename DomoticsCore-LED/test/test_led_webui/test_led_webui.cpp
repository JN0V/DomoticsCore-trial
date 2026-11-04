/**
 * @file test_led_webui.cpp
 * @brief The LEDWebUI provider — the only path by which a user's browser
 *        reaches the LED component.
 *
 * It carries the input validation nobody had ever exercised: index clamping,
 * brightness range, effect name parsing, and rejection of malformed requests.
 */

#include <DomoticsCore/Logger.h>  // Required for DLOG macros used in LED.h
#include <DomoticsCore/LEDWebUI.h>
#include <unity.h>
#include <cstring>
#include <map>

using namespace DomoticsCore::Components;

void setUp(void) {}
void tearDown(void) {}

static std::map<String, String> fieldValue(const char* field, const char* value) {
    std::map<String, String> params;
    params[String("field")] = String(field);
    params[String("value")] = String(value);
    return params;
}

static String post(LEDWebUI& ui, const char* field, const char* value) {
    return ui.handleWebUIRequest(String("led_dashboard"), String("/api/ui/action"),
                                 String("POST"), fieldValue(field, value));
}

static bool contains(const String& haystack, const char* needle) {
    return strstr(haystack.c_str(), needle) != nullptr;
}

static bool succeeded(const String& response) {
    return contains(response, "\"success\":true");
}

// Two LEDs, up and running — the state every test starts from.
struct Fixture {
    LEDComponent led;
    LEDWebUI ui;

    Fixture() : ui(&led) {
        led.addSingleLED(2, "Status");
        led.addRGBLED(25, 26, 27, "Mood");
        led.begin();
    }
};

// ============================================================================
// Identity
// ============================================================================

void test_webui_name_follows_the_component(void) {
    Fixture f;
    TEST_ASSERT_EQUAL_STRING("LED", f.ui.getWebUIName().c_str());
}

void test_webui_survives_a_null_component(void) {
    // The provider is constructed from a pointer; nothing forbids a null one.
    LEDWebUI ui(nullptr);
    TEST_ASSERT_EQUAL_STRING("LED", ui.getWebUIName().c_str());
    TEST_ASSERT_FALSE(succeeded(ui.handleWebUIRequest(String("led_dashboard"), String(""),
                                                      String("POST"), fieldValue("brightness", "10"))));
}

// ============================================================================
// Request rejection
// ============================================================================

void test_get_requests_are_rejected(void) {
    Fixture f;
    String response = f.ui.handleWebUIRequest(String("led_dashboard"), String(""),
                                              String("GET"), fieldValue("brightness", "10"));
    TEST_ASSERT_FALSE(succeeded(response));
}

void test_request_without_field_is_rejected(void) {
    Fixture f;
    std::map<String, String> params;
    params[String("value")] = String("10");
    TEST_ASSERT_FALSE(succeeded(f.ui.handleWebUIRequest(String("led_dashboard"), String(""),
                                                        String("POST"), params)));
}

void test_request_without_value_is_rejected(void) {
    Fixture f;
    std::map<String, String> params;
    params[String("field")] = String("brightness");
    TEST_ASSERT_FALSE(succeeded(f.ui.handleWebUIRequest(String("led_dashboard"), String(""),
                                                        String("POST"), params)));
}

void test_unknown_field_is_rejected(void) {
    Fixture f;
    TEST_ASSERT_FALSE(succeeded(post(f.ui, "colour_temperature", "4000")));
}

// ============================================================================
// enabled_toggle
// ============================================================================

void test_enabling_lights_the_selected_led_white(void) {
    Fixture f;
    TEST_ASSERT_TRUE(succeeded(post(f.ui, "enabled_toggle", "true")));

    String status = f.led.getLEDStatus(0);
    TEST_ASSERT_TRUE(contains(status, "Enabled"));
    TEST_ASSERT_TRUE(contains(status, "Color: RGB(255,255,255)"));
    TEST_ASSERT_TRUE(contains(status, "Brightness: 128"));  // the provider's default
}

void test_disabling_turns_the_led_off(void) {
    Fixture f;
    post(f.ui, "enabled_toggle", "true");
    TEST_ASSERT_TRUE(succeeded(post(f.ui, "enabled_toggle", "false")));
    TEST_ASSERT_TRUE(contains(f.led.getLEDStatus(0), "Disabled"));
}

void test_only_the_boolean_vocabulary_is_accepted(void) {
    for (const char* v : {"1", "on", "true"}) {
        Fixture f;
        TEST_ASSERT_TRUE_MESSAGE(succeeded(post(f.ui, "enabled_toggle", v)), v);
        TEST_ASSERT_TRUE_MESSAGE(contains(f.led.getLEDStatus(0), "Enabled"), v);
    }
    for (const char* v : {"0", "off", "false"}) {
        Fixture f;
        post(f.ui, "enabled_toggle", "true");
        TEST_ASSERT_TRUE_MESSAGE(succeeded(post(f.ui, "enabled_toggle", v)), v);
        TEST_ASSERT_TRUE_MESSAGE(contains(f.led.getLEDStatus(0), "Disabled"), v);
    }
    for (const char* v : {"TRUE", "garbage", ""}) {
        Fixture f;
        String r = post(f.ui, "enabled_toggle", v);
        TEST_ASSERT_FALSE_MESSAGE(succeeded(r), v);
        TEST_ASSERT_TRUE_MESSAGE(contains(r, "\"error\":\"Invalid boolean value\""), v);
    }
}

// ============================================================================
// brightness
// ============================================================================

void test_brightness_reaches_the_led_when_enabled(void) {
    Fixture f;
    post(f.ui, "enabled_toggle", "true");
    TEST_ASSERT_TRUE(succeeded(post(f.ui, "brightness", "200")));
    TEST_ASSERT_TRUE(contains(f.led.getLEDStatus(0), "Brightness: 200"));
}

void test_brightness_is_clamped_above(void) {
    Fixture f;
    post(f.ui, "enabled_toggle", "true");
    post(f.ui, "brightness", "300");
    TEST_ASSERT_TRUE(contains(f.led.getLEDStatus(0), "Brightness: 255"));
}

void test_brightness_that_is_not_a_number_is_refused(void) {
    Fixture f;
    post(f.ui, "enabled_toggle", "true");
    post(f.ui, "brightness", "200");
    // The field is unsigned, so "-5" is not a value out of range but a value of
    // the wrong shape, and toInt() used to read it as -5 and darken the LED —
    // as it read "abc" as 0. A value that parses and overshoots still clamps.
    // "4294967296" is the one that matters: without the parser's width check it
    // truncates to 0, darkens the LED and answers success, and the clamp below
    // never sees a value to refuse.
    for (const char* bad : {"-5", "abc", "", "200x", "4294967296", "99999999999999999999"}) {
        TEST_ASSERT_FALSE_MESSAGE(succeeded(post(f.ui, "brightness", bad)), bad);
        TEST_ASSERT_TRUE_MESSAGE(contains(f.led.getLEDStatus(0), "Brightness: 200"), bad);
    }
}

void test_brightness_on_a_disabled_led_stays_dark(void) {
    Fixture f;
    post(f.ui, "brightness", "200");
    // Remembered for later, but the LED is off and must stay off.
    TEST_ASSERT_TRUE(contains(f.led.getLEDStatus(0), "Brightness: 0"));

    post(f.ui, "enabled_toggle", "true");
    TEST_ASSERT_TRUE(contains(f.led.getLEDStatus(0), "Brightness: 200"));
}

// ============================================================================
// effect
// ============================================================================

void test_each_effect_name_is_parsed(void) {
    struct { const char* name; const char* expected; } cases[] = {
        {"Solid", "Effect: Solid"},
        {"Blink", "Effect: Blink"},
        {"Fade", "Effect: Fade"},
        {"Pulse", "Effect: Pulse"},
        {"Rainbow", "Effect: Rainbow"},
        {"Breathing", "Effect: Breathing"},
    };

    for (const auto& c : cases) {
        Fixture f;
        TEST_ASSERT_TRUE(succeeded(post(f.ui, "effect", c.name)));
        TEST_ASSERT_TRUE(contains(f.led.getLEDStatus(0), c.expected));
    }
}

void test_an_unknown_effect_name_is_refused(void) {
    Fixture f;
    post(f.ui, "enabled_toggle", "true");
    post(f.ui, "effect", "Blink");

    // Asserted from Blink, not from the default: the old fallback answered
    // success and left Solid, which a test starting at Solid cannot tell apart.
    String response = post(f.ui, "effect", "Strobe");
    TEST_ASSERT_FALSE(succeeded(response));
    TEST_ASSERT_TRUE(contains(response, "Unknown effect"));
    TEST_ASSERT_TRUE(contains(f.led.getLEDStatus(0), "Effect: Blink"));
}

void test_selecting_solid_while_disabled_turns_the_led_off(void) {
    Fixture f;
    post(f.ui, "effect", "Blink");
    post(f.ui, "effect", "Solid");
    String status = f.led.getLEDStatus(0);
    TEST_ASSERT_TRUE(contains(status, "Effect: Solid"));
    TEST_ASSERT_TRUE(contains(status, "Brightness: 0"));
}

// ============================================================================
// led_select
// ============================================================================

void test_selecting_a_led_by_name_moves_the_target(void) {
    Fixture f;
    TEST_ASSERT_TRUE(succeeded(post(f.ui, "led_select", "Mood")));
    post(f.ui, "enabled_toggle", "true");

    // Brightness, not the enabled flag: an LED starts enabled at the component
    // level, so only what was written tells the two apart.
    TEST_ASSERT_TRUE(contains(f.led.getLEDStatus(1), "Brightness: 128"));
    TEST_ASSERT_TRUE(contains(f.led.getLEDStatus(0), "Brightness: 0"));
}

void test_selecting_an_unknown_name_is_refused(void) {
    Fixture f;
    post(f.ui, "led_select", "Mood");
    String response = post(f.ui, "led_select", "Nope");
    TEST_ASSERT_FALSE(succeeded(response));
    TEST_ASSERT_TRUE(contains(response, "Unknown LED"));

    // The target did not move: only Mood lights up.
    post(f.ui, "enabled_toggle", "true");
    TEST_ASSERT_TRUE(contains(f.led.getLEDStatus(1), "Brightness: 128"));
    TEST_ASSERT_TRUE(contains(f.led.getLEDStatus(0), "Brightness: 0"));
}

// The dropdown's options and the parser's accepted names are two hand-written
// lists. Since an unknown name is refused, a drift between them is a control that
// offers a value the device will not take.
void test_every_option_the_card_offers_is_accepted(void) {
    Fixture f;
    WebUIContext dash = f.ui.getWebUIContext(String("led_dashboard"));
    size_t checked = 0;
    for (const auto& field : dash.fields) {
        if (field.options.empty()) continue;
        for (const auto& option : field.options) {
            const String response = post(f.ui, field.getNameCStr(), option.c_str());
            TEST_ASSERT_TRUE_MESSAGE(succeeded(response),
                                     (String(field.getNameCStr()) + " = " + option).c_str());
            ++checked;
        }
    }
    // Six effects and two LED names, so a silently emptied list cannot pass.
    TEST_ASSERT_EQUAL_size_t(8, checked);
}

// A dashboard already built must follow the LED list: an LED added or renamed
// afterwards is offered, and every offered name is accepted.
static void assert_every_led_option_accepted(Fixture& f, const char* expected, size_t count) {
    WebUIContext dash = f.ui.getWebUIContext(String("led_dashboard"));
    bool listed = false;
    size_t checked = 0;
    for (const auto& field : dash.fields) {
        if (strcmp(field.getNameCStr(), "led_select") != 0) continue;
        for (const auto& option : field.options) {
            if (option == expected) listed = true;
            TEST_ASSERT_TRUE_MESSAGE(succeeded(post(f.ui, "led_select", option.c_str())), option.c_str());
            ++checked;
        }
    }
    TEST_ASSERT_TRUE_MESSAGE(listed, expected);
    TEST_ASSERT_EQUAL_size_t(count, checked);
}

void test_a_led_added_after_the_build_is_offered_and_accepted(void) {
    Fixture f;
    f.ui.getContextCount();
    f.led.addSingleLED(4, "Late");
    assert_every_led_option_accepted(f, "Late", 3);
}

void test_a_led_renamed_after_the_build_is_offered_and_accepted(void) {
    Fixture f;
    f.ui.getContextCount();
    TEST_ASSERT_TRUE(f.led.setLEDName(1, String("Renamed")));
    assert_every_led_option_accepted(f, "Renamed", 2);
}

// A refusal that names nothing cannot be shown on the field it was refused for.
void test_every_refusal_names_its_reason(void) {
    Fixture f;
    TEST_ASSERT_TRUE(contains(post(f.ui, "brightness", "abc"), "Invalid brightness"));
    TEST_ASSERT_TRUE(contains(post(f.ui, "nosuchfield", "1"), "Unknown field"));

    std::map<String, String> empty;
    TEST_ASSERT_TRUE(contains(f.ui.handleWebUIRequest(String("led_dashboard"), String("/"),
                                                     String("POST"), empty),
                              "Invalid request"));
    TEST_ASSERT_TRUE(contains(f.ui.handleWebUIRequest(String("led_dashboard"), String("/"),
                                                     String("GET"), fieldValue("brightness", "10")),
                              "Method not allowed"));

    LEDWebUI orphan(nullptr);
    TEST_ASSERT_TRUE(contains(post(orphan, "brightness", "10"), "Component not available"));
}

// ============================================================================
// getWebUIData
// ============================================================================

void test_status_context_reports_off_then_on(void) {
    Fixture f;
    TEST_ASSERT_TRUE(contains(f.ui.getWebUIData(String("led_status")), "\"state\":\"OFF\""));
    post(f.ui, "enabled_toggle", "true");
    TEST_ASSERT_TRUE(contains(f.ui.getWebUIData(String("led_status")), "\"state\":\"ON\""));
}

void test_dashboard_context_mirrors_the_provider_state(void) {
    Fixture f;
    post(f.ui, "led_select", "Mood");
    post(f.ui, "enabled_toggle", "true");
    post(f.ui, "brightness", "77");
    post(f.ui, "effect", "Pulse");

    String data = f.ui.getWebUIData(String("led_dashboard"));
    TEST_ASSERT_TRUE(contains(data, "\"led_select\":\"Mood\""));
    TEST_ASSERT_TRUE(contains(data, "\"enabled_toggle\":true"));
    TEST_ASSERT_TRUE(contains(data, "\"brightness\":77"));
    TEST_ASSERT_TRUE(contains(data, "\"effect\":\"Pulse\""));
}

void test_unknown_context_returns_an_empty_object(void) {
    Fixture f;
    // An untouched JsonDocument serializes as null in ArduinoJson 7, which is
    // not the object IWebUIProvider promises for a context nobody handles.
    TEST_ASSERT_EQUAL_STRING("{}", f.ui.getWebUIData(String("nope")).c_str());
}

void test_a_provider_without_a_component_answers_an_empty_object(void) {
    // Alone of the nine providers this one dereferenced its component here.
    LEDWebUI orphan(nullptr);
    TEST_ASSERT_EQUAL_STRING("{}", orphan.getWebUIData(String("led_dashboard")).c_str());
    TEST_ASSERT_EQUAL_STRING("{}", orphan.getWebUIData(String("led_status")).c_str());
}

// ============================================================================
// Contexts
// ============================================================================

void test_two_contexts_are_published(void) {
    Fixture f;
    TEST_ASSERT_EQUAL_size_t(2, f.ui.getContextCount());
    TEST_ASSERT_EQUAL_STRING("led_status", f.ui.getWebUIContext(String("led_status")).getContextIdCStr());
    TEST_ASSERT_EQUAL_STRING("led_dashboard", f.ui.getWebUIContext(String("led_dashboard")).getContextIdCStr());
}

void test_building_contexts_applies_the_initial_off_state(void) {
    Fixture f;
    f.ui.getContextCount();  // triggers buildContexts(), hence ensureInitialized()
    TEST_ASSERT_TRUE(contains(f.led.getLEDStatus(0), "Disabled"));
}

int main(int argc, char** argv) {
    UNITY_BEGIN();

    RUN_TEST(test_webui_name_follows_the_component);
    RUN_TEST(test_webui_survives_a_null_component);

    RUN_TEST(test_get_requests_are_rejected);
    RUN_TEST(test_request_without_field_is_rejected);
    RUN_TEST(test_request_without_value_is_rejected);
    RUN_TEST(test_unknown_field_is_rejected);

    RUN_TEST(test_enabling_lights_the_selected_led_white);
    RUN_TEST(test_disabling_turns_the_led_off);
    RUN_TEST(test_only_the_boolean_vocabulary_is_accepted);

    RUN_TEST(test_brightness_reaches_the_led_when_enabled);
    RUN_TEST(test_brightness_is_clamped_above);
    RUN_TEST(test_brightness_that_is_not_a_number_is_refused);
    RUN_TEST(test_brightness_on_a_disabled_led_stays_dark);

    RUN_TEST(test_each_effect_name_is_parsed);
    RUN_TEST(test_an_unknown_effect_name_is_refused);
    RUN_TEST(test_selecting_solid_while_disabled_turns_the_led_off);

    RUN_TEST(test_selecting_a_led_by_name_moves_the_target);
    RUN_TEST(test_selecting_an_unknown_name_is_refused);
    RUN_TEST(test_every_option_the_card_offers_is_accepted);
    RUN_TEST(test_a_led_added_after_the_build_is_offered_and_accepted);
    RUN_TEST(test_a_led_renamed_after_the_build_is_offered_and_accepted);
    RUN_TEST(test_every_refusal_names_its_reason);

    RUN_TEST(test_status_context_reports_off_then_on);
    RUN_TEST(test_dashboard_context_mirrors_the_provider_state);
    RUN_TEST(test_unknown_context_returns_an_empty_object);

    RUN_TEST(test_two_contexts_are_published);
    RUN_TEST(test_building_contexts_applies_the_initial_off_state);

    // Last: on the unfixed provider this one segfaults rather than failing
    // red, and would take every test after it down with it.
    RUN_TEST(test_a_provider_without_a_component_answers_an_empty_object);
    return UNITY_END();
}
