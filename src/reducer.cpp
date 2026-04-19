#include "reducer.hpp"

// Cheap safe reductions:
//
// 1. Isolated vertex (N[v] == {v}): must be in the solution.
// 2. Leaf-pendant: if v has degree 1 and its unique neighbour u has
//    degree ≥ 2, we can WLOG include u instead of v (u dominates at
//    least the same set as v plus more). If an isolated-edge component
//    {u, v} (both degree 1), pick one arbitrarily — we pick the smaller
//    index to keep behaviour deterministic.
//
// Rules are iterated because forcing a vertex can make others reducible
// (e.g. a forced vertex's neighbours may become already-dominated leaves).
// The "dominated" bitmask is maintained alongside so the heuristic/B&B
// can skip already-satisfied constraints.

// Dominated-vertex / twin reductions — O(n²) per pass, capped via threshold.
// Correctness: if N[u] ⊆ N[v] with u ≠ v, any optimal S containing u can be
// converted to optimal S' = (S \ {u}) ∪ {v}, which dominates ≥ what S did.
// So u need never be in the solution — exclude it from consideration.
// For ties N[u] == N[v] ("twins"), the tiebreak keeps the lower-indexed one
// to ensure at least one of every twin-class remains a candidate.
static bool dominance_pass(const Graph& g, BitSet& excluded, const BitSet& forced) {
    const int n = g.n;
    bool changed = false;
    for (int v = 0; v < n; ++v) {
        if (excluded.test(v)) continue;
        BitSetView Nv = g.closed_nbhd[v];
        int popv = Nv.count();
        for (int u = 0; u < n; ++u) {
            if (u == v) continue;
            if (excluded.test(u)) continue;
            if (forced.test(u)) continue; // forced vertex cannot be excluded
            BitSetView Nu = g.closed_nbhd[u];
            if (Nu.count() > popv) continue; // fast reject
            if (!Nu.is_subset_of(Nv)) continue;
            if (Nu.equals(Nv) && u < v) continue; // twin tiebreak
            excluded.set(u);
            changed = true;
        }
    }
    return changed;
}

ReducedProblem reduce(const Graph& g) {
    ReducedProblem rp(g);

    bool changed = true;
    while (changed) {
        changed = false;

        // Rule 1: isolated vertex → must be included.
        for (int v = 0; v < g.n; ++v) {
            if (rp.forced_in.test(v)) continue;
            if (rp.dominated0.test(v)) continue;
            if (g.degree(v) == 0) {
                rp.forced_in.set(v);
                rp.dominated0 |= g.closed_nbhd[v];
                rp.excluded.reset(v); // forcing overrides exclusion
                changed = true;
            }
        }

        // Rule 2: leaf-pendant.
        for (int v = 0; v < g.n; ++v) {
            if (rp.forced_in.test(v)) continue;
            if (rp.dominated0.test(v)) continue;
            if (g.degree(v) != 1) continue;
            int u = g.adj[v][0];
            if (rp.forced_in.test(u)) continue;
            int pick = (g.degree(u) >= 2) ? u : std::min(u, v);
            rp.forced_in.set(pick);
            rp.dominated0 |= g.closed_nbhd[pick];
            rp.excluded.reset(pick);
            changed = true;
        }
    }

    // Rule 3: dominated-vertex / twin. O(n²) — skip for very large graphs.
    if (g.n <= 3000) {
        dominance_pass(g, rp.excluded, rp.forced_in);
    }

    return rp;
}
