// Move tables and pruning tables for the two-phase search.
//
// Move table:    newCoord = table[coord * 18 + move]
// Pruning table: a lower bound on the moves needed to bring a PAIR of
//                coordinates to zero, found by breadth-first search from
//                the solved state. The search uses max() of several tables.
//
// All tables together take about 12 MB (3 MB move tables, 8.5 MB pruning
// tables) and are built in well under a second.
// They are read-only after construction, so all threads share them safely.
#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "cubesolver/coord.hpp"
#include "cubesolver/cubie.hpp"

namespace cube {

// Moves that keep a cube inside G1: U, U2, U', D, D2, D', R2, L2, F2, B2.
extern const std::array<Move, 10> kPhase2Moves;
bool isPhase2Move(Move m);

struct Tables {
    // Move tables (uint16 is enough: largest coordinate is 40319).
    std::vector<uint16_t> twistMove, flipMove, sliceMove;
    std::vector<uint16_t> cornersMove;                 // all 18 moves
    std::vector<uint16_t> udEdgesMove, slicePermMove;  // phase-2 moves only (G1)

    // Phase 1 pruning: index = b * nA + a.
    std::vector<uint8_t> twistSlicePrune;  // a = twist, b = slice
    std::vector<uint8_t> flipSlicePrune;   // a = flip,  b = slice
    std::vector<uint8_t> twistFlipPrune;   // a = twist, b = flip

    // Phase 2 pruning.
    std::vector<uint8_t> cornersSlicePrune;  // a = corners, b = slicePerm
    std::vector<uint8_t> edgesSlicePrune;    // a = udEdges, b = slicePerm

    double buildMilliseconds = 0;

    int phase1Distance(int twist, int flip, int slice) const {
        int a = twistSlicePrune[slice * kNumTwist + twist];
        int b = flipSlicePrune[slice * kNumFlip + flip];
        int c = twistFlipPrune[flip * kNumTwist + twist];
        return a > b ? (a > c ? a : c) : (b > c ? b : c);
    }

    int phase2Distance(int corners, int udEdges, int slicePerm) const {
        int a = cornersSlicePrune[slicePerm * kNumCornersPerm + corners];
        int b = edgesSlicePrune[slicePerm * kNumUdEdges + udEdges];
        return a > b ? a : b;
    }
};

// Built on first use (thread-safe), then shared.
const Tables& tables();

}  // namespace cube
