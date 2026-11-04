#include "DomoticsCore/OTA.h"
#include "DomoticsCore/Events.h"
#include "DomoticsCore/OTAEvents.h"
#include "DomoticsCore/Platform_HAL.h"  // For HAL::SHA256
#include "DomoticsCore/Update_HAL.h"
#include <ArduinoJson.h>

#include "DomoticsCore/Logger.h"

using namespace DomoticsCore::Components;

namespace {

String toHex(const uint8_t* data, size_t len) {
    static const char* hex = "0123456789ABCDEF";
    String out;
    out.reserve(len * 2);
    for (size_t i = 0; i < len; ++i) {
        out += hex[(data[i] >> 4) & 0x0F];
        out += hex[data[i] & 0x0F];
    }
    return out;
}

const char* stateToString(OTAComponent::State state) {
    switch (state) {
        case OTAComponent::State::Idle: return "idle";
        case OTAComponent::State::Checking: return "checking";
        case OTAComponent::State::Downloading: return "downloading";
        case OTAComponent::State::Applying: return "applying";
        case OTAComponent::State::RebootPending: return "reboot_pending";
        case OTAComponent::State::Error: return "error";
    }
    return "unknown";
}

} // namespace

// Restore constructor and helper methods outside anonymous namespace
OTAComponent::OTAComponent(const OTAConfig& cfg) : config(cfg) {
    metadata.name = "OTA";
    metadata.version = "1.11.0";  // x-release-please-version
    metadata.description = "Secure over-the-air firmware updates";
    metadata.author = "DomoticsCore";
    metadata.category = "system";
}

void OTAComponent::transition(State nextState, const String& reason) {
    state = nextState;
    stateChangeMillis = HAL::Platform::getMillis();
    lastProgressPublishMillis = stateChangeMillis;
    if (!reason.isEmpty()) {
        lastResult = reason;
    }
    const String reasonSuffix = reason.isEmpty() ? String("") : String(" | ") + reason;
    DLOG_I(LOG_OTA, "State -> %s%s", stateToString(state), reasonSuffix.c_str());
}

bool OTAComponent::shouldCheckNow() const {
    if (config.checkIntervalMs == 0) return false;
    return HAL::Platform::getMillis() >= nextCheckMillis;
}

bool OTAComponent::triggerUpdateFromUrl(const String& url, bool force) {
    return triggerUpdateFromUrl(url, String(""), force);
}

bool OTAComponent::triggerUpdateFromUrl(const String& url, const String& expectedSha256, bool force) {
    if (url.isEmpty()) return false;
    if (config.requireDownloadHash && expectedSha256.isEmpty()) {
        lastError = "Firmware hash required";
        DLOG_W(LOG_OTA, "URL install refused: requireDownloadHash is set and no SHA-256 was supplied");
        return false;
    }
    pendingUrl = url;
    pendingUrlSha256 = expectedSha256;
    pendingUrlForce = force;
    pendingUrlUpdate = true;
    return true;
}

ComponentStatus OTAComponent::begin() {
    state = State::Idle;
    stateChangeMillis = HAL::Platform::getMillis();
    lastProgressPublishMillis = stateChangeMillis;
    nextCheckMillis = HAL::Platform::getMillis() + config.checkIntervalMs;
    lastResult = "Idle";
    lastError.clear();
    pendingCheck = false;
    pendingForce = false;
    pendingUrlUpdate = false;
    pendingUrlForce = false;
    pendingUrl.clear();
    pendingUrlSha256.clear();
    uploadSession = UploadSession{};
    progress = 0.0f;
    downloadedBytes = 0;
    totalBytes = 0;
    lastLoggedProgress = -1.0f;
    lastLoggedBytes = 0;
    return ComponentStatus::Success;
}

