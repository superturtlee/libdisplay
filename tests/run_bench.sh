#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/../build"
SOCK="/tmp/display_bench_$$.sock"
DURATION="${1:-10}"

if [ ! -f "${BUILD_DIR}/bench_producer" ]; then
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

echo "=== Starting 1080P frame rate benchmark (${DURATION}s) ==="

"${BUILD_DIR}/display_daemon" "$SOCK" &
DAEMON_PID=$!
sleep 0.3

"${BUILD_DIR}/bench_consumer" "$SOCK" &
CONSUMER_PID=$!
sleep 0.3

"${BUILD_DIR}/bench_producer" "$SOCK" "$DURATION"
