// Brute-force Minimum Dominating Set for small n (≤ 24).
// Used as ground truth for correctness tests.

#include "../src/bitset.hpp"
#include "../src/graph.hpp"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>

int main(int argc, char** argv) {
    if (argc < 2 || argc > 3) {
        std::fprintf(stderr, "Usage: %s <input> [<output>]\n", argv[0]);
        return 2;
    }
    Graph g;
    g.load(argv[1]);
    if (g.n > 24) {
        std::fprintf(stderr, "brute_force: refuse to run for n > 24 (got n=%d)\n", g.n);
        return 2;
    }

    // Precompute closed neighborhoods as uint64_t (n ≤ 24 so fits easily).
    std::vector<uint64_t> nbhd(g.n);
    for (int v = 0; v < g.n; ++v) {
        uint64_t mask = 0;
        g.closed_nbhd[v].for_each_set([&](int u) { mask |= 1ULL << u; });
        nbhd[v] = mask;
    }
    uint64_t full = (g.n == 64) ? ~0ULL : ((1ULL << g.n) - 1ULL);

    int best_size = g.n + 1;
    uint64_t best_set = 0;

    // Enumerate subsets in order of popcount to find minimum early.
    for (int k = 0; k <= g.n && k < best_size; ++k) {
        // Iterate all subsets of size k via Gosper's hack.
        if (k == 0) {
            if (full == 0) { best_size = 0; best_set = 0; break; }
            continue;
        }
        uint64_t s = (1ULL << k) - 1ULL;
        uint64_t limit = full + 1;
        while (s < limit) {
            uint64_t dom = 0;
            uint64_t bits = s;
            while (bits) {
                int b = __builtin_ctzll(bits);
                dom |= nbhd[b];
                bits &= bits - 1;
            }
            if ((dom & full) == full) {
                best_size = k;
                best_set = s;
                break;
            }
            // Gosper
            uint64_t c = s & (-(int64_t)s);
            uint64_t r = s + c;
            s = (((r ^ s) >> 2) / c) | r;
        }
        if (best_size == k) break;
    }

    std::fprintf(stdout, "optimum=%d\n", best_size);
    if (argc == 3) {
        std::ofstream out(argv[2]);
        for (int i = 0; i < g.n; ++i) out << ((best_set >> i) & 1ULL ? '1' : '0');
        out << '\n';
    }
    return 0;
}