void OTAComponent::loop() {
    const unsigned long now = HAL::Platform::getMillis();

    if (pendingUrlUpdate) {
        const bool force = pendingUrlForce;
        const String url = pendingUrl;
        const String sha256 = pendingUrlSha256;
        pendingUrlUpdate = false;
        pendingUrlForce = false;
        pendingUrl.clear();
        pendingUrlSha256.clear();
        installFromUrl(url, sha256, force);
    } else if (pendingCheck) {
        const bool force = pendingForce;
        pendingCheck = false;
        pendingForce = false;
        performCheck(force);
    } else if (shouldCheckNow()) {
        performCheck(false);
    }

    if (state == State::RebootPending && config.autoReboot) {
        if (now - stateChangeMillis > 2000UL) {  // 2 seconds to allow final UI update
            DLOG_I(LOG_OTA, "Rebooting to apply firmware update");
            HAL::Platform::delayMs(100);
            HAL::restart();
        }
    }
}

ComponentStatus OTAComponent::shutdown() {
    if (uploadSession.active) {
        HAL::OTAUpdate::abort();
        uploadSha.abort();
    }
    state = State::Idle;
    return ComponentStatus::Success;
}

bool OTAComponent::triggerImmediateCheck(bool force) {
    pendingCheck = true;
    pendingForce = force;
    return true;
}

bool OTAComponent::beginUpload(size_t expectedSize, const String& expectedSha256) {
    if (uploadSession.active) {
        lastError = "Upload already in progress";
        return false;
    }

    // SEC-7: refuse before HAL::OTAUpdate::begin(), which erases flash. A refusal
    // after that point would have destroyed the running firmware to reject an
    // upload it never intended to install.
    if (config.requireUploadHash && expectedSha256.isEmpty()) {
        lastError = "Firmware hash required";
        DLOG_W(LOG_OTA, "Upload rejected: requireUploadHash is set and no SHA-256 was supplied");
        return false;
    }

    // SEC-8: maxDownloadSize used to apply only to the transfer this device
    // initiates, and not to the one anybody could POST. Refused here, before
    // begin() opens an update, for the same reason as the hash check above.
    //
    // SEC-9: expectedSize is what the sender *announced*, and on the browser
    // path OTAWebUI announces request->contentLength() — the whole multipart
    // body, framing included, measured at 220 bytes over the firmware on both
    // boards. That makes it an upper bound rather than a size, so this check
    // can only ever over-refuse, never under-refuse, and the log has to say
    // which number it compared. The ceiling that *decides* is the one in
    // acceptUploadChunk(), which counts the bytes that actually arrive; this
    // one exists to refuse the obviously-oversized before an update is opened
    // at all. That ordering is not cosmetic: HAL::OTAUpdate::begin() targets
    // esp_ota_get_next_update_partition(), which holds the image canRollBack()
    // would boot — and on a single-slot ESP32 is the running code itself. See
    // the partition note in DomoticsCore-OTA/platformio.ini.
    if (config.maxDownloadSize > 0 && expectedSize > config.maxDownloadSize) {
        lastError = "Firmware too large";
        // Kept short on purpose: DLOG_W formats into DOMOTICS_DLOG_BUF_SIZE, which
        // is 128 bytes on ESP8266 (Platform_ESP8266.h:49). At 96 characters with
        // two 10-digit figures this fits; a longer sentence would be truncated on
        // the one platform where the ceiling is tightest.
        DLOG_W(LOG_OTA, "Upload rejected: %lu announced bytes (framing included) against a %lu byte ceiling",
               static_cast<unsigned long>(expectedSize),
               static_cast<unsigned long>(config.maxDownloadSize));
        return false;
    }

    size_t updateSize = expectedSize > 0 ? expectedSize : UPDATE_SIZE_UNKNOWN;
    if (!HAL::OTAUpdate::begin(updateSize)) {
        lastError = HAL::OTAUpdate::errorString();
        return false;
    }

    // A previous session may have been abandoned without finishing its digest.
    uploadSha.abort();
    uploadSha.begin();

    uploadSession = UploadSession{};
    uploadSession.active = true;
    uploadSession.expected = expectedSize;
    uploadSession.received = 0;
    uploadSession.success = false;
    uploadSession.error.clear();
    uploadSession.expectedSha256 = expectedSha256;

    totalBytes = expectedSize;
    downloadedBytes = 0;
    progress = 0.0f;
    lastLoggedProgress = 0.0f;
    lastLoggedBytes = 0;

    transition(State::Downloading, "Manual upload started");
    lastResult = "Uploading firmware";
    if (expectedSize > 0) {
        DLOG_I(LOG_OTA, "Upload started | expected bytes=%lu", static_cast<unsigned long>(expectedSize));
    } else {
        DLOG_I(LOG_OTA, "Upload started | expected bytes=unknown");
    }
    // BUG-21: the lifecycle event. EVENT_INFO below is kept because the published
    // reference names it as the upload-start signal and this library is installed
    // by version — removing it would break whoever followed the documentation.
    publishStatusEvent(DomoticsCore::OTAEvents::EVENT_START, [this](JsonDocument& doc){
        doc["success"] = true;
        doc["source"] = "upload";
        doc["total"] = totalBytes;
    }, false);
    publishStatusEvent(DomoticsCore::OTAEvents::EVENT_INFO, [this](JsonDocument& doc){
        doc["success"] = true;
        doc["message"] = "Upload started";
        doc["source"] = "upload";
    }, false);
    return true;
}

