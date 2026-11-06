#ifndef DOMOTICS_CORE_HAL_NTP_H
#define DOMOTICS_CORE_HAL_NTP_H

/**
 * @file NTP_HAL.h
 * @brief NTP (Network Time Protocol) Hardware Abstraction Layer.
 * 
 * Provides a unified interface for time synchronization across platforms:
 * - ESP32: Uses esp_sntp API
 * - ESP8266: Uses lwIP's sntp client
 * - Other platforms: Stub implementation
 */

#include "DomoticsCore/Platform_HAL.h"
#include <time.h>

namespace DomoticsCore {
namespace HAL {
namespace NTP {

// Forward declarations for common functions
inline bool isSynced();
inline time_t getTime();

} // namespace NTP
} // namespace HAL
} // namespace DomoticsCore

// ============================================================================
// Platform-specific Implementation Files
// ============================================================================

#if DOMOTICS_PLATFORM_ESP32
    #include "NTP_ESP32.h"
#elif DOMOTICS_PLATFORM_ESP8266
    #include "NTP_ESP8266.h"
    #include <coredecls.h>  // settimeofday_cb
#else
    #include "NTP_Stub.h"
#endif

// ============================================================================
// Backward-Compatible API (delegates to NTPImpl namespace)
// ============================================================================

namespace DomoticsCore {
namespace HAL {
namespace NTP {

#if DOMOTICS_PLATFORM_ESP32 || DOMOTICS_PLATFORM_ESP8266
/// Syncs a server answered, counted from the SNTP client's notification.
inline uint32_t& answeredSyncs() {
    static uint32_t value = 0;
    return value;
}
#endif

#if DOMOTICS_PLATFORM_ESP32
inline void onSntpAnswer(struct timeval*) { answeredSyncs()++; }
#elif DOMOTICS_PLATFORM_ESP8266
// Called on every settimeofday(), SNTP's or not. Count it only when the clock
// agrees with the SNTP client's own timestamp (the server's answer); a clock
// set by other code does not match it and is ignored.
inline void onSntpAnswer() {
    uint32_t server = sntp_get_current_timestamp();
    if (server == 0) return;
    long diff = (long)time(nullptr) - (long)server;
    if (diff < -2 || diff > 2) return;
    answeredSyncs()++;
}
#endif

inline void init(const char* server1, const char* server2 = nullptr, const char* server3 = nullptr) {
#if DOMOTICS_PLATFORM_ESP32
    sntp_set_time_sync_notification_cb(onSntpAnswer);
#elif DOMOTICS_PLATFORM_ESP8266
    settimeofday_cb(onSntpAnswer);
#endif
    NTPImpl::init(server1, server2, server3);
}

/// Count of syncs a server answered; a change tells a requested sync ended.
inline uint32_t syncCounter() {
#if DOMOTICS_PLATFORM_ESP32
    return answeredSyncs();
#elif DOMOTICS_PLATFORM_ESP8266
    return answeredSyncs();
#else
    return NTPImpl::syncCounter();
#endif
}

inline void setTimezone(const char* tz) {
    NTPImpl::setTimezone(tz);
}

inline void setSyncInterval(uint32_t intervalMs) {
    NTPImpl::setSyncInterval(intervalMs);
}

inline void stop() {
    NTPImpl::stop();
}

inline void forceSync() {
    NTPImpl::forceSync();
}

inline time_t getTime() {
    return NTPImpl::now();
}

inline bool isSynced() {
    return getTime() > 1577836800;  // 2020-01-01 00:00:00 UTC
}

inline bool getFormattedTime(const char* format, char* buffer, size_t bufferSize) {
    if (!isSynced()) {
        if (bufferSize > 0) buffer[0] = '\0';
        return false;
    }
    
    time_t now = getTime();
    struct tm timeinfo;
    localtime_r(&now, &timeinfo);
    strftime(buffer, bufferSize, format, &timeinfo);
    return true;
}

} // namespace NTP
} // namespace HAL
} // namespace DomoticsCore

#endif // DOMOTICS_CORE_HAL_NTP_H
