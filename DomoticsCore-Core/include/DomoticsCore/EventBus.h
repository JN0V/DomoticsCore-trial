#pragma once

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

// Minimal core event enum kept here to avoid extra headers.
namespace DomoticsCore { namespace Utils { enum class EventType : uint8_t { Custom = 1 }; }}

namespace DomoticsCore {
namespace Utils {

/**
 * What one queued event costs the heap.
 *
 * A close estimate, not a ceiling: a full queue measures 2-3 % above what this
 * charges on the platforms a board has measured.
 */
struct QueueCostShape {
    // The allocator's figures are the platform's, not this file's (Constitution IX).
    static constexpr size_t kNode     = HAL::Platform::AllocatorShape::kDequeNodeBytes;
    static constexpr size_t kOverhead = HAL::Platform::AllocatorShape::kBlockOverhead;
    static constexpr size_t kSsoChars = HAL::Platform::AllocatorShape::kSsoChars;
    static constexpr size_t roundUp4(size_t n) { return ((n + 3) / 4) * 4; }
    static constexpr size_t block(size_t n) { return n == 0 ? 0 : kOverhead + roundUp4(n); }
    // Arduino String rounds its buffer to (len + 16) & ~0xf (WString.cpp:193);
    // below the SSO threshold it allocates nothing.
    static constexpr size_t topicBlock(size_t len) {
        return len <= kSsoChars ? 0 : kOverhead + ((len + 16) & ~(size_t)0xf);
    }
};

// Outside EventBus, and derived one layer out from the shape: a constexpr
// initialiser cannot call a member of a class that is not yet complete.
struct QueueCost : QueueCostShape {
    static constexpr size_t of(size_t payloadBytes, size_t topicLen) {
        return kNode + block(payloadBytes) + topicBlock(topicLen);
    }
    // A complete event: drop the topic term and the budget holds 30, not 32.
    static constexpr size_t kReference   = kNode + block(830) + topicBlock(12);
#ifdef DOMOTICS_EVENTBUS_QUEUE_BYTES
    static constexpr size_t kBudgetBytes = (size_t)(DOMOTICS_EVENTBUS_QUEUE_BYTES);
#else
    static constexpr size_t kBudgetBytes = 32 * kReference;
#endif
    static constexpr size_t kMaxEntries  = 256;   // guard rail against model drift
};
static_assert(QueueCost::kBudgetBytes <= UINT16_MAX, "queuedBytes_ is a uint16_t");
// Below one reference event, every MQTT publish is refused in silence.
static_assert(QueueCost::kBudgetBytes >= QueueCost::kReference,
              "DOMOTICS_EVENTBUS_QUEUE_BYTES is below one reference event");

// Out of line: a DLOG buffer is reserved in its function's prologue whether the
// branch runs or not, and enqueue() is on every publish() path.
inline void __attribute__((noinline)) logRefusedEvent(const char* topic, size_t cost) {
    DLOG_W(LOG_CORE, "EventBus refused '%s': %u B over a %u B budget",
           topic, (unsigned)cost, (unsigned)QueueCost::kBudgetBytes);
}

/**
 * @brief Queued publish/subscribe, drained by the application loop.
 *
 * publish() is safe from any task; subscribing is the loop's alone.
 */
class EventBus {
public:
    using Handler = std::function<void(const void* /*payload*/)>;

    struct Subscription {
        uint32_t id;
        void* owner; // component or object owning the subscription
        Handler handler;
    };

    struct QueuedEvent {
        // Either a typed event or a topic-based event. If topic is non-empty, it takes precedence.
        EventType type{EventType::Custom};
        String topic{};
        // Copy of payload bytes; we keep a small vector to store arbitrary payloads
        std::vector<uint8_t> data;
        bool countedPending = false;  // counted in pendingByTopic; released only if so
    };


    EventBus() : nextId(1) {}

    /** @brief True when an event of this class can never fit, whatever is queued. */
    static bool exceedsBudget(size_t payloadBytes, size_t topicLen) {
        return QueueCost::of(payloadBytes, topicLen) > QueueCost::kBudgetBytes;
    }

