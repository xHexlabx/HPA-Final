FROM alpine:3.19 AS builder
RUN apk add --no-cache g++ make libgomp
WORKDIR /build
COPY src ./src
# -march=znver1 targets Ryzen 5 2600 (Zen+) explicitly so the binary
# runs on the grading machine regardless of build-host CPU.
RUN g++ -O3 -march=znver1 -mtune=znver1 \
        -funroll-loops -flto \
        -fopenmp -std=c++20 -Wall \
        src/main.cpp src/graph.cpp src/reducer.cpp src/heuristic.cpp src/bnb.cpp \
        -o /solver \
        -static-libgcc -static-libstdc++

FROM alpine:3.19
RUN apk add --no-cache libgomp libstdc++
COPY --from=builder /solver /solver
ENTRYPOINT ["/solver"]
