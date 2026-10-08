#include "cubesolver/solver.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "cubesolver/coord.hpp"
#include "cubesolver/tables.hpp"

namespace cube {

namespace {

using Clock = std::chrono::steady_clock;

constexpr int kNoSolution = 99;
constexpr int kMaxPhase1Depth = 20;
constexpr int kMaxPhase2Depth = 18;  // every G1 state is solvable in 18 phase-2 moves
constexpr int kMaxSearches = 6;      // 3 rotations x {cube, inverse}

// ---------------------------------------------------------------------------
// Whole-cube rotation by 120 degrees about the URF-DBL diagonal.
// Searching a rotated (conjugated) cube explores different phase-1 subgroups
// (UD, RL and FB axis), which often finds shorter solutions sooner.
struct Symmetry {
    std::array<CubieCube, 3> rot;     // rot[k] = S^k
    std::array<CubieCube, 3> rotInv;  // (S^k)^-1
    // A move found for the rotated cube, translated back to the original cube.
    std::array<std::array<Move, kNumMoves>, 3> mapBack{};
};

Symmetry buildSymmetry() {
    Symmetry s;
    CubieCube S;
    S.cp = {URF, DFR, DLF, UFL, UBR, DRB, DBL, ULB};
    S.co = {1, 2, 1, 2, 2, 1, 2, 1};
    S.ep = {UF, FR, DF, FL, UB, BR, DB, BL, UR, DR, DL, UL};
    S.eo = {1, 0, 1, 0, 1, 0, 1, 0, 1, 1, 1, 1};
    s.rot[0] = CubieCube();
    s.rot[1] = S;
    s.rot[2] = S * S;
    for (int k = 0; k < 3; ++k) s.rotInv[k] = s.rot[k].inverse();

    // If moves M1..Mn solve S^-1 C S, then (S M1 S^-1)..(S Mn S^-1) solve C.
    for (int k = 0; k < 3; ++k) {
        for (int m = 0; m < kNumMoves; ++m) {
            const CubieCube target = s.rot[k] * moveCube(static_cast<Move>(m)) * s.rotInv[k];
            bool found = false;
            for (int m2 = 0; m2 < kNumMoves && !found; ++m2) {
                if (moveCube(static_cast<Move>(m2)) == target) {
                    s.mapBack[k][m] = static_cast<Move>(m2);
                    found = true;
                }
            }
            if (!found) throw std::logic_error("symmetry table is inconsistent");
        }
    }
    return s;
}

const Symmetry& symmetry() {
    static const Symmetry s = buildSymmetry();
    return s;
}

// ---------------------------------------------------------------------------
// State shared by all parallel searches.
struct SharedState {
    SharedState(int targetLength, Clock::time_point earliestStop, Clock::time_point stopTime)
        : target(targetLength), polishEnd(earliestStop), deadline(stopTime) {}

    // True when the best solution is good enough to stop now.
    bool doneAt(Clock::time_point now) const {
        const int length = bestLength.load(std::memory_order_relaxed);
        if (length >= kNoSolution) return false;  // always find at least one solution
        return now >= deadline || (length <= target && now >= polishEnd);
    }

    std::atomic<int> bestLength{kNoSolution};
    std::atomic<bool> stop{false};
    std::mutex mutex;  // guards best
    MoveSeq best;
    const int target;
    const Clock::time_point polishEnd;
    const Clock::time_point deadline;
};

// One IDA* two-phase search on one orientation of the cube.
class Search {
public:
    Search(const Tables& t, SharedState& shared, const CubieCube& original, int rotation, bool inverse)
        : t_(t), shared_(shared), rotation_(rotation), inverse_(inverse) {
        const Symmetry& sym = symmetry();
        cube_ = sym.rotInv[rotation] * original * sym.rot[rotation];
        if (inverse) cube_ = cube_.inverse();
    }

