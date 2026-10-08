# Cube Solver: notes for Claude Code

A 3x3 Rubik's cube solver in C++17 (Kociemba two-phase, IDA*), with a CLI, an HTTP API and a 3D web demo.

## Commands

```bash
cmake --preset release && cmake --build --preset release -j   # build into ./build
ctest --preset release                                        # all tests (must pass before every commit)
./build/cubesolver bench --count 200                          # performance check
./build/cubesolver_server                                     # web demo on http://localhost:8080
cmake --preset tsan && cmake --build --preset tsan -j && ctest --preset tsan   # after touching threads
```

## Layout

- `include/cubesolver/`, `src/core/`: solver library, no dependencies.
- `src/cli/`: command-line tool. `src/server/`: HTTP API (cpp-httplib), LRU cache.
- `web/`: plain ES modules, no build step. `web/cube-model.js` must stay in sync with the C++ model; `tools/check_web_model.mjs` checks it in ctest.
- `tests/`: doctest. `third_party/`: vendored single headers. Never edit them.

## Conventions that must not change

- Kociemba numbering. Faces `U R F D L B` (0..5). Corners `URF UFL ULB UBR DFR DLF DBL DRB`. Edges `UR UF UL UB DR DF DL DB FR FL BL BR`.
- Move index = face * 3 + (quarter turns - 1). So 0 = U, 1 = U2, 2 = U'.
- `a * b` means "first a, then b". Apply move m to cube c with `c * moveCube(m)`.
- Facelet strings: 54 letters, faces in order U R F D L B, each read row by row.
- Every solution is verified by applying it before it is returned. Keep it that way.

## Working rules

- Write or update a test for every behavior change. Use known facts where possible (move orders, reference strings).
- Keep the build free of warnings (`-Wall -Wextra -Wpedantic -Wshadow -Wconversion`).
- Format C++ with `clang-format -i` (config in `.clang-format`).
- After performance work, run `bench --count 500` before and after, and record the numbers in `docs/BENCHMARKS.md`.
- Docs: simple English, short sentences.
