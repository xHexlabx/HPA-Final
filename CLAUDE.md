# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

**HPC Final Project — Power Plant Placement.** Solves **Minimum Dominating Set** (NP-Complete) on undirected graphs. A power plant at vertex `v` powers `N[v]` (closed neighborhood); find minimum `S` such that every vertex is powered.

I/O contract (strict — grader validates the **last line only**):
- Input: `n\n m\n` then `m` lines of `u v` (0-indexed edges).
- Output: binary string of length `n`, bit `i` = 1 iff a plant is placed at vertex `i`.
- Invocation: `docker run -v data/input:/input -v data/output:/output <image> /input/<in>.txt /output/<out>.out`.

## Hard Constraints (grading rules — do not violate)

- **Timeout**: 3 min for full credit, 5 min hard kill. Container startup counts. Always write a valid best-so-far before the deadline.
- **Banned dependencies**: OR-Tools, GLPK, PuLP. Using any of these forfeits the +10 bonus. Prefer no external solver libraries at all.
- **Target hardware**: AMD Ryzen 5 2600 — 6 cores × 2 SMT = **12 threads**, AVX2 (no AVX-512), 16 GB RAM. Tune parallelism and SIMD for exactly this.
- **Deliverable**: public Docker image. Keep it small (target < 25 MB, < 200 ms startup) via multi-stage Alpine build + static libgcc/libstdc++.

## Build & Run

Compiler flags are **non-negotiable** — `__builtin_popcountll`/`__builtin_ctzll` only emit hardware `POPCNT`/`TZCNT` under `-march=native`:

```
-O3 -march=native -mtune=native -funroll-loops -flto -fopenmp -std=c++20
```

Common workflows (implement in `Makefile`):
- `make` — build `solver` with release flags.
- `make test` — run `tests/run_tests.sh` (validator + brute-force check for small n).
- `docker build -t hpa-final .` — build submission image.
- `docker run -v $(pwd)/inputs:/input -v $(pwd)/outputs:/output hpa-final /input/grid-6-7.txt /output/grid-6-7.out` — local equivalent of grader invocation.

Single-test debugging: build with `-O0 -g -fsanitize=address,undefined` (separate target) before profiling with `perf record ./solver inputs/<file>.txt /tmp/out`.

## Architecture

Pipeline in `src/main.cpp`. **Invariant**: the output file is written with a valid dominating set as soon as the graph loads (all-ones fallback), then overwritten with better solutions as they are found. Any abrupt kill still leaves a legal answer on disk.

1. **Preprocess** (`graph.{hpp,cpp}`, `reducer.{hpp,cpp}`) — parse, build closed neighbourhoods in a flat `NbhdMatrix` (single contiguous `uint64_t[n·nw]` allocation; rows exposed as non-owning `BitSetView`). Apply reduction rules: isolated → `forced_in`, leaf+pendant → neighbor `forced_in`, `N[u] ⊆ N[v]` → `excluded` (twin tiebreak by index).
2. **Decompose** — `graph.cpp::connected_components` splits into self-contained `Subgraph`s with local 0..k-1 indexing; solved smallest-first so quick components tighten the global UB. Global `forced_in` / `excluded` are projected into each component's local indices.
3. **Heuristic upper bound** (`heuristic.{hpp,cpp}`) — greedy (`max |N[v] ∩ uncov|`, respects `excluded`), then 1-remove local search, then 1-swap chained with follow-up remove, then **parallel portfolio restart**: all OpenMP threads run perturb/rebuild loops with distinct RNG seeds; share a global best via mutex. Budget scales with `n`: 0.05–8 s. Falls back to single-threaded when n < 40 to avoid thread-team startup cost.
4. **Parallel Branch & Bound** (`bnb.{hpp,cpp}`) — OpenMP **tasks** seeded with Stage 3's UB. Branches on each pivot u's undominated-dominator candidates. Three reductions per node:
   - **Unit propagation**: any undominated vertex with exactly 1 allowed dominator forces that pick inline (no task spawn).
   - **Dynamic dominance exclusion** (depth ≤ 2, n ≤ 500): if `N[v] ∩ uncov ⊆ N[w] ∩ uncov` for some non-forbidden w, mark v forbidden for this subtree.
   - **Candidate skipping**: at the branch point, drop any candidate whose uncov-coverage is strictly dominated by another.

   Lower bound = `max(⌈uncov / max|N[v∉forbidden] ∩ uncov|⌉, disjoint-packing-over-allowed-dominators)`. The packing accumulates `N[u] ∩ ¬forbidden` (not `∩ uncov`) — using `uncov` is theoretically invalid because two uncov vertices can share a dominator that is itself already dominated (but not in S). Stress-testing with 500 random instances caught this: the invalid variant gives wrong answers on ~0.2 % of random inputs.

   Task cutoff depth `⌈log2(threads)⌉+6`. `std::atomic<int> best_size` for lock-free pruning; `std::mutex` only when copying the BitSet on improvement.
5. **Output** — safety flush happens immediately after load; after each component; at end of the pipeline.

### Foundation: BitSet + BitSetView (`bitset.hpp`)

Every hot op runs billions of times. Mutable state lives in `BitSet` (owns `std::vector<uint64_t>`); the static closed-neighbourhood matrix lives in `NbhdMatrix` (single contiguous allocation) and exposes rows as `BitSetView` (non-owning pointer + nw + n). BitSet has overloads that accept BitSetView (`|=`, `&=`, `and_not`, `and_popcount`, `is_subset_of`, `==`), and a `BitSet(BitSetView)` copy-ctor for the rare cases that need an owning copy.

The three-loop pattern (`|=`, `&=`, `and_popcount`) must stay trivial so GCC auto-vectorises to AVX2 (`VPOR`/`VPAND` on 256-bit lanes). Do **not** replace with `std::set<int>` or `std::vector<bool>` — roughly 100× slowdown.

### Testing strategy

- `tests/brute_force.cpp` — enumerates all 2ⁿ subsets for `n ≤ 20`, produces ground-truth optimum.
- `tests/validator.cpp` — given input + output, checks (a) output length == n, (b) every vertex in `∪ N[v for v in S]`. Run on every solver output.
- The grader doesn't require the *first* line to be valid — only the last line of the output file. Keep this in mind when implementing early-exit writes.

## Common pitfalls that have already been identified

- Forgetting `-march=native` → software popcount, massive slowdown.
- Spawning OpenMP tasks past the cutoff depth → task overhead dominates.
- Race on `best_solution` writes → always `#pragma omp critical` around the full BitSet copy (atomic only guards the int `global_best`).
- Greedy alone (no local search) → typically 1–3 vertices over optimum, which loses ranking points.
- Starting B&B with `best = ∞` → virtually no pruning; always seed with Stage 2.
- Docker image with dev tools or debug symbols → slow startup eats into the 3-min budget.

## Notes for future agents

- Code does not exist yet. The repository currently contains only `LICENSE` and `.gitignore`. When scaffolding, follow the layout in the project briefing (`src/`, `tests/`, `inputs/`, `Dockerfile`, `Makefile`).
- The `.gitignore` is pre-configured for C/C++/Fortran build artifacts — no changes needed for a C++ project.
- Sample inputs will be placed in `inputs/` (not in git). Use `inputs/grid-6-7.txt` as the canonical smoke test — any valid 2-vertex dominating set is optimal (e.g., `000110` or `100011`).
