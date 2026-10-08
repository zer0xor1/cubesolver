// Command-line front end: solve, scramble and benchmark.
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <map>
#include <numeric>
#include <random>
#include <string>
#include <vector>

#include "cubesolver/cubie.hpp"
#include "cubesolver/facelet.hpp"
#include "cubesolver/solver.hpp"
#include "cubesolver/tables.hpp"

namespace {

constexpr const char* kUsage = R"(cubesolver - fast 3x3 Rubik's cube solver (Kociemba two-phase)

Usage:
  cubesolver solve "<scramble>"            solve the cube made by this scramble
  cubesolver solve --facelets <54 chars>   solve a cube given by its stickers
  cubesolver scramble [--count N]          print random-state scrambles
  cubesolver bench [--count N]             solve N random cubes and report stats
  cubesolver facelets "<scramble>"         print the 54-sticker string a scramble makes

Options:
  --target N     stop when a solution of N moves or fewer is found (default 20)
  --timeout MS   return the best solution found after MS milliseconds (default 1000)
  --polish MS    keep looking for a shorter solution for at least MS milliseconds
                 (default 20; short scrambles get their optimal answer in this time)
  --threads N    parallel searches, 1..6 (default 6)
  --seed N       random seed for scramble and bench (default: random)
  -h, --help     show this help

Examples:
  cubesolver solve "R U R' U' F2 D L2 B"
  cubesolver solve --facelets UUUUUUUUURRRRRRRRRFFFFFFFFFDDDDDDDDDLLLLLLLLLBBBBBBBBB
  cubesolver bench --count 200 --threads 6
)";

struct Args {
    std::string command;
    std::string scramble;
    std::string facelets;
    int count = -1;
    uint64_t seed = 0;
    bool hasSeed = false;
    cube::SolveOptions options;
};

[[noreturn]] void fail(const std::string& message) {
    std::fprintf(stderr, "error: %s\n\nRun 'cubesolver --help' for usage.\n", message.c_str());
    std::exit(2);
}

int parseInt(const std::string& name, const char* value, int lo, int hi) {
    char* end = nullptr;
    const long v = std::strtol(value, &end, 10);
    if (end == value || *end != '\0' || v < lo || v > hi) {
        fail(name + " must be a whole number between " + std::to_string(lo) + " and " + std::to_string(hi));
    }
    return static_cast<int>(v);
}

Args parseArgs(int argc, char** argv) {
    Args a;
    std::vector<std::string> positional;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto value = [&]() -> const char* {
            if (i + 1 >= argc) fail(arg + " needs a value");
            return argv[++i];
        };
        if (arg == "-h" || arg == "--help") {
            std::fputs(kUsage, stdout);
            std::exit(0);
        } else if (arg == "--facelets") {
            a.facelets = value();
        } else if (arg == "--count") {
            a.count = parseInt(arg, value(), 1, 1000000);
        } else if (arg == "--target") {
            a.options.targetLength = parseInt(arg, value(), 0, 30);
        } else if (arg == "--polish") {
            a.options.polishMs = parseInt(arg, value(), 0, 600000);
        } else if (arg == "--timeout") {
            a.options.timeoutMs = parseInt(arg, value(), 0, 600000);
        } else if (arg == "--threads") {
            a.options.threads = parseInt(arg, value(), 1, 6);
        } else if (arg == "--seed") {
            a.seed = static_cast<uint64_t>(std::strtoull(value(), nullptr, 10));
            a.hasSeed = true;
        } else if (!arg.empty() && arg[0] == '-' && arg.size() > 1 && arg[1] == '-') {
            fail("unknown option " + arg);
        } else {
            positional.push_back(arg);
        }
    }
    if (positional.empty()) {
        std::fputs(kUsage, stdout);
        std::exit(0);
    }
    a.command = positional[0];
    for (size_t i = 1; i < positional.size(); ++i) a.scramble += (i > 1 ? " " : "") + positional[i];
    return a;
}

double percentile(std::vector<double> v, double p) {
    if (v.empty()) return 0;
    std::sort(v.begin(), v.end());
    const size_t idx = static_cast<size_t>(p * static_cast<double>(v.size() - 1) + 0.5);
    return v[std::min(idx, v.size() - 1)];
}