    void run() {
        const int twist = getTwist(cube_);
        const int flip = getFlip(cube_);
        const int slice = getSlice(cube_);
        const int start = t_.phase1Distance(twist, flip, slice);
        for (int depth = start; depth <= kMaxPhase1Depth; ++depth) {
            // A phase-1 path of this length cannot beat the best total any more.
            if (depth >= shared_.bestLength.load(std::memory_order_relaxed)) return;
            if (phase1(twist, flip, slice, 0, depth)) return;
        }
    }

    uint64_t nodes() const { return nodes_; }
    const CubieCube& searchedCube() const { return cube_; }

private:
    // Two rules remove sequences that are equivalent to shorter ones:
    // never turn the same face twice in a row, and for opposite faces
    // (U/D, R/L, F/B) allow only one order (U then D, never D then U).
    static bool skipFace(int face, int lastFace) { return face == lastFace || face + 3 == lastFace; }

    bool shouldStop() {
        if (shared_.stop.load(std::memory_order_relaxed)) return true;
        if ((++stopCheck_ & 1023) == 0 && shared_.doneAt(Clock::now())) {
            shared_.stop.store(true);
            return true;
        }
        return false;
    }

    // Returns true when the whole search must stop.
    bool phase1(int twist, int flip, int slice, int depth, int togo) {
        if (togo == 0) {
            // A path ending in a G1 move is redundant: its prefix already reached G1.
            if (depth > 0 && isPhase2Move(path1_[depth - 1])) return false;
            return startPhase2(depth);
        }
        if (shouldStop()) return true;
        const int lastFace = depth > 0 ? faceOf(path1_[depth - 1]) : -1;
        for (int m = 0; m < kNumMoves; ++m) {
            if (skipFace(m / 3, lastFace)) continue;
            const int nt = t_.twistMove[twist * kNumMoves + m];
            const int nf = t_.flipMove[flip * kNumMoves + m];
            const int ns = t_.sliceMove[slice * kNumMoves + m];
            ++nodes_;
            if (t_.phase1Distance(nt, nf, ns) >= togo) continue;  // cannot reach G1 in time
            path1_[depth] = static_cast<Move>(m);
            if (phase1(nt, nf, ns, depth + 1, togo - 1)) return true;
        }
        return false;
    }

    bool startPhase2(int depth1) {
        CubieCube c = cube_;
        for (int i = 0; i < depth1; ++i) c.applyMove(path1_[i]);
        const int corners = getCornersPerm(c);
        const int udEdges = getUdEdges(c);
        const int slicePerm = getSlicePerm(c);

        const int maxDepth2 =
            std::min(kMaxPhase2Depth, shared_.bestLength.load(std::memory_order_relaxed) - 1 - depth1);
        const int start = t_.phase2Distance(corners, udEdges, slicePerm);
        if (start > maxDepth2) return false;

        const int lastFace = depth1 > 0 ? faceOf(path1_[depth1 - 1]) : -1;
        for (int depth2 = start; depth2 <= maxDepth2; ++depth2) {
            if (phase2(corners, udEdges, slicePerm, 0, depth2, lastFace)) {
                record(depth1, depth2);
                return shared_.stop.load(std::memory_order_relaxed);
            }
            if (aborted_) return true;
        }
        return false;
    }

    bool phase2(int corners, int udEdges, int slicePerm, int depth, int togo, int lastPhase1Face) {
        if (togo == 0) return corners == 0 && udEdges == 0 && slicePerm == 0;
        if (shouldStop()) {
            aborted_ = true;
            return false;
        }
        const int lastFace = depth > 0 ? faceOf(path2_[depth - 1]) : lastPhase1Face;
        for (Move m : kPhase2Moves) {
            if (skipFace(m / 3, lastFace)) continue;
            const int nc = t_.cornersMove[corners * kNumMoves + m];
            const int ne = t_.udEdgesMove[udEdges * kNumMoves + m];
            const int ns = t_.slicePermMove[slicePerm * kNumMoves + m];
            ++nodes_;
            if (t_.phase2Distance(nc, ne, ns) >= togo) continue;
            path2_[depth] = m;
            if (phase2(nc, ne, ns, depth + 1, togo - 1, lastPhase1Face)) return true;
            if (aborted_) return false;
        }
        return false;
    }

