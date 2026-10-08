#include <random>

#include "cubesolver/cubie.hpp"
#include "doctest/doctest.h"

using namespace cube;

namespace {

CubieCube fromMoves(const char* text) {
    CubieCube c;
    c.applyMoves(parseMoves(text));
    return c;
}

// How many times the sequence must be repeated to get back to solved.
int orderOf(const char* text) {
    const MoveSeq seq = parseMoves(text);
    CubieCube c;
    int n = 0;
    do {
        c.applyMoves(seq);
        ++n;
    } while (!c.isSolved() && n < 100000);
    return n;
}

}  // namespace

TEST_CASE("a new cube is solved and valid") {
    CubieCube c;
    CHECK(c.isSolved());
    CHECK(c.verify() == Validity::Ok);
}

TEST_CASE("every move done four times returns to solved") {
    for (int m = 0; m < kNumMoves; ++m) {
        CubieCube c;
        for (int i = 0; i < 4; ++i) c.applyMove(static_cast<Move>(m));
        CHECK_MESSAGE(c.isSolved(), "move " << moveName(static_cast<Move>(m)));
    }
}

TEST_CASE("a move followed by its inverse returns to solved") {
    for (int m = 0; m < kNumMoves; ++m) {
        CubieCube c;
        c.applyMove(static_cast<Move>(m));
        c.applyMove(inverseMove(static_cast<Move>(m)));
        CHECK(c.isSolved());
    }
}

TEST_CASE("known orders of move sequences") {
    CHECK(orderOf("R U") == 105);
    CHECK(orderOf("R U R' U'") == 6);
    CHECK(orderOf("R U2 D' B D'") == 1260);   // the largest possible order
    CHECK(orderOf("R U R' U R U2 R'") == 6);  // Sune
}

TEST_CASE("exact state after R U (catches moves applied in reverse order)") {
    const CubieCube c = fromMoves("R U");
    CHECK(c.cp == std::array<uint8_t, 8>{URF, DFR, UFL, ULB, DRB, DLF, DBL, UBR});
    CHECK(c.co == std::array<uint8_t, 8>{1, 2, 0, 0, 1, 0, 0, 2});
    CHECK(c.ep == std::array<uint8_t, 12>{UB, FR, UF, UL, BR, DF, DL, DB, DR, FL, BL, UR});
    CHECK(c.eo == std::array<uint8_t, 12>{});
}

TEST_CASE("invariants hold after many random moves") {
    std::mt19937_64 rng(1);
    std::uniform_int_distribution<int> pick(0, kNumMoves - 1);
    CubieCube c;
    for (int i = 0; i < 100000; ++i) {
        c.applyMove(static_cast<Move>(pick(rng)));
        if (c.verify() != Validity::Ok) {
            FAIL("invalid state after " << i + 1 << " moves");
        }
    }
}

TEST_CASE("inverse of a cube undoes it") {
    std::mt19937_64 rng(2);
    for (int i = 0; i < 200; ++i) {
        const CubieCube c = randomCube(rng);
        CHECK((c * c.inverse()).isSolved());
        CHECK((c.inverse() * c).isSolved());
    }
}

TEST_CASE("a scramble followed by its inverse returns to solved") {
    const MoveSeq s = parseMoves("R U F' D2 L B' R2 U' F D L2 B");
    CubieCube c;
    c.applyMoves(s);
    CHECK_FALSE(c.isSolved());
    c.applyMoves(invertMoves(s));
    CHECK(c.isSolved());
}

TEST_CASE("move parsing and formatting") {
    CHECK(formatMoves(parseMoves("R U2 F' D")) == "R U2 F' D");
    CHECK(formatMoves(parseMoves("RU2F'D")) == "R U2 F' D");  // spaces are optional
    CHECK(formatMoves(parseMoves("  R2'  ,U ")) == "R2 U");   // R2' means R2
    CHECK(parseMoves("").empty());
    CHECK_THROWS_AS(parseMoves("R X"), std::invalid_argument);
    CHECK_THROWS_AS(parseMoves("r"), std::invalid_argument);
    CHECK_THROWS_AS(parseMoves("R3"), std::invalid_argument);
    // Non-printable bytes are described, not echoed (keeps JSON errors valid).
    CHECK_THROWS_WITH(parseMoves("R\xFF"), doctest::Contains("byte 0xFF"));
}

TEST_CASE("random cubes are valid and varied") {
    std::mt19937_64 rng(3);
    int solved = 0;
    for (int i = 0; i < 1000; ++i) {
        const CubieCube c = randomCube(rng);
        REQUIRE(c.verify() == Validity::Ok);
        solved += c.isSolved() ? 1 : 0;
    }
    CHECK(solved == 0);
}

TEST_CASE("verify detects impossible cubes") {
    CubieCube twisted;
    twisted.co[0] = 1;
    CHECK(twisted.verify() == Validity::TwistedCorner);

    CubieCube flipped;
    flipped.eo[0] = 1;
    CHECK(flipped.verify() == Validity::FlippedEdge);

    CubieCube swapped;
    std::swap(swapped.ep[0], swapped.ep[1]);
    CHECK(swapped.verify() == Validity::Parity);

    CubieCube duplicated;
    duplicated.cp[0] = duplicated.cp[1];
    CHECK(duplicated.verify() == Validity::BadCornerSet);
}