    // Subscribe to an event type. Returns a subscription id.
    // WARNING: Must not be called during poll() dispatch.
    uint32_t subscribe(EventType type, Handler handler, void* owner = nullptr) {
        assert(!dispatching_ && "Cannot subscribe during EventBus dispatch");
        if (!handler) return 0;
        HAL::Platform::LockGuard guard(lock_);
        uint32_t id = allocateId();
        subscriptions[type].push_back({id, owner, std::move(handler)});
        return id;
    }

    // Subscribe to a topic string (e.g., "wifi.connected"). Returns a subscription id.
    // If replayLast is true and a sticky event exists for this topic, the handler is invoked immediately once.
    // WARNING: Must not be called during poll() dispatch.
    uint32_t subscribe(const String& topic, Handler handler, void* owner = nullptr, bool replayLast = false) {
        assert(!dispatching_ && "Cannot subscribe during EventBus dispatch");
        if (!handler || topic.length() == 0) return 0;
        HAL::Platform::LockGuard guard(lock_);
        uint32_t id = allocateId();
        if (isWildcard(topic)) {
            wildcardTopicSubscriptions[topic].push_back({id, owner, std::move(handler)});
        } else {
            topicSubscriptions[topic].push_back({id, owner, std::move(handler)});
            if (replayLast) {
                auto it = lastByTopic.find(topic);
                if (it != lastByTopic.end()) {
                    // Avoid duplicate if there are pending queued events for this topic
                    int pending = 0;
                    auto itp = pendingByTopic.find(topic);
                    if (itp != pendingByTopic.end()) pending = itp->second;
                    if (pending <= 0) {
                        const void* payloadPtr = it->second.empty() ? nullptr : it->second.data();
                        // Call immediately
                        auto& vec = topicSubscriptions[topic];
                        for (const auto& sub : vec) {
                            if (sub.id == id && sub.handler) { sub.handler(payloadPtr); break; }
                        }
                    }
                }
            }
        }
        return id;
    }

    // Unsubscribe by id
    // WARNING: Must not be called during poll() dispatch.
    void unsubscribe(uint32_t id) {
        assert(!dispatching_ && "Cannot unsubscribe during EventBus dispatch");
        HAL::Platform::LockGuard guard(lock_);
        auto pred = [id](const Subscription& s){ return s.id == id; };
        if (pruneMap(subscriptions, pred)) return;
        if (pruneMap(topicSubscriptions, pred)) return;
        pruneMap(wildcardTopicSubscriptions, pred);
    }

    // Unsubscribe all belonging to a given owner pointer
    // WARNING: Must not be called during poll() dispatch.
    void unsubscribeOwner(void* owner) {
        assert(!dispatching_ && "Cannot unsubscribeOwner during EventBus dispatch");
        if (!owner) return;
        HAL::Platform::LockGuard guard(lock_);
        auto pred = [owner](const Subscription& s){ return s.owner == owner; };
        pruneMap(subscriptions, pred);
        pruneMap(topicSubscriptions, pred);
        pruneMap(wildcardTopicSubscriptions, pred);
    }

    // Publish an event with an arbitrary payload type (copy).
    template<typename PayloadT>
    void publish(EventType type, const PayloadT& payload) {
        static_assert(std::is_trivially_copyable<PayloadT>::value,
                      "EventBus payload must be trivially copyable");
        if (refuseOversized(nullptr, 0, sizeof(PayloadT))) return;
        QueuedEvent qe;
        qe.type = type;
        const uint8_t* p = reinterpret_cast<const uint8_t*>(&payload);
        qe.data.assign(p, p + sizeof(PayloadT));
        enqueue(std::move(qe));
    }

    // Publish without payload (or with external storage) — sends nullptr to handlers
    void publish(EventType type) {
        QueuedEvent qe;
        qe.type = type;
        enqueue(std::move(qe));
    }

