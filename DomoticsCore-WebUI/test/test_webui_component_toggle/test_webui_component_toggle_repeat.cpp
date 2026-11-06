/**
 * @file test_webui_component_toggle_repeat.cpp
 * @brief Switching a component to the state it is already in does nothing.
 *
 * This file sits in the toggle suite's directory, which the project's test
 * filter names, so it is built into that suite. Its tests run from a static
 * initialiser, before the suite's main() starts the Unity run.
 */

#include <unity.h>

#include <DomoticsCore/WebUI.h>

using namespace DomoticsCore;
using namespace DomoticsCore::Components;

namespace {

class RepeatCounted : public IComponent {
public:
    int begins = 0;
    int shutdowns = 0;
    ComponentStatus begin() override { begins++; return ComponentStatus::Success; }
    void loop() override {}
    ComponentStatus shutdown() override { shutdowns++; return ComponentStatus::Success; }
};

class RepeatProvider : public CachingWebUIProvider {
protected:
    void buildContexts(std::vector<WebUIContext>& contexts) override {
        contexts.push_back(WebUIContext::dashboard("toggle_ctx", "Toggle"));
    }
public:
    String getWebUIName() const override { return "Toggle"; }
    String getWebUIVersion() const override { return "1.0.0"; }
    String handleWebUIRequest(const String&, const String&, const String&,
                              const std::map<String, String>&) override { return "{}"; }
    String getWebUIData(const String&) override { return "{}"; }
};

String repeatToken() {
    AsyncWebServerRequest request;
    AsyncWebServer::findRouteAnywhere("/api/ui/token")->handler(&request);
    const String body = request.sentBody;
    const int start = body.indexOf("\":\"");
    const int end = body.lastIndexOf('"');
    return (start < 0 || end <= start + 3) ? String("") : body.substring(start + 3, end);
}

void repeatPost(const char* enabled, AsyncWebServerRequest& request) {
    request.addHeader("X-DC-Token", repeatToken());
    request.addParam("name", "Toggle", true);
    request.addParam("enabled", enabled, true);
    RecordedRoute* route = AsyncWebServer::findRouteAnywhere("/api/components/enable");
    TEST_ASSERT_NOT_NULL(route);
    route->handler(&request);
}

bool repeatListedEnabled() {
    AsyncWebServerRequest request;
    AsyncWebServer::findRouteAnywhere("/api/components")->handler(&request);
    JsonDocument doc;
    deserializeJson(doc, request.sentBody);
    for (JsonObject c : doc["components"].as<JsonArray>()) {
        if (strcmp(c["name"] | "", "Toggle") == 0) return c["enabled"] | false;
    }
    TEST_FAIL_MESSAGE("Toggle is not listed");
    return false;
}

// Enable a running component: Given a component that is on, with begin() called
// once, When enabled=true is posted to /api/components/enable and the WebUI's
// loop() runs, Then begin() is not called again and the component stays on.
void test_enable_a_running_component() {
    WebUIComponent webui;
    webui.begin();
    RepeatCounted component;
    RepeatProvider provider;
    webui.registerProviderWithComponent(&provider, &component);
    component.begin();
    TEST_ASSERT_EQUAL_INT(1, component.begins);

    AsyncWebServerRequest request;
    repeatPost("true", request);
    webui.loop();

    TEST_ASSERT_EQUAL_INT_MESSAGE(1, component.begins, "begin() ran again on a running component");
    TEST_ASSERT_TRUE(repeatListedEnabled());
}

// Disable twice: Given a component that is on, When enabled=false is posted
// twice, with a loop() after each, Then shutdown() is called once.
void test_disable_twice() {
    WebUIComponent webui;
    webui.begin();
    RepeatCounted component;
    RepeatProvider provider;
    webui.registerProviderWithComponent(&provider, &component);

    AsyncWebServerRequest first;
    repeatPost("false", first);
    webui.loop();
    AsyncWebServerRequest second;
    repeatPost("false", second);
    webui.loop();

    TEST_ASSERT_EQUAL_INT_MESSAGE(1, component.shutdowns, "shutdown() ran again on a stopped component");
    TEST_ASSERT_FALSE(repeatListedEnabled());
}

struct RepeatRunner {
    RepeatRunner() {
        Unity.TestFile = __FILE__;
        UnityDefaultTestRun(test_enable_a_running_component, "test_enable_a_running_component", __LINE__);
        UnityDefaultTestRun(test_disable_twice, "test_disable_twice", __LINE__);
    }
} repeatRunner;

}  // namespace
