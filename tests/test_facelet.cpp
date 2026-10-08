#include <random>
#include <string>

#include "cubesolver/facelet.hpp"
#include "doctest/doctest.h"

using namespace cube;

namespace {

std::string faceletsAfter(const char* moves) {
    CubieCube c;
    c.applyMoves(parseMoves(moves));
    return toFacelets(c);
}

}  // namespace

TEST_CASE("solved cube facelets") {
    CHECK(toFacelets(CubieCube()) == kSolvedFacelets);
    CHECK(fromFacelets(kSolvedFacelets).isSolved());
}

// Expected strings were produced by Herbert Kociemba's reference implementation.
TEST_CASE("facelets after known move sequences") {
    CHECK(faceletsAfter("F") == "UUUUUULLLURRURRURRFFFFFFFFFRRRDDDDDDLLDLLDLLDBBBBBBBBB");
    CHECK(faceletsAfter("R U") == "UUUUUUFFFUBBRRRRRRRRRFFDFFDDDBDDBDDBFFDLLLLLLLLLUBBUBB");
    CHECK(faceletsAfter("R U R' U'") == "UULUUFUUFRRUBRRURRFFDFFUFFFDDRDDDDDDBLLLLLLLLBRRBBBBBB");
}

TEST_CASE("superflip: every edge flipped") {
    const char* superflip = "UBULURUFURURFRBRDRFUFLFRFDFDFDLDRDBDLULBLFLDLBUBRBLBDB";
    CHECK(faceletsAfter("U R2 F B R B2 R U2 L B2 R U' D' R2 F R' L B2 U2 F2") == superflip);
    const CubieCube c = fromFacelets(superflip);
    for (int i = 0; i < kNumEdges; ++i) CHECK(c.eo[i] == 1);
}

TEST_CASE("facelets round trip for random cubes") {
    std::mt19937_64 rng(4);
    for (int i = 0; i < 1000; ++i) {
        const CubieCube c = randomCube(rng);
        REQUIRE(fromFacelets(toFacelets(c)) == c);
    }
}

TEST_CASE("invalid facelet strings give clear errors") {
    const std::string solved = kSolvedFacelets;

    CHECK_THROWS_WITH_AS(fromFacelets("UUU"), "facelet string must have 54 characters, got 3", InvalidCubeError);

    std::string bad = solved;
    bad[0] = 'X';
    CHECK_THROWS_WITH_AS(fromFacelets(bad), doctest::Contains("invalid character 'X'"), InvalidCubeError);

    bad = solved;
    bad[0] = 'R';  // ten R stickers, eight U stickers
    CHECK_THROWS_WITH_AS(fromFacelets(bad), doctest::Contains("appears"), InvalidCubeError);

    bad = solved;
    std::swap(bad[4], bad[13]);  // swap U and R centers
    CHECK_THROWS_WITH_AS(fromFacelets(bad), doctest::Contains("center"), InvalidCubeError);
}

TEST_CASE("physically impossible cubes are rejected") {
    // Twist one corner in place: rotate the three stickers of URF (U9, R1, F3).
    std::string twisted = kSolvedFacelets;
    const char u9 = twisted[8], r1 = twisted[9], f3 = twisted[20];
    twisted[8] = f3;
    twisted[9] = u9;
    twisted[20] = r1;
    CHECK_THROWS_WITH_AS(fromFacelets(twisted), doctest::Contains("twisted"), InvalidCubeError);

    // Flip one edge in place: swap the two stickers of UR (U6, R2).
    std::string flipped = kSolvedFacelets;
    std::swap(flipped[5], flipped[10]);
    CHECK_THROWS_WITH_AS(fromFacelets(flipped), doctest::Contains("flipped"), InvalidCubeError);

    // Swap two edges (UR and UF): stickers U6<->U8 and R2<->F2.
    std::string swapped = kSolvedFacelets;
    std::swap(swapped[5], swapped[7]);
    std::swap(swapped[10], swapped[19]);
    CHECK_THROWS_WITH_AS(fromFacelets(swapped), doctest::Contains("swapped"), InvalidCubeError);

    // A corner with two opposite colors (U and D) cannot exist.
    std::string impossible = kSolvedFacelets;
    std::swap(impossible[9], impossible[29]);  // R1 <-> D3
    CHECK_THROWS_AS(fromFacelets(impossible), InvalidCubeError);
}