bool OTAComponent::acceptUploadChunk(const uint8_t* data, size_t length) {
    if (!uploadSession.active) {
        // A session closed by its own failure keeps that failure as the answer.
        lastError = uploadSession.error.isEmpty() ? String("Upload not active") : uploadSession.error;
        return false;
    }
    if (!data || length == 0) {
        return true;
    }

    // SEC-8: the announced size is a number the sender chose, and beginUpload(0)
    // means it announced nothing at all. Counting what actually arrives is the
    // only check that holds in either case.
    if (config.maxDownloadSize > 0 &&
        uploadSession.received + length > config.maxDownloadSize) {
        lastError = "Firmware too large";
        DLOG_W(LOG_OTA, "Upload aborted: %lu bytes would pass a %lu byte ceiling",
               static_cast<unsigned long>(uploadSession.received + length),
               static_cast<unsigned long>(config.maxDownloadSize));
        uploadSession.error = lastError;
        HAL::OTAUpdate::abort();
        uploadSha.abort();
        uploadSession.active = false;
        transition(State::Error, lastError);
        publishStatusEvent(DomoticsCore::OTAEvents::EVENT_ERROR, [this](JsonDocument& doc){
            doc["success"] = false;
            doc["error"] = lastError.c_str();
            doc["source"] = "upload";
        }, false);
        return false;
    }

    size_t written = HAL::OTAUpdate::write(const_cast<uint8_t*>(data), length);
    if (written != length) {
        lastError = HAL::OTAUpdate::errorString();
        uploadSession.error = lastError;
        HAL::OTAUpdate::abort();
        uploadSha.abort();
        uploadSession.active = false;
        transition(State::Error, lastError);
        publishStatusEvent(DomoticsCore::OTAEvents::EVENT_ERROR, [this](JsonDocument& doc){
            doc["success"] = false;
            doc["error"] = lastError.c_str();
            doc["source"] = "upload";
        }, false);
        return false;
    }

    // Hash what was written, not what was offered — the same distinction
    // installFromUrl makes.
    uploadSha.update(data, written);

    uploadSession.received += written;

    downloadedBytes = uploadSession.received;

    // Update progress based on received bytes
    if (uploadSession.expected > 0) {
        progress = (uploadSession.received * 100.0f) / static_cast<float>(uploadSession.expected);
    } else {
        progress = 0.0f;
    }

    // Log progress periodically
    if (uploadSession.expected > 0) {
        float delta = fabs(progress - lastLoggedProgress);
        if (delta >= 10.0f || (uploadSession.received - lastLoggedBytes) >= 256 * 1024) {
            DLOG_I(LOG_OTA, "Upload progress: %.1f%% (%lu/%lu bytes)",
                   static_cast<double>(progress),
                   static_cast<unsigned long>(uploadSession.received),
                   static_cast<unsigned long>(uploadSession.expected));
            lastLoggedProgress = progress;
            lastLoggedBytes = uploadSession.received;
        }
    } else if ((uploadSession.received - lastLoggedBytes) >= 256 * 1024) {
        DLOG_I(LOG_OTA, "Upload received: %lu bytes (no size known)", static_cast<unsigned long>(uploadSession.received));
        lastLoggedBytes = uploadSession.received;
    }

    // Throttle progress broadcasts to avoid EventBus queue overflow (every 1 second)
    const unsigned long now = HAL::Platform::getMillis();
    if ((now - lastProgressPublishMillis) > 1000) {
        publishStatusEvent(DomoticsCore::OTAEvents::EVENT_PROGRESS, [this](JsonDocument& doc){
            doc["success"] = true;
            doc["source"] = "upload";
            doc["bytes"] = uploadSession.received;
            doc["total"] = uploadSession.expected;
        }, false);
        lastProgressPublishMillis = now;
    }
    return true;
}

