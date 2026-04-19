#include "heuristic.hpp"

#include <algorithm>
#include <mutex>
#include <numeric>
#include <random>
#include <vector>

#ifdef _OPENMP
#  include <omp.h>
#endif

// ---------- Greedy ---------------------------------------------------------

BitSet greedy_dominating_set(const Graph& g,
                             const BitSet* forced,
                             const BitSet* excluded) {
    const int n = g.n;
    BitSet solution(n);
    BitSet uncovered = g.all_mask;

    if (forced) {
        forced->for_each_set([&](int v) {
            solution.set(v);
            uncovered.and_not(g.closed_nbhd[v]);
        });
    }

    while (uncovered.any()) {
        int best_v = -1, best_gain = 0;
        for (int v = 0; v < n; ++v) {
            if (solution.test(v)) continue;
            if (excluded && excluded->test(v)) continue;
            int gain = g.closed_nbhd[v].and_popcount(uncovered);
            if (gain > best_gain) {
                best_gain = gain;
                best_v = v;
            }
        }
        if (best_v < 0) {
            // All remaining "allowed" candidates give zero gain — fall back
            // to any vertex that covers a still-uncovered slot (including
            // excluded ones) to guarantee a valid solution.
            for (int v = 0; v < n; ++v) {
                if (solution.test(v)) continue;
                int gain = g.closed_nbhd[v].and_popcount(uncovered);
                if (gain > best_gain) { best_gain = gain; best_v = v; }
            }
            if (best_v < 0) break;
        }
        solution.set(best_v);
        uncovered.and_not(g.closed_nbhd[best_v]);
    }
    return solution;
}

// ---------- Dominator counts ----------------------------------------------
// count[u] = | {v ∈ S : u ∈ N[v]} | = how many vertices in S dominate u.

static std::vector<int> compute_dominator_counts(const Graph& g, const BitSet& S) {
    std::vector<int> cnt(g.n, 0);
    S.for_each_set([&](int v) {
        g.closed_nbhd[v].for_each_set([&](int u) { ++cnt[u]; });
    });
    return cnt;
}

// A vertex v ∈ S is removable iff every u ∈ N[v] has cnt[u] ≥ 2.
static bool is_removable(const Graph& g, int v, const std::vector<int>& cnt) {
    bool ok = true;
    g.closed_nbhd[v].for_each_set_until([&](int u) {
        if (cnt[u] < 2) { ok = false; return false; }
        return true;
    });
    return ok;
}

static void remove_vertex(const Graph& g, int v, BitSet& S, std::vector<int>& cnt) {
    S.reset(v);
    g.closed_nbhd[v].for_each_set([&](int u) { --cnt[u]; });
}

static void add_vertex(const Graph& g, int v, BitSet& S, std::vector<int>& cnt) {
    S.set(v);
    g.closed_nbhd[v].for_each_set([&](int u) { ++cnt[u]; });
}

// ---------- 1-remove local search -----------------------------------------

void local_search_remove(const Graph& g, BitSet& S, const BitSet* forced) {
    auto cnt = compute_dominator_counts(g, S);
    bool improved = true;
    while (improved) {
        improved = false;
        for (int v = 0; v < g.n; ++v) {
            if (!S.test(v)) continue;
            if (forced && forced->test(v)) continue;
            if (is_removable(g, v, cnt)) {
                remove_vertex(g, v, S, cnt);
                improved = true;
            }
        }
    }
}

// ---------- 1-swap + chained remove ---------------------------------------
//
// Try: for each v in S, find some w ∉ S such that swapping (remove v, add w)
// keeps coverage AND enables a second removal in the new configuration.
// Net change: -1. O(n²) per pass — fine as a refinement on top of greedy.

bool try_swap_improve(const Graph& g, BitSet& S, const BitSet* forced,
                      const BitSet* excluded) {
    auto cnt = compute_dominator_counts(g, S);
    const int n = g.n;
    bool improved_any = false;

    for (int v = 0; v < n; ++v) {
        if (!S.test(v)) continue;
        if (forced && forced->test(v)) continue;

        // Collect vertices that would become uncovered if v leaves.
        std::vector<int> need;
        g.closed_nbhd[v].for_each_set([&](int u) {
            if (cnt[u] == 1) need.push_back(u);
        });

        for (int w = 0; w < n; ++w) {
            if (w == v) continue;
            if (S.test(w)) continue;
            if (excluded && excluded->test(w)) continue;

            // w must dominate every "need" vertex.
            bool covers = true;
            for (int u : need) {
                if (!g.closed_nbhd[w].test(u)) { covers = false; break; }
            }
            if (!covers) continue;

            // Apply swap tentatively.
            remove_vertex(g, v, S, cnt);
            add_vertex(g, w, S, cnt);

            // Is any other vertex now removable?
            int extra = -1;
            for (int x = 0; x < n; ++x) {
                if (!S.test(x)) continue;
                if (forced && forced->test(x)) continue;
                if (is_removable(g, x, cnt)) { extra = x; break; }
            }
            if (extra >= 0) {
                remove_vertex(g, extra, S, cnt);
                improved_any = true;
                break; // restart outer loop with fresh S
            }

            // Undo swap — no improvement here.
            remove_vertex(g, w, S, cnt);
            add_vertex(g, v, S, cnt);
        }
        if (improved_any) break;
    }
    return improved_any;
}

