CXX      ?= g++
CXXSTD   := -std=c++20
WARN     := -Wall -Wextra -Wno-unused-parameter
OPT      := -O3 -march=native -mtune=native -funroll-loops -flto
OMP      := -fopenmp
CXXFLAGS := $(CXXSTD) $(WARN) $(OPT) $(OMP)
LDFLAGS  := $(OMP) -flto

SRC_SOLVER := src/main.cpp src/graph.cpp src/reducer.cpp src/heuristic.cpp src/bnb.cpp
HDRS       := $(wildcard src/*.hpp)

.PHONY: all clean test docker debug asan

all: solver

solver: $(SRC_SOLVER) $(HDRS)
	$(CXX) $(CXXFLAGS) $(SRC_SOLVER) -o solver $(LDFLAGS)

debug: $(SRC_SOLVER) $(HDRS)
	$(CXX) $(CXXSTD) $(WARN) -O0 -g $(OMP) $(SRC_SOLVER) -o solver-debug $(OMP)

asan: $(SRC_SOLVER) $(HDRS)
	$(CXX) $(CXXSTD) $(WARN) -O1 -g -fsanitize=address,undefined $(OMP) $(SRC_SOLVER) -o solver-asan $(OMP)

validator: tests/validator.cpp src/graph.cpp $(HDRS)
	$(CXX) $(CXXSTD) -O2 -Isrc tests/validator.cpp src/graph.cpp -o validator

brute_force: tests/brute_force.cpp src/graph.cpp $(HDRS)
	$(CXX) $(CXXSTD) -O2 -Isrc tests/brute_force.cpp src/graph.cpp -o brute_force

test: solver validator brute_force
	bash tests/run_tests.sh

docker:
	docker build -t hpa-final .

clean:
	rm -f solver solver-debug solver-asan validator brute_force
