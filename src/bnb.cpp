#include "bnb.hpp"

#include <algorithm>
#include <atomic>
#include <climits>
#include <cmath>
#include <mutex>
#include <utility>
#include <vector>

#ifdef _OPENMP
#  include <omp.h>
#endif

namespace {

// ---------------------------------------------------------------
// Shared search state.
// ---------------------------------------------------------------

struct SharedState {
    const Graph& g;
    Timer& timer;
    std::atomic<int> best_size;
    BitSet best_sol;
    std::mutex sol_mx;
    // Task cutoff: don't spawn new tasks below this depth.
    int task_cutoff_depth;

    SharedState(const Graph& g_, Timer& t_, int init_size, BitSet init_sol, int cutoff)
        : g(g_), timer(t_), best_size(init_size),
          best_sol(std::move(init_sol)), task_cutoff_depth(cutoff) {}

    void maybe_update(int sz, const BitSet& S) {
        int cur = best_size.load(std::memory_order_relaxed);
        if (sz >= cur) return;
        std::lock_guard<std::mutex> lk(sol_mx);
        if (sz < best_size.load(std::memory_order_relaxed)) {
            best_size.store(sz, std::memory_order_relaxed);
            best_sol = S;
        }
    }
};

// ---------------------------------------------------------------
// Lower bound = max of two valid bounds:
//
//   (1) Greedy ratio bound:  ceil(|U| / max |N[v] ∩ U|) over v that
//       are still allowed to be picked (i.e. v ∉ forbidden).
//
//   (2) Packing bound: a set P ⊆ U such that for every distinct
//       u₁, u₂ ∈ P, every ALLOWED dominator of u₁ differs from every
//       allowed dominator of u₂. Each element of P needs its own
//       fresh dominator, so |P| is a valid LB. The "allowed" qualifier
//       matters: two uncovered vertices might share a dominator that
//       is forbidden, in which case the shared candidate is irrelevant
//       and they can still be packed independently.
//
// Both bounds are safe (never overestimate the true optimum). Taking
// their max tightens pruning without risking incorrect cuts.
// ---------------------------------------------------------------

static int lower_bound(const Graph& g, const BitSet& uncovered,
                       const BitSet& forbidden) {
    int u_count = uncovered.count();
    if (u_count == 0) return 0;

    int max_cov = 1;
    for (int v = 0; v < g.n; ++v) {
        if (forbidden.test(v)) continue;
        int c = g.closed_nbhd[v].and_popcount(uncovered);
        if (c > max_cov) max_cov = c;
    }
    int bound_ratio = (u_count + max_cov - 1) / max_cov;

    // Packing: find u ∈ U such that no single ALLOWED dominator can cover
    // two of them. "Allowed" = not forbidden. Shared dominators that are
    // already forbidden don't invalidate packing (they can't be picked
    // anyway); shared dominators outside forbidden WOULD let us cover
    // both u's with one pick, so they must be caught.
    // Valid packing over ALLOWED dominators (not forbidden).
    // We pack uncovered vertices u such that for any previously-packed u',
    // the intersection N[u] ∩ N[u'] contains no allowed vertex — i.e.
    // u and u' cannot share a (not-yet-forbidden) dominator, so they
    // need separate additions to S. Using ~forbidden (not uncov) matters
    // for correctness: a dominator that is itself already "dominated"
    // (covered by some v ∈ S) can still be picked and cover both u and u'.
    BitSet taken(g.n);
    int packing = 0;
    std::vector<std::pair<int,int>> order;
    order.reserve(u_count);
    uncovered.for_each_set([&](int u) {
        BitSetView nbhd = g.closed_nbhd[u];
        int sz = 0;
        for (int i = 0; i < nbhd.nw; ++i)
            sz += __builtin_popcountll(nbhd.data[i] & ~forbidden.data[i]);
        order.emplace_back(sz, u);
    });
    std::sort(order.begin(), order.end());
    for (auto [sz, u] : order) {
        bool disjoint = true;
        BitSetView nbhd = g.closed_nbhd[u];
        for (int i = 0; i < nbhd.nw && disjoint; ++i) {
            uint64_t allowed = nbhd.data[i] & ~forbidden.data[i];
            if (allowed & taken.data[i]) disjoint = false;
        }
        if (disjoint) {
            ++packing;
            for (int i = 0; i < nbhd.nw; ++i)
                taken.data[i] |= nbhd.data[i] & ~forbidden.data[i];
        }
    }
    return std::max(bound_ratio, packing);
}

// ---------------------------------------------------------------
// Dynamic dominance reduction: mark additional vertices as forbidden
// when, under the current state, some OTHER allowed vertex w has
// N[w] ∩ uncov ⊇ N[v] ∩ uncov. Then v is never strictly necessary
// (w is always at least as good as v) and can be excluded without
// losing any optimal solution.
// O(n² · nw) per call — only worthwhile at shallow depths.
// ---------------------------------------------------------------

static void dynamic_exclude(const Graph& g, const BitSet& uncov,
                            BitSet& forbidden) {
    const int n = g.n;
    std::vector<BitSet> cov;
    cov.reserve(n);
    std::vector<int> allowed_ids;
    allowed_ids.reserve(n);
    for (int v = 0; v < n; ++v) {
        if (forbidden.test(v)) {
            cov.emplace_back();
            continue;
        }
        BitSet c(g.closed_nbhd[v]);
        c &= uncov;
        cov.push_back(std::move(c));
        allowed_ids.push_back(v);
    }
    // Sort by count ascending — smallest coverage tested first (cheapest
    // to dominate from).
    std::sort(allowed_ids.begin(), allowed_ids.end(), [&](int a, int b) {
        return cov[a].count() < cov[b].count();
    });

    for (int v : allowed_ids) {
        if (forbidden.test(v)) continue;
        if (cov[v].none()) continue; // v covers nothing in uncov — don't exclude
        for (int w : allowed_ids) {
            if (w == v) continue;
            if (forbidden.test(w)) continue;
            if (!cov[v].is_subset_of(cov[w])) continue;
            if (cov[v] == cov[w] && v < w) continue; // twin tiebreak
            forbidden.set(v);
            break;
        }
    }
}

// ---------------------------------------------------------------
// Pick the undominated vertex whose closed neighbourhood has the
// smallest effective size (most constrained → earliest forcing).
// ---------------------------------------------------------------

static int pick_branch_vertex(const Graph& g, const BitSet& dominated,
                              const BitSet& forbidden) {
    int best_v = -1;
    int best_sz = INT_MAX;
    for (int v = 0; v < g.n; ++v) {
        if (dominated.test(v)) continue;
        // Candidates to dominate v are N[v] \ forbidden.
        int sz = 0;
        BitSetView nbhd = g.closed_nbhd[v];
        for (int i = 0; i < nbhd.nw; ++i)
            sz += __builtin_popcountll(nbhd.data[i] & ~forbidden.data[i]);
        if (sz == 0) return -2; // v uncoverable under current decisions — infeasible subtree
        if (sz < best_sz) {
            best_sz = sz;
            best_v = v;
            if (sz == 1) break; // forced move — no need to search further
        }
    }
    return best_v;
}

// ---------------------------------------------------------------
// Recursive branch. BitSets are passed by value (copied per call)
// — fine because BitSet data is a small std::vector<uint64_t>.
// ---------------------------------------------------------------

static void branch(SharedState& st, BitSet S, BitSet dominated, BitSet forbidden,
                   int s_size, int depth) {
    if (st.timer.timed_out()) return;

    int cur_best = st.best_size.load(std::memory_order_relaxed);
    if (s_size >= cur_best) return;

    // ----- Unit propagation -----
    // Repeatedly look for an undominated vertex whose candidate-dominator
    // set has exactly one allowed member. That member is forced into S;
    // we inline the move rather than spawn a "branch" of width 1.
    // If any undominated vertex has zero candidates, the subtree is
    // infeasible → return immediately. Running this loop before branching
    // both shrinks the search tree and skips the OpenMP task overhead for
    // forced moves.
    while (true) {
        if (st.timer.timed_out()) return;
        if (s_size >= cur_best) return;
        if (dominated.is_full()) {
            st.maybe_update(s_size, S);
            return;
        }

        int forced_v = -1;
        bool infeasible = false;
        for (int v = 0; v < st.g.n; ++v) {
            if (dominated.test(v)) continue;
            BitSetView nbhd = st.g.closed_nbhd[v];
            int cnt = 0;
            int single_idx = -1;
            for (int i = 0; i < nbhd.nw; ++i) {
                uint64_t allowed = nbhd.data[i] & ~forbidden.data[i];
                if (!allowed) continue;
                int pc = __builtin_popcountll(allowed);
                cnt += pc;
                if (cnt > 1) break;
                single_idx = i * 64 + __builtin_ctzll(allowed);
            }
            if (cnt == 0) { infeasible = true; break; }
            if (cnt == 1) { forced_v = single_idx; break; }
        }
        if (infeasible) return;
        if (forced_v < 0) break; // nothing to propagate; go branch

        // Commit the forced move.
        S.set(forced_v);
        dominated |= st.g.closed_nbhd[forced_v];
        ++s_size;
    }
    // After propagation we may already have a complete solution.
    if (dominated.is_full()) {
        st.maybe_update(s_size, S);
        return;
    }
    cur_best = st.best_size.load(std::memory_order_relaxed);
    if (s_size >= cur_best) return;

    // Uncovered vertices = all_mask \ dominated.
    BitSet uncov = st.g.all_mask;
    uncov.and_not(dominated);

    // Dynamic reductions are expensive (O(n²·nw)); only apply near the
    // root where their effect propagates through the whole subtree and
    // the n for each component is what the algorithm works on. The copy
    // of `forbidden` avoids polluting the sibling branches with an
    // exclusion that only makes sense given THIS node's `dominated`.
    if (depth <= 2 && st.g.n <= 500) {
        dynamic_exclude(st.g, uncov, forbidden);
    }

    int lb = lower_bound(st.g, uncov, forbidden);
    if (s_size + lb >= cur_best) return;

    int u = pick_branch_vertex(st.g, dominated, forbidden);
    if (u == -2) return;   // some undominated vertex has no legal dominator
    if (u < 0) return;     // defensive

    // Candidates = N[u] \ forbidden.
    std::vector<int> cands;
    cands.reserve(16);
    BitSetView Nu = st.g.closed_nbhd[u];
    for (int i = 0; i < Nu.nw; ++i) {
        uint64_t m = Nu.data[i] & ~forbidden.data[i];
        while (m) {
            int b = __builtin_ctzll(m);
            cands.push_back(i * 64 + b);
            m &= m - 1ULL;
        }
    }

    // Dominated-candidate skipping: if N[v₂] ∩ uncov ⊆ N[v₁] ∩ uncov
    // (strict subset, or equal and v₁ < v₂), we never need to branch on
    // v₂ — any solution using v₂ as u's dominator can be transformed to
    // one using v₁ with at least as much coverage.
    std::vector<BitSet> cov;
    cov.reserve(cands.size());
    for (int v : cands) {
        BitSet c(st.g.closed_nbhd[v]);
        c &= uncov;
        cov.push_back(std::move(c));
    }
    std::vector<int> kept;
    kept.reserve(cands.size());
    for (size_t i = 0; i < cands.size(); ++i) {
        bool dominated_by_other = false;
        for (size_t j = 0; j < cands.size(); ++j) {
            if (i == j) continue;
            if (!cov[i].is_subset_of(cov[j])) continue;
            if (cov[i] == cov[j] && cands[i] < cands[j]) continue;
            dominated_by_other = true;
            break;
        }
        if (!dominated_by_other) kept.push_back((int)i);
    }
    // Order kept candidates by coverage gain (desc).
    std::sort(kept.begin(), kept.end(), [&](int a, int b) {
        return cov[a].count() > cov[b].count();
    });
    std::vector<int> ordered;
    ordered.reserve(kept.size());
    for (int idx : kept) ordered.push_back(cands[idx]);
    cands.swap(ordered);

    bool spawn_tasks = (depth < st.task_cutoff_depth);

    // Local forbidden grows as we commit to "exclude this candidate" after trying it.
    BitSet local_forbidden = forbidden;

    for (size_t ci = 0; ci < cands.size(); ++ci) {
        int v = cands[ci];
        // Prune if adding v exceeds best.
        if (s_size + 1 >= st.best_size.load(std::memory_order_relaxed)) break;

        BitSet S2 = S; S2.set(v);
        BitSet d2 = dominated; d2 |= st.g.closed_nbhd[v];
        BitSet f2 = local_forbidden; // v is allowed in this branch

        if (spawn_tasks) {
#pragma omp task firstprivate(S2, d2, f2, s_size, depth) shared(st) default(none)
            branch(st, std::move(S2), std::move(d2), std::move(f2), s_size + 1, depth + 1);
        } else {
            branch(st, std::move(S2), std::move(d2), std::move(f2), s_size + 1, depth + 1);
        }

        // In the remaining sibling branches, we commit that v is NOT picked
        // (otherwise we'd revisit the same solutions). This is the standard
        // "branch-and-price over dominators of u" enumeration scheme.
        local_forbidden.set(v);
    }

    if (spawn_tasks) {
#pragma omp taskwait
    }
}

} // namespace

