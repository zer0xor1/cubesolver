# Benchmarks

All numbers below were measured on a 2-core cloud machine (Intel Xeon, 2.8 GHz, Linux, GCC 13, Release build). Run the same commands on your own computer and replace these numbers with yours.

## How to reproduce

```bash
cmake --preset release && cmake --build --preset release -j
./build/cubesolver bench --count 500                  # default settings
./build/cubesolver bench --count 500 --polish 0       # raw time to reach 20 moves
./build/cubesolver bench --count 500 --threads 1      # one search only
./build/cubesolver bench --count 200 --target 19 --timeout 2000
```

`bench` uses uniformly random cube states (seed 2026 unless you pass `--seed`), so every run with the same seed solves the same cubes.

## Default settings: 6 searches, target 20, polish 20 ms, timeout 1 s

```text
Random-state cubes: 500 (seed 2026)
Settings: target 20 moves, polish 20 ms, timeout 1000 ms, 6 parallel searches

| Metric | Value |
|---|---|
| Average length | 19.48 moves |
| Max length | 20 moves |
| Reached target | 100.0 % |
| Average time | 26.2 ms |
| Median time | 20.3 ms |
| 95th percentile time | 51.0 ms |
| Max time | 291.9 ms |
| Search speed | 135.1 million nodes/s |

Length histogram:
  16 moves:     2  
  17 moves:     9  #
  18 moves:    37  ####
  19 moves:   150  ##################
  20 moves:   302  ####################################
```

The median is about 20 ms because of the polish window: the search keeps looking for a shorter answer for at least 20 ms.

## No polish window: raw time to reach a 20-move solution

```text
Random-state cubes: 500 (seed 2026)
Settings: target 20 moves, polish 0 ms, timeout 1000 ms, 6 parallel searches

| Metric | Value |
|---|---|
| Average length | 19.81 moves |
| Max length | 20 moves |
| Reached target | 100.0 % |
| Average time | 17.3 ms |
| Median time | 9.6 ms |
| 95th percentile time | 57.9 ms |
| Max time | 277.0 ms |
| Search speed | 110.2 million nodes/s |
```

## One search instead of six

```text
Random-state cubes: 500 (seed 2026)
Settings: target 20 moves, polish 20 ms, timeout 1000 ms, 1 parallel searches

| Metric | Value |
|---|---|
| Average length | 19.78 moves |
| Max length | 21 moves |
| Reached target | 95.4 % |
| Average time | 157.7 ms |
| Median time | 34.1 ms |
| 95th percentile time | 953.3 ms |
| Max time | 1000.6 ms |
| Search speed | 58.0 million nodes/s |
```

With one search, 4.6 % of cubes hit the 1-second limit before a 20-move solution was found. With six searches, none did.

## Target 19 moves, timeout 2 s

```text
Random-state cubes: 200 (seed 2026)
Settings: target 19 moves, polish 20 ms, timeout 2000 ms, 6 parallel searches

| Metric | Value |
|---|---|
| Average length | 18.89 moves |
| Max length | 20 moves |
| Reached target | 91.0 % |
| Average time | 343.9 ms |
| Median time | 55.3 ms |
| 95th percentile time | 2000.3 ms |
| Max time | 2000.6 ms |
| Search speed | 128.9 million nodes/s |
```

Asking for 19 moves or fewer is much harder: 91 % reach it within 2 seconds.

## Starting point: zer0xor1/RubiksCube

The project this one started from has its own Kociemba solver (one search, stops at the first phase-2 solution). Measured on the same machine with a small harness that calls its `solve()` on 50 cubes made by 200 random turns:

```text
init_s=0.28
n=50 scramble=200 threads=1 avg_len=23.1 min=20 max=26 avg_s=0.717 max_s=4.375 unsolved=0
```

| | This project (6 searches) | This project (1 search) | Starting point |
|---|---|---|---|
| Average length | 19.48 | 19.78 | 23.1 |
| Average time | 26.2 ms | 157.7 ms | 717 ms |
| Slowest cube | 291.9 ms | 1000.6 ms (time limit) | 4375 ms |

## Table build time

| Version | Time on 2 cores |
|---|---|
| Forward BFS, one table at a time | 876 ms |
| Backward filling for late layers + 5 tables in parallel | 250 to 400 ms |