bool OTAComponent::finalizeUpload() {
    if (!uploadSession.active) {
        // A session closed by its own failure keeps that failure as the answer.
        lastError = uploadSession.error.isEmpty() ? String("Upload not active") : uploadSession.error;
        return false;
    }

    DLOG_I(LOG_OTA, "Upload finalizing | received=%lu bytes",
           static_cast<unsigned long>(uploadSession.received));

    // SEC-9: the transfer is over, so the announced figure has outlived its
    // usefulness and the counted one is now known exactly. Narrowed here, above
    // the first event that reports it, so ota/end and ota/completed cannot give
    // two different totals for one upload. The announced figure survives only as
    // ota/progress's denominator, where it is the only number there is.
    //
    // finalizeUpdateOperation() later does downloadedBytes = totalBytes to make
    // the completion event self-consistent. That is defensible for a download,
    // where totalBytes is what the server announced, and wrong for an upload,
    // where it is the multipart envelope. Narrowing here rather than in that
    // shared helper leaves the download path exactly as it was — including its
    // own version of this overstatement, which is recorded in TEST-8.
    //
    // NOT PINNED BY ANY TEST. The ordering is invisible to the native suite: the
    // EventBus dispatches after publish, so a subscriber reading getTotalBytes()
    // always sees the narrowed value whatever the order, and reading the payload
    // itself needs on<String>, which is BUG-30's use-after-free. A test asserting
    // this was written, was proved vacuous by moving this line back, and was
    // deleted rather than kept as decoration.
    totalBytes = uploadSession.received;

    // BUG-21: the transfer is over and the verdict is not in yet — which is what
    // "before verification" in the OTAEvents.h contract means. Emitted here so a
    // subscriber can tell a transfer that died from an image that was rejected.
    publishStatusEvent(DomoticsCore::OTAEvents::EVENT_END, [this](JsonDocument& doc){
        doc["success"] = true;
        doc["source"] = "upload";
        doc["bytes"] = uploadSession.received;
        doc["total"] = totalBytes;
    }, false);

    // SEC-7: verify before committing, on the ordering SEC-2 established.
    // end(true) is the point of no return — it switches the ESP32 boot partition
    // and stages the ESP8266 eboot copy — and no Arduino core lets the
    // application undo it. See the contract in Update_HAL.h.
    uint8_t digest[32];
    uploadSha.finish(digest);

    if (!uploadSession.expectedSha256.isEmpty() &&
        !verifySha256(digest, uploadSession.expectedSha256)) {
        lastError = "SHA256 mismatch";
        uploadSession.error = lastError;
        HAL::OTAUpdate::abort();
        uploadSession.active = false;
        transition(State::Error, lastError);
        publishStatusEvent(DomoticsCore::OTAEvents::EVENT_ERROR, [this](JsonDocument& doc){
            doc["success"] = false;
            doc["error"] = lastError.c_str();
            doc["source"] = "upload";
        }, false);
        return false;
    }

    if (!HAL::OTAUpdate::end(true)) {
        lastError = HAL::OTAUpdate::errorString();
        uploadSession.error = lastError;
        HAL::OTAUpdate::abort();
        uploadSession.active = false;
        transition(State::Error, lastError);
        publishStatusEvent(DomoticsCore::OTAEvents::EVENT_ERROR, [this](JsonDocument& doc){
            doc["success"] = false;
            doc["error"] = lastError.c_str();
            doc["source"] = "upload";
        }, false);
        return false;
    }

    uploadSession.success = true;
    uploadSession.active = false;
    DLOG_I(LOG_OTA, "Upload finalized | bytes=%lu", static_cast<unsigned long>(uploadSession.received));
    finalizeUpdateOperation("upload", config.autoReboot);
    return true;
}

