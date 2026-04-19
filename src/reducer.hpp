#pragma once
#include "bitset.hpp"
#include "graph.hpp"

// Result of applying reduction rules to the original graph.
// The solver works in "reduced-vertex space" and then re-lifts to the
// original indices for output.
struct ReducedProblem {
    const Graph& g;
    BitSet forced_in;  // vertices that MUST be in the solution
    BitSet excluded;   // vertices that WLOG need not be in the solution
    BitSet dominated0; // vertices already dominated by forced_in

    explicit ReducedProblem(const Graph& g_)
        : g(g_), forced_in(g_.n), excluded(g_.n), dominated0(g_.n) {}
};

// Run safe reduction rules:
//   - Isolated vertex: must be included.
//   - Leaf-pendant: neighbour of a degree-1 vertex dominates it better.
//   - Dominated-vertex: if N[u] ⊆ N[v] (u ≠ v) there's an optimal
//     solution without u, so exclude u.
//   - Twin: N[u] == N[v] → keep the lower index, exclude the other.
ReducedProblem reduce(const Graph& g);
