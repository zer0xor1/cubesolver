// Cubie-level model of the 3x3 Rubik's cube.
//
// A cube state is stored as four arrays: which corner/edge sits in each slot
// (permutation) and how it is twisted/flipped (orientation). Numbering follows
// Herbert Kociemba's convention so results can be compared with published data.
#pragma once

#include <array>
#include <cstdint>
#include <random>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace cube {

// Faces in Kociemba order. Move index = face * 3 + (quarter turns - 1).
enum Face : uint8_t { U = 0, R = 1, F = 2, D = 3, L = 4, B = 5 };

enum Corner : uint8_t { URF, UFL, ULB, UBR, DFR, DLF, DBL, DRB };
enum Edge : uint8_t { UR, UF, UL, UB, DR, DF, DL, DB, FR, FL, BL, BR };

constexpr int kNumCorners = 8;
constexpr int kNumEdges = 12;
constexpr int kNumMoves = 18;

using Move = uint8_t;  // 0..17, e.g. 0 = U, 1 = U2, 2 = U'
using MoveSeq = std::vector<Move>;

constexpr int faceOf(Move m) {
    return m / 3;
}
constexpr int powerOf(Move m) {
    return m % 3 + 1;
}  // 1, 2 or 3 quarter turns
constexpr Move makeMove(int face, int power) {
    return static_cast<Move>(face * 3 + power - 1);
}
constexpr Move inverseMove(Move m) {
    return makeMove(faceOf(m), 4 - powerOf(m));
}

// Result of checking whether a cube state can be reached from solved.
enum class Validity {
    Ok,
    BadCornerSet,   // some corner missing or duplicated
    BadEdgeSet,     // some edge missing or duplicated
    TwistedCorner,  // corner orientation sum not divisible by 3
    FlippedEdge,    // edge orientation sum odd
    Parity,         // corner and edge permutation parities differ
};
const char* validityMessage(Validity v);

// Thrown when input does not describe a real, solvable cube.
class InvalidCubeError : public std::invalid_argument {
public:
    using std::invalid_argument::invalid_argument;
};

struct CubieCube {
    std::array<uint8_t, kNumCorners> cp{};  // corner in each corner slot
    std::array<uint8_t, kNumCorners> co{};  // its twist: 0, 1, 2
    std::array<uint8_t, kNumEdges> ep{};    // edge in each edge slot
    std::array<uint8_t, kNumEdges> eo{};    // its flip: 0, 1

    CubieCube();  // solved cube

    bool isSolved() const;
    bool operator==(const CubieCube& o) const;
    bool operator!=(const CubieCube& o) const { return !(*this == o); }

    // Composition: (a * b) means "first a, then b". Applying move m to cube c
    // is c * moveCube(m).
    CubieCube operator*(const CubieCube& b) const;
    CubieCube inverse() const;

    void applyMove(Move m);
    void applyMoves(const MoveSeq& seq);

    int cornerParity() const;  // 0 = even, 1 = odd
    int edgeParity() const;
    Validity verify() const;
};

// The cube after one move from solved, for all 18 moves.
const CubieCube& moveCube(Move m);

// Parse "R U R' U2" (spaces optional). Throws std::invalid_argument on bad input.
MoveSeq parseMoves(std::string_view text);
std::string moveName(Move m);
std::string formatMoves(const MoveSeq& seq);
MoveSeq invertMoves(const MoveSeq& seq);

// "'X'" for printable characters, "byte 0xFF" otherwise (keeps error
// messages valid UTF-8 and readable).
std::string describeChar(char ch);

// A uniformly random reachable cube state.
CubieCube randomCube(std::mt19937_64& rng);

}  // namespace cube
