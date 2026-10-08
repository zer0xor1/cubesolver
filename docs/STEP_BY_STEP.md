# Step-by-step guide

This guide has three parts:

1. **Run this project on your Mac** with VS Code.
2. **How the project was built, step by step.** Each step has a goal, a check, and a prompt you can give Claude Code. Use it to rebuild the project yourself, or to understand every part before an interview.
3. **Ideas to go further.**

---

## Part 1: Run the project on your Mac

### 1. Install the tools

Open the Terminal app and run:

```bash
xcode-select --install
```

This installs Apple's C++ compiler (clang). If it says the tools are already installed, that is fine.

Install Homebrew if you do not have it (see https://brew.sh), then install CMake:

```bash
brew install cmake
cmake --version          # must be 3.21 or newer
```

Optional: `brew install node`. With Node.js installed, the tests also check that the web demo's cube model matches the C++ one. Without it, that one test is skipped.

### 2. Open the folder in VS Code

1. Unzip `cubesolver.zip`.
2. In VS Code, choose **File > Open Folder** and pick the `cubesolver` folder.
3. VS Code asks to install the recommended extensions. Say yes. They are:
   - **C/C++** (Microsoft): code completion and error squiggles.
   - **CMake Tools** (Microsoft): configure and build buttons.
   - **CodeLLDB**: the debugger that works best on Apple Silicon Macs.
4. CMake Tools may ask you to pick a configure preset. Choose **Release (fast)**.

### 3. Build

Press **Cmd+Shift+B**. This runs the task "Build (release)", which is the same as:

```bash
cmake --preset release
cmake --build --preset release -j
```

The programs appear in the `build/` folder:

| File | What it is |
|---|---|
| `build/cubesolver` | command-line solver and benchmark |
| `build/cubesolver_server` | web server with the 3D demo and the JSON API |
| `build/cubesolver_tests` | all unit and integration tests |

### 4. Run the tests

In the VS Code terminal (**Ctrl+`**):

```bash
ctest --preset release
```

You should see `100% tests passed`. To see each test case by name:

```bash
./build/cubesolver_tests --list-test-cases
```

### 5. Try the command line

```bash
./build/cubesolver solve "R U R' U'"
./build/cubesolver solve "R U F' D2 L B' R2 U' F D L2 B"
./build/cubesolver scramble --count 3
./build/cubesolver bench --count 200
```

`bench` prints a table you can paste into your README with numbers from your own Mac.

### 6. Run the web demo

```bash
./build/cubesolver_server
```

Then open **http://localhost:8080**. Press **Scramble**, then **Solve**, then **Play**. Stop the server with **Ctrl+C**.

You can also run it from VS Code: **Terminal > Run Task > Start web demo**.

If you edit files in `web/`, just refresh the browser. The server reads them straight from the `web/` folder.

### 7. Debug with breakpoints

1. Open `src/core/solver.cpp` and click left of a line number to set a breakpoint (for example inside `Search::record`).
2. Open the **Run and Debug** panel (Cmd+Shift+D).
3. Pick **Debug: solve a scramble** and press the green arrow.

This builds a debug version into `build-debug/` first, then stops at your breakpoint.

### 8. Common problems

| Problem | Fix |
|---|---|
| `cmake: command not found` | Run `brew install cmake`, then open a new terminal. |
| `CMake 3.21 or higher is required` | Run `brew upgrade cmake`. |
| `No CMAKE_CXX_COMPILER could be found` | Run `xcode-select --install`. |
| The page says "Can't reach the solver" | Start `./build/cubesolver_server` and open the page from `http://localhost:8080`, not by double-clicking the HTML file. |
| `Could not use 127.0.0.1:8080. Is another server running on that port?` | An older server is still running (stop it with Ctrl+C in its terminal), or another program uses the port. You can also run `./build/cubesolver_server --port 8081` and open `http://localhost:8081`. |
| Red squiggles everywhere in VS Code | Build once (Cmd+Shift+B). IntelliSense reads `build/compile_commands.json`, which the build creates. |
| A different font than in the screenshot | The page loads its font from Google Fonts. Without internet it uses your system font. Everything else works offline. |

---

## Part 2: How the project was built, step by step

Each step below is small enough for one Claude Code session. Do them in order, and commit after each step once its checks pass. **Do not move on until you can explain the step in your own words.** Interviewers will ask.

Tip: keep a file called `CLAUDE.md` in the project root (this project has one). Claude Code reads it at the start of every session, so your conventions do not get lost.

### Step 0: Project skeleton

**Goal:** an empty C++17 project that builds and runs one test.

**Build:** `CMakeLists.txt`, folders `include/`, `src/`, `tests/`, the doctest header in `third_party/`, and `CLAUDE.md`.

**Check:** `cmake --preset release && cmake --build --preset release && ctest --preset release` passes.

**Prompt:**
```text
Create a C++17 CMake project: a static library cubesolver_core (src/core, include/cubesolver),
a test executable using doctest from third_party/doctest/doctest.h, and CMakePresets.json with a
"release" preset that builds into ./build. Write CLAUDE.md with the build and test commands.
Add one trivial test and make sure ctest passes.
```

### Step 1: The cube model

**Goal:** store a cube and apply moves correctly.

**Key idea:** a cube is four arrays: `cp` (which corner is in each corner slot), `co` (its twist 0..2), `ep` and `eo` (the same for edges). A move is also a cube, so applying a move is a multiplication:

```cpp
r.cp[i] = a.cp[b.cp[i]];
r.co[i] = (a.co[b.cp[i]] + b.co[i]) % 3;   // edges: same, but % 2
```

Use Kociemba's numbering: corners `URF UFL ULB UBR DFR DLF DBL DRB`, edges `UR UF UL UB DR DF DL DB FR FL BL BR`, faces `U R F D L B`.

**Check (tests):**
- Each move done 4 times gives the solved cube.
- Repeating `R U` returns to solved after 105 times, `R U R' U'` after 6, `R U2 D' B D'` after 1260.
- The exact state after `R U` (see `tests/test_cubie.cpp`). This catches moves applied in the wrong order, which the repeat tests cannot catch.
- After 100,000 random moves: twist sum divisible by 3, flip sum even, corner and edge parity equal.

**Prompt:**
```text
Read CLAUDE.md. Implement CubieCube (cp, co, ep, eo), multiplication, inverse, the six basic
moves in Kociemba's convention, and all 18 moves. Write the tests first: move^4 = identity,
order(R U) = 105, order(R U R' U') = 6, order(R U2 D' B D') = 1260, the exact state after
"R U", and invariants after 100,000 random moves. Then implement until all pass.
```

### Step 2: Reading and writing moves

**Goal:** parse `"R U2 F'"` and print move lists.

**Check:** `"RU2F'D"` parses (spaces optional), `"R X"` and `"R3"` give a clear error, and a scramble followed by its inverse gives the solved cube.

**Prompt:**
```text
Add parseMoves (throws std::invalid_argument with a helpful message), formatMoves and
invertMoves, with tests for valid input, missing spaces, R2', and bad tokens.
```

### Step 3: Stickers (facelets) and validation

**Goal:** convert between the cube model and a 54-letter sticker string, and reject impossible cubes with a message a person understands.

**Key idea:** each corner slot has three sticker positions. Find the U or D colored sticker: its position gives the twist, and the other two colors identify the corner.

**Check:**
- `toFacelets` after `F`, after `R U`, and after the superflip sequence must equal the strings from Kociemba's reference implementation (they are in `tests/test_facelet.cpp`).
- Round trip for 1,000 random cubes.
- A twisted corner, a flipped edge, two swapped edges, and wrong color counts each give their own error message.

**Prompt:**
```text
Implement toFacelets and fromFacelets for the standard URFDLB facelet order. fromFacelets must
throw InvalidCubeError with a clear message for: wrong length, bad characters, wrong color
counts, wrong centers, impossible corners or edges, twisted corner, flipped edge, parity.
Use the expected strings in my test file as ground truth.
```

### Step 4: Coordinates and move tables

**Goal:** describe parts of the cube as small integers and precompute how each move changes them.

| Coordinate | Range | Meaning |
|---|---|---|
| twist | 0..2186 | corner orientations |
| flip | 0..2047 | edge orientations |
| slice | 0..494 | which 4 slots hold the middle-layer edges |
| corners | 0..40319 | corner permutation |
| udEdges | 0..40319 | permutation of the 8 top and bottom edges (phase 2 only) |
| slicePerm | 0..23 | permutation of the 4 middle-layer edges (phase 2 only) |

A move table is `newCoord = table[coord * 18 + move]`.

**Check:** every coordinate value round trips (`set` then `get`), and for 300 random cubes every move table entry equals what the cube model computes.

**Prompt:**
```text
Add coordinates twist, flip, slice (combination rank, solved = 0), corners, udEdges and
slicePerm (Lehmer codes), with get/set for each. Build uint16 move tables by setting a cube to
each coordinate value and applying the move at cubie level. Test round trips for every value and
compare move tables against cubie moves for random cubes.
```

### Step 5: Pruning tables

**Goal:** a fast lower bound on the distance to the goal, for IDA*.

**Key idea:** a pruning table for a pair of coordinates stores the exact number of moves needed to bring both to zero. Fill it with breadth-first search from the solved state. The real distance is always at least this number, so the search can skip any branch where `depth + bound > limit`.

Tables used: phase 1: twist x slice, flip x slice, twist x flip. Phase 2: corners x slicePerm, udEdges x slicePerm. The heuristic is the maximum of the tables.

**Check:** every entry is filled, and for random sequences of n moves the bound is never more than n.

**Prompt:**
```text
Build five pruning tables with breadth-first search: twist x slice, flip x slice, twist x flip
(all 18 moves), corners x slicePerm and udEdges x slicePerm (phase-2 moves only). Test that all
entries are filled and that the bound never exceeds the length of a random move sequence.
```

### Step 6: Two-phase search

**Goal:** solve any cube.

**Key idea:**
- Phase 1: IDA* on (twist, flip, slice) until all three are 0. The cube is then in G1 = `<U, D, R2, L2, F2, B2>`.
- Phase 2: from there, IDA* on (corners, udEdges, slicePerm) using only G1 moves.
- Skip useless sequences: never turn the same face twice in a row, and for opposite faces allow only one order (U then D, never D then U).
- A phase-1 path that ends with a G1 move is redundant (its shorter prefix already reached G1), so skip it.

**Check:** solved cube gives 0 moves, every single move is solved by its inverse, and 30 random cubes are solved. Always verify a solution by applying it.

**Prompt:**
```text
Implement Kociemba two-phase IDA* using the coordinates and tables. Include the same-face and
opposite-face pruning rules and skip phase-1 paths that end in a G1 move. Verify every returned
solution by applying it. Test: solved cube, all 18 single moves, 30 random cubes.
```

### Step 7: Shorter solutions and a time limit

**Goal:** find about 20-move solutions, not just the first one.

**Key idea:** after a solution of length L, keep searching longer phase-1 paths with phase 2 limited to `L - 1 - phase1Length`. Stop when a solution is at most the target length (default 20) or when the time limit passes. Also keep searching for a short "polish" time (default 20 ms): short scrambles finish the whole search in that window, so they get the shortest possible answer.

**Check:** a 1-move scramble gives 1 move, `R U R' U'` gives 4 moves, scrambles of up to 9 moves never get a longer answer, and random cubes average at most 21 moves.

### Step 8: Parallel search

**Goal:** faster and shorter solutions using all CPU cores.

**Key idea:** run six searches at once: the cube rotated 0, 120 and 240 degrees about the corner-to-corner diagonal (so phase 1 targets a different axis each time), and the inverse of each. If moves M1..Mn solve `S^-1 C S`, then `S M1 S^-1 ... S Mn S^-1` solve C. All searches share an atomic "best length so far", so each one prunes with the others' results.

**Check:** with ThreadSanitizer (`cmake --preset tsan`), all tests pass with no data race reports. On 2 cores, average time dropped from 158 ms to 26 ms.

**Prompt:**
```text
Run up to 6 searches in parallel: rotations by S_URF3^k (k = 0, 1, 2) of the cube and of its
inverse. Build a table that maps moves of the rotated cube back to the original. Share the best
length through std::atomic and the best solution through a mutex. Add a test that solves 4 cubes
from 4 threads at once, and run the tests under ThreadSanitizer.
```

### Step 9: Faster start-up

**Goal:** build the tables quickly.

**Key idea:** the twist x flip table (4.5 million entries) took 560 ms. Two fixes: (1) once more than half the table is filled, fill the remaining entries backward: for each empty entry, check whether one move leads to the current layer; (2) build the five tables in parallel with `std::async`. Total start-up went from 876 ms to about 250 to 400 ms on 2 cores.

**Check:** the new tables are byte-for-byte identical to the old ones.

### Step 10: Command-line tool and benchmark

**Goal:** `cubesolver solve`, `scramble`, `bench` and `facelets` commands, with helpful errors.

**Check:** `bench --count 500` prints average and maximum length, percent within target, median and 95th percentile time, and a length histogram.

### Step 11: HTTP API

**Goal:** a small web service around the solver.

**Key ideas:**
- `GET /api/solve` is a pure function of its input, so a GET with an LRU cache is a natural fit.
- A counting semaphore limits concurrent solves, because each solve already uses up to 6 threads. A request that waits more than 5 seconds for a slot gets 503.
- Bind the port exclusively, so a second server on the same port fails with a clear message instead of silently sharing it.
- Bad input returns 400 with a message, unknown paths return a JSON 404.

**Check:** unit tests for the cache and for each error case, plus an end-to-end test that starts the server on a free port and calls it with an HTTP client.

### Step 12: Web demo

**Goal:** a page that shows the cube in 3D and plays the solution.

**Key ideas:**
- The browser keeps its own sticker model: each sticker has a position, a normal and a color. A clockwise turn maps `v` to `v x a + (v . a) a` for the face normal `a`.
- The renderer projects points with perspective, hides faces that point away, and paints from back to front. During a turn it splits the cube into two blocks (turning layer and the rest) and paints the farther block first.
- `tools/check_web_model.mjs` checks the JavaScript model against the C++ solver on random scrambles, as part of `ctest`.

**Check:** scramble, solve, play to the end, and the cube is solved. Works at phone width with no sideways scrolling.

### Step 13: Quality

- `CMakePresets.json` with release, debug, asan and tsan builds.
- Warnings on (`-Wall -Wextra -Wpedantic -Wshadow -Wconversion`) with zero warnings.
- `.clang-format` for consistent style.
- `.github/workflows/ci.yml` builds and tests on Linux and macOS, and runs the sanitizers.

### Step 14: Publish

1. Create a **new** GitHub repository (not a fork), for example `cube-solver`.
2. In the project folder:
   ```bash
   git init
   git add .
   git commit -m "Cube solver: two-phase C++ solver with web demo"
   git branch -M main
   git remote add origin https://github.com/<your-username>/cube-solver.git
   git push -u origin main
   ```
3. Check that the **Actions** tab shows CI passing. Fix anything that fails on macOS or Linux before you share the link.
4. Run `./build/cubesolver bench --count 500` on your Mac and put your own numbers in the README.
5. Pin the repository on your GitHub profile.

---

## Part 3: Ideas to go further

- **Live demo link.** Compile the solver to WebAssembly with Emscripten and host the page on GitHub Pages, so anyone can try it without installing anything.
- **Camera input.** Read the six faces of a real cube with the phone camera and fill the sticker editor automatically.
- **Optimal solver.** Add Korf's IDA* with a corner pattern database (88 million entries, 44 MB at 4 bits each) for truly shortest solutions of short scrambles.
- **Stronger phase-1 table.** Use cube symmetries to reduce the flip x slice x twist space, as Kociemba's own program does. This makes 20-move solutions much faster.
- **Deploy the API.** Add a Dockerfile and run the server on a small cloud machine, with request logs and a rate limit.