    void record(int depth1, int depth2) {
        MoveSeq seq(path1_, path1_ + depth1);
        seq.insert(seq.end(), path2_, path2_ + depth2);
        if (inverse_) seq = invertMoves(seq);
        for (Move& m : seq) m = symmetry().mapBack[rotation_][m];

        const int length = depth1 + depth2;
        std::lock_guard<std::mutex> lock(shared_.mutex);
        if (length < shared_.bestLength.load()) {
            shared_.best = std::move(seq);
            shared_.bestLength.store(length);
            if (shared_.doneAt(Clock::now())) shared_.stop.store(true);
        }
    }

    const Tables& t_;
    SharedState& shared_;
    CubieCube cube_;
    const int rotation_;
    const bool inverse_;
    Move path1_[kMaxPhase1Depth + 1]{};
    Move path2_[kMaxPhase2Depth + 1]{};
    uint64_t nodes_ = 0;
    uint32_t stopCheck_ = 0;
    bool aborted_ = false;
};

}  // namespace

SolveResult solve(const CubieCube& cube, const SolveOptions& options) {
    const Validity v = cube.verify();
    if (v != Validity::Ok) throw InvalidCubeError(std::string("unsolvable cube: ") + validityMessage(v));

    const Tables& t = tables();  // built once; not counted in the solve time
    symmetry();
    const auto start = Clock::now();

    SolveResult result;
    if (cube.isSolved()) {
        result.targetReached = true;
        return result;
    }

    // Default: all 6 searches. They help even with fewer CPU cores, because
    // each search prunes with the best length the others have found.
    const int searches = std::clamp(options.threads > 0 ? options.threads : kMaxSearches, 1, kMaxSearches);

    const int timeoutMs = std::max(0, options.timeoutMs);
    const int polishMs = std::clamp(options.polishMs, 0, timeoutMs);
    SharedState shared(options.targetLength, start + std::chrono::milliseconds(polishMs),
                       start + std::chrono::milliseconds(timeoutMs));
    std::vector<std::unique_ptr<Search>> workers;
    for (int i = 0; i < searches; ++i) {
        auto w = std::make_unique<Search>(t, shared, cube, i % 3, i >= 3);
        // A symmetric cube (like the superflip) looks the same after rotation
        // or inversion. Searching an identical cube twice is wasted work.
        bool duplicate = false;
        for (const auto& other : workers) duplicate = duplicate || other->searchedCube() == w->searchedCube();
        if (!duplicate) workers.push_back(std::move(w));
    }
    result.searches = static_cast<int>(workers.size());
    if (workers.size() == 1) {
        workers[0]->run();
    } else {
        std::vector<std::thread> threads;
        for (auto& w : workers) threads.emplace_back([&w] { w->run(); });
        for (auto& th : threads) th.join();
    }

    result.moves = shared.best;
    for (const auto& w : workers) result.nodes += w->nodes();
    result.targetReached = shared.bestLength.load() <= options.targetLength;
    result.milliseconds = std::chrono::duration<double, std::milli>(Clock::now() - start).count();

    // Never return an unchecked answer.
    CubieCube check = cube;
    check.applyMoves(result.moves);
    if (!check.isSolved()) throw std::logic_error("internal error: solver produced an invalid solution");
    return result;
}

Scramble randomScramble(std::mt19937_64& rng, const SolveOptions& options) {
    Scramble s;
    s.cube = randomCube(rng);
    s.moves = invertMoves(solve(s.cube, options).moves);
    return s;
}

}  // namespace cube