void OTAComponent::abortUpload(const String& reason) {
    if (!uploadSession.active) {
        return;
    }
    HAL::OTAUpdate::abort();
    uploadSha.abort();
    uploadSession.active = false;
    uploadSession.success = false;
    uploadSession.error = reason;
    lastError = reason;
    transition(State::Error, reason);
    publishStatusEvent(DomoticsCore::OTAEvents::EVENT_ERROR, [this](JsonDocument& doc){
        doc["success"] = false;
        doc["error"] = lastError.c_str();
        doc["source"] = "upload";
    }, false);
}

bool OTAComponent::isIdle() const {
    return state == State::Idle || state == State::Error;
}

bool OTAComponent::isBusy() const {
    return state == State::Checking || state == State::Downloading || state == State::Applying;
}

void OTAComponent::scheduleNextCheck(uint32_t delayMs) {
    if (config.checkIntervalMs == 0 && delayMs == 0) {
        return;
    }
    unsigned long interval = delayMs ? delayMs : config.checkIntervalMs;
    nextCheckMillis = HAL::Platform::getMillis() + interval;
}

bool OTAComponent::performCheck(bool force) {
    if (!force && config.manifestUrl.isEmpty() && config.updateUrl.isEmpty()) {
        lastResult = "No update URL configured";
        return false;
    }

    transition(State::Checking, "Checking for updates");

    ManifestInfo manifest;
    if (!config.manifestUrl.isEmpty()) {
        manifest = fetchManifest();
        if (!manifest.valid) {
            lastError = "Failed to fetch manifest";
            transition(State::Error, lastError);
            scheduleNextCheck();
            return false;
        }
        if (!force && !manifest.version.isEmpty() && !isNewerVersion(manifest.version) && !config.allowDowngrades) {
            lastResult = "Firmware already up to date";
            transition(State::Idle, "No update needed");
            scheduleNextCheck();
            return true;
        }
    } else {
        manifest.url = config.updateUrl;
        manifest.valid = true;
    }

    if (manifest.url.isEmpty()) {
        lastError = "Manifest missing URL";
        transition(State::Error, lastError);
        scheduleNextCheck();
        return false;
    }

    bool ok = installFromUrl(manifest.url, manifest.sha256, force || config.allowDowngrades);
    if (!manifest.version.isEmpty()) {
        lastVersionSeen = manifest.version;
    }
    scheduleNextCheck();
    return ok;
}

OTAComponent::ManifestInfo OTAComponent::fetchManifest() {
    ManifestInfo info;
    if (config.manifestUrl.isEmpty()) {
        return info;
    }
    if (!manifestFetcher) {
        DLOG_E(LOG_OTA, "No manifest fetcher set");
        return info;
    }
    String payload;
    if (!manifestFetcher(config.manifestUrl, payload)) {
        DLOG_E(LOG_OTA, "Manifest fetch failed");
        return info;
    }
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, payload.c_str());
    if (err) {
        DLOG_E(LOG_OTA, "Manifest JSON parse failed: %s", err.c_str());
        return info;
    }

    const char* ver = doc["version"] | "";
    const char* urlStr = doc["url"] | "";
    const char* sha = doc["sha256"] | "";
    const char* sig = doc["signature"] | "";
    info.version = ver;
    info.url = urlStr;
    info.sha256 = sha;
    info.signature = sig;
    info.valid = true;
    return info;
}

