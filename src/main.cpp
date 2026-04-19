#include "bitset.hpp"
#include "bnb.hpp"
#include "graph.hpp"
#include "heuristic.hpp"
#include "reducer.hpp"
#include "timer.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {

void write_solution(const std::string& path, const BitSet& S, int n) {
    std::ofstream out(path, std::ios::out | std::ios::trunc);
    if (!out) {
        std::fprintf(stderr, "ERROR: cannot open output file: %s\n", path.c_str());
        std::exit(2);
    }
    std::string buf;
    buf.reserve(n + 1);
    for (int i = 0; i < n; ++i) buf.push_back(S.test(i) ? '1' : '0');
    buf.push_back('\n');
    out << buf;
}

// Size-aware heuristic budget for one component.
double heuristic_budget_for(int n) {
    if (n <= 32)       return 0.05;
    if (n <= 128)      return 0.3;
    if (n <= 512)      return 1.5;
    if (n <= 2048)     return 4.0;
    return 8.0;
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 3) {
        std::fprintf(stderr, "Usage: %s <input> <output>\n", argv[0]);
        return 1;
    }
    const std::string in_path = argv[1];
    const std::string out_path = argv[2];

    // 160 s leaves ~20 s margin under the 3-min grading wall.
    Timer timer(160.0);

    Graph g;
    try {
        g.load(in_path);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "ERROR loading input: %s\n", e.what());
        return 1;
    }

    // Start with the all-vertices solution as a "valid-but-terrible" fallback.
    // As each component is solved we will clear the non-solution vertices of
    // that component. If the process is killed mid-loop, whatever is written
    // on disk is still a legal dominating set (the un-solved components
    // contribute all their vertices).
    BitSet final_solution(g.n);
    final_solution.set_all();

    if (g.n == 0) {
        std::ofstream out(out_path, std::ios::out | std::ios::trunc);
        out << "\n";
        return 0;
    }

    // Earliest possible safety output: the trivial all-ones solution is
    // always a valid dominating set, so even if something blows up before
    // we compute a real answer, the output file has a legal fallback.
    write_solution(out_path, final_solution, g.n);

    double t_load = timer.elapsed();

    // Stage 1: reductions on the WHOLE graph (forced_in + excluded).
    ReducedProblem rp = reduce(g);
    double t_reduce = timer.elapsed();

    // Stage 1b: connected-component decomposition. Each component is
    // independent, so we solve it in isolation and union the result.
    auto components = connected_components(g);
    double t_comps = timer.elapsed();

    // Reserve some slack so late components still get time for B&B.
    for (size_t ci = 0; ci < components.size(); ++ci) {
        auto& comp = components[ci];
        const int k = comp.g.n;
        BitSet comp_best(k);

        // Project global forced/excluded into this component's local indices.
        BitSet local_forced(k);
        BitSet local_excluded(k);
        for (int i = 0; i < k; ++i) {
            int gi = comp.global_of[i];
            if (rp.forced_in.test(gi)) local_forced.set(i);
            if (rp.excluded.test(gi))  local_excluded.set(i);
        }

        // Stage 2: heuristic per-component.
        double heur_budget = heuristic_budget_for(k);
        // Don't exceed a fair share of remaining time across components.
        int remaining_comps = (int)components.size() - (int)ci;
        double share = std::max(0.1, timer.remaining() / std::max(1, remaining_comps * 2));
        heur_budget = std::min(heur_budget, share);

        double t_before_heur = timer.elapsed();
        comp_best = heuristic_pipeline(comp.g, timer, heur_budget,
                                       &local_forced, &local_excluded);
        double t_after_heur = timer.elapsed();
        if (!comp.g.is_valid_solution(comp_best)) {
            comp_best = comp.g.all_mask; // defensive
        }

        // Stage 3: B&B on this component. Skip it when the heuristic has
        // clearly already hit the lower bound — the OpenMP thread-team
        // startup cost alone is several ms and dominates tiny instances.
        bool skip_bnb =
            comp_best.count() <= 1 ||
            comp_best.count() <= local_forced.count();
        if (!skip_bnb && timer.remaining() > 1.0) {
            BitSet bnb = branch_and_bound(comp.g, comp_best, timer,
                                          &local_forced, &local_excluded);
            if (bnb.count() < comp_best.count() && comp.g.is_valid_solution(bnb)) {
                comp_best = bnb;
            }
        }
        (void)t_before_heur; (void)t_after_heur;

        // Replace this component's contribution: clear every global vertex
        // in the component, then set only those chosen by comp_best. This
        // unseats the "all-ones fallback" for this component.
        for (int i = 0; i < k; ++i) final_solution.reset(comp.global_of[i]);
        comp_best.for_each_set([&](int i) {
            final_solution.set(comp.global_of[i]);
        });

        // Flush after every component so any abrupt kill still leaves
        // a valid (partial-if-needed) answer on disk.
        write_solution(out_path, final_solution, g.n);
    }

    // Final flush.
    write_solution(out_path, final_solution, g.n);

    std::fprintf(stderr,
                 "n=%d m=%d  comps=%zu  solution=%d  elapsed=%.2fs\n",
                 g.n, g.m, components.size(),
                 final_solution.count(), timer.elapsed());
    (void)t_load; (void)t_reduce; (void)t_comps;
    return 0;
}
