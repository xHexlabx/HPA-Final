#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

SOLVER="${SOLVER:-./solver}"
VALIDATOR="${VALIDATOR:-./validator}"
BRUTE="${BRUTE:-./brute_force}"
OUT_DIR="$(mktemp -d)"
trap 'rm -rf "$OUT_DIR"' EXIT

pass=0
fail=0
for f in inputs/*.txt; do
    [ -e "$f" ] || continue
    name="$(basename "$f" .txt)"
    out="$OUT_DIR/$name.out"

    # Run solver.
    start=$(date +%s.%N)
    "$SOLVER" "$f" "$out" 2>"$OUT_DIR/$name.err" || {
        echo "FAIL [$name]: solver exited non-zero"
        cat "$OUT_DIR/$name.err" || true
        fail=$((fail+1))
        continue
    }
    end=$(date +%s.%N)
    elapsed=$(awk "BEGIN{print $end-$start}")

    # Validate.
    if ! "$VALIDATOR" "$f" "$out" > "$OUT_DIR/$name.val" 2>&1; then
        echo "FAIL [$name]: invalid solution"
        cat "$OUT_DIR/$name.val"
        fail=$((fail+1))
        continue
    fi
    size=$(awk '{for(i=1;i<=NF;i++) if($i ~ /^size=/){sub("size=","",$i); print $i}}' "$OUT_DIR/$name.val")

    # If graph is tiny, cross-check vs brute force.
    n=$(head -n1 "$f")
    if [ "$n" -le 20 ]; then
        opt=$("$BRUTE" "$f" 2>/dev/null | awk -F= '/optimum/{print $2}')
        if [ "$size" -ne "$opt" ]; then
            echo "FAIL [$name]: solver=$size, optimum=$opt"
            fail=$((fail+1))
            continue
        fi
        echo "PASS [$name] size=$size (optimum) t=${elapsed}s"
    else
        echo "PASS [$name] size=$size t=${elapsed}s"
    fi
    pass=$((pass+1))
done

echo "---"
echo "$pass passed, $fail failed"
exit "$fail"
