/**
 * @file test_ntp_sync_timeout.cpp
 * @brief A manual sync that no server answers is a failure, not a success.
 *        The clock is the stub's, set per test; its forceSync() never answers.
 */

#include <unity.h>
#include <memory>
#include <DomoticsCore/Core.h>
#include <DomoticsCore/NTP.h>

using namespace DomoticsCore;
using namespace DomoticsCore::Components;

static const time_t kFirst = 1700000000;
static const time_t kLater = 1700000100;

void setUp() {
    HAL::setCanOpenServerForTest(true);
    HAL::NTPImpl::clockForTest() = 0;
}

void tearDown() {
    HAL::NTPImpl::clockForTest() = 0;
}

namespace {

struct SyncedOnce {
    Core core;
    NTPComponent* ntp = nullptr;
    int successCalls = 0;
    int failureCalls = 0;
    int failedEvents = 0;

    SyncedOnce() {
        NTPConfig cfg;
        cfg.timeoutMs = 30;
        auto owned = std::make_unique<NTPComponent>(cfg);
        ntp = owned.get();
        core.addComponent(std::move(owned));
        HAL::NTPImpl::clockForTest() = kFirst;
        core.begin();
        core.loop();
        TEST_ASSERT_TRUE_MESSAGE(ntp->isSynced(), "the fixture's component never synced");
        TEST_ASSERT_EQUAL_UINT32(1, ntp->getStatistics().syncCount);
        core.on<bool>(NTPEvents::EVENT_SYNC_FAILED, [this](const bool&) { failedEvents++; });
        ntp->onSync([this](bool ok) { if (ok) successCalls++; else failureCalls++; });
    }

    ~SyncedOnce() { core.shutdown(); }
};

}  // namespace

// No server answers: Given an NTP component that synced once, When I call
// syncNow() and no server answers within timeoutMs, Then syncErrors is 1,
// consecutiveFailures is 1, onSync(false) is called and ntp/sync_failed is emitted.
void test_no_server_answers() {
    SyncedOnce f;
    TEST_ASSERT_TRUE(f.ntp->syncNow());
    HAL::NTPImpl::clockForTest() = kLater;

    HAL::delay(80);
    f.core.loop();
    f.core.loop();

    const NTPStatistics& st = f.ntp->getStatistics();
    TEST_ASSERT_EQUAL_UINT32(1, st.syncErrors);
    TEST_ASSERT_EQUAL_UINT32(1, st.consecutiveFailures);
    TEST_ASSERT_EQUAL_INT(1, f.failureCalls);
    TEST_ASSERT_EQUAL_INT(0, f.successCalls);
    TEST_ASSERT_EQUAL_INT(1, f.failedEvents);
}

// Success is not counted early: Given the same setup, When the first loop()
// runs after syncNow(), Then syncCount stays at 1 and onSync(true) is not called.
void test_success_is_not_counted_early() {
    SyncedOnce f;
    TEST_ASSERT_TRUE(f.ntp->syncNow());
    HAL::NTPImpl::clockForTest() = kLater;

    f.core.loop();

    TEST_ASSERT_EQUAL_UINT32(1, f.ntp->getStatistics().syncCount);
    TEST_ASSERT_EQUAL_INT(0, f.successCalls);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_no_server_answers);
    RUN_TEST(test_success_is_not_counted_early);
    return UNITY_END();
}
