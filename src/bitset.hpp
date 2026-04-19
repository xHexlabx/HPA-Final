#pragma once
#include <cstdint>
#include <vector>
#include <algorithm>
#include <cstring>

// Forward decl so BitSet can reference BitSetView in overloads.
class BitSet;
struct BitSetView;

// Dynamically-sized bitset specialised for dominating-set work:
// every inner loop walks a std::vector<uint64_t> so GCC auto-vectorises
// to AVX2 (VPOR / VPAND on 256-bit lanes) under -O3 -march=native.
class BitSet {
public:
    static constexpr int W = 64;
    int n = 0;
    int nw = 0;
    std::vector<uint64_t> data;

    BitSet() = default;
    explicit BitSet(int n_) : n(n_), nw((n_ + W - 1) / W), data(nw, 0ULL) {}
    // Copy a BitSetView into an owning BitSet (defined after BitSetView).
    inline explicit BitSet(BitSetView v);

    void resize(int n_) {
        n = n_;
        nw = (n_ + W - 1) / W;
        data.assign(nw, 0ULL);
    }

    inline void set(int i)   { data[i >> 6] |=  (1ULL << (i & 63)); }
    inline void reset(int i) { data[i >> 6] &= ~(1ULL << (i & 63)); }
    inline bool test(int i) const { return (data[i >> 6] >> (i & 63)) & 1ULL; }

    void clear() { std::fill(data.begin(), data.end(), 0ULL); }

    void set_all() {
        if (nw == 0) return;
        for (int i = 0; i < nw - 1; ++i) data[i] = ~0ULL;
        int rem = n - (nw - 1) * W;
        data[nw - 1] = (rem == W) ? ~0ULL : ((1ULL << rem) - 1ULL);
    }

    int count() const {
        int c = 0;
        for (int i = 0; i < nw; ++i) c += __builtin_popcountll(data[i]);
        return c;
    }

    bool any() const {
        for (int i = 0; i < nw; ++i) if (data[i]) return true;
        return false;
    }
    bool none() const { return !any(); }

    bool is_full() const {
        if (nw == 0) return true;
        for (int i = 0; i < nw - 1; ++i) if (data[i] != ~0ULL) return false;
        int rem = n - (nw - 1) * W;
        uint64_t mask = (rem == W) ? ~0ULL : ((1ULL << rem) - 1ULL);
        return (data[nw - 1] & mask) == mask;
    }

    BitSet& operator|=(const BitSet& o) {
        for (int i = 0; i < nw; ++i) data[i] |= o.data[i];
        return *this;
    }
    BitSet& operator&=(const BitSet& o) {
        for (int i = 0; i < nw; ++i) data[i] &= o.data[i];
        return *this;
    }
    // this &= ~o
    BitSet& and_not(const BitSet& o) {
        for (int i = 0; i < nw; ++i) data[i] &= ~o.data[i];
        return *this;
    }

    // popcount(this & o)
    int and_popcount(const BitSet& o) const {
        int c = 0;
        for (int i = 0; i < nw; ++i) c += __builtin_popcountll(data[i] & o.data[i]);
        return c;
    }
    // popcount(this & ~o)  — i.e. elements in *this not in o
    int and_not_popcount(const BitSet& o) const {
        int c = 0;
        for (int i = 0; i < nw; ++i) c += __builtin_popcountll(data[i] & ~o.data[i]);
        return c;
    }

    bool is_subset_of(const BitSet& o) const {
        for (int i = 0; i < nw; ++i) if (data[i] & ~o.data[i]) return false;
        return true;
    }

    bool operator==(const BitSet& o) const { return data == o.data; }
    bool operator!=(const BitSet& o) const { return data != o.data; }

    template <typename F>
    inline void for_each_set(F&& f) const {
        for (int w = 0; w < nw; ++w) {
            uint64_t m = data[w];
            while (m) {
                int b = __builtin_ctzll(m);
                f(w * W + b);
                m &= m - 1ULL;
            }
        }
    }

    // Early-exit variant: f returns bool; iteration stops when f returns false.
    template <typename F>
    inline bool for_each_set_until(F&& f) const {
        for (int w = 0; w < nw; ++w) {
            uint64_t m = data[w];
            while (m) {
                int b = __builtin_ctzll(m);
                if (!f(w * W + b)) return false;
                m &= m - 1ULL;
            }
        }
        return true;
    }

    int first_set() const {
        for (int w = 0; w < nw; ++w) if (data[w]) return w * W + __builtin_ctzll(data[w]);
        return -1;
    }

