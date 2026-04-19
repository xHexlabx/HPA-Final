#pragma once
#include "bitset.hpp"
#include "graph.hpp"
#include "timer.hpp"

// `forced` bits are pre-included; `excluded` bits are never picked.
// Both pointers may be null.
BitSet greedy_dominating_set(const Graph& g,
                             const BitSet* forced = nullptr,
                             const BitSet* excluded = nullptr);

void local_search_remove(const Graph& g, BitSet& S,
                         const BitSet* forced = nullptr);

bool try_swap_improve(const Graph& g, BitSet& S,
                      const BitSet* forced = nullptr,
                      const BitSet* excluded = nullptr);

BitSet heuristic_pipeline(const Graph& g, const Timer& t, double budget_sec,
                          const BitSet* forced = nullptr,
                          const BitSet* excluded = nullptr);
