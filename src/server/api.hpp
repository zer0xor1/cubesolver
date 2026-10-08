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
// Solving is a pure function of its input, so /api/solve is a GET and its
// results are cached (LRU). A semaphore limits how many solves run at once,
// because each solve already uses up to 6 threads. A request that cannot get
// a solve slot within a few seconds gets 503 instead of waiting forever.
#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <random>
#include <string>

#include "counters.hpp"
#include "lru_cache.hpp"

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

class Service {
public:
    explicit Service(size_t cacheCapacity = 2048, int maxConcurrentSolves = 4, int slotWaitMs = 5000,
                     const std::string& countersFile = "");

    Response health() const;
    Response solve(const SolveRequest& request);
    Response scramble();
    Response stats() const;
    Response visit();
    Response counters() const;

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
    const int maxConcurrent_;
    const int slotWaitMs_;
    int running_ = 0;
    std::mutex slotMutex_;
    std::condition_variable slotFree_;

    std::mutex rngMutex_;
    std::mt19937_64 rng_;

    const std::chrono::steady_clock::time_point started_;
    std::atomic<uint64_t> solveRequests_{0}, cacheHits_{0}, badRequests_{0}, scrambles_{0}, busyRejects_{0};
    std::atomic<uint64_t> solveMicrosTotal_{0}, solvesComputed_{0};
};

// Server settings: exclusive port binding (so a second server on the same
// port fails loudly) and a larger worker pool.
void configureServer(httplib::Server& server);

// Registers the /api routes and serves the web demo from webDir (if not empty).
void registerRoutes(httplib::Server& server, Service& service, const std::string& webDir);

// Escapes a string for use inside JSON quotes.
std::string jsonEscape(const std::string& s);

}  // namespace cube::server
