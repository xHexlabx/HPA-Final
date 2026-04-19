#include "graph.hpp"
#include <algorithm>
#include <fstream>
#include <queue>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

void Graph::load(const std::string& path) {
    std::ifstream in(path);
    if (!in) throw std::runtime_error("cannot open input: " + path);

    if (!(in >> n >> m)) throw std::runtime_error("bad header (expected n and m)");
    if (n < 0) throw std::runtime_error("negative n");
    if (m < 0) throw std::runtime_error("negative m");

    closed_nbhd.resize(n);
    adj.assign(n, {});
    for (int v = 0; v < n; ++v) closed_nbhd.set_bit(v, v);

    for (int i = 0; i < m; ++i) {
        int u, v;
        if (!(in >> u >> v))
            throw std::runtime_error("bad edge at index " + std::to_string(i));
        if (u < 0 || u >= n || v < 0 || v >= n)
            throw std::runtime_error("edge out of range");
        if (u == v) continue; // self-loop: already in closed neighbourhood
        if (closed_nbhd.test_bit(u, v)) continue; // duplicate edge
        closed_nbhd.set_bit(u, v);
        closed_nbhd.set_bit(v, u);
        adj[u].push_back(v);
        adj[v].push_back(u);
    }

    all_mask = BitSet(n);
    all_mask.set_all();
}

bool Graph::is_valid_solution(const BitSet& S) const {
    if (S.n != n) return false;
    BitSet dominated(n);
    S.for_each_set([&](int v) { dominated |= closed_nbhd[v]; });
    return dominated.is_full();
}

std::vector<Subgraph> connected_components(const Graph& g) {
    std::vector<int> comp_of(g.n, -1);
    std::vector<std::vector<int>> buckets; // vertex lists per component

    for (int s = 0; s < g.n; ++s) {
        if (comp_of[s] != -1) continue;
        int c = (int)buckets.size();
        buckets.emplace_back();
        auto& bucket = buckets.back();
        std::queue<int> q;
        q.push(s);
        comp_of[s] = c;
        while (!q.empty()) {
            int v = q.front(); q.pop();
            bucket.push_back(v);
            for (int u : g.adj[v]) {
                if (comp_of[u] == -1) {
                    comp_of[u] = c;
                    q.push(u);
                }
            }
        }
    }

    // Reusable scratch: global -> local index for the current component.
    std::vector<int> local_of(g.n, -1);

    std::vector<Subgraph> out;
    out.reserve(buckets.size());
    for (auto& verts : buckets) {
        int k = (int)verts.size();
        Subgraph sub;
        sub.global_of = verts;
        sub.g.n = k;
        sub.g.adj.assign(k, {});
        sub.g.closed_nbhd.resize(k);
        for (int i = 0; i < k; ++i) {
            sub.g.closed_nbhd.set_bit(i, i);
            local_of[verts[i]] = i;
        }
        int m_sub = 0;
        for (int i = 0; i < k; ++i) {
            int v = verts[i];
            for (int u : g.adj[v]) {
                int j = local_of[u];
                if (j > i) {
                    sub.g.adj[i].push_back(j);
                    sub.g.adj[j].push_back(i);
                    sub.g.closed_nbhd.set_bit(i, j);
                    sub.g.closed_nbhd.set_bit(j, i);
                    ++m_sub;
                }
            }
        }
        sub.g.m = m_sub;
        sub.g.all_mask = BitSet(k);
        sub.g.all_mask.set_all();
        // Reset scratch.
        for (int v : verts) local_of[v] = -1;
        out.push_back(std::move(sub));
    }

    std::sort(out.begin(), out.end(), [](const Subgraph& a, const Subgraph& b) {
        return a.g.n < b.g.n;
    });
    return out;
}
