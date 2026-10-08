#include <random>
#include <thread>
#include <vector>

#include "cubesolver/facelet.hpp"
#include "cubesolver/solver.hpp"
#include "doctest/doctest.h"

using namespace cube;

namespace {

bool solves(const CubieCube& c, const MoveSeq& moves) {
    CubieCube x = c;
    x.applyMoves(moves);
    return x.isSolved();
}

CubieCube fromMoves(const char* text) {
    CubieCube c;
    c.applyMoves(parseMoves(text));
    return c;
}

// Lets the search run to completion. Short scrambles finish in milliseconds,
// but sanitizer builds are much slower than the default 20 ms polish window.
SolveOptions exhaustive() {
    SolveOptions opt;
    opt.polishMs = 10000;
    opt.timeoutMs = 10000;
    return opt;
}

}  // namespace

TEST_CASE("solved cube needs no moves") {
    const SolveResult r = solve(CubieCube());
    CHECK(r.moves.empty());
    CHECK(r.targetReached);
}

TEST_CASE("one move is undone by its inverse") {
    for (int m = 0; m < kNumMoves; ++m) {
        CubieCube c;
        c.applyMove(static_cast<Move>(m));
        const SolveResult r = solve(c, exhaustive());
        REQUIRE(r.moves.size() == 1);
        CHECK(r.moves[0] == inverseMove(static_cast<Move>(m)));
    }
}

TEST_CASE("short scrambles get solutions no longer than the scramble") {
    std::mt19937_64 rng(8);
    std::uniform_int_distribution<int> pick(0, kNumMoves - 1);
    for (int trial = 0; trial < 60; ++trial) {
        const int length = 1 + trial % 9;
        CubieCube c;
        for (int i = 0; i < length; ++i) c.applyMove(static_cast<Move>(pick(rng)));
        const SolveResult r = solve(c, exhaustive());
        REQUIRE(solves(c, r.moves));
        CHECK(static_cast<int>(r.moves.size()) <= length);
    }
    CHECK(solve(fromMoves("R U R' U'"), exhaustive()).moves.size() == 4);
}

TEST_CASE("random cubes are solved in about 20 moves") {
    std::mt19937_64 rng(9);
    SolveOptions opt;
    opt.timeoutMs = 3000;  // generous, for slow CI machines
    int total = 0;
    const int count = 30;
    for (int i = 0; i < count; ++i) {
        const CubieCube c = randomCube(rng);
        const SolveResult r = solve(c, opt);
        REQUIRE(solves(c, r.moves));
        CHECK(r.moves.size() <= 22);
        total += static_cast<int>(r.moves.size());
    }
    CHECK(total <= 21 * count);  // average at most 21
}

TEST_CASE("one search thread and six search threads both work") {
    std::mt19937_64 rng(10);
    const CubieCube c = randomCube(rng);
    for (int threads : {1, 2, 6}) {
        SolveOptions opt;
        opt.threads = threads;
        const SolveResult r = solve(c, opt);
        CHECK(r.searches == threads);
        CHECK(solves(c, r.moves));
    }
}

TEST_CASE("superflip is solved") {
    const CubieCube c = fromFacelets("UBULURUFURURFRBRDRFUFLFRFDFDFDLDRDBDLULBLFLDLBUBRBLBDB");
    SolveOptions opt;
    opt.timeoutMs = 3000;
    const SolveResult r = solve(c, opt);
    CHECK(solves(c, r.moves));
    CHECK(r.moves.size() <= 22);
    // Superflip looks the same after any rotation and is its own inverse, so
    // the 6 parallel searches collapse into 1.
    CHECK(r.searches == 1);
}

TEST_CASE("a zero time limit still returns a valid solution") {
    std::mt19937_64 rng(11);
    const CubieCube c = randomCube(rng);
    SolveOptions opt;
    opt.timeoutMs = 0;
    const SolveResult r = solve(c, opt);
    CHECK(solves(c, r.moves));
}

TEST_CASE("impossible cubes are rejected") {
    CubieCube c;
    c.co[0] = 2;
    CHECK_THROWS_AS(solve(c), InvalidCubeError);
}

TEST_CASE("random-state scrambles produce their cube") {
    std::mt19937_64 rng(12);
    for (int i = 0; i < 5; ++i) {
        const Scramble s = randomScramble(rng);
        CubieCube c;
        c.applyMoves(s.moves);
        CHECK(c == s.cube);
    }
}

TEST_CASE("many solves at the same time (as in the web server)") {
    std::vector<CubieCube> cubes;
    std::mt19937_64 rng(13);
    for (int i = 0; i < 4; ++i) cubes.push_back(randomCube(rng));
    std::vector<int> ok(cubes.size(), 0);
    std::vector<std::thread> threads;
    for (size_t i = 0; i < cubes.size(); ++i) {
        threads.emplace_back([&, i] {
            SolveOptions opt;
            opt.threads = 2;
            ok[i] = solves(cubes[i], solve(cubes[i], opt).moves) ? 1 : 0;
        });
    }
    for (auto& t : threads) t.join();
    for (int v : ok) CHECK(v == 1);
}
