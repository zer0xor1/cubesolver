#include "api.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <stdexcept>
#include <thread>

#include "cubesolver/cubie.hpp"
#include "cubesolver/facelet.hpp"
#include "cubesolver/solver.hpp"
#include "cubesolver/tables.hpp"
#include "httplib/httplib.h"

namespace cube::server {

namespace {

constexpr size_t kMaxScrambleChars = 1000;
constexpr int kMaxTimeoutMs = 5000;

// Thrown when no solve slot frees up in time; becomes HTTP 503.
struct ServerBusy {};

std::string errorJson(const std::string& message) {
    return "{\"error\":\"" + jsonEscape(message) + "\"}";
}

std::string number(double v, int decimals) {
    char buf[64];
    std::snprintf(buf, sizeof buf, "%.*f", decimals, v);
    return buf;
}

// Reads an optional integer query parameter, or throws a readable error.
int intParam(const std::optional<std::string>& raw, const char* name, int fallback, int lo, int hi) {
    if (!raw || raw->empty()) return fallback;
    char* end = nullptr;
    const long v = std::strtol(raw->c_str(), &end, 10);
    if (end == raw->c_str() || *end != '\0' || v < lo || v > hi) {
        throw std::invalid_argument(std::string(name) + " must be a whole number from " + std::to_string(lo) + " to " +
                                    std::to_string(hi));
    }
    return static_cast<int>(v);
}

std::string movesJson(const MoveSeq& moves) {
    std::string out = "[";
    for (size_t i = 0; i < moves.size(); ++i) {
        if (i) out += ',';
        out += '"' + moveName(moves[i]) + '"';
    }
    return out + "]";
}

}  // namespace

std::string jsonEscape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (const char ch : s) {
        switch (ch) {
            case '"':
                out += "\\\"";
                break;
            case '\\':
                out += "\\\\";
                break;
            case '\n':
                out += "\\n";
                break;
            case '\r':
                out += "\\r";
                break;
            case '\t':
                out += "\\t";
                break;
            default:
                if (static_cast<unsigned char>(ch) < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof buf, "\\u%04x", static_cast<unsigned>(static_cast<unsigned char>(ch)));
                    out += buf;
                } else {
                    out += ch;
                }
        }
    }
    return out;
}

Service::SolveSlot::SolveSlot(Service& s) : s_(s) {
    std::unique_lock<std::mutex> lock(s_.slotMutex_);
    const bool free = s_.slotFree_.wait_for(lock, std::chrono::milliseconds(s_.slotWaitMs_),
                                            [this] { return s_.running_ < s_.maxConcurrent_; });
    if (!free) throw ServerBusy{};
    ++s_.running_;
}

Service::SolveSlot::~SolveSlot() {
    {
        std::lock_guard<std::mutex> lock(s_.slotMutex_);
        --s_.running_;
    }
    s_.slotFree_.notify_one();
}

Service::Service(size_t cacheCapacity, int maxConcurrentSolves, int slotWaitMs, const std::string& countersFile)
    : cache_(cacheCapacity),
      counters_(countersFile),
      maxConcurrent_(std::max(1, maxConcurrentSolves)),
      slotWaitMs_(std::max(0, slotWaitMs)),
      rng_(std::random_device{}()),
      started_(std::chrono::steady_clock::now()) {}

Response Service::health() const {
    return {200, "{\"status\":\"ok\",\"version\":\"1.0.0\",\"tablesBuildMs\":" + number(tables().buildMilliseconds, 0) +
                     "}"};
}

Response Service::solve(const SolveRequest& request) {
    ++solveRequests_;
    try {
        if (request.facelets && request.scramble) {
            throw std::invalid_argument("give either facelets or scramble, not both");
        }
        CubieCube c;
        if (request.facelets) {
            c = fromFacelets(*request.facelets);
        } else if (request.scramble) {
            if (request.scramble->size() > kMaxScrambleChars) throw std::invalid_argument("scramble is too long");
            c.applyMoves(parseMoves(*request.scramble));
        } else {
            throw std::invalid_argument("missing parameter: facelets or scramble");
        }
        const int target = intParam(request.target, "target", 20, 0, 30);
        const int timeout = intParam(request.timeout, "timeout", 1000, 0, kMaxTimeoutMs);

        const std::string facelets = toFacelets(c);
        const std::string key = facelets + '|' + std::to_string(target) + '|' + std::to_string(timeout);
        const bool alreadySolved = facelets == kSolvedFacelets;
        if (auto hit = cache_.get(key)) {
            ++cacheHits_;
            if (!alreadySolved) counters_.addSolve();
            return {200, "{" + *hit + ",\"cached\":true}"};
        }

        SolveOptions options;
        options.targetLength = target;
        options.timeoutMs = timeout;
        SolveResult r;
        {
            SolveSlot slot(*this);
            r = cube::solve(c, options);
        }
        solveMicrosTotal_ += static_cast<uint64_t>(r.milliseconds * 1000.0);
        ++solvesComputed_;

        const std::string fields =
            "\"solution\":\"" + formatMoves(r.moves) + "\"" + ",\"length\":" + std::to_string(r.moves.size()) +
            ",\"moves\":" + movesJson(r.moves) + ",\"facelets\":\"" + facelets + "\"" +
            ",\"timeMs\":" + number(r.milliseconds, 2) + ",\"nodes\":" + std::to_string(r.nodes) +
            ",\"searches\":" + std::to_string(r.searches) +
            ",\"targetReached\":" + (r.targetReached ? "true" : "false");
        cache_.put(key, fields);
        if (!alreadySolved) counters_.addSolve();
        return {200, "{" + fields + ",\"cached\":false}"};
    } catch (const std::invalid_argument& e) {  // includes InvalidCubeError
        ++badRequests_;
        return {400, errorJson(e.what())};
    } catch (const ServerBusy&) {
        ++busyRejects_;
        return {503, errorJson("the server is busy, try again in a moment")};
    }
}

