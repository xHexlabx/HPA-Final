// Validator: checks whether a solver output is a valid dominating set for
// the given input graph. Exits 0 if valid, 1 otherwise.

#include "../src/bitset.hpp"
#include "../src/graph.hpp"

#include <cstdio>
#include <fstream>
#include <string>

int main(int argc, char** argv) {
    if (argc != 3) {
        std::fprintf(stderr, "Usage: %s <input> <output>\n", argv[0]);
        return 2;
    }
    Graph g;
    try {
        g.load(argv[1]);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "validator: cannot load input: %s\n", e.what());
        return 2;
    }

    // Read last non-empty line of output file.
    std::ifstream in(argv[2]);
    if (!in) {
        std::fprintf(stderr, "validator: cannot open output: %s\n", argv[2]);
        return 2;
    }
    std::string line, last;
    while (std::getline(in, line)) {
        // Trim trailing whitespace/CR.
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t'))
            line.pop_back();
        if (!line.empty()) last = line;
    }

    if ((int)last.size() != g.n) {
        std::fprintf(stderr,
                     "validator: expected %d bits, got %d\n",
                     g.n, (int)last.size());
        return 1;
    }

    BitSet S(g.n);
    int ones = 0;
    for (int i = 0; i < g.n; ++i) {
        char c = last[i];
        if (c == '1') { S.set(i); ++ones; }
        else if (c != '0') {
            std::fprintf(stderr, "validator: invalid char '%c' at index %d\n", c, i);
            return 1;
        }
    }

    if (!g.is_valid_solution(S)) {
        std::fprintf(stderr, "validator: solution does NOT dominate all vertices\n");
        return 1;
    }
    std::fprintf(stdout, "valid   size=%d  (n=%d m=%d)\n", ones, g.n, g.m);
    return 0;
}
