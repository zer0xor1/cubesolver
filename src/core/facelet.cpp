#include "cubesolver/facelet.hpp"

#include <array>

namespace cube {

namespace {

// Facelet index = face * 9 + (sticker number - 1).
constexpr int fi(Face f, int n) {
    return f * 9 + n - 1;
}

// Facelets of each corner slot, starting with the U or D sticker and going clockwise.
constexpr std::array<std::array<int, 3>, kNumCorners> kCornerFacelet{{
    {fi(U, 9), fi(R, 1), fi(F, 3)},  // URF
    {fi(U, 7), fi(F, 1), fi(L, 3)},  // UFL
    {fi(U, 1), fi(L, 1), fi(B, 3)},  // ULB
    {fi(U, 3), fi(B, 1), fi(R, 3)},  // UBR
    {fi(D, 3), fi(F, 9), fi(R, 7)},  // DFR
    {fi(D, 1), fi(L, 9), fi(F, 7)},  // DLF
    {fi(D, 7), fi(B, 9), fi(L, 7)},  // DBL
    {fi(D, 9), fi(R, 9), fi(B, 7)},  // DRB
}};

constexpr std::array<std::array<int, 2>, kNumEdges> kEdgeFacelet{{
    {fi(U, 6), fi(R, 2)},  // UR
    {fi(U, 8), fi(F, 2)},  // UF
    {fi(U, 4), fi(L, 2)},  // UL
    {fi(U, 2), fi(B, 2)},  // UB
    {fi(D, 6), fi(R, 8)},  // DR
    {fi(D, 2), fi(F, 8)},  // DF
    {fi(D, 4), fi(L, 8)},  // DL
    {fi(D, 8), fi(B, 8)},  // DB
    {fi(F, 6), fi(R, 4)},  // FR
    {fi(F, 4), fi(L, 6)},  // FL
    {fi(B, 6), fi(L, 4)},  // BL
    {fi(B, 4), fi(R, 6)},  // BR
}};

constexpr std::array<std::array<Face, 3>, kNumCorners> kCornerColor{{
    {U, R, F},
    {U, F, L},
    {U, L, B},
    {U, B, R},
    {D, F, R},
    {D, L, F},
    {D, B, L},
    {D, R, B},
}};

constexpr std::array<std::array<Face, 2>, kNumEdges> kEdgeColor{{
    {U, R},
    {U, F},
    {U, L},
    {U, B},
    {D, R},
    {D, F},
    {D, L},
    {D, B},
    {F, R},
    {F, L},
    {B, L},
    {B, R},
}};

constexpr char kFaceChars[] = "URFDLB";
constexpr const char* kCornerNames[] = {"URF", "UFL", "ULB", "UBR", "DFR", "DLF", "DBL", "DRB"};
constexpr const char* kEdgeNames[] = {"UR", "UF", "UL", "UB", "DR", "DF", "DL", "DB", "FR", "FL", "BL", "BR"};

int faceIndex(char ch) {
    for (int f = 0; f < 6; ++f)
        if (kFaceChars[f] == ch) return f;
    return -1;
}

}  // namespace

const char* const kSolvedFacelets = "UUUUUUUUURRRRRRRRRFFFFFFFFFDDDDDDDDDLLLLLLLLLBBBBBBBBB";

std::string toFacelets(const CubieCube& c) {
    std::string f(kNumFacelets, '?');
    for (int face = 0; face < 6; ++face) f[face * 9 + 4] = kFaceChars[face];
    for (int i = 0; i < kNumCorners; ++i) {
        const int piece = c.cp[i];
        const int ori = c.co[i];
        for (int n = 0; n < 3; ++n) f[kCornerFacelet[i][(n + ori) % 3]] = kFaceChars[kCornerColor[piece][n]];
    }
    for (int i = 0; i < kNumEdges; ++i) {
        const int piece = c.ep[i];
        const int ori = c.eo[i];
        for (int n = 0; n < 2; ++n) f[kEdgeFacelet[i][(n + ori) % 2]] = kFaceChars[kEdgeColor[piece][n]];
    }
    return f;
}

CubieCube fromFacelets(std::string_view text) {
    if (text.size() != kNumFacelets) {
        throw InvalidCubeError("facelet string must have 54 characters, got " + std::to_string(text.size()));
    }
    std::array<int, kNumFacelets> f{};
    std::array<int, 6> count{};
    for (int i = 0; i < kNumFacelets; ++i) {
        const int face = faceIndex(text[i]);
        if (face < 0) {
            throw InvalidCubeError("invalid character " + describeChar(text[i]) + " at position " +
                                   std::to_string(i + 1) + " (use only U R F D L B)");
        }
        f[i] = face;
        ++count[face];
    }
    for (int face = 0; face < 6; ++face) {
        if (count[face] != 9) {
            throw InvalidCubeError(std::string("color ") + kFaceChars[face] + " appears " +
                                   std::to_string(count[face]) + " times, expected 9");
        }
        if (f[face * 9 + 4] != face) {
            throw InvalidCubeError(std::string("center of face ") + kFaceChars[face] + " must be '" + kFaceChars[face] +
                                   "'");
        }
    }

    CubieCube c;
    std::array<bool, kNumCorners> seenCorner{};
    for (int i = 0; i < kNumCorners; ++i) {
        int ori = 0;
        while (ori < 3 && f[kCornerFacelet[i][ori]] != U && f[kCornerFacelet[i][ori]] != D) ++ori;
        bool found = false;
        if (ori < 3) {
            const int c1 = f[kCornerFacelet[i][(ori + 1) % 3]];
            const int c2 = f[kCornerFacelet[i][(ori + 2) % 3]];
            for (int j = 0; j < kNumCorners; ++j) {
                if (kCornerColor[j][0] == f[kCornerFacelet[i][ori]] && kCornerColor[j][1] == c1 &&
                    kCornerColor[j][2] == c2) {
                    if (seenCorner[j]) {
                        throw InvalidCubeError(std::string("corner ") + kCornerNames[j] + " appears twice");
                    }
                    seenCorner[j] = true;
                    c.cp[i] = static_cast<uint8_t>(j);
                    c.co[i] = static_cast<uint8_t>(ori);
                    found = true;
                    break;
                }
            }
        }
        if (!found) {
            throw InvalidCubeError(std::string("the corner in slot ") + kCornerNames[i] +
                                   " has a color combination that does not exist");
        }
    }

    std::array<bool, kNumEdges> seenEdge{};
    for (int i = 0; i < kNumEdges; ++i) {
        const int a = f[kEdgeFacelet[i][0]];
        const int b = f[kEdgeFacelet[i][1]];
        bool found = false;
        for (int j = 0; j < kNumEdges && !found; ++j) {
            for (int ori = 0; ori < 2; ++ori) {
                if (kEdgeColor[j][ori] == a && kEdgeColor[j][1 - ori] == b) {
                    if (seenEdge[j]) {
                        throw InvalidCubeError(std::string("edge ") + kEdgeNames[j] + " appears twice");
                    }
                    seenEdge[j] = true;
                    c.ep[i] = static_cast<uint8_t>(j);
                    c.eo[i] = static_cast<uint8_t>(ori);
                    found = true;
                    break;
                }
            }
        }
        if (!found) {
            throw InvalidCubeError(std::string("the edge in slot ") + kEdgeNames[i] +
                                   " has a color combination that does not exist");
        }
    }

    const Validity v = c.verify();
    if (v != Validity::Ok) throw InvalidCubeError(std::string("unsolvable cube: ") + validityMessage(v));
    return c;
}

}  // namespace cube