    // Topic-based publish (with payload copy)
    template<typename PayloadT>
    void publish(const String& topic, const PayloadT& payload) {
        assertTriviallyCopyable<PayloadT>();
        publishBytes(topic.c_str(), topic.length(), &payload, sizeof(PayloadT));
    }
    // A const char* topic is copied once, into the queued event.
    template<typename PayloadT>
    void publish(const char* topic, const PayloadT& payload) {
        assertTriviallyCopyable<PayloadT>();
        publishBytes(topic, topic ? strlen(topic) : 0, &payload, sizeof(PayloadT));
    }

    // Topic-based publish with a variable-length payload copy.
    // The caller retains ownership; the queued event owns its byte copy.
    void publish(const String& topic, const void* payload, size_t payloadSize) {
        publishBytes(topic.c_str(), topic.length(), payload, payloadSize);
    }
    void publish(const char* topic, const void* payload, size_t payloadSize) {
        publishBytes(topic, topic ? strlen(topic) : 0, payload, payloadSize);
    }

    // Topic-based publish without payload
    void publish(const String& topic) {
        if (topic.length() == 0) return;
        QueuedEvent qe;
        qe.topic = topic;
        enqueue(std::move(qe));
    }

    // Sticky publish: store last payload for the topic and publish as usual
    template<typename PayloadT>
    void publishSticky(const String& topic, const PayloadT& payload) {
        if (topic.length() == 0) return;
        if (refuseOversized(topic.c_str(), topic.length(), sizeof(PayloadT))) return;
        const uint8_t* p = reinterpret_cast<const uint8_t*>(&payload);
        {
            HAL::Platform::LockGuard guard(lock_);
            lastByTopic[topic] = std::vector<uint8_t>(p, p + sizeof(PayloadT));
        }
        publish(topic, payload);
    }
    void publishSticky(const String& topic, const void* payload, size_t payloadSize) {
        if (topic.length() == 0 || payload == nullptr || payloadSize == 0) return;
        if (refuseOversized(topic.c_str(), topic.length(), payloadSize)) return;
        const uint8_t* p = static_cast<const uint8_t*>(payload);
        {
            HAL::Platform::LockGuard guard(lock_);
            lastByTopic[topic] = std::vector<uint8_t>(p, p + payloadSize);
        }
        publish(topic, payload, payloadSize);
    }
    void publishSticky(const String& topic) {
        if (topic.length() == 0) return;
        {
            HAL::Platform::LockGuard guard(lock_);
            lastByTopic[topic].clear();
            lastByTopic[topic].shrink_to_fit();
        }
        publish(topic);
    }

    // Dispatch queued events; call from main loop. Handlers must NOT call
    // subscribe/unsubscribe during dispatch; dispatching_ guards that in debug
    // builds. The lock is released before a handler runs: a publisher on
    // another task must not wait behind one.
    void poll(size_t maxPerPoll = 8) {
        size_t processed = 0;
        dispatching_ = true;
        while (processed < maxPerPoll) {
            QueuedEvent qe;
            {
                HAL::Platform::LockGuard guard(lock_);
                if (queue.empty()) break;
                qe = std::move(queue.front());
                queue.pop();
                queuedBytes_ -= static_cast<uint16_t>(QueueCost::of(qe.data.size(), qe.topic.length()));
            }
            processed++;

            const void* payloadPtr = nullptr;
            if (!qe.data.empty()) payloadPtr = qe.data.data();

            if (qe.topic.length() > 0) {
                // Exact topic subscribers
                auto itT = topicSubscriptions.find(qe.topic);
                if (itT != topicSubscriptions.end()) {
                    const auto& handlers = itT->second;
                    for (const auto& sub : handlers) {
                        if (sub.handler) sub.handler(payloadPtr);
                    }
                }
                // Wildcard subscribers (prefix match e.g., "sensor/*")
                for (const auto& kv : wildcardTopicSubscriptions) {
                    if (matchesWildcard(qe.topic, kv.first)) {
                        const auto& handlers = kv.second;
                        for (const auto& sub : handlers) {
                            if (sub.handler) sub.handler(payloadPtr);
                        }
                    }
                }
                if (qe.countedPending) {
                    HAL::Platform::LockGuard guard(lock_);
                    releasePending(qe.topic);
                }
            } else {
                auto it = subscriptions.find(qe.type);
                if (it != subscriptions.end()) {
                    const auto& handlers = it->second;
                    for (const auto& sub : handlers) {
                        if (sub.handler) sub.handler(payloadPtr);
                    }
                }
            }
        }
        dispatching_ = false;
    }

