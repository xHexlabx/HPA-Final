#!/usr/bin/env bash
# Usage:
#   RUNS=5 ./tests/bench.sh                  # all inputs/*.txt
#   RUNS=3 ./tests/bench.sh inputs/foo.txt   # specific files
#
# Env knobs:
#   RUNS=<n>          — number of repetitions per input (default 5)
#   SOLVER=<path>     — solver binary to benchmark (default ./solver)
#   SKIP_SLOW=1       — skip any input that timed out on run 1 (saves iteration time)
set -uo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

RUNS="${RUNS:-5}"
SOLVER="${SOLVER:-./solver}"
SKIP_SLOW="${SKIP_SLOW:-0}"

INPUTS=("$@")
if [ ${#INPUTS[@]} -eq 0 ]; then
    mapfile -t INPUTS < <(ls inputs/*.txt 2>/dev/null | sort)
fi

if [ ${#INPUTS[@]} -eq 0 ]; then
    echo "No input files found in inputs/*.txt" >&2
    exit 1
fi

if [ ! -x "$SOLVER" ]; then
    echo "Solver not executable: $SOLVER (run 'make' first)" >&2
    exit 1
fi

OUT_DIR=$(mktemp -d)
trap 'rm -rf "$OUT_DIR"' EXIT

export SOLVER SKIP_SLOW RUNS OUT_DIR
python3 - "${INPUTS[@]}" <<'PY'
import os, statistics, subprocess, sys, time

solver  = os.environ["SOLVER"]
out_dir = os.environ["OUT_DIR"]
runs    = int(os.environ["RUNS"])
skip_slow = os.environ.get("SKIP_SLOW", "0") == "1"
inputs = sys.argv[1:]

hdr = (
    f"{'input':<22} {'n':>5} {'m':>6} {'runs':>4}  "
    f"{'avg(s)':>9} {'min(s)':>9} {'max(s)':>9} {'std(s)':>8} "
    f"{'size':>5} {'stable':>6}"
)
print(hdr)
print("-" * len(hdr))

for inp in inputs:
    name = os.path.basename(inp).rsplit(".", 1)[0]
    try:
        with open(inp) as fp:
            n = int(fp.readline().strip())
            m = int(fp.readline().strip())
    except Exception as e:
        print(f"{name:<22}  parse error: {e}")
        continue

    times, sizes, ok = [], [], True
    for i in range(runs):
        t0 = time.monotonic()
        r = subprocess.run(
            [solver, inp, f"{out_dir}/o"],
            stderr=subprocess.DEVNULL,
        )
        t1 = time.monotonic()
        if r.returncode != 0:
            ok = False
            break
        times.append(t1 - t0)
        try:
            with open(f"{out_dir}/o") as fp:
                last = [l.strip() for l in fp if l.strip()][-1]
            sizes.append(last.count("1"))
        except Exception:
            ok = False
            break
        if skip_slow and i == 0 and (t1 - t0) > 30:
            # Long first run → skip remaining repeats for this input.
            break

    if not ok or not times:
        print(f"{name:<22} {n:>5d} {m:>6d} {len(times):>4d}  FAILED")
        continue

    avg = statistics.mean(times)
    mn, mx = min(times), max(times)
    std = statistics.pstdev(times) if len(times) > 1 else 0.0
    stable = "yes" if all(s == sizes[0] for s in sizes) else "NO"
    print(
        f"{name:<22} {n:>5d} {m:>6d} {len(times):>4d}  "
        f"{avg:>9.4f} {mn:>9.4f} {mx:>9.4f} {std:>8.4f} "
        f"{sizes[0]:>5d} {stable:>6}"
    )
PY