int runSolve(const Args& a) {
    cube::CubieCube c;
    if (!a.facelets.empty()) {
        c = cube::fromFacelets(a.facelets);
    } else if (!a.scramble.empty()) {
        c.applyMoves(cube::parseMoves(a.scramble));
    } else {
        fail("give a scramble in quotes, or --facelets");
    }
    std::printf("Building tables... ");
    std::fflush(stdout);
    std::printf("done in %.0f ms\n", cube::tables().buildMilliseconds);

    const cube::SolveResult r = cube::solve(c, a.options);
    std::printf("Solution (%zu moves): %s\n", r.moves.size(), cube::formatMoves(r.moves).c_str());
    std::printf("Time: %.1f ms | nodes: %llu | parallel searches: %d%s\n", r.milliseconds,
                static_cast<unsigned long long>(r.nodes), r.searches,
                r.targetReached ? "" : " | target not reached (time limit)");
    return 0;
}

int runScramble(const Args& a) {
    std::mt19937_64 rng(a.hasSeed ? a.seed : std::random_device{}());
    const int count = a.count > 0 ? a.count : 1;
    cube::SolveOptions opt = a.options;
    for (int i = 0; i < count; ++i) {
        const cube::Scramble s = cube::randomScramble(rng, opt);
        std::printf("%s\n", cube::formatMoves(s.moves).c_str());
    }
    return 0;
}

int runFacelets(const Args& a) {
    cube::CubieCube c;
    c.applyMoves(cube::parseMoves(a.scramble));
    std::printf("%s\n", cube::toFacelets(c).c_str());
    return 0;
}

int runBench(const Args& a) {
    const int count = a.count > 0 ? a.count : 100;
    const uint64_t seed = a.hasSeed ? a.seed : 2026;
    std::mt19937_64 rng(seed);

    std::printf("Building tables... ");
    std::fflush(stdout);
    std::printf("done in %.0f ms\n", cube::tables().buildMilliseconds);

    std::vector<double> lengths, times;
    std::map<size_t, int> histogram;
    uint64_t nodes = 0;
    int reached = 0, searches = 0;
    for (int i = 0; i < count; ++i) {
        const cube::CubieCube c = cube::randomCube(rng);
        const cube::SolveResult r = cube::solve(c, a.options);
        lengths.push_back(static_cast<double>(r.moves.size()));
        times.push_back(r.milliseconds);
        ++histogram[r.moves.size()];
        nodes += r.nodes;
        reached += r.targetReached ? 1 : 0;
        searches = r.searches;
        if ((i + 1) % 10 == 0 || i + 1 == count) {
            std::fprintf(stderr, "\r  solved %d / %d", i + 1, count);
        }
    }
    std::fprintf(stderr, "\n");

    const double totalMs = std::accumulate(times.begin(), times.end(), 0.0);
    const double avgLen = std::accumulate(lengths.begin(), lengths.end(), 0.0) / count;
    std::printf("\nRandom-state cubes: %d (seed %llu)\n", count, static_cast<unsigned long long>(seed));
    std::printf("Settings: target %d moves, polish %d ms, timeout %d ms, %d parallel searches\n\n",
                a.options.targetLength, a.options.polishMs, a.options.timeoutMs, searches);
    std::printf("| Metric | Value |\n|---|---|\n");
    std::printf("| Average length | %.2f moves |\n", avgLen);
    std::printf("| Max length | %.0f moves |\n", percentile(lengths, 1.0));
    std::printf("| Reached target | %.1f %% |\n", 100.0 * reached / count);
    std::printf("| Average time | %.1f ms |\n", totalMs / count);
    std::printf("| Median time | %.1f ms |\n", percentile(times, 0.5));
    std::printf("| 95th percentile time | %.1f ms |\n", percentile(times, 0.95));
    std::printf("| Max time | %.1f ms |\n", percentile(times, 1.0));
    std::printf("| Search speed | %.1f million nodes/s |\n",
                totalMs > 0 ? static_cast<double>(nodes) / totalMs / 1000.0 : 0.0);
    std::printf("\nLength histogram:\n");
    for (const auto& [len, n] : histogram) {
        std::printf("  %2zu moves: %5d  %s\n", len, n,
                    std::string(static_cast<size_t>(60.0 * n / count + 0.5), '#').c_str());
    }
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const Args a = parseArgs(argc, argv);
        if (a.command == "solve") return runSolve(a);
        if (a.command == "scramble") return runScramble(a);
        if (a.command == "bench") return runBench(a);
        if (a.command == "facelets") return runFacelets(a);
        fail("unknown command '" + a.command + "' (use solve, scramble, bench or facelets)");
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
}
