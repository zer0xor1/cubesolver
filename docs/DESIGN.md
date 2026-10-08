# Design notes

This file explains how the solver works and why each design choice was made. The numbers come from a 2-core cloud machine (Intel Xeon, 2.8 GHz) unless noted.

## Architecture

```text
            +-------------------+        +-------------------------+
 browser -> | web/ (HTML, JS)   |  HTTP  | src/server              |
            | sticker model,    | -----> | cpp-httplib, JSON API,  |
            | canvas renderer   |        | LRU cache, semaphore    |
            +-------------------+        +-----------+-------------+
                                                     |
 terminal -> src/cli (solve, scramble, bench) -------+
                                                     v
            +---------------------------------------------------------+
            | src/core: the solver library (no dependencies)          |
            |                                                         |
            |  cubie.cpp    cube model, moves, parsing, random cubes  |
            |  facelet.cpp  54-sticker strings, validation            |
            |  coord.cpp    coordinates (small integers)              |
            |  tables.cpp   move tables + pruning tables (built once) |
            |  solver.cpp   two-phase IDA*, 6 parallel searches       |
            +---------------------------------------------------------+
```

The core library has no dependencies beyond the C++17 standard library. The server and the tests add one single-header library each.

## 1. Cube model

A cube state is `cp[8], co[8], ep[12], eo[12]`: for each slot, which piece is there and how it is turned. This is the standard "cubie" model.

A move is stored as the cube state it produces from solved. Applying move M to cube C is the product `C * M`:

```text
(A * B).cp[i] = A.cp[B.cp[i]]
(A * B).co[i] = (A.co[B.cp[i]] + B.co[i]) mod 3
```

**Why:** only six tables (U, R, F, D, L, B) are written by hand. U2 is `U * U` and U' is `U * U * U`, so all 18 moves come for free. The original project wrote separate swap maps for each direction, which doubles the chances of a mistake.

**How we know it is right:** group-theory facts make strong tests. `R U` has order 105, `R U R' U'` has order 6, and `R U2 D' B D'` has order 1260 (the largest possible). A fixed expected state after `R U` catches the one bug the order tests miss: multiplying in the wrong order.

## 2. Coordinates and move tables

The search would be slow if it copied 40-byte cube states. Instead it uses six coordinates:

| Phase | Coordinate | Values | Solved value |
|---|---|---|---|
| 1 | twist (corner orientation) | 2,187 | 0 |
| 1 | flip (edge orientation) | 2,048 | 0 |
| 1 | slice (where the 4 middle edges are) | 495 | 0 |
| 2 | corners (corner permutation) | 40,320 | 0 |
| 2 | udEdges (top and bottom edge permutation) | 40,320 | 0 |
| 2 | slicePerm (middle edge permutation) | 24 | 0 |

A move table answers "coordinate x after move m" with one lookup: `table[x * 18 + m]`. Tables are built by setting a cube to each coordinate value and applying the move at cubie level. Total size: about 3 MB.

## 3. Pruning tables

A pruning table for two coordinates (a, b) stores the exact number of moves needed to bring both to zero. It is filled by breadth-first search from (0, 0).

Solving the whole cube also solves those two coordinates, so the table value is a **lower bound** on the true distance. That makes it an admissible heuristic for IDA*: the search can skip a branch whenever `moves so far + bound > depth limit` without ever missing a solution.

| Table | Entries | Max depth |
|---|---|---|
| twist x slice | 1,082,565 | 9 |
| flip x slice | 1,013,760 | 9 |
| twist x flip | 4,478,976 | 9 |
| corners x slicePerm (phase 2) | 967,680 | 14 |
| udEdges x slicePerm (phase 2) | 967,680 | 12 |

The heuristic is the maximum of the tables for the current phase. One byte per entry, about 8.5 MB in total.

**Trade-off: the twist x flip table.** It costs 4.5 MB and was the slowest table to build (560 ms of 876 ms). Without it, the average solve time went from 26.6 ms to 31.3 ms and the slowest cube from 148 ms to 240 ms (200 cubes, 6 searches). It stays, and the build was made faster instead (section 6).

## 4. Two-phase search

```text
cube --phase 1 (all 18 moves)--> G1 = <U, D, R2, L2, F2, B2> --phase 2 (10 moves)--> solved
          twist, flip, slice = 0                 corners, udEdges, slicePerm = 0
```

Both phases use IDA*: depth-first search with an increasing depth limit, pruned by the tables. Depth-first search needs almost no memory, and the tables do the heavy lifting.

Rules that remove duplicate work:

- Never turn the same face twice in a row (`R R2` is just `R'`).
- Opposite faces commute (`U D` = `D U`), so only one order is searched.
- A phase-1 path that ends in a G1 move is skipped, because its shorter prefix already reached G1 and phase 2 will find the same total.
- The first phase-2 move follows the same rules against the last phase-1 move.

**Finding short solutions.** The first solution is usually 21 to 24 moves. The search then continues with longer phase-1 paths, each followed by a phase-2 search limited to `best - 1 - phase1Length` moves. This bound gets tighter with every improvement, so most phase-2 searches end at once.

**When to stop.** The search stops when:

1. a solution of at most `targetLength` moves (default 20) is found **and** at least `polishMs` (default 20 ms) have passed, or
2. `timeoutMs` (default 1000 ms) has passed and some solution exists, or
3. no shorter solution is possible (every phase-1 length below the best has been searched).

The polish window matters for short scrambles. Without it, `R` was solved as `R U2 D2 R2 L2 U2 D2 L2` (8 moves), because that was the first answer within 20 moves. With it, the search finishes completely in well under a millisecond and returns `R'`. In tests, scrambles of up to 9 moves finished the full search within 1 ms, and scrambles of 10 to 11 moves within about 30 ms.

