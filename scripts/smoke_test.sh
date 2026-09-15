#!/usr/bin/env bash
set -euo pipefail

PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${PROJECT_DIR}/build"
SERVER_PID=""

cleanup() {
    if [[ -n "${SERVER_PID}" ]] && kill -0 "${SERVER_PID}" 2>/dev/null; then
        kill "${SERVER_PID}"
        wait "${SERVER_PID}" 2>/dev/null || true
    fi
}
trap cleanup EXIT

cmake -S "${PROJECT_DIR}" -B "${BUILD_DIR}" -DCMAKE_BUILD_TYPE=Release
cmake --build "${BUILD_DIR}" -j
ctest --test-dir "${BUILD_DIR}" --output-on-failure

"${BUILD_DIR}/http_server" --port 18080 --workers 2 &
SERVER_PID=$!

for _ in {1..30}; do
    if curl --silent --fail http://127.0.0.1:18080/ >/dev/null; then
        break
    fi
    sleep 0.1
done

HTTP_CODE="$(curl --silent --write-out '%{http_code}' http://127.0.0.1:18080/)"
if [[ "${HTTP_CODE}" != "200" ]]; then
    echo "Expected HTTP 200, got ${HTTP_CODE}" >&2
    exit 1
fi

echo "Smoke test passed"
