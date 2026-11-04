#include <unity.h>
#include <DomoticsCore/OTA.h>

using namespace DomoticsCore::Components;

void setUp() {}
void tearDown() {}

static OTAConfig capConfig() {
    OTAConfig cfg;
    cfg.maxDownloadSize = 1000;
    cfg.requireUploadHash = false;
    cfg.autoReboot = false;
    cfg.checkIntervalMs = 0;
    return cfg;
}

// Scenario: Cap exceeded
// Given an upload announcing N bytes,
// When more than N bytes are delivered,
// Then the answer says `Firmware too large`, not `Upload not active`.
void test_cap_exceeded() {
    OTAComponent ota(capConfig());
    ota.begin();

    uint8_t buf[400];
    for (size_t i = 0; i < sizeof(buf); ++i) buf[i] = static_cast<uint8_t>(i);

    TEST_ASSERT_TRUE(ota.beginUpload(1000, String("")));
    TEST_ASSERT_TRUE(ota.acceptUploadChunk(buf, 400));
    TEST_ASSERT_TRUE(ota.acceptUploadChunk(buf, 400));

    // The chunk that crosses the cap.
    TEST_ASSERT_FALSE(ota.acceptUploadChunk(buf, 400));
    TEST_ASSERT_EQUAL_STRING("Firmware too large", ota.getLastError().c_str());
    TEST_ASSERT_FALSE(ota.isBusy());

    // Further chunks and the final call keep the same error.
    TEST_ASSERT_FALSE(ota.acceptUploadChunk(buf, 400));
    TEST_ASSERT_EQUAL_STRING("Firmware too large", ota.getLastError().c_str());
    TEST_ASSERT_FALSE(ota.acceptUploadChunk(buf, 400));
    TEST_ASSERT_EQUAL_STRING("Firmware too large", ota.getLastError().c_str());
    TEST_ASSERT_FALSE(ota.finalizeUpload());
    TEST_ASSERT_EQUAL_STRING("Firmware too large", ota.getLastError().c_str());
    TEST_ASSERT_FALSE(ota.isBusy());
}

#ifdef ARDUINO
void setup() {
    delay(2000);
    UNITY_BEGIN();
    RUN_TEST(test_cap_exceeded);
    UNITY_END();
}
void loop() {}
#else
int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_cap_exceeded);
    return UNITY_END();
}
#endif
