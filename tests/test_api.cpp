#include <chrono>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <regex>
#include <string>
#include <thread>

#include "../src/server/api.hpp"
#include "cubesolver/facelet.hpp"
#include "doctest/doctest.h"
#include "httplib/httplib.h"

using namespace cube::server;

TEST_CASE("LRU cache keeps the most recent entries") {
    LruCache<int, int> cache(2);
    cache.put(1, 10);
    cache.put(2, 20);
    CHECK(cache.get(1).value() == 10);  // 1 is now most recent
    cache.put(3, 30);                   // evicts 2, the least recent
    CHECK_FALSE(cache.get(2).has_value());
    CHECK(cache.get(1).value() == 10);
    CHECK(cache.get(3).value() == 30);
    CHECK(cache.size() == 2);
}

TEST_CASE("JSON escaping") {
    CHECK(jsonEscape("a\"b\\c\n") == "a\\\"b\\\\c\\n");
}

TEST_CASE("solve endpoint logic") {
    Service service;

    SolveRequest byScramble;
    byScramble.scramble = "R U R' U'";
    Response r = service.solve(byScramble);
    CHECK(r.status == 200);
    CHECK(r.body.find("\"solution\":\"U R U' R'\"") != std::string::npos);
    CHECK(r.body.find("\"cached\":false") != std::string::npos);

    // Same cube again: served from the cache.
    r = service.solve(byScramble);
    CHECK(r.body.find("\"cached\":true") != std::string::npos);

    SolveRequest byFacelets;
    byFacelets.facelets = cube::kSolvedFacelets;
    r = service.solve(byFacelets);
    CHECK(r.status == 200);
    CHECK(r.body.find("\"length\":0") != std::string::npos);
}

TEST_CASE("site counters count visitors and solves, and survive a restart") {
    const std::filesystem::path file = std::filesystem::temp_directory_path() / "cubesolver_test_counters.txt";
    std::filesystem::remove(file);
    {
        SiteCounters counters(std::make_unique<FileStore>(file.string()));
        CHECK(counters.json() == "{\"visitors\":0,\"cubesSolved\":0}");
        counters.addVisitor();
        counters.addVisitor();
        counters.addSolve();
        CHECK(counters.json() == "{\"visitors\":2,\"cubesSolved\":1}");  // before any save
    }  // the destructor saves
    SiteCounters reloaded(std::make_unique<FileStore>(file.string()));
    CHECK(reloaded.counts().visitors == 2);
    CHECK(reloaded.counts().cubesSolved == 1);
    std::filesystem::remove(file);
}

// A stand-in for Upstash's /pipeline endpoint: understands two INCRBY commands.
struct FakeUpstash {
    httplib::Server server;
    std::thread thread;
    int port = 0;
    std::mutex mutex;
    std::map<std::string, long long> values;
    bool fail = false;