// ---------- Randomised restart loop ---------------------------------------

// Perturb: remove K random vertices from S, then greedily rebuild.
static void perturb_and_rebuild(const Graph& g, BitSet& S, std::mt19937& rng,
                                int k, const BitSet* forced,
                                const BitSet* excluded) {
    std::vector<int> in_S;
    S.for_each_set([&](int v) {
        if (!forced || !forced->test(v)) in_S.push_back(v);
    });
    std::shuffle(in_S.begin(), in_S.end(), rng);
    k = std::min(k, (int)in_S.size());
    for (int i = 0; i < k; ++i) S.reset(in_S[i]);

    BitSet dominated(g.n);
    S.for_each_set([&](int v) { dominated |= g.closed_nbhd[v]; });

    BitSet uncovered = g.all_mask;
    uncovered.and_not(dominated);
    while (uncovered.any()) {
        int best_v = -1, best_gain = 0;
        for (int v = 0; v < g.n; ++v) {
            if (S.test(v)) continue;
            if (excluded && excluded->test(v)) continue;
            int gain = g.closed_nbhd[v].and_popcount(uncovered);
            if (gain > best_gain) { best_gain = gain; best_v = v; }
        }
        if (best_v < 0) {
            // Fallback: ignore excluded to keep the rebuild valid.
            for (int v = 0; v < g.n; ++v) {
                if (S.test(v)) continue;
                int gain = g.closed_nbhd[v].and_popcount(uncovered);
                if (gain > best_gain) { best_gain = gain; best_v = v; }
            }
            if (best_v < 0) break;
        }
        S.set(best_v);
        uncovered.and_not(g.closed_nbhd[best_v]);
    }
}

BitSet heuristic_pipeline(const Graph& g, const Timer& t, double budget_sec,
                          const BitSet* forced, const BitSet* excluded) {
    double deadline = std::min(t.elapsed() + budget_sec, t.budget() - 1.0);

    BitSet best = greedy_dominating_set(g, forced, excluded);
    local_search_remove(g, best, forced);
    while (try_swap_improve(g, best, forced, excluded)) {
        local_search_remove(g, best, forced);
        if (t.elapsed() >= deadline) return best;
    }

    // Portfolio: diversify via parallel restarts. Each thread runs its
    // own perturb/rebuild loop with a distinct RNG seed. A shared
    // `best` is updated under a critical section whenever any thread
    // improves on it. If OpenMP is disabled the outer pragma no-ops
    // and we get the original single-threaded behaviour.
    const int patience_each = std::max(50, std::min(2000, g.n * 4));
    std::mutex best_mx;

#ifdef _OPENMP
    int nthreads = omp_get_max_threads();
#else
    int nthreads = 1;
#endif
    // For very small problems the OpenMP team startup is the dominant cost
    // — stay single-threaded when there's no point diversifying.
    if (g.n < 40) nthreads = 1;

#pragma omp parallel num_threads(nthreads)
    {
#ifdef _OPENMP
        int tid = omp_get_thread_num();
#else
        int tid = 0;
#endif
        std::mt19937 local_rng(0x5EEDu + 0x9E3779B1u * (uint32_t)tid);
        BitSet local_best;
        {
            std::lock_guard<std::mutex> lk(best_mx);
            local_best = best; // start from current shared best
        }
        int k = std::max(1, local_best.count() / 5);
        int no_improve = 0;

        while (t.elapsed() < deadline && no_improve < patience_each) {
            BitSet candidate = local_best;
            perturb_and_rebuild(g, candidate, local_rng, k, forced, excluded);
            local_search_remove(g, candidate, forced);
            if (try_swap_improve(g, candidate, forced, excluded))
                local_search_remove(g, candidate, forced);

            if (candidate.count() < local_best.count() &&
                g.is_valid_solution(candidate)) {
                local_best = candidate;
                k = std::max(1, local_best.count() / 5);
                no_improve = 0;

                // Share improvement with other threads.
                std::lock_guard<std::mutex> lk(best_mx);
                if (local_best.count() < best.count()) {
                    best = local_best;
                }
            } else {
                ++no_improve;
                if ((local_rng() & 7u) == 0)
                    k = std::min(g.n, k + 1);
                // Occasionally pick up improvements another thread made.
                if ((local_rng() & 63u) == 0) {
                    std::lock_guard<std::mutex> lk(best_mx);
                    if (best.count() < local_best.count()) {
                        local_best = best;
                        k = std::max(1, local_best.count() / 5);
                    }
                }
            }
        }

        // Final commit.
        std::lock_guard<std::mutex> lk(best_mx);
        if (local_best.count() < best.count()) best = local_best;
    }
    return best;
}
