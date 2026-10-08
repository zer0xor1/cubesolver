#include "cubesolver/cubie.hpp"

#include <algorithm>
#include <cstdio>
#include <stdexcept>

namespace cube {

namespace {

// The six quarter turns as cube states (Kociemba's "replaced by" convention):
// cp[i] is the corner that moves INTO slot i, co[i] is the twist it gains.
CubieCube makeBasic(std::array<uint8_t, 8> cp, std::array<uint8_t, 8> co, std::array<uint8_t, 12> ep,
                    std::array<uint8_t, 12> eo) {
    CubieCube c;
    c.cp = cp;
    c.co = co;
    c.ep = ep;
    c.eo = eo;
    return c;
}

std::array<CubieCube, 6> buildBasicMoves() {
    return {
        // U
        makeBasic({UBR, URF, UFL, ULB, DFR, DLF, DBL, DRB}, {0, 0, 0, 0, 0, 0, 0, 0},
                  {UB, UR, UF, UL, DR, DF, DL, DB, FR, FL, BL, BR}, {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}),
        // R
        makeBasic({DFR, UFL, ULB, URF, DRB, DLF, DBL, UBR}, {2, 0, 0, 1, 1, 0, 0, 2},
                  {FR, UF, UL, UB, BR, DF, DL, DB, DR, FL, BL, UR}, {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}),
        // F
        makeBasic({UFL, DLF, ULB, UBR, URF, DFR, DBL, DRB}, {1, 2, 0, 0, 2, 1, 0, 0},
                  {UR, FL, UL, UB, DR, FR, DL, DB, UF, DF, BL, BR}, {0, 1, 0, 0, 0, 1, 0, 0, 1, 1, 0, 0}),
        // D
        makeBasic({URF, UFL, ULB, UBR, DLF, DBL, DRB, DFR}, {0, 0, 0, 0, 0, 0, 0, 0},
                  {UR, UF, UL, UB, DF, DL, DB, DR, FR, FL, BL, BR}, {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}),
        // L
        makeBasic({URF, ULB, DBL, UBR, DFR, UFL, DLF, DRB}, {0, 1, 2, 0, 0, 2, 1, 0},
                  {UR, UF, BL, UB, DR, DF, FL, DB, FR, UL, DL, BR}, {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}),
        // B
        makeBasic({URF, UFL, UBR, DRB, DFR, DLF, ULB, DBL}, {0, 0, 1, 2, 0, 0, 2, 1},
                  {UR, UF, UL, BR, DR, DF, DL, BL, FR, FL, UB, DB}, {0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 1, 1}),
    };
}

std::array<CubieCube, kNumMoves> buildAllMoves() {
    const auto basic = buildBasicMoves();
    std::array<CubieCube, kNumMoves> all;
    for (int f = 0; f < 6; ++f) {
        CubieCube c;
        for (int p = 1; p <= 3; ++p) {
            c = c * basic[f];
            all[makeMove(f, p)] = c;
        }
    }
    return all;
}

template <size_t N>
int permutationParity(const std::array<uint8_t, N>& p) {
    int inversions = 0;
    for (size_t i = 0; i < N; ++i)
        for (size_t j = i + 1; j < N; ++j)
            if (p[i] > p[j]) ++inversions;
    return inversions & 1;
}

constexpr char kFaceChars[] = "URFDLB";

}  // namespace

const char* validityMessage(Validity v) {
    switch (v) {
        case Validity::Ok:
            return "ok";
        case Validity::BadCornerSet:
            return "some corner is missing or appears twice";
        case Validity::BadEdgeSet:
            return "some edge is missing or appears twice";
        case Validity::TwistedCorner:
            return "one corner is twisted (corner orientation sum is not a multiple of 3)";
        case Validity::FlippedEdge:
            return "one edge is flipped (edge orientation sum is odd)";
        case Validity::Parity:
            return "two pieces are swapped (corner and edge parity differ)";
    }
    return "unknown";
}

CubieCube::CubieCube() {
    for (int i = 0; i < kNumCorners; ++i) cp[i] = static_cast<uint8_t>(i);
    for (int i = 0; i < kNumEdges; ++i) ep[i] = static_cast<uint8_t>(i);
}

bool CubieCube::isSolved() const {
    return *this == CubieCube();
}

bool CubieCube::operator==(const CubieCube& o) const {
    return cp == o.cp && co == o.co && ep == o.ep && eo == o.eo;
}

CubieCube CubieCube::operator*(const CubieCube& b) const {
    CubieCube r;
    for (int i = 0; i < kNumCorners; ++i) {
        r.cp[i] = cp[b.cp[i]];
        r.co[i] = static_cast<uint8_t>((co[b.cp[i]] + b.co[i]) % 3);
    }
    for (int i = 0; i < kNumEdges; ++i) {
        r.ep[i] = ep[b.ep[i]];
        r.eo[i] = static_cast<uint8_t>((eo[b.ep[i]] + b.eo[i]) % 2);
    }
    return r;
}

CubieCube CubieCube::inverse() const {
    CubieCube r;
    for (int i = 0; i < kNumCorners; ++i) {
        r.cp[cp[i]] = static_cast<uint8_t>(i);
        r.co[cp[i]] = static_cast<uint8_t>((3 - co[i]) % 3);
    }
    for (int i = 0; i < kNumEdges; ++i) {
        r.ep[ep[i]] = static_cast<uint8_t>(i);
        r.eo[ep[i]] = eo[i];
    }
    return r;
}

void CubieCube::applyMove(Move m) {
    *this = *this * moveCube(m);
}

void CubieCube::applyMoves(const MoveSeq& seq) {
    for (Move m : seq) applyMove(m);
}

int CubieCube::cornerParity() const {
    return permutationParity(cp);
}
int CubieCube::edgeParity() const {
    return permutationParity(ep);
}

Validity CubieCube::verify() const {
    std::array<int, kNumCorners> cornerCount{};
    for (uint8_t c : cp) {
        if (c >= kNumCorners) return Validity::BadCornerSet;
        ++cornerCount[c];
    }
    for (int n : cornerCount)
        if (n != 1) return Validity::BadCornerSet;

    std::array<int, kNumEdges> edgeCount{};
    for (uint8_t e : ep) {
        if (e >= kNumEdges) return Validity::BadEdgeSet;
        ++edgeCount[e];
    }
    for (int n : edgeCount)
        if (n != 1) return Validity::BadEdgeSet;

    int twist = 0;
    for (uint8_t o : co) {
        if (o > 2) return Validity::TwistedCorner;
        twist += o;
    }
    if (twist % 3 != 0) return Validity::TwistedCorner;

    int flip = 0;
    for (uint8_t o : eo) {
        if (o > 1) return Validity::FlippedEdge;
        flip += o;
    }
    if (flip % 2 != 0) return Validity::FlippedEdge;

    if (cornerParity() != edgeParity()) return Validity::Parity;
    return Validity::Ok;
}

const CubieCube& moveCube(Move m) {
    static const std::array<CubieCube, kNumMoves> moves = buildAllMoves();
    return moves.at(m);
}

MoveSeq parseMoves(std::string_view text) {
    MoveSeq seq;
    size_t i = 0;
    while (i < text.size()) {
        const char ch = text[i];
        if (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r' || ch == ',') {
            ++i;
            continue;
        }
        const char* pos = nullptr;
        for (const char* p = kFaceChars; *p; ++p)
            if (*p == ch) pos = p;
        if (!pos) {
            throw std::invalid_argument("unknown move " + describeChar(ch) + " at position " + std::to_string(i + 1) +
                                        " (use U R F D L B with optional 2 or ')");
        }
        const int face = static_cast<int>(pos - kFaceChars);
        int power = 1;
        ++i;
        if (i < text.size() && text[i] == '2') {
            power = 2;
            ++i;
            if (i < text.size() && text[i] == '\'') ++i;  // accept R2' as R2
        } else if (i < text.size() && text[i] == '\'') {
            power = 3;
            ++i;
        }
        seq.push_back(makeMove(face, power));
    }
    return seq;
}

std::string describeChar(char ch) {
    const auto u = static_cast<unsigned char>(ch);
    if (u >= 0x20 && u < 0x7F) return std::string("'") + ch + "'";
    char buf[16];
    std::snprintf(buf, sizeof buf, "byte 0x%02X", static_cast<unsigned>(u));
    return buf;
}

std::string moveName(Move m) {
    std::string s(1, kFaceChars[faceOf(m)]);
    if (powerOf(m) == 2) s += '2';
    if (powerOf(m) == 3) s += '\'';
    return s;
}

std::string formatMoves(const MoveSeq& seq) {
    std::string out;
    for (Move m : seq) {
        if (!out.empty()) out += ' ';
        out += moveName(m);
    }
    return out;
}

MoveSeq invertMoves(const MoveSeq& seq) {
    MoveSeq out(seq.rbegin(), seq.rend());
    for (Move& m : out) m = inverseMove(m);
    return out;
}

CubieCube randomCube(std::mt19937_64& rng) {
    CubieCube c;
    std::shuffle(c.cp.begin(), c.cp.end(), rng);
    std::shuffle(c.ep.begin(), c.ep.end(), rng);
    // Fixing parity with one fixed swap keeps the distribution uniform:
    // every valid state has exactly two preimages.
    if (c.cornerParity() != c.edgeParity()) std::swap(c.ep[0], c.ep[1]);

    std::uniform_int_distribution<int> twist(0, 2), flip(0, 1);
    int twistSum = 0, flipSum = 0;
    for (int i = 0; i < kNumCorners - 1; ++i) {
        c.co[i] = static_cast<uint8_t>(twist(rng));
        twistSum += c.co[i];
    }
    c.co[kNumCorners - 1] = static_cast<uint8_t>((3 - twistSum % 3) % 3);
    for (int i = 0; i < kNumEdges - 1; ++i) {
        c.eo[i] = static_cast<uint8_t>(flip(rng));
        flipSum += c.eo[i];
    }
    c.eo[kNumEdges - 1] = static_cast<uint8_t>(flipSum % 2);
    return c;
}

}  // namespace cube