Response Service::scramble() {
    ++scrambles_;
    CubieCube c;
    {
        std::lock_guard<std::mutex> lock(rngMutex_);
        c = randomCube(rng_);
    }
    SolveOptions options;
    options.timeoutMs = 500;
    SolveResult r;
    try {
        SolveSlot slot(*this);
        r = cube::solve(c, options);
    } catch (const ServerBusy&) {
        ++busyRejects_;
        return {503, errorJson("the server is busy, try again in a moment")};
    }
    const MoveSeq moves = invertMoves(r.moves);
    return {200, "{\"scramble\":\"" + formatMoves(moves) + "\",\"length\":" + std::to_string(moves.size()) +
                     ",\"moves\":" + movesJson(moves) + ",\"facelets\":\"" + toFacelets(c) + "\"}"};
}

Response Service::stats() const {
    const double uptime = std::chrono::duration<double>(std::chrono::steady_clock::now() - started_).count();
    const uint64_t computed = solvesComputed_.load();
    const double avgMs =
        computed ? static_cast<double>(solveMicrosTotal_.load()) / 1000.0 / static_cast<double>(computed) : 0.0;
    return {200, "{\"uptimeSeconds\":" + number(uptime, 1) + ",\"solveRequests\":" +
                     std::to_string(solveRequests_.load()) + ",\"solvesComputed\":" + std::to_string(computed) +
                     ",\"cacheHits\":" + std::to_string(cacheHits_.load()) + ",\"cacheEntries\":" +
                     std::to_string(cache_.size()) + ",\"badRequests\":" + std::to_string(badRequests_.load()) +
                     ",\"scrambles\":" + std::to_string(scrambles_.load()) + ",\"averageSolveMs\":" + number(avgMs, 2) +
                     ",\"maxConcurrentSolves\":" + std::to_string(maxConcurrent_) + "}"};
}

Response Service::visit() {
    counters_.addVisitor();
    return counters();
}

Response Service::counters() const {
    return {200, counters_.json()};
}

void configureServer(httplib::Server& server) {
    // cpp-httplib sets SO_REUSEPORT by default, which lets a second server
    // silently share the port. Use exclusive binding instead.
    server.set_socket_options([](socket_t sock) {  // socket_t comes from httplib.h
#ifdef _WIN32
        httplib::set_socket_opt(sock, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, 1);
#else
        httplib::set_socket_opt(sock, SOL_SOCKET, SO_REUSEADDR, 1);  // allow quick restarts only
#endif
    });
    // Solves can hold a worker for up to 5 s, so keep extra workers for
    // cheap requests like /api/health and static files.
    server.new_task_queue = [] { return new httplib::ThreadPool(16, 64); };
}

void registerRoutes(httplib::Server& server, Service& service, const std::string& webDir) {
    auto send = [](httplib::Response& res, const Response& r) {
        res.status = r.status;
        res.set_header("Cache-Control", "no-store");
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_content(r.body, "application/json");
    };
    auto param = [](const httplib::Request& req, const char* name) -> std::optional<std::string> {
        if (!req.has_param(name)) return std::nullopt;
        return req.get_param_value(name);
    };

    server.Get("/api/health",
               [&service, send](const httplib::Request&, httplib::Response& res) { send(res, service.health()); });
    server.Get("/api/solve", [&service, send, param](const httplib::Request& req, httplib::Response& res) {
        SolveRequest r;
        r.facelets = param(req, "facelets");
        r.scramble = param(req, "scramble");
        r.target = param(req, "target");
        r.timeout = param(req, "timeout");
        send(res, service.solve(r));
    });
    server.Get("/api/scramble",
               [&service, send](const httplib::Request&, httplib::Response& res) { send(res, service.scramble()); });
    server.Get("/api/stats",
               [&service, send](const httplib::Request&, httplib::Response& res) { send(res, service.stats()); });
    server.Post("/api/visit",
                [&service, send](const httplib::Request&, httplib::Response& res) { send(res, service.visit()); });
    server.Get("/api/counters",
               [&service, send](const httplib::Request&, httplib::Response& res) { send(res, service.counters()); });

    // Unknown /api paths get a JSON error instead of an HTML page.
    server.set_error_handler([send](const httplib::Request& req, httplib::Response& res) {
        if (req.path.rfind("/api/", 0) == 0 && res.status == 404) {
            send(res, {404, errorJson("no such endpoint: " + req.path)});
        }
    });
    server.set_exception_handler([send](const httplib::Request&, httplib::Response& res, std::exception_ptr ep) {
        std::string message = "internal error";
        try {
            if (ep) std::rethrow_exception(ep);
        } catch (const std::exception& e) {
            message += std::string(": ") + e.what();
        } catch (...) {
        }
        send(res, {500, errorJson(message)});
    });

    if (!webDir.empty()) server.set_mount_point("/", webDir);
}

}  // namespace cube::server
