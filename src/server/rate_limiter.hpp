// Per-client rate limit (token bucket). Each client may make `burst` requests
// at once, then `perSecond` more every second. Thread-safe.
#pragma once

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iterator>
#include <mutex>
#include <string>
#include <unordered_map>

namespace cube::server {

class RateLimiter {
public:
    using Clock = std::chrono::steady_clock;

    RateLimiter(double perSecond, double burst, size_t maxClients = 10000)
        : perSecond_(perSecond), burst_(burst), maxClients_(maxClients) {}

    // Returns 0 if the client may go ahead (and uses up one request), or the
    // number of seconds it should wait.
    int check(const std::string& client, Clock::time_point now = Clock::now()) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (buckets_.size() >= maxClients_ && !buckets_.count(client)) prune(now);
        auto [it, isNew] = buckets_.try_emplace(client, Bucket{burst_, now});
        Bucket& b = it->second;
        if (!isNew) {
            b.tokens = std::min(burst_, b.tokens + seconds(now - b.last) * perSecond_);
            b.last = now;
        }
        if (b.tokens >= 1.0) {
            b.tokens -= 1.0;
            return 0;
        }
        return std::max(1, static_cast<int>(std::ceil((1.0 - b.tokens) / perSecond_)));
    }

private:
    struct Bucket {
        double tokens;
        Clock::time_point last;
    };

    static double seconds(Clock::duration d) { return std::chrono::duration<double>(d).count(); }

    // Forgets clients whose bucket has refilled; they would start full anyway.
    // If that is not enough (a flood of new clients), forget everyone.
    void prune(Clock::time_point now) {
        for (auto it = buckets_.begin(); it != buckets_.end();) {
            const bool full = it->second.tokens + seconds(now - it->second.last) * perSecond_ >= burst_;
            it = full ? buckets_.erase(it) : std::next(it);
        }
        if (buckets_.size() >= maxClients_) buckets_.clear();
    }

    const double perSecond_;
    const double burst_;
    const size_t maxClients_;
    std::mutex mutex_;
    std::unordered_map<std::string, Bucket> buckets_;
};

}  // namespace cube::server