bool OTAComponent::installFromUrl(const String& url, const String& expectedSha256, bool allowDowngrade) {
    if (url.isEmpty()) {
        return false;
    }
    if (!downloader) {
        lastError = "No downloader set";
        transition(State::Error, lastError);
        return false;
    }
    // Refused before the download opens an update, as beginUpload() refuses before erasing.
    if (config.requireDownloadHash && expectedSha256.isEmpty()) {
        lastError = "Firmware hash required";
        transition(State::Error, lastError);
        return false;
    }

    // Prepare update
    transition(State::Downloading, "Downloading firmware");
    downloadedBytes = 0;
    progress = 0.0f;

    // BUG-21: see beginUpload() — same lifecycle event, other entry point.
    publishStatusEvent(DomoticsCore::OTAEvents::EVENT_START, [this, &url](JsonDocument& doc){
        doc["success"] = true;
        doc["source"] = "download";
        doc["url"] = url.c_str();
    }, false);

    HAL::SHA256 shaCtx;

    bool started = false;
    size_t announcedSize = 0;
    bool ok = downloader(url, announcedSize, [this, &started, &announcedSize, &shaCtx](const uint8_t* data, size_t len) -> bool {
        if (!started) {
            totalBytes = announcedSize;
            if (config.maxDownloadSize > 0 && totalBytes > config.maxDownloadSize) {
                lastError = "Firmware too large";
                return false;
            }
            size_t updateSize = totalBytes > 0 ? totalBytes : UPDATE_SIZE_UNKNOWN;
            if (!HAL::OTAUpdate::begin(updateSize)) {
                lastError = HAL::OTAUpdate::errorString();
                return false;
            }
            started = true;
        }
        if (len == 0) return true;
        // SEC-8: the check above trusts the size the server announced. This one
        // counts what it actually sent, which is the only figure a lying server
        // does not control.
        if (config.maxDownloadSize > 0 && downloadedBytes + len > config.maxDownloadSize) {
            lastError = "Firmware too large";
            DLOG_W(LOG_OTA, "Download aborted: %lu bytes would pass a %lu byte ceiling",
                   static_cast<unsigned long>(downloadedBytes + len),
                   static_cast<unsigned long>(config.maxDownloadSize));
            return false;
        }
        size_t written = HAL::OTAUpdate::write(const_cast<uint8_t*>(data), len);
        if (written != len) {
            lastError = HAL::OTAUpdate::errorString();
            return false;
        }
        shaCtx.update(data, written);
        downloadedBytes += written;
        if (totalBytes > 0) {
            progress = (downloadedBytes * 100.0f) / static_cast<float>(totalBytes);
        }
        // Yield to prevent watchdog timeout during long download operations
        HAL::Platform::yield();
        return true;
    });

    if (!ok) {
        HAL::OTAUpdate::abort();
        shaCtx.abort();
        transition(State::Error, lastError.isEmpty() ? String("Download failed") : lastError);
        publishStatusEvent(DomoticsCore::OTAEvents::EVENT_ERROR, [this](JsonDocument& doc){
            doc["success"] = false;
            doc["error"] = lastError.c_str();
            doc["source"] = "download";
        }, false);
        return false;
    }

    // TEST-8: the download half of what SEC-9 fixed for uploads, and it fails
    // both ways. totalBytes is the size the *server* announced, and
    // finalizeUpdateOperation() copies it over downloadedBytes to make the
    // completion event self-consistent — so a server announcing nothing, which is
    // what a chunked response does, completed reporting **zero bytes
    // downloaded** after transferring the whole image, and a server announcing
    // more than it sent completed reporting the larger number. SEC-8 exists
    // because that figure is one a lying server controls; it has no business
    // outliving the transfer that can be counted instead.
    //
    // Narrowed here, above the first event that reports it, for the same reason
    // finalizeUpload() narrows above its own: otherwise ota/end and ota/completed
    // give two different totals for one transfer.
    totalBytes = downloadedBytes;

    // BUG-21: transfer over, verdict not in yet. Same point in the sequence as
    // the one finalizeUpload() emits.
    publishStatusEvent(DomoticsCore::OTAEvents::EVENT_END, [this](JsonDocument& doc){
        doc["success"] = true;
        doc["source"] = "download";
        doc["bytes"] = downloadedBytes;
        doc["total"] = totalBytes;
    }, false);

    // Verify BEFORE committing. end(true) is the point of no return — it switches
    // the ESP32 boot partition and stages the ESP8266 eboot copy — and no Arduino
    // core lets the application undo it. An image that fails its hash must never
    // reach that call. See the abort()-before-end() contract in Update_HAL.h.
    // finish() releases the hash context on every platform, so the error paths
    // below need no shaCtx.abort() — only the download failure above, which
    // returns before the digest is ever finalized.
    uint8_t digest[32];
    shaCtx.finish(digest);

    if (!expectedSha256.isEmpty() && !verifySha256(digest, expectedSha256)) {
        lastError = "SHA256 mismatch";
        HAL::OTAUpdate::abort();
        transition(State::Error, lastError);
        publishStatusEvent(DomoticsCore::OTAEvents::EVENT_ERROR, [this](JsonDocument& doc){
            doc["success"] = false;
            doc["error"] = lastError.c_str();
            doc["source"] = "download";
        }, false);
        return false;
    }

    if (!HAL::OTAUpdate::end(true)) {
        lastError = HAL::OTAUpdate::errorString();
        HAL::OTAUpdate::abort();
        transition(State::Error, lastError);
        return false;
    }

    finalizeUpdateOperation("download", config.autoReboot);
    return true;
}

