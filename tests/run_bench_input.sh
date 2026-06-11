#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/../build"
SOCK="/tmp/display_bench_input_$$.sock"
THROUGHPUT_SEC="${1:-5}"
PAUSE_SEC="${2:-3}"
SLOW_DELAY_US="${3:-1000}"

if [ ! -f "${BUILD_DIR}/bench_input_producer" ]; then
    echo "Building..."
    cmake -B "${BUILD_DIR}" -S "${SCRIPT_DIR}/.."
    cmake --build "${BUILD_DIR}" -j$(nproc 2>/dev/null || sysctl -n hw.ncpu)
fi

cleanup() {
    kill $DAEMON_PID $CONSUMER_PID 2>/dev/null
    wait $DAEMON_PID $CONSUMER_PID 2>/dev/null
    rm -f "$SOCK"
}
trap cleanup EXIT

# Consumer sends for: throughput + slow_reader + accumulation phases
TOTAL_SEC=$((THROUGHPUT_SEC * 2 + PAUSE_SEC))

echo "=== Input Event Benchmark ==="
echo "  Phase 1: Throughput (max speed) for ${THROUGHPUT_SEC}s"
echo "  Phase 2: Slow reader (${SLOW_DELAY_US}μs delay) for ${THROUGHPUT_SEC}s"
echo "  Phase 3: Accumulation (${PAUSE_SEC}s pause, then drain)"
echo ""

"${BUILD_DIR}/display_daemon" "$SOCK" &
DAEMON_PID=$!
sleep 0.3

"${BUILD_DIR}/bench_input_consumer" "$SOCK" "$TOTAL_SEC" &
CONSUMER_PID=$!
sleep 0.3

"${BUILD_DIR}/bench_input_producer" "$SOCK" "$THROUGHPUT_SEC" "$PAUSE_SEC" "$SLOW_DELAY_US"
