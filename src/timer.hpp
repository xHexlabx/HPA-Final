#pragma once
#include <chrono>

// Wall-clock timer with a soft budget. The 3-min hard grading limit means we
// give ourselves a margin: default budget is 160 s (20 s margin for Docker
// startup, I/O, and final output flush).
class Timer {
    using clock = std::chrono::steady_clock;
    clock::time_point start_;
    double budget_sec_;

public:
    explicit Timer(double budget_sec = 160.0)
        : start_(clock::now()), budget_sec_(budget_sec) {}

    double elapsed() const {
        return std::chrono::duration<double>(clock::now() - start_).count();
    }
    double remaining() const { return budget_sec_ - elapsed(); }
    double budget() const { return budget_sec_; }
    bool timed_out() const { return elapsed() >= budget_sec_; }

    void set_budget(double s) { budget_sec_ = s; }
    void reset() { start_ = clock::now(); }
};
