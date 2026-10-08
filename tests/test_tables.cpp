#include <random>

#include "cubesolver/coord.hpp"
#include "cubesolver/tables.hpp"
#include "doctest/doctest.h"

using namespace cube;

namespace {

template <typename Get, typename Set>
void checkRoundTrip(int size, Get get, Set set) {
    for (int i = 0; i < size; ++i) {
        CubieCube c;
        set(c, i);
        if (get(c) != i) {
            FAIL("coordinate " << i << " does not round trip");
        }
    }
}

// A random cube inside G1, made with phase-2 moves only.
CubieCube randomG1Cube(std::mt19937_64& rng) {
    std::uniform_int_distribution<int> pick(0, static_cast<int>(kPhase2Moves.size()) - 1);
    CubieCube c;
    for (int i = 0; i < 40; ++i) c.applyMove(kPhase2Moves[pick(rng)]);
    return c;
}

}  // namespace

TEST_CASE("coordinates round trip for every value") {
    checkRoundTrip(kNumTwist, getTwist, setTwist);
    checkRoundTrip(kNumFlip, getFlip, setFlip);
    checkRoundTrip(kNumSlice, getSlice, setSlice);
    checkRoundTrip(kNumCornersPerm, getCornersPerm, setCornersPerm);
    checkRoundTrip(kNumUdEdges, getUdEdges, setUdEdges);
    checkRoundTrip(kNumSlicePerm, getSlicePerm, setSlicePerm);
}

TEST_CASE("solved cube has all coordinates zero") {
    CubieCube c;
    CHECK(getTwist(c) == 0);
    CHECK(getFlip(c) == 0);
    CHECK(getSlice(c) == 0);
    CHECK(getCornersPerm(c) == 0);
    CHECK(getUdEdges(c) == 0);
    CHECK(getSlicePerm(c) == 0);
}

TEST_CASE("phase-1 move tables agree with cubie moves") {
    const Tables& t = tables();
    std::mt19937_64 rng(5);
    for (int i = 0; i < 300; ++i) {
        const CubieCube c = randomCube(rng);
        for (int m = 0; m < kNumMoves; ++m) {
            CubieCube moved = c;
            moved.applyMove(static_cast<Move>(m));
            REQUIRE(t.twistMove[getTwist(c) * kNumMoves + m] == getTwist(moved));
            REQUIRE(t.flipMove[getFlip(c) * kNumMoves + m] == getFlip(moved));
            REQUIRE(t.sliceMove[getSlice(c) * kNumMoves + m] == getSlice(moved));
            REQUIRE(t.cornersMove[getCornersPerm(c) * kNumMoves + m] == getCornersPerm(moved));
        }
    }
}

TEST_CASE("phase-2 move tables agree with cubie moves") {
    const Tables& t = tables();
    std::mt19937_64 rng(6);
    for (int i = 0; i < 300; ++i) {
        const CubieCube c = randomG1Cube(rng);
        REQUIRE(getTwist(c) == 0);
        REQUIRE(getFlip(c) == 0);
        REQUIRE(getSlice(c) == 0);
        for (Move m : kPhase2Moves) {
            CubieCube moved = c;
            moved.applyMove(m);
            REQUIRE(t.udEdgesMove[getUdEdges(c) * kNumMoves + m] == getUdEdges(moved));
            REQUIRE(t.slicePermMove[getSlicePerm(c) * kNumMoves + m] == getSlicePerm(moved));
        }
    }
}

TEST_CASE("pruning tables are complete") {
    const Tables& t = tables();
    for (const auto* table :
         {&t.twistSlicePrune, &t.flipSlicePrune, &t.twistFlipPrune, &t.cornersSlicePrune, &t.edgesSlicePrune}) {
        CHECK((*table)[0] == 0);
        int unfilled = 0;
        for (uint8_t d : *table) unfilled += d == 0xFF ? 1 : 0;
        CHECK(unfilled == 0);
    }
}

TEST_CASE("pruning tables never overestimate (admissible heuristic)") {
    const Tables& t = tables();
    std::mt19937_64 rng(7);
    std::uniform_int_distribution<int> pickAny(0, kNumMoves - 1);
    std::uniform_int_distribution<int> pickG1(0, static_cast<int>(kPhase2Moves.size()) - 1);
    for (int trial = 0; trial < 2000; ++trial) {
        const int length = trial % 13;
        CubieCube a, b;
        for (int i = 0; i < length; ++i) {
            a.applyMove(static_cast<Move>(pickAny(rng)));
            b.applyMove(kPhase2Moves[pickG1(rng)]);
        }
        // A cube made with n moves can be solved in n moves, so a lower bound must be <= n.
        REQUIRE(t.phase1Distance(getTwist(a), getFlip(a), getSlice(a)) <= length);
        REQUIRE(t.phase2Distance(getCornersPerm(b), getUdEdges(b), getSlicePerm(b)) <= length);
    }
}
