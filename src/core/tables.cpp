#include "cubesolver/tables.hpp"

#include <chrono>
#include <future>

namespace cube {

const std::array<Move, 10> kPhase2Moves = {
    makeMove(U, 1), makeMove(U, 2), makeMove(U, 3), makeMove(R, 2), makeMove(F, 2),
    makeMove(D, 1), makeMove(D, 2), makeMove(D, 3), makeMove(L, 2), makeMove(B, 2),
};

bool isPhase2Move(Move m) {
    const int face = faceOf(m);
    return face == U || face == D || powerOf(m) == 2;
}

namespace {

constexpr uint16_t kNoMove = 0xFFFF;
constexpr uint8_t kUnknown = 0xFF;

// Builds table[coord * 18 + move] by setting a cube to each coordinate value,
// applying the move at cubie level, and reading the coordinate back.
template <typename Getter, typename Setter>
std::vector<uint16_t> buildMoveTable(int size, Getter get, Setter set, bool phase2Only) {
    std::vector<uint16_t> table(static_cast<size_t>(size) * kNumMoves, kNoMove);
    for (int i = 0; i < size; ++i) {
        CubieCube c;
        set(c, i);
        for (int face = 0; face < 6; ++face) {
            CubieCube x = c;
            for (int power = 1; power <= 3; ++power) {
                x = x * moveCube(makeMove(face, 1));
                const Move m = makeMove(face, power);
                if (phase2Only && !isPhase2Move(m)) continue;
                table[static_cast<size_t>(i) * kNumMoves + m] = static_cast<uint16_t>(get(x));
            }
        }
    }
    return table;
}

// Breadth-first search over pairs (a, b) starting from (0, 0).
//
// Early layers are small, so we expand them FORWARD from a frontier list.
// Late layers hold most states, so we switch to BACKWARD filling: scan every
// unknown entry and check if one move leads to the current layer. Each unknown
// entry stops at its first hit, which is much cheaper than expanding millions
// of frontier states. (Both move sets are closed under inverses, so "one move
// away" works in both directions.)
template <size_t NumMoves>
std::vector<uint8_t> buildPruneTable(int sizeA, int sizeB, const std::vector<uint16_t>& moveA,
                                     const std::vector<uint16_t>& moveB, const std::array<Move, NumMoves>& moves) {
    const size_t total = static_cast<size_t>(sizeA) * static_cast<size_t>(sizeB);
    std::vector<uint8_t> table(total, kUnknown);
    auto neighbor = [&](size_t idx, Move m) {
        const size_t a = idx % static_cast<size_t>(sizeA);
        const size_t b = idx / static_cast<size_t>(sizeA);
        const size_t na = moveA[a * kNumMoves + m];
        const size_t nb = moveB[b * kNumMoves + m];
        return nb * static_cast<size_t>(sizeA) + na;
    };

    std::vector<uint32_t> frontier{0}, next;
    table[0] = 0;
    size_t filled = 1;
    bool backward = false;
    for (uint8_t depth = 0;; ++depth) {
        const uint8_t newDepth = static_cast<uint8_t>(depth + 1);
        size_t added = 0;
        if (!backward && filled < total / 2) {
            next.clear();
            for (uint32_t idx : frontier) {
                for (Move m : moves) {
                    const size_t n = neighbor(idx, m);
                    if (table[n] == kUnknown) {
                        table[n] = newDepth;
                        next.push_back(static_cast<uint32_t>(n));
                    }
                }
            }
            added = next.size();
            frontier.swap(next);
        } else {
            backward = true;
            for (size_t idx = 0; idx < total; ++idx) {
                if (table[idx] != kUnknown) continue;
                for (Move m : moves) {
                    if (table[neighbor(idx, m)] == depth) {
                        table[idx] = newDepth;
                        ++added;
                        break;
                    }
                }
            }
        }
        if (added == 0) break;
        filled += added;
    }
    return table;
}

std::array<Move, kNumMoves> allMoves() {
    std::array<Move, kNumMoves> moves{};
    for (int m = 0; m < kNumMoves; ++m) moves[m] = static_cast<Move>(m);
    return moves;
}

Tables buildTables() {
    const auto start = std::chrono::steady_clock::now();
    Tables t;
    t.twistMove = buildMoveTable(kNumTwist, getTwist, setTwist, false);
    t.flipMove = buildMoveTable(kNumFlip, getFlip, setFlip, false);
    t.sliceMove = buildMoveTable(kNumSlice, getSlice, setSlice, false);
    t.cornersMove = buildMoveTable(kNumCornersPerm, getCornersPerm, setCornersPerm, false);
    t.udEdgesMove = buildMoveTable(kNumUdEdges, getUdEdges, setUdEdges, true);
    t.slicePermMove = buildMoveTable(kNumSlicePerm, getSlicePerm, setSlicePerm, true);

    // The five pruning tables are independent, so build them in parallel.
    const auto all = allMoves();
    auto twistSlice = std::async(std::launch::async,
                                 [&] { return buildPruneTable(kNumTwist, kNumSlice, t.twistMove, t.sliceMove, all); });
    auto flipSlice = std::async(std::launch::async,
                                [&] { return buildPruneTable(kNumFlip, kNumSlice, t.flipMove, t.sliceMove, all); });
    auto twistFlip = std::async(std::launch::async,
                                [&] { return buildPruneTable(kNumTwist, kNumFlip, t.twistMove, t.flipMove, all); });
    auto cornersSlice = std::async(std::launch::async, [&] {
        return buildPruneTable(kNumCornersPerm, kNumSlicePerm, t.cornersMove, t.slicePermMove, kPhase2Moves);
    });
    auto edgesSlice = std::async(std::launch::async, [&] {
        return buildPruneTable(kNumUdEdges, kNumSlicePerm, t.udEdgesMove, t.slicePermMove, kPhase2Moves);
    });
    t.twistSlicePrune = twistSlice.get();
    t.flipSlicePrune = flipSlice.get();
    t.twistFlipPrune = twistFlip.get();
    t.cornersSlicePrune = cornersSlice.get();
    t.edgesSlicePrune = edgesSlice.get();

    t.buildMilliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    return t;
}

}  // namespace

const Tables& tables() {
    static const Tables t = buildTables();
    return t;
}

}  // namespace cube
