# Interview prep

Use this to explain the project with confidence. Read [DESIGN.md](DESIGN.md) first.

## Before you put this on your resume

An interviewer will pick one part and go deep. Make sure you can do these without notes:

- Derive the multiplication rule `r.co[i] = (a.co[b.cp[i]] + b.co[i]) % 3` and explain what each index means.
- Explain why a pruning table value is a lower bound, and why the maximum of two tables is still a lower bound but the sum is not.
- Walk through one IDA* iteration on paper.
- Explain how six searches share one best length, and how threads stop.
- Change something small (for example, add a `--max-length` option) and get the tests passing again.

If you rebuild a few steps yourself from [STEP_BY_STEP.md](STEP_BY_STEP.md), these will come naturally.

## 60-second pitch

"I built a Rubik's cube solver in C++17. It uses Kociemba's two-phase algorithm. The first phase brings the cube into a subgroup where only certain moves are needed. The second phase solves it from there. Both phases are IDA* searches guided by pruning tables that I fill with breadth-first search.

I made it fast in three ways. Coordinates and move tables turn each search step into a few array lookups. Six searches run in parallel on rotated and inverted copies of the cube and share the best length found. And a backward-filling BFS halves the table build time.

On 500 random cubes it averages 19.5 moves and 26 milliseconds, and every solution is 20 moves or fewer. The project I started from averaged 23 moves and 700 milliseconds on the same machine. It also has a JSON API with an LRU cache, a 3D web demo, 39 test cases, and sanitizer runs in CI."

## Numbers to remember

| Fact | Value |
|---|---|
| Cube positions | about 4.3 x 10^19 (43,252,003,274,489,856,000) |
| Positions in G1 | 8! x 8! x 4! / 2 = about 19.5 billion |
| Phase-1 states (twist x flip x slice) | 2187 x 2048 x 495 = about 2.2 billion |
| God's number (half-turn metric) | 20 |
| Average solution length | 19.5 moves (500 random cubes) |
| Solutions of 20 moves or fewer | 100 % |
| Average / 95th percentile time | 26 ms / 51 ms (2 cores) |
| One search instead of six | 158 ms average, 95.4 % within 20 moves |
| Table memory | about 12 MB |
| Table build time | 0.25 to 0.4 s (was 0.88 s) |
| Search speed | about 135 million nodes per second (2 cores) |
| Tests | 39 test cases, 35,000+ checks |

## Resume bullets (pick one or two)

- Built a Rubik's cube solver in C++17 (Kociemba two-phase, IDA*) that solves random cubes in 19.5 moves on average, all within 20 moves, in 26 ms on average; about 27 times faster than the codebase I started from.
- Cut average solve time 6x (158 ms to 26 ms) by running six searches on rotated and inverted copies of the cube in parallel with a shared atomic bound; verified race-free with ThreadSanitizer.
- Added a JSON API (cpp-httplib) with an LRU cache and bounded concurrency, and a dependency-free 3D web demo; 39 test cases, ASan/UBSan/TSan, and GitHub Actions CI on Linux and macOS.

Measure on your own Mac before you use these numbers, and update them if they differ.

## Likely questions

**Why not just use breadth-first search?**
There are about 4.3 x 10^19 positions. Even one bit per position would need billions of gigabytes. Two-phase search splits the problem into two smaller searches that each fit in memory.