    /** @brief Events dropped on queue overflow since construction or reset(). */
    uint32_t getDroppedCount() const { return droppedEvents_; }
    /** @brief Bytes the queue currently holds, as QueueCost models them. */
    uint16_t getQueuedBytes() const { return queuedBytes_; }
    /** @brief Highest occupancy reached since construction or reset(), in percent of the budget. */
    uint8_t getQueueHighWaterPct() const { return highWaterPct_; }

    // Contract: reset() must leave the EventBus in the exact same state
    // as a freshly constructed instance. If you add new members, update this method.
    // Note: dispatching_ is not reset because the assert guarantees it is already false.
    void reset() {
        assert(!dispatching_ && "Cannot reset during EventBus dispatch");
        HAL::Platform::LockGuard guard(lock_);
        queue = std::queue<QueuedEvent>();
        subscriptions.clear();
        topicSubscriptions.clear();
        wildcardTopicSubscriptions.clear();
        nextId = 1;
        lastByTopic.clear();
        pendingByTopic.clear();
        droppedEvents_ = 0;
        queuedBytes_ = 0;
        highWaterPct_ = 0;
    }

private:
    // Counts and names the refusal, so a caller that never reaches enqueue()
    // is still visible in getDroppedCount() and in the log.
    bool refuseOversized(const char* topic, size_t topicLen, size_t payloadBytes) {
        if (!exceedsBudget(payloadBytes, topicLen)) return false;
        {
            HAL::Platform::LockGuard guard(lock_);
            ++droppedEvents_;
        }
        // Logged outside the lock: it writes to a port.
        logRefusedEvent(topicLen ? topic : "<typed event>",
                        QueueCost::of(payloadBytes, topicLen));
        return true;
    }

    template<typename PayloadT>
    static void assertTriviallyCopyable() {
        // The payload is byte-copied, so an owning type would be dispatched
        // after the original is gone. Use the (topic, void*, size) overload.
        static_assert(std::is_trivially_copyable<PayloadT>::value,
                      "EventBus payload must be trivially copyable. For a String or "
                      "any other owning type, publish the bytes instead: "
                      "emit(topic, s.c_str(), s.length() + 1, sticky).");
    }

    void publishBytes(const char* topic, size_t topicLen, const void* payload, size_t payloadSize) {
        if (topicLen == 0 || payload == nullptr || payloadSize == 0) return;
        if (refuseOversized(topic, topicLen, payloadSize)) return;
        QueuedEvent qe;
        qe.topic = topic;
        const uint8_t* p = static_cast<const uint8_t*>(payload);
        qe.data.assign(p, p + payloadSize);
        enqueue(std::move(qe));
    }

    void enqueue(QueuedEvent&& qe) {
        HAL::Platform::LockGuard guard(lock_);
        const size_t cost = QueueCost::of(qe.data.size(), qe.topic.length());
        // An event larger than the whole budget can never be queued: evicting
        // for it would empty the queue and still fail. Name it instead.
        if (cost > QueueCost::kBudgetBytes) {
            ++droppedEvents_;
            logRefusedEvent(qe.topic.length() ? qe.topic.c_str() : "<typed event>", cost);
            return;
        }
        while (!queue.empty() && (queuedBytes_ + cost > QueueCost::kBudgetBytes ||
                                  queue.size() >= QueueCost::kMaxEntries)) {
            // Release the pending count too, or sticky replay stays blocked
            // for this topic for the life of the process.
            const QueuedEvent& front = queue.front();
            queuedBytes_ -= static_cast<uint16_t>(QueueCost::of(front.data.size(), front.topic.length()));
            if (front.countedPending) releasePending(front.topic);
            queue.pop();
            ++droppedEvents_;
        }
        queuedBytes_ += static_cast<uint16_t>(cost);
        const uint8_t pct = static_cast<uint8_t>(static_cast<size_t>(queuedBytes_) * 100u / QueueCost::kBudgetBytes);
        if (pct > highWaterPct_) highWaterPct_ = pct;
        queue.push(std::move(qe));
        // Pending counts only matter where a sticky value could be replayed twice.
        QueuedEvent& back = queue.back();
        if (back.topic.length() > 0 && lastByTopic.count(back.topic)) {
            pendingByTopic[back.topic] = pendingByTopic[back.topic] + 1;
            back.countedPending = true;
        }
    }