**Safety.** Every solution is applied to the input cube before it is returned. Invalid cubes are rejected before the search, because an unsolvable cube would make the search run forever.

## 5. Parallel search

Six searches run at the same time:

| Search | Cube searched |
|---|---|
| 0, 1, 2 | the cube rotated by 0, 120, 240 degrees about the URF-DBL diagonal |
| 3, 4, 5 | the inverse of each of those |

A rotation changes which axis phase 1 aims for (UD, RL or FB), and the inverse has the same distance but a different search tree. So the six searches explore different parts of the space.

They share an `std::atomic<int>` best length. When one finds a 21-move solution, all others immediately limit phase 2 to shorter totals. The best solution itself is guarded by a mutex (it changes rarely).

Mapping back: if moves M1..Mn solve the rotated cube `S^-1 C S`, then `S Mi S^-1` solve C. A small table maps each of the 18 moves; it is built and checked at start-up. For the inverse cube, the solution is reversed and each move inverted.

Results (500 cubes, target 20):

| Searches | Average length | Within 20 | Average time | 95th percentile |
|---|---|---|---|---|
| 1 | 19.78 | 95.4 % | 158 ms | 953 ms |
| 6 | 19.48 | 100 % | 26 ms | 51 ms |

Six searches help even on 2 cores. It works like a portfolio: whichever search gets lucky first sets a tight bound for all the others.

## 6. Fast table build

Two changes cut start-up from 876 ms to about 250 to 400 ms on 2 cores:

1. **Backward filling.** Breadth-first search expands the frontier forward while the table is less than half full. After that, it scans the empty entries instead and asks: "does one move lead to the layer we just finished?" Each empty entry stops at its first hit, so late layers cost much less. The move sets are closed under inverses, so "one move away" works in both directions.
2. **Parallel build.** The five pruning tables are independent and are built with `std::async`.

The new tables were checked to be byte-for-byte identical to the old ones.

Why not save the tables to a file? At 0.25 to 0.4 s the build is fast enough, and a cache file adds versioning and corruption problems. This is a reasonable next step if start-up time ever matters more.

## 7. HTTP API

- **cpp-httplib** (one header) keeps the build simple: no package manager is needed.
- **GET for solve.** Solving is a pure function of the cube and the settings, so the request is a GET with query parameters, and results are cached.
- **LRU cache.** A hash map pointing into a linked list gives O(1) get and put. Key: sticker string + target + timeout. Capacity 2048 entries.
- **Bounded concurrency.** Each solve already uses 6 threads, so a counting semaphore allows at most 4 solves at once. Extra requests wait up to 5 seconds for a slot, then get 503. This sheds load instead of building an endless queue. The worker pool has 16 threads, so cheap requests like `/api/health` are not stuck behind slow solves.
- **Exclusive port binding.** cpp-httplib sets `SO_REUSEPORT` by default, which let a second server start on a port that was already in use and silently share it. The server sets only `SO_REUSEADDR`, binds first, and prints the URL only after the bind succeeded.
- **Errors.** Invalid input returns 400 with a message a person can act on. Unknown `/api` paths return JSON 404, and uncaught exceptions return JSON 500.
- **Thread safety.** The tables are read-only after start-up, so all request threads share them without locks. ThreadSanitizer runs over the server tests.

## 8. Web demo

- **No frameworks or build step.** Plain ES modules, so the folder can be served as is.
- **Sticker model.** The browser keeps 54 stickers, each with a position, a normal and a color. A clockwise quarter turn about face normal `a` maps every vector `v` in that layer to `v x a + (v . a) a`. `tools/check_web_model.mjs` compares this model with the C++ solver on random scrambles as part of `ctest`.
- **Renderer.** About 260 lines on a 2D canvas: perspective projection, back-face culling, and painting from back to front. During a turn, the cube is split into the turning layer and the rest. Both blocks are convex and separated by a plane, so painting the block on the far side of that plane first is always correct.
- **Accessibility.** Keyboard controls for every action, visible focus, `prefers-reduced-motion` respected, and a layout that works at phone width.

## 9. Testing strategy

| Level | What is tested |
|---|---|
| Cube model | group facts (orders 105, 6, 1260), exact state after `R U`, invariants over 100,000 random moves, inverse |
| Stickers | strings from Kociemba's reference implementation, round trips, every kind of invalid input |
| Coordinates | every value round trips; move tables match cubie moves for random cubes |
| Pruning | every entry filled; bound never exceeds the true length of random sequences |
| Solver | solved cube, all single moves, short scrambles optimal, random cubes within 22 moves, superflip, zero time limit, invalid cubes, 4 solves at once |
| API | LRU behavior, JSON escaping, every 400 case, 503 when overloaded, cache hits, a second server cannot share the port, a real HTTP server end to end |
| Web | JavaScript move model equals the C++ model |
| Tools | AddressSanitizer + UBSan, ThreadSanitizer, `-Wall -Wextra -Wpedantic -Wshadow -Wconversion` with zero warnings, CI on Linux and macOS |

## Known limits

- Solutions are near-optimal, not proven optimal. Random cubes average 19.5 moves; the true optimum averages about 18. Superflip (optimal 20) gets 21 moves in 3 seconds. It is fully symmetric, so the six parallel searches collapse into one.
- The twist and flip tables do not use cube symmetry. Kociemba's own program does, and finds 20-move solutions faster.
- The server is meant for local use and demos. A public deployment would also need rate limiting and request logging.