**What is G1, and why that group?**
G1 is everything you can reach with U, D, R2, L2, F2 and B2. These moves never change corner orientation or edge orientation (in Kociemba's convention), and never move the four middle-layer edges out of the middle layer. So phase 1 only needs to fix those three things: twist, flip and slice. Phase 2 then never needs a quarter turn of R, L, F or B.

**Why IDA* and not A*?**
A* stores every visited node and runs out of memory quickly at depth 10 to 12. IDA* is depth-first, so memory is just the current path (about 20 moves). It repeats some work across iterations, but each iteration is dominated by the last one, because the tree grows by a factor of about 13 per level.

**What makes your heuristic admissible?**
Each pruning table holds the exact distance for a simpler problem: fixing only two coordinates. Any full solution also fixes those two coordinates, so the true distance is at least the table value. The maximum of several lower bounds is still a lower bound.

**Why the maximum and not the sum of the tables?**
One move changes both coordinate pairs at the same time. If the sum were used, a single move would be counted twice, so the sum can overestimate. An overestimate can skip the best solution.

**Why does your solver not always find the optimal solution?**
Two-phase search finds the best split it sees within the time limit, not the global optimum. Proving optimality needs a much stronger heuristic, like Korf's 88-million-entry corner database. For short scrambles the full two-phase search finishes in a few milliseconds, and the result is optimal.

**How do the six parallel searches work?**
Three copies of the cube are rotated by 0, 120 and 240 degrees around a corner diagonal, so phase 1 targets a different axis each time. Each copy is also searched as its inverse, since a solution of the inverse, reversed and inverted, solves the cube. All six share an atomic best length. When one finds a shorter solution, the others immediately limit their phase-2 search to beat it.

**Why does it help on a 2-core machine?**
It works like a portfolio. The time to the first good solution varies a lot between orientations. With six searches, the luckiest one sets a tight bound for all the others early.

**How do threads stop?**
An atomic stop flag. Each search checks it at every node and checks the clock every 1,024 nodes. It stops when a solution within the target exists and the polish time has passed, or when the time limit is over. Threads are joined before the function returns.

**How did you check for data races?**
The shared state is one `std::atomic<int>` (best length), one `std::atomic<bool>` (stop) and a mutex around the best move list. The tables are read-only after start-up. The test suite runs under ThreadSanitizer, including a test that runs four solves at once and an end-to-end HTTP test.

**How do you know the solver is correct?**
Every returned solution is applied to the input cube before it is returned. The tests also use known group facts (`R U` has order 105), strings from Kociemba's reference implementation, a check of every coordinate value, and a check that the pruning tables never overestimate on random sequences.

**What was the hardest bug?**
Good stories from this project:
- A solved `R` came back as an 8-move solution. The search stopped at the first answer within 20 moves. The fix was a short polish window: if the search can finish in 20 ms, it returns the shortest answer.
- A test of move orders could not detect multiplication in the wrong order, because `R U` and `U R` have the same order. A test with an exact expected state after `R U` catches it.
- The web demo flashed for one frame after each turn. The renderer drew the old stickers unrotated before the new state arrived. The fix was to draw the final frame fully rotated, then swap.

**How did you make start-up faster?**
Profiling showed one table (twist x flip, 4.5 million entries) took 560 of 876 ms. Late BFS layers contain most states, so expanding them forward is wasteful. Once the table is half full, the build fills the empty entries backward instead. The five tables are also built in parallel. The result is byte-for-byte identical.

**Why is the solve endpoint a GET?**
Solving is a pure function of the input: the same cube and settings give an equally good answer. GET with query parameters makes that clear, and makes the response cacheable.

**Why an LRU cache and a semaphore?**
The cache makes repeated requests (for example, a page refresh) instant, with O(1) get and put. The semaphore limits concurrent solves to four, because each solve already uses six threads. Without it, many requests at once would fight for the CPU and every request would get slow. A request that cannot get a slot within 5 seconds gets 503 (load shedding), so the server never builds an endless queue.

**How would you scale the API?**
The server keeps no user state, so many copies can run behind a load balancer. The cache could move to a shared store like Redis. The tables could be loaded from a file, or shared between processes with memory mapping, to make start-up instant. A rate limit per client would protect against abuse.

**What would you do next?**
Use cube symmetries to shrink a stronger phase-1 table, as Kociemba's own program does. Compile to WebAssembly for a public demo link. Add a Korf-style optimal solver for short scrambles.

**Did you use AI tools?**
Answer honestly. A good answer: "I used Claude Code to speed up writing code. I designed the tests around known facts, checked every result against a reference implementation, measured the performance myself, and I can walk you through any part of the code." Then be ready to prove the last part.
