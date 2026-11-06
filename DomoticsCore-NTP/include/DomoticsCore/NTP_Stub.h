#ifndef DOMOTICS_CORE_NTP_STUB_H
#define DOMOTICS_CORE_NTP_STUB_H

/**
 * @file NTP_Stub.h
 * @brief Stub NTP implementation for unsupported platforms.
 */

#include <cstdint>
#include <cstdlib>
#include <string>
#include <time.h>

#if !DOMOTICS_PLATFORM_ESP32 && !DOMOTICS_PLATFORM_ESP8266

namespace DomoticsCore {
namespace HAL {
namespace NTPImpl {

/// The last interval handed over, in milliseconds. A host suite has no SNTP
/// client to interrogate, so the stub keeps what it was given.
inline uint32_t& lastSyncIntervalMs() {
    static uint32_t value = 0;
    return value;
}

/// Whether the client is started, and how many stops and syncs were requested of it.
inline bool& clientRunning() {
    static bool value = false;
    return value;
}
inline uint32_t& stopCalls() {
    static uint32_t value = 0;
    return value;
}
inline uint32_t& forceSyncCalls() {
    static uint32_t value = 0;
    return value;
}

/// The servers the last init() was given, "" for none.
inline std::string& initServer(int i) {
    static std::string servers[3];
    return servers[i];
}

/// A clock a host suite can set; 0 reads the real one.
inline time_t& clockForTest() {
    static time_t value = 0;
    return value;
}

/// Whether a sync request is answered, and how many syncs a server answered.
/// A host suite chooses: by default no server answers.
inline bool& answerSyncForTest() {
    static bool value = false;
    return value;
}
inline uint32_t& syncAnswers() {
    static uint32_t value = 0;
    return value;
}

inline void init(const char* s1, const char* s2, const char* s3) {
    initServer(0) = s1 ? s1 : "";
    initServer(1) = s2 ? s2 : "";
    initServer(2) = s3 ? s3 : "";
    clientRunning() = true;
}
// As on the boards: the C library applies the POSIX rule, DST included.
inline void setTimezone(const char* tz) {
    setenv("TZ", tz, 1);
    tzset();
}
inline time_t now() { return clockForTest() ? clockForTest() : time(nullptr); }
inline void setSyncInterval(uint32_t intervalMs) { lastSyncIntervalMs() = intervalMs; }
inline void stop() { clientRunning() = false; stopCalls()++; }
inline void forceSync() {
    forceSyncCalls()++;
    if (answerSyncForTest()) syncAnswers()++;
}
inline uint32_t syncCounter() { return syncAnswers(); }

} // namespace NTPImpl
} // namespace HAL
} // namespace DomoticsCore

#endif // Stub

#endif // DOMOTICS_CORE_NTP_STUB_H
