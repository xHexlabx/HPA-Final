#pragma once
#include "bitset.hpp"
#include "graph.hpp"
#include "timer.hpp"

// Parallel branch-and-bound for Minimum Dominating Set.
// `initial_solution` seeds the upper bound (best known).
// `forced_in`  (optional): every set bit is pre-committed to S.
// `excluded`   (optional): every set bit is pre-committed to S^c (WLOG).
// Returns the best solution discovered within the time budget (never worse
// than `initial_solution`).
BitSet branch_and_bound(const Graph& g,
                        const BitSet& initial_solution,
                        Timer& timer,
                        const BitSet* forced_in = nullptr,
                        const BitSet* excluded = nullptr);