    // BitSetView-accepting overloads are defined after BitSetView below.
    inline BitSet& operator|=(BitSetView v);
    inline BitSet& operator&=(BitSetView v);
    inline BitSet& and_not(BitSetView v);
    inline int and_popcount(BitSetView v) const;
    inline bool is_subset_of(BitSetView v) const;
    inline bool equals(BitSetView v) const;
    inline bool operator==(BitSetView v) const;
    inline bool operator!=(BitSetView v) const;
};

// Non-owning read-only view of a BitSet-shaped word array. Used to present
// a flat `uint64_t[n*nw]` neighbourhood matrix with BitSet-like semantics
// without forcing a heap allocation per row.
struct BitSetView {
    static constexpr int W = 64;
    const uint64_t* data = nullptr;
    int nw = 0;
    int n = 0;

    bool test(int i) const { return (data[i >> 6] >> (i & 63)) & 1ULL; }
    int count() const {
        int c = 0;
        for (int i = 0; i < nw; ++i) c += __builtin_popcountll(data[i]);
        return c;
    }

    int and_popcount(const BitSet& o) const {
        int c = 0;
        for (int i = 0; i < nw; ++i) c += __builtin_popcountll(data[i] & o.data[i]);
        return c;
    }
    int and_popcount(BitSetView v) const {
        int c = 0;
        for (int i = 0; i < nw; ++i) c += __builtin_popcountll(data[i] & v.data[i]);
        return c;
    }

    bool is_subset_of(const BitSet& o) const {
        for (int i = 0; i < nw; ++i) if (data[i] & ~o.data[i]) return false;
        return true;
    }
    bool is_subset_of(BitSetView v) const {
        for (int i = 0; i < nw; ++i) if (data[i] & ~v.data[i]) return false;
        return true;
    }

    bool equals(BitSetView v) const {
        if (nw != v.nw) return false;
        for (int i = 0; i < nw; ++i) if (data[i] != v.data[i]) return false;
        return true;
    }
    bool equals(const BitSet& o) const {
        if (nw != o.nw) return false;
        for (int i = 0; i < nw; ++i) if (data[i] != o.data[i]) return false;
        return true;
    }

    template <typename F>
    inline void for_each_set(F&& f) const {
        for (int w = 0; w < nw; ++w) {
            uint64_t m = data[w];
            while (m) {
                int b = __builtin_ctzll(m);
                f(w * W + b);
                m &= m - 1ULL;
            }
        }
    }

    template <typename F>
    inline bool for_each_set_until(F&& f) const {
        for (int w = 0; w < nw; ++w) {
            uint64_t m = data[w];
            while (m) {
                int b = __builtin_ctzll(m);
                if (!f(w * W + b)) return false;
                m &= m - 1ULL;
            }
        }
        return true;
    }

    bool operator==(BitSetView v) const { return equals(v); }
    bool operator==(const BitSet& o) const { return equals(o); }
    bool operator!=(BitSetView v) const { return !equals(v); }
    bool operator!=(const BitSet& o) const { return !equals(o); }
};

// Inline definitions of the BitSet ↔ BitSetView overloads.
inline BitSet& BitSet::operator|=(BitSetView v) {
    for (int i = 0; i < nw; ++i) data[i] |= v.data[i];
    return *this;
}
inline BitSet& BitSet::operator&=(BitSetView v) {
    for (int i = 0; i < nw; ++i) data[i] &= v.data[i];
    return *this;
}
inline BitSet& BitSet::and_not(BitSetView v) {
    for (int i = 0; i < nw; ++i) data[i] &= ~v.data[i];
    return *this;
}
inline int BitSet::and_popcount(BitSetView v) const {
    int c = 0;
    for (int i = 0; i < nw; ++i) c += __builtin_popcountll(data[i] & v.data[i]);
    return c;
}
inline bool BitSet::is_subset_of(BitSetView v) const {
    for (int i = 0; i < nw; ++i) if (data[i] & ~v.data[i]) return false;
    return true;
}
inline bool BitSet::equals(BitSetView v) const {
    if (nw != v.nw) return false;
    for (int i = 0; i < nw; ++i) if (data[i] != v.data[i]) return false;
    return true;
}
inline bool BitSet::operator==(BitSetView v) const { return equals(v); }
inline bool BitSet::operator!=(BitSetView v) const { return !equals(v); }

// Constructor: copy a view into an owning BitSet.
inline BitSet::BitSet(BitSetView v)
    : n(v.n), nw(v.nw), data(v.data, v.data + v.nw) {}
