// Coordinates: small integers that describe one part of a cube state.
//
// The search never touches CubieCube directly. It works on these integers and
// looks up the effect of each move in precomputed tables (see tables.hpp).
//
// Phase 1 (reach the subgroup G1 = <U, D, R2, L2, F2, B2>):
//   twist  0..2186   orientation of the corners      (solved/G1 = 0)
//   flip   0..2047   orientation of the edges        (solved/G1 = 0)
//   slice  0..494    which 4 slots hold the FR, FL, BL, BR edges (G1 = 0)
// Phase 2 (solve inside G1):
//   corners    0..40319  permutation of the 8 corners
//   udEdges    0..40319  permutation of the 8 U and D layer edges
//   slicePerm  0..23     permutation of the 4 middle-layer edges
#pragma once

#include <cstdint>

#include "cubesolver/cubie.hpp"

namespace cube {

constexpr int kNumTwist = 2187;         // 3^7
constexpr int kNumFlip = 2048;          // 2^11
constexpr int kNumSlice = 495;          // C(12, 4)
constexpr int kNumCornersPerm = 40320;  // 8!
constexpr int kNumUdEdges = 40320;      // 8!
constexpr int kNumSlicePerm = 24;       // 4!

int getTwist(const CubieCube& c);
void setTwist(CubieCube& c, int twist);

int getFlip(const CubieCube& c);
void setFlip(CubieCube& c, int flip);

int getSlice(const CubieCube& c);
void setSlice(CubieCube& c, int slice);

int getCornersPerm(const CubieCube& c);
void setCornersPerm(CubieCube& c, int idx);

// Only meaningful when the cube is in G1 (U/D edges in U/D slots).
int getUdEdges(const CubieCube& c);
void setUdEdges(CubieCube& c, int idx);

int getSlicePerm(const CubieCube& c);
void setSlicePerm(CubieCube& c, int idx);

// Lehmer code of a permutation of 0..n-1 (identity = 0).
int permutationIndex(const uint8_t* p, int n);
void permutationFromIndex(int idx, uint8_t* p, int n);

}  // namespace cube
