#pragma once
#include "bitset.hpp"
#include <cstddef>
#include <string>
#include <vector>

// Flat-stored n × nw matrix of closed neighbourhoods.
// Replaces std::vector<BitSet> (which heap-allocated each row separately).
// Single contiguous allocation → the hardware prefetcher kicks in on
// sequential v-loops and the matrix plays well with AVX2 auto-vectorisation.
struct NbhdMatrix {
    int n = 0;
    int nw = 0;
    std::vector<uint64_t> data; // size = n * nw

    void resize(int n_) {
        n = n_;
        nw = (n_ + 63) / 64;
        data.assign((std::size_t)n * nw, 0ULL);
    }

    // O(1) row view into the flat storage.
    BitSetView operator[](int v) const {
        return { data.data() + (std::size_t)v * nw, nw, n };
    }

    // Mutating helpers (used during graph construction).
    void set_bit(int v, int i) {
        data[(std::size_t)v * nw + (i >> 6)] |= (1ULL << (i & 63));
    }
    bool test_bit(int v, int i) const {
        return (data[(std::size_t)v * nw + (i >> 6)] >> (i & 63)) & 1ULL;
    }
};

struct Graph {
    int n = 0;
    int m = 0;
    NbhdMatrix closed_nbhd;
    std::vector<std::vector<int>> adj;
    BitSet all_mask;

    void load(const std::string& path);
    bool is_valid_solution(const BitSet& S) const;
    int degree(int v) const { return (int)adj[v].size(); }
};

// A connected component expressed as a self-contained Graph with local
// vertex indexing 0..sub.g.n-1 plus a mapping back to the original graph.
struct Subgraph {
    Graph g;
    std::vector<int> global_of; // local index -> original vertex id
};

// Split `g` into its connected components; isolated vertices become
// single-vertex components. Returned list is sorted by size ascending
// (small components solved first so quick wins tighten the global UB).
std::vector<Subgraph> connected_components(const Graph& g);
