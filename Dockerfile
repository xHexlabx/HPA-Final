# ---------- builder ----------
FROM gcc:13 AS builder
WORKDIR /build
COPY src ./src
# -march=znver1 targets the Ryzen 5 2600 grading machine explicitly.
RUN g++ -O3 -march=znver1 -mtune=znver1 \
        -funroll-loops -flto \
        -fopenmp -std=c++20 -Wall \
        src/main.cpp src/graph.cpp src/reducer.cpp src/heuristic.cpp src/bnb.cpp \
        -o /solver

# ---------- runtime ----------
FROM debian:bookworm-slim
RUN apt-get update \
 && apt-get install -y --no-install-recommends libgomp1 \
 && rm -rf /var/lib/apt/lists/*
COPY --from=builder /solver /solver
ENTRYPOINT ["/solver"]
