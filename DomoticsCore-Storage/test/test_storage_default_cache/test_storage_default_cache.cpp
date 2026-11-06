// Tests that a default handed to a getter is never taken for a stored value:
// the cache holds only what the backend holds.

#include <unity.h>
#include <DomoticsCore/Core.h>
#include <DomoticsCore/Storage.h>

using namespace DomoticsCore;
using namespace DomoticsCore::Components;

void setUp(void) {
    HAL::RAMOnlyStorage::forgetPersistedForTest();
    HAL::RAMOnlyStorage::persistAcrossInstancesForTest = true;
}

void tearDown(void) {
    HAL::RAMOnlyStorage::persistAcrossInstancesForTest = false;
    HAL::RAMOnlyStorage::forgetPersistedForTest();
}

static StorageComponent* boot(Core& core, const char* ns) {
    StorageConfig config;
    config.namespace_name = ns;
    auto p = std::make_unique<StorageComponent>(config);
    StorageComponent* s = p.get();
    core.addComponent(std::move(p));
    core.begin();
    return s;
}

// Scenario: Default read, then save
// Given a fresh namespace with no key `mode`,
// When I read `getString("mode", "auto")` and then call `putString("mode", "auto")`,
// Then `exists("mode")` is true and the value is still there after a reboot.
void test_default_read_then_save(void) {
    {
        Core core;
        StorageComponent* s = boot(core, "dflt_str");
        TEST_ASSERT_EQUAL_STRING("auto", s->getString("mode", "auto").c_str());
        TEST_ASSERT_FALSE(s->exists("mode"));
        unsigned before = HAL::RAMOnlyStorage::writesForTest;
        TEST_ASSERT_TRUE(s->putString("mode", "auto"));
        TEST_ASSERT_TRUE(HAL::RAMOnlyStorage::writesForTest > before);
        TEST_ASSERT_TRUE(s->exists("mode"));
        core.shutdown();
    }
    {
        Core core;
        StorageComponent* s = boot(core, "dflt_str");
        TEST_ASSERT_TRUE(s->exists("mode"));
        TEST_ASSERT_EQUAL_STRING("auto", s->getString("mode", "other").c_str());
        core.shutdown();
    }
}

// Scenario: Second default
// Given no key `mode`,
// When I read it with default `"auto"` and then with default `"manual"`,
// Then the second read returns `"manual"`.
void test_second_default(void) {
    Core core;
    StorageComponent* s = boot(core, "dflt_two");
    TEST_ASSERT_EQUAL_STRING("auto", s->getString("mode", "auto").c_str());
    TEST_ASSERT_EQUAL_STRING("manual", s->getString("mode", "manual").c_str());
    core.shutdown();
}

// Scenario: Integer
// Given no key `interval`,
// When I read `getInt("interval", 60)` and then call `putInt("interval", 60)`,
// Then `exists("interval")` is true.
void test_integer(void) {
    Core core;
    StorageComponent* s = boot(core, "dflt_int");
    TEST_ASSERT_EQUAL_INT32(60, s->getInt("interval", 60));
    TEST_ASSERT_FALSE(s->exists("interval"));
    unsigned before = HAL::RAMOnlyStorage::writesForTest;
    TEST_ASSERT_TRUE(s->putInt("interval", 60));
    TEST_ASSERT_TRUE(HAL::RAMOnlyStorage::writesForTest > before);
    TEST_ASSERT_TRUE(s->exists("interval"));
    core.shutdown();
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_default_read_then_save);
    RUN_TEST(test_second_default);
    RUN_TEST(test_integer);
    return UNITY_END();
}
