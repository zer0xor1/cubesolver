// Two-phase solver (Kociemba's algorithm) with parallel search.
//
// Phase 1 brings the cube into G1 = <U, D, R2, L2, F2, B2>. Phase 2 solves it
// using only G1 moves. Both phases use IDA* with the pruning tables as the
// heuristic. After the first solution, the search keeps going with longer
// phase-1 paths to find shorter TOTAL solutions, until it reaches the target
// length or the time limit. If it runs out of shorter candidates first, the
// answer is the shortest one two-phase search can find (optimal for short
// scrambles).
//
// With several threads, the cube is also searched in its 3 rotations about the
// URF-DBL diagonal and as its inverse (6 searches). They share the best length
// found so far, so each one prunes with the others' results.
#pragma once

#include <cstdint>
#include <random>

#include "cubesolver/cubie.hpp"

namespace cube {

struct SolveOptions {
    int targetLength = 20;  // a solution this short (or shorter) is good enough...
    int polishMs = 20;      // ...once the search has run this long. Short scrambles
                            // finish the whole search inside this window, so they
                            // get the shortest possible answer (e.g. "R" -> "R'").
    int timeoutMs = 1000;   // after this, return the best solution found so far
    int threads = 0;        // parallel searches, 1..6; 0 = default (6)
};

struct SolveResult {
    MoveSeq moves;
    bool targetReached = false;  // a solution <= targetLength was found
    double milliseconds = 0;     // wall-clock time of the search
    uint64_t nodes = 0;          // search nodes expanded (all threads)
    int searches = 1;            // parallel searches used (identical copies of a
                                 // symmetric cube are searched only once)
};

// Throws InvalidCubeError if the cube cannot be solved.
SolveResult solve(const CubieCube& cube, const SolveOptions& options = {});

// Random-state scramble (as used in competitions): a uniformly random cube and
// a move sequence that produces it.
struct Scramble {
    CubieCube cube;
    MoveSeq moves;
};
Scramble randomScramble(std::mt19937_64& rng, const SolveOptions& options = {});

}  // namespace cube