    FakeUpstash() {
        server.Post("/pipeline", [this](const httplib::Request& req, httplib::Response& res) {
            std::lock_guard<std::mutex> lock(mutex);
            if (fail || req.get_header_value("Authorization") != "Bearer secret") {
                res.status = 401;
                res.set_content("{\"error\":\"Unauthorized\"}", "application/json");
                return;
            }
            const std::regex command(R"re(\["INCRBY","([^"]+)","(\d+)"\])re");
            std::string reply = "[";
            for (std::sregex_iterator it(req.body.begin(), req.body.end(), command), endIt; it != endIt; ++it) {
                const long long v = values[(*it)[1]] += std::stoll((*it)[2]);
                reply += std::string(reply.size() > 1 ? "," : "") + "{\"result\":" + std::to_string(v) + "}";
            }
            res.set_content(reply + "]", "application/json");
        });
        port = server.bind_to_any_port("127.0.0.1");
        thread = std::thread([this] { server.listen_after_bind(); });
        server.wait_until_ready();
    }
    ~FakeUpstash() {
        server.stop();
        thread.join();
    }
    std::string url() const { return "http://127.0.0.1:" + std::to_string(port); }
};

TEST_CASE("counts saved to Upstash survive a restart") {
    FakeUpstash upstash;
    {
        SiteCounters counters(std::make_unique<UpstashStore>(upstash.url(), "secret"));
        counters.addVisitor();
        counters.addSolve();
        counters.addSolve();
        CHECK(counters.flush());
        counters.addVisitor();
    }  // the destructor sends the last visitor
    CHECK(upstash.values["cubesolver:visitors"] == 2);
    CHECK(upstash.values["cubesolver:cubesSolved"] == 2);

    SiteCounters reloaded(std::make_unique<UpstashStore>(upstash.url() + "/", "secret"));
    CHECK(reloaded.json() == "{\"visitors\":2,\"cubesSolved\":2}");
}

TEST_CASE("counts are kept when Upstash fails, and sent once it works again") {
    FakeUpstash upstash;
    SiteCounters counters(std::make_unique<UpstashStore>(upstash.url(), "secret"));
    {
        std::lock_guard<std::mutex> lock(upstash.mutex);
        upstash.fail = true;
    }
    counters.addVisitor();
    CHECK_FALSE(counters.flush());
    CHECK(counters.counts().visitors == 1);  // still shown
    {
        std::lock_guard<std::mutex> lock(upstash.mutex);
        upstash.fail = false;
    }
    CHECK(counters.flush());
    std::lock_guard<std::mutex> lock(upstash.mutex);
    CHECK(upstash.values["cubesolver:visitors"] == 1);
}

TEST_CASE("Upstash with a wrong token does not crash the server") {
    FakeUpstash upstash;
    SiteCounters counters(std::make_unique<UpstashStore>(upstash.url(), "wrong"));
    counters.addSolve();
    CHECK_FALSE(counters.flush());
    CHECK(counters.counts().cubesSolved == 1);
}

TEST_CASE("rate limiter allows a burst, then a steady rate") {
    using namespace std::chrono_literals;
    RateLimiter limiter(/*perSecond=*/0.5, /*burst=*/2);
    const auto t0 = RateLimiter::Clock::now();
    CHECK(limiter.check("a", t0) == 0);
    CHECK(limiter.check("a", t0) == 0);
    CHECK(limiter.check("a", t0) == 2);         // empty: one request needs 2 s
    CHECK(limiter.check("b", t0) == 0);         // other clients are not affected
    CHECK(limiter.check("a", t0 + 1s) == 1);    // half a request saved up
    CHECK(limiter.check("a", t0 + 2s) == 0);    // one full request
    CHECK(limiter.check("a", t0 + 100s) == 0);  // refills only up to the burst
    CHECK(limiter.check("a", t0 + 100s) == 0);
    CHECK(limiter.check("a", t0 + 100s) == 2);
}

TEST_CASE("rate limiter forgets idle clients when it is full") {
    using namespace std::chrono_literals;
    RateLimiter limiter(1, 1, /*maxClients=*/2);
    const auto t0 = RateLimiter::Clock::now();
    CHECK(limiter.check("a", t0) == 0);
    CHECK(limiter.check("b", t0) == 0);
    CHECK(limiter.check("c", t0 + 5s) == 0);  // a and b refilled and were forgotten
    CHECK(limiter.check("c", t0 + 5s) == 1);
}

TEST_CASE("client address, site origin and page template") {
    CHECK(clientKey("", "10.0.0.1") == "10.0.0.1");
    CHECK(clientKey("203.0.113.7, 10.0.0.2", "10.0.0.1") == "203.0.113.7");
    CHECK(clientKey("  198.51.100.4 ", "10.0.0.1") == "198.51.100.4");

    CHECK(siteOrigin("cube.onrender.com", "https") == "https://cube.onrender.com");
    CHECK(siteOrigin("localhost:8080", "") == "http://localhost:8080");
    CHECK(siteOrigin("a.com", "https,http") == "https://a.com");
    CHECK(siteOrigin("evil.com\"><script>", "https").empty());  // never put markup in the page
    CHECK(siteOrigin("", "https").empty());

    CHECK(fillPageTemplate("<a href=\"__SITE_ORIGIN__/\">__SITE_ORIGIN__</a>", "https://x.io") ==
          "<a href=\"https://x.io/\">https://x.io</a>");
    CHECK(fillPageTemplate("no marker", "https://x.io") == "no marker");
}

TEST_CASE("a cube counts as solved only when the solver solved something") {
    Service service;
    SolveRequest scrambled;
    scrambled.scramble = "R U";
    CHECK(service.solve(scrambled).status == 200);
    CHECK(service.solve(scrambled).status == 200);  // cached, still counts
    SolveRequest solved;
    solved.facelets = cube::kSolvedFacelets;
    CHECK(service.solve(solved).status == 200);  // nothing to solve
    SolveRequest bad;
    bad.scramble = "R X";
    CHECK(service.solve(bad).status == 400);
    CHECK(service.counters().body == "{\"visitors\":0,\"cubesSolved\":2}");
    CHECK(service.visit().body == "{\"visitors\":1,\"cubesSolved\":2}");
}

TEST_CASE("solve endpoint rejects bad input with 400") {
    Service service;
    SolveRequest none;
    CHECK(service.solve(none).status == 400);

    SolveRequest badMove;
    badMove.scramble = "R X";
    CHECK(service.solve(badMove).status == 400);

    SolveRequest badCube;
    badCube.facelets = "UUU";
    const Response r = service.solve(badCube);
    CHECK(r.status == 400);
    CHECK(r.body.find("54 characters") != std::string::npos);

    SolveRequest badTimeout;
    badTimeout.scramble = "R";
    badTimeout.timeout = "-5";
    CHECK(service.solve(badTimeout).status == 400);

    SolveRequest both;
    both.scramble = "R";
    both.facelets = cube::kSolvedFacelets;
    CHECK(service.solve(both).status == 400);
}

TEST_CASE("solve endpoint answers 503 when every solve slot stays busy") {
    Service service(16, /*maxConcurrentSolves=*/1, /*slotWaitMs=*/100);
    SolveRequest slow;
    slow.facelets = "UBULURUFURURFRBRDRFUFLFRFDFDFDLDRDBDLULBLFLDLBUBRBLBDB";  // superflip: uses the full timeout
    slow.timeout = "1500";
    std::thread holder([&] { CHECK(service.solve(slow).status == 200); });
    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    SolveRequest quick;
    quick.scramble = "R";
    const Response r = service.solve(quick);
    CHECK(r.status == 503);
    CHECK(r.body.find("busy") != std::string::npos);
    holder.join();
}

TEST_CASE("HTTP server end to end") {
    httplib::Server server;
    Service service;
    configureServer(server);
    registerRoutes(server, service, "");
    const int port = server.bind_to_any_port("127.0.0.1");
    REQUIRE(port > 0);
    std::thread listener([&] { server.listen_after_bind(); });
    server.wait_until_ready();

    // A second server must not be able to share the port.
    httplib::Server second;
    configureServer(second);
    CHECK_FALSE(second.bind_to_port("127.0.0.1", port));

    httplib::Client client("127.0.0.1", port);
    client.set_read_timeout(10, 0);

    auto health = client.Get("/api/health");
    REQUIRE(health);
    CHECK(health->status == 200);

    auto solved = client.Get("/api/solve?scramble=R%20U%20R'%20U'");
    REQUIRE(solved);
    CHECK(solved->status == 200);
    CHECK(solved->get_header_value("Content-Type") == "application/json");
    CHECK(solved->body.find("\"length\":4") != std::string::npos);

    auto scramble = client.Get("/api/scramble");
    REQUIRE(scramble);
    CHECK(scramble->status == 200);
    CHECK(scramble->body.find("\"scramble\":") != std::string::npos);

    auto bad = client.Get("/api/solve?facelets=XYZ");
    REQUIRE(bad);
    CHECK(bad->status == 400);

    auto missing = client.Get("/api/nope");
    REQUIRE(missing);
    CHECK(missing->status == 404);
    CHECK(missing->body.find("no such endpoint") != std::string::npos);

    auto visit = client.Post("/api/visit");
    REQUIRE(visit);
    CHECK(visit->status == 200);
    CHECK(visit->body == "{\"visitors\":1,\"cubesSolved\":1}");
    auto counters = client.Get("/api/counters");
    REQUIRE(counters);
    CHECK(counters->body == "{\"visitors\":1,\"cubesSolved\":1}");

    auto stats = client.Get("/api/stats");
    REQUIRE(stats);
    CHECK(stats->body.find("\"solveRequests\":2") != std::string::npos);

    server.stop();
    listener.join();
}

TEST_CASE("HTTP rate limits and the filled-in page") {
    const std::filesystem::path web = std::filesystem::temp_directory_path() / "cubesolver_test_web";
    std::filesystem::create_directories(web);
    std::ofstream(web / "index.html") << "<meta property=\"og:image\" content=\"__SITE_ORIGIN__/og.png\">";

    httplib::Server server;
    RateLimits limits;
    limits.solvesPerMinute = 1;
    limits.solveBurst = 2;
    limits.visitsPerHour = 1;
    limits.visitBurst = 1;
    Service service(16, 4, 5000, nullptr, limits);
    configureServer(server);
    registerRoutes(server, service, web.string());
    const int port = server.bind_to_any_port("127.0.0.1");
    REQUIRE(port > 0);
    std::thread listener([&] { server.listen_after_bind(); });
    server.wait_until_ready();
    httplib::Client client("127.0.0.1", port);
    client.set_read_timeout(10, 0);

    // Two solves pass, the third is limited. Another client is not affected.
    const httplib::Headers alice = {{"X-Forwarded-For", "203.0.113.1"}};
    const httplib::Headers bob = {{"X-Forwarded-For", "203.0.113.2"}};
    CHECK(client.Get("/api/solve?scramble=R", alice)->status == 200);
    CHECK(client.Get("/api/scramble", alice)->status == 200);
    auto limited = client.Get("/api/solve?scramble=R", alice);
    REQUIRE(limited);
    CHECK(limited->status == 429);
    CHECK(limited->get_header_value("Retry-After") == "60");
    CHECK(limited->body.find("too many requests") != std::string::npos);
    CHECK(client.Get("/api/solve?scramble=R", bob)->status == 200);

    // The second visit from the same address is answered but not counted.
    CHECK(client.Post("/api/visit", alice, "", "text/plain")->body.find("\"visitors\":1") != std::string::npos);
    auto again = client.Post("/api/visit", alice, "", "text/plain");
    REQUIRE(again);
    CHECK(again->status == 200);
    CHECK(again->body.find("\"visitors\":1") != std::string::npos);

    // The page gets the site's address filled in.
    const httplib::Headers site = {{"Host", "cube.example.com"}, {"X-Forwarded-Proto", "https"}};
    auto page = client.Get("/", site);
    REQUIRE(page);
    CHECK(page->status == 200);
    CHECK(page->body == "<meta property=\"og:image\" content=\"https://cube.example.com/og.png\">");
    CHECK(page->get_header_value("Content-Type") == "text/html; charset=utf-8");

    server.stop();
    listener.join();
    std::filesystem::remove_all(web);
}