bool OTAComponent::verifySha256(const uint8_t* digest, const String& expectedHex) {
    String candidate = toHex(digest, 32);
    String expected = expectedHex;
    expected.toUpperCase();
    expected.replace(" ", "");
    expected.replace(":", "");
    return candidate.equalsIgnoreCase(expected);
}

bool OTAComponent::isNewerVersion(const String& candidate) const {
    // Parse semantic versions (major.minor.patch)
    struct Version {
        int major = 0;
        int minor = 0;
        int patch = 0;

        Version(const String& v) {
            int pos1 = v.indexOf('.');
            int pos2 = pos1 >= 0 ? v.indexOf('.', pos1 + 1) : -1;

            if (pos1 > 0) major = v.substring(0, pos1).toInt();
            if (pos2 > pos1 + 1) {
                minor = v.substring(pos1 + 1, pos2).toInt();
                patch = v.substring(pos2 + 1).toInt();
            } else if (pos1 > 0) {
                minor = v.substring(pos1 + 1).toInt();
            }
        }
    };

    Version current(metadata.version);
    Version cand(candidate);

    if (cand.major > current.major) return true;
    if (cand.major < current.major) return false;
    if (cand.minor > current.minor) return true;
    if (cand.minor < current.minor) return false;
    if (cand.patch > current.patch) return true;
    return false;
}

bool OTAComponent::finalizeUpdateOperation(const String& source, bool autoRebootPending) {
    progress = 100.0f;
    // Both live callers now narrow totalBytes to what they counted before they
    // get here — finalizeUpload() for SEC-9, installFromUrl() for TEST-8 — so
    // this is a no-op on every path a board takes, and kept only so the two
    // figures cannot disagree if a third caller ever appears. It used to be the
    // line that put the announced size back after the counted one was known.
    downloadedBytes = totalBytes;

    if (autoRebootPending) {
        transition(State::RebootPending, source + " complete");
        lastResult = "Update complete - rebooting in 2s";
        DLOG_I(LOG_OTA, "%s complete. Reboot scheduled in 2s.", source.c_str());
    } else {
        transition(State::Idle, source + " complete");
        lastResult = "Update applied. Reboot to finish.";
        DLOG_I(LOG_OTA, "%s complete. Manual reboot required.", source.c_str());
    }

    // DC-6: Emit on the canonical EVENT_COMPLETED topic
    auto buildPayload = [this, &source](JsonDocument& doc) {
        doc["success"] = true;
        doc["source"] = source.c_str();
        doc["autoReboot"] = config.autoReboot;
        doc["bytes"] = downloadedBytes;
        doc["total"] = totalBytes;
        doc["progress"] = 100.0f;
        doc["message"] = (config.autoReboot ? "Update complete, rebooting" : "Update complete, reboot manually");
    };
    publishStatusEvent(DomoticsCore::OTAEvents::EVENT_COMPLETED, buildPayload, true);

    return true;
}

void OTAComponent::publishStatusEvent(const char* topic, std::function<void(JsonDocument&)> fn, bool sticky) {
    JsonDocument doc;
    fn(doc);
    doc["state"] = stateToString(state);
    doc["progress"] = progress;
    doc["lastResult"] = lastResult.c_str();
    // Publish the bytes, not a String: the queue deep-copies them, NUL included.
    // A short status is serialized on the stack, a longer one into one reserved buffer.
    char buf[192];
    const size_t len = measureJson(doc);
    if (len < sizeof(buf)) {
        serializeJson(doc, buf, sizeof(buf));
        emit(topic, buf, len + 1, sticky);
        return;
    }
    String payload;
    payload.reserve(len + 1);
    serializeJson(doc, payload);
    emit(topic, payload.c_str(), payload.length() + 1, sticky);
}