// ---------------------------------------------------------------
// Driver.
// ---------------------------------------------------------------

BitSet branch_and_bound(const Graph& g,
                        const BitSet& initial_solution,
                        Timer& timer,
                        const BitSet* forced_in,
                        const BitSet* excluded) {
    int init_size = initial_solution.count();
    if (init_size == 0 || g.n == 0) return initial_solution;

#ifdef _OPENMP
    int threads = omp_get_max_threads();
    // Task cutoff depth scaled with thread count: enough shallow tasks
    // to saturate threads, but not so many that scheduling dominates.
    int cutoff = std::max(3, (int)std::ceil(std::log2((double)std::max(1, threads)))) + 3;
#else
    int cutoff = 0;
#endif

    SharedState st(g, timer, init_size, initial_solution, cutoff);

    BitSet S(g.n);
    BitSet dominated(g.n);
    BitSet forbidden(g.n);
    int s_size = 0;

    if (forced_in) {
        forced_in->for_each_set([&](int v) {
            S.set(v);
            dominated |= g.closed_nbhd[v];
            ++s_size;
        });
        if (s_size >= init_size) {
            if (dominated.is_full()) st.maybe_update(s_size, S);
        }
    }
    if (excluded) {
        // Excluded vertices are WLOG-absent from the solution, so add them
        // to the "forbidden" set at the root. If a forced vertex is also
        // marked excluded (defensive — shouldn't happen), forced wins.
        forbidden |= *excluded;
        if (forced_in) forbidden.and_not(*forced_in);
    }

#ifdef _OPENMP
#pragma omp parallel
    {
#pragma omp single nowait
        branch(st, S, dominated, forbidden, s_size, 0);
    }
#else
    branch(st, S, dominated, forbidden, s_size, 0);
#endif

    std::lock_guard<std::mutex> lk(st.sol_mx);
    return st.best_sol;
}