    void releasePending(const String& topic) {
        if (topic.length() == 0) return;
        auto itp = pendingByTopic.find(topic);
        if (itp == pendingByTopic.end()) return;
        if (itp->second > 1) itp->second -= 1;
        else pendingByTopic.erase(itp);
    }

    static bool isWildcard(const String& topic) {
        // Support prefix wildcard: e.g., "sensor/*"
        int idx = HAL::indexOf(topic, '*');
        return (idx >= 0);
    }

    static bool matchesWildcard(const String& concrete, const String& pattern) {
        int star = HAL::indexOf(pattern, '*');
        if (star < 0) return false; // not a wildcard pattern
        // Allow only prefix+"*" patterns for simplicity
        String prefix = HAL::substring(pattern, 0, star);
        if (star != (int)pattern.length() - 1) {
            // If pattern has chars after '*', require full match (very simple contains)
            String suffix = HAL::substring(pattern, star + 1);
            return HAL::startsWith(concrete, prefix) && HAL::endsWith(concrete, suffix);
        }
        return HAL::startsWith(concrete, prefix);
    }

    template<typename Map, typename Pred>
    static bool pruneMap(Map& m, Pred pred) {
        bool found = false;
        for (auto it = m.begin(); it != m.end(); ) {
            auto& vec = it->second;
            size_t oldSize = vec.size();
            vec.erase(std::remove_if(vec.begin(), vec.end(), pred), vec.end());
            if (vec.size() != oldSize) {
                vec.shrink_to_fit();
                found = true;
            }
            if (vec.empty()) it = m.erase(it);
            else ++it;
        }
        return found;
    }

    static bool idInMap(const std::map<EventType, std::vector<Subscription>>& m, uint32_t id) {
        for (const auto& kv : m) for (const auto& s : kv.second) if (s.id == id) return true;
        return false;
    }
    static bool idInMap(const std::map<String, std::vector<Subscription>>& m, uint32_t id) {
        for (const auto& kv : m) for (const auto& s : kv.second) if (s.id == id) return true;
        return false;
    }
    // Next subscription id: never 0, never one still in use (the counter may wrap).
    // Caller holds lock_.
    uint32_t allocateId() {
        for (;;) {
            uint32_t id = nextId++;
            if (id == 0) continue;
            if (!idInMap(subscriptions, id) && !idInMap(topicSubscriptions, id) &&
                !idInMap(wildcardTopicSubscriptions, id)) return id;
        }
    }

    // Internal state — if you add a new member, update reset() to clear it.
    std::map<EventType, std::vector<Subscription>> subscriptions;
    std::map<String, std::vector<Subscription>> topicSubscriptions;
    std::map<String, std::vector<Subscription>> wildcardTopicSubscriptions;
    std::queue<QueuedEvent> queue;
    uint32_t nextId;
    // Sticky last payload per topic
    std::map<String, std::vector<uint8_t>> lastByTopic;
    // Pending counts per topic to prevent duplicate sticky replay
    std::map<String, int> pendingByTopic;
    uint32_t droppedEvents_ = 0;   // events popped on overflow
    // Before dispatching_: after it the uint16_t lands on an odd offset and the
    // object grows four bytes.
    uint16_t queuedBytes_ = 0;
    uint8_t highWaterPct_ = 0;
    bool dispatching_ = false;
    // Recursive: a handler that publishes re-enters through enqueue().
    mutable HAL::Platform::RecursiveLock lock_;
};

} // namespace Utils
} // namespace DomoticsCore
