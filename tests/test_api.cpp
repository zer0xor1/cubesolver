#include <chrono>
#include <filesystem>
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
        SiteCounters counters(file.string());
        CHECK(counters.json() == "{\"visitors\":0,\"cubesSolved\":0}");
        counters.addVisitor();
        counters.addVisitor();
        counters.addSolve();
    }
    SiteCounters reloaded(file.string());
    CHECK(reloaded.visitors() == 2);
    CHECK(reloaded.cubesSolved() == 1);
    std::filesystem::remove(file);
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
