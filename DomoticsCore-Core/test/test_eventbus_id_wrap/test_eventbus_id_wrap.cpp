// EventBus subscription ids come from a 32-bit counter (nextId) that
// overflows after about 4 billion subscribe calls. Ids must never repeat
// once the counter wraps, or an unsubscribe could remove another handler.
// The counter is private and has no accessor, so the test opens it, as a
// white-box test: every header EventBus.h pulls in is included first.
#include <unity.h>
#include <functional>
#include <vector>
#include <map>
#include <queue>
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <type_traits>
#include <DomoticsCore/Platform_HAL.h>
#include <DomoticsCore/Logger.h>
#define private public
#include <DomoticsCore/EventBus.h>
#undef private

using DomoticsCore::Utils::EventBus;

void setUp(void) {}
void tearDown(void) {}

// Scenario: Wrap
// Given an event bus whose counter is near its limit,
// When a handler subscribes and then unsubscribes,
// Then only that handler is removed.
void test_wrap(void) {
    EventBus bus;
    int first = 0, last = 0, other = 0;
    const String topic("wrap/topic");
    uint32_t idFirst = bus.subscribe(topic, [&](const void*) { ++first; });
    TEST_ASSERT_NOT_EQUAL_UINT32(0, idFirst);

    bus.nextId = UINT32_MAX;  // near the limit: the next few numbers cross the wrap
    uint32_t ids[3];
    ids[0] = bus.subscribe(topic, [&](const void*) { ++other; });
    ids[1] = bus.subscribe(topic, [&](const void*) { ++other; });
    ids[2] = bus.subscribe(topic, [&](const void*) { ++last; });
    uint32_t idLast = ids[2];

    for (int i = 0; i < 3; ++i) {
        TEST_ASSERT_NOT_EQUAL_UINT32_MESSAGE(0, ids[i], "an id must never be zero");
        TEST_ASSERT_NOT_EQUAL_UINT32_MESSAGE(idFirst, ids[i], "a live id must not be reissued");
        for (int j = i + 1; j < 3; ++j) {
            TEST_ASSERT_NOT_EQUAL_UINT32_MESSAGE(ids[i], ids[j], "ids must be distinct across the wrap");
        }
    }

    bus.unsubscribe(idLast);

    int payload = 1;
    bus.publish(topic, payload);
    bus.poll();

    TEST_ASSERT_EQUAL_INT_MESSAGE(1, first, "the earlier handler must survive");
    TEST_ASSERT_EQUAL_INT_MESSAGE(2, other, "the other handlers must survive");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, last, "the unsubscribed handler must be gone");
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_wrap);
    return UNITY_END();
}
