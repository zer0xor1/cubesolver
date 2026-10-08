#include "cubesolver/coord.hpp"

#include <array>

namespace cube {

namespace {

int binomial(int n, int k) {
    if (k < 0 || k > n) return 0;
    int r = 1;
    for (int i = 1; i <= k; ++i) r = r * (n - k + i) / i;
    return r;
}

bool isSliceEdge(uint8_t e) {
    return e >= FR;
}

}  // namespace

int permutationIndex(const uint8_t* p, int n) {
    int idx = 0;
    for (int i = 0; i < n; ++i) {
        int smaller = 0;
        for (int j = i + 1; j < n; ++j)
            if (p[j] < p[i]) ++smaller;
        idx = idx * (n - i) + smaller;
    }
    return idx;
}

void permutationFromIndex(int idx, uint8_t* p, int n) {
    std::array<int, 12> digits{};
    for (int i = n - 1; i >= 0; --i) {
        digits[i] = idx % (n - i);
        idx /= (n - i);
    }
    std::array<uint8_t, 12> pool{};
    for (int i = 0; i < n; ++i) pool[i] = static_cast<uint8_t>(i);
    int poolSize = n;
    for (int i = 0; i < n; ++i) {
        p[i] = pool[digits[i]];
        for (int k = digits[i]; k + 1 < poolSize; ++k) pool[k] = pool[k + 1];
        --poolSize;
    }
}

int getTwist(const CubieCube& c) {
    int t = 0;
    for (int i = 0; i < kNumCorners - 1; ++i) t = 3 * t + c.co[i];
    return t;
}

void setTwist(CubieCube& c, int twist) {
    int sum = 0;
    for (int i = kNumCorners - 2; i >= 0; --i) {
        c.co[i] = static_cast<uint8_t>(twist % 3);
        sum += c.co[i];
        twist /= 3;
    }
    c.co[kNumCorners - 1] = static_cast<uint8_t>((3 - sum % 3) % 3);
}

int getFlip(const CubieCube& c) {
    int f = 0;
    for (int i = 0; i < kNumEdges - 1; ++i) f = 2 * f + c.eo[i];
    return f;
}

void setFlip(CubieCube& c, int flip) {
    int sum = 0;
    for (int i = kNumEdges - 2; i >= 0; --i) {
        c.eo[i] = static_cast<uint8_t>(flip % 2);
        sum += c.eo[i];
        flip /= 2;
    }
    c.eo[kNumEdges - 1] = static_cast<uint8_t>(sum % 2);
}

// Rank of the set of slots holding slice edges. Slots are scanned from the
// last to the first; the solved arrangement (slots 8..11) has rank 0.
int getSlice(const CubieCube& c) {
    int a = 0, x = 0;
    for (int j = kNumEdges - 1; j >= 0; --j) {
        if (isSliceEdge(c.ep[j])) {
            a += binomial(kNumEdges - 1 - j, x + 1);
            ++x;
        }
    }
    return a;
}

void setSlice(CubieCube& c, int slice) {
    const uint8_t sliceEdges[4] = {FR, FL, BL, BR};
    const uint8_t otherEdges[8] = {UR, UF, UL, UB, DR, DF, DL, DB};
    int a = slice, x = 4, s = 0, o = 0;
    for (int j = 0; j < kNumEdges; ++j) {
        const int b = binomial(kNumEdges - 1 - j, x);
        if (x > 0 && a >= b) {
            c.ep[j] = sliceEdges[s++];
            a -= b;
            --x;
        } else {
            c.ep[j] = otherEdges[o++];
        }
    }
}

int getCornersPerm(const CubieCube& c) {
    return permutationIndex(c.cp.data(), kNumCorners);
}

void setCornersPerm(CubieCube& c, int idx) {
    permutationFromIndex(idx, c.cp.data(), kNumCorners);
}

int getUdEdges(const CubieCube& c) {
    return permutationIndex(c.ep.data(), 8);
}

void setUdEdges(CubieCube& c, int idx) {
    permutationFromIndex(idx, c.ep.data(), 8);
}

int getSlicePerm(const CubieCube& c) {
    uint8_t p[4];
    for (int i = 0; i < 4; ++i) p[i] = static_cast<uint8_t>(c.ep[8 + i] - FR);
    return permutationIndex(p, 4);
}

void setSlicePerm(CubieCube& c, int idx) {
    uint8_t p[4];
    permutationFromIndex(idx, p, 4);
    for (int i = 0; i < 4; ++i) c.ep[8 + i] = static_cast<uint8_t>(p[i] + FR);
}

}  // namespace cube
