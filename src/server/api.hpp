// HTTP API for the solver.
//
//   GET /api/health                          -> {"status":"ok", ...}
//   GET /api/solve?facelets=<54 chars>       -> solution for a sticker string
//   GET /api/solve?scramble=R%20U%20F2       -> solution for the cube a scramble makes
//       optional: &target=20&timeout=1000
//   GET /api/scramble                        -> random-state scramble
//   GET /api/stats                           -> request counters, cache hits, timings
//   POST /api/visit                          -> counts a new visitor, returns the site counters
//   GET /api/counters                        -> {"visitors":<n>,"cubesSolved":<n>}
//
// A cube counts as solved when /api/solve returns a solution for a cube that
// was not already solved (cached answers count too: someone was helped).
//
// Rate limits per client address: /api/solve and /api/scramble share one limit
// (429 with Retry-After when exceeded). /api/visit has its own; past it, the
// visit is simply not counted. Behind a proxy (Render, Fly.io) the address is
// the first entry of X-Forwarded-For. A client can fake that header, so the
// limit stops accidents and casual abuse, not a determined attacker; the solve
// slots below still cap the total load.
//
// GET / and /index.html serve the page with __SITE_ORIGIN__ replaced by the
// site's address, so link previews get the absolute image URL they need.
//
// Solving is a pure function of its input, so /api/solve is a GET and its
// results are cached (LRU). A semaphore limits how many solves run at once,
// because each solve already uses up to 6 threads. A request that cannot get
// a solve slot within a few seconds gets 503 instead of waiting forever.
#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <optional>
#include <random>
#include <string>

#include "counters.hpp"
#include "lru_cache.hpp"
#include "rate_limiter.hpp"

namespace httplib {
class Server;
}

namespace cube::server {

struct Response {
    int status = 200;
    std::string body;  // JSON
};

struct SolveRequest {
    std::optional<std::string> facelets;
    std::optional<std::string> scramble;
    std::optional<std::string> target;  // raw query values, validated by the service
    std::optional<std::string> timeout;
};

struct RateLimits {
    double solvesPerMinute = 30;  // /api/solve and /api/scramble together
    double solveBurst = 15;
    double visitsPerHour = 10;
    double visitBurst = 5;
};

enum class Limit { Solve, Visit };

class Service {
public:
    explicit Service(size_t cacheCapacity = 2048, int maxConcurrentSolves = 4, int slotWaitMs = 5000,
                     std::unique_ptr<CounterStore> counterStore = nullptr, const RateLimits& limits = {});

    Response health() const;
    Response solve(const SolveRequest& request);
    Response scramble();
    Response stats() const;
    Response visit();
    Response counters() const;

    // 0 if this client may go ahead, else the seconds it should wait.
    int checkLimit(Limit which, const std::string& client);

private:
    // Counting semaphore (C++17 has none built in). Throws ServerBusy if no
    // slot frees up within slotWaitMs.
    class SolveSlot {
    public:
        explicit SolveSlot(Service& s);
        ~SolveSlot();
        SolveSlot(const SolveSlot&) = delete;
        SolveSlot& operator=(const SolveSlot&) = delete;

    private:
        Service& s_;
    };

    LruCache<std::string, std::string> cache_;
    SiteCounters counters_;
    RateLimiter solveLimiter_;
    RateLimiter visitLimiter_;
    const int maxConcurrent_;
    const int slotWaitMs_;
    int running_ = 0;
    std::mutex slotMutex_;
    std::condition_variable slotFree_;

    std::mutex rngMutex_;
    std::mt19937_64 rng_;

    const std::chrono::steady_clock::time_point started_;
    std::atomic<uint64_t> solveRequests_{0}, cacheHits_{0}, badRequests_{0}, scrambles_{0}, busyRejects_{0},
        rateLimited_{0};
    std::atomic<uint64_t> solveMicrosTotal_{0}, solvesComputed_{0};
};

// Server settings: exclusive port binding (so a second server on the same
// port fails loudly) and a larger worker pool.
void configureServer(httplib::Server& server);

// Registers the /api routes and serves the web demo from webDir (if not empty).
void registerRoutes(httplib::Server& server, Service& service, const std::string& webDir);

// Escapes a string for use inside JSON quotes.
std::string jsonEscape(const std::string& s);

// The client address used for rate limits: the first X-Forwarded-For entry if
// there is one, else the address of the connection.
std::string clientKey(const std::string& forwardedFor, const std::string& remoteAddr);

// "https://example.com" from the Host and X-Forwarded-Proto headers, or "" if
// the host looks wrong (it goes into HTML, so only safe characters pass).
std::string siteOrigin(const std::string& host, const std::string& forwardedProto);

// Replaces every __SITE_ORIGIN__ in page with origin.
std::string fillPageTemplate(std::string page, const std::string& origin);

}  // namespace cube::server
