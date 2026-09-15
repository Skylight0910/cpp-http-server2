#!/usr/bin/env bash
set -euo pipefail

PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${PROJECT_DIR}/build"
PORT=18081
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

"${BUILD_DIR}/http_server" \
    --port "${PORT}" \
    --workers 2 \
    --idle-timeout-ms 1000 &
SERVER_PID=$!

for _ in {1..30}; do
    if curl --silent --fail "http://127.0.0.1:${PORT}/" >/dev/null; then
        break
    fi
    sleep 0.1
done

python3 - "${PORT}" <<'PY'
import socket
import sys
import time

port = int(sys.argv[1])


def read_response(sock: socket.socket, buffer: bytearray) -> bytes:
    while b"\r\n\r\n" not in buffer:
        chunk = sock.recv(4096)
        if not chunk:
            raise AssertionError("connection closed before response header")
        buffer.extend(chunk)

    header_end = buffer.index(b"\r\n\r\n") + 4
    headers = bytes(buffer[:header_end]).decode("latin-1")
    content_length = 0
    for line in headers.split("\r\n")[1:]:
        if line.lower().startswith("content-length:"):
            content_length = int(line.split(":", 1)[1].strip())
            break

    while len(buffer) < header_end + content_length:
        chunk = sock.recv(4096)
        if not chunk:
            raise AssertionError("connection closed before response body")
        buffer.extend(chunk)

    response = bytes(buffer[:header_end + content_length])
    del buffer[:header_end + content_length]
    return response


def assert_response(
    response: bytes,
    expected_status: bytes,
    expected_connection: bytes,
    name: str,
) -> None:
    if not response.startswith(b"HTTP/1.1 " + expected_status):
        raise AssertionError(
            f"{name}: expected status {expected_status}, got {response[:80]!r}"
        )
    if b"Connection: " + expected_connection + b"\r\n" not in response:
        raise AssertionError(
            f"{name}: expected Connection {expected_connection!r}, "
            f"got {response[:160]!r}"
        )


with socket.create_connection(("127.0.0.1", port), timeout=3) as sock:
    buffer = bytearray()
    sock.sendall(b"GET / HTTP/1.1\r\nHost: test\r\n\r\n")
    response = read_response(sock, buffer)
    assert_response(response, b"200", b"keep-alive", "first GET")

    sock.sendall(b"GET /missing HTTP/1.1\r\nHost: test\r\n\r\n")
    response = read_response(sock, buffer)
    assert_response(response, b"404", b"keep-alive", "second GET")

with socket.create_connection(("127.0.0.1", port), timeout=3) as sock:
    buffer = bytearray()
    request = b"GET / HTTP/1.1\r\nHost: test\r\n\r\n"
    sock.sendall(request + request)
    assert_response(read_response(sock, buffer), b"200", b"keep-alive", "pipelined GET 1")
    assert_response(read_response(sock, buffer), b"200", b"keep-alive", "pipelined GET 2")

with socket.create_connection(("127.0.0.1", port), timeout=3) as sock:
    buffer = bytearray()
    sock.sendall(
        b"POST / HTTP/1.1\r\nHost: test\r\nContent-Length: 0\r\nConnection: close\r\n\r\n"
    )
    response = read_response(sock, buffer)
    assert_response(response, b"405", b"close", "POST with close")

with socket.create_connection(("127.0.0.1", port), timeout=3) as sock:
    buffer = bytearray()
    sock.sendall(b"GET\r\n\r\n")
    response = read_response(sock, buffer)
    assert_response(response, b"400", b"close", "malformed request")

with socket.create_connection(("127.0.0.1", port), timeout=3) as sock:
    buffer = bytearray()
    long_header = b"X-Long: " + b"a" * 9000 + b"\r\n\r\n"
    sock.sendall(b"GET / HTTP/1.1\r\nHost: test\r\n" + long_header)
    response = read_response(sock, buffer)
    assert_response(response, b"431", b"close", "oversized header")

with socket.create_connection(("127.0.0.1", port), timeout=3) as sock:
    started = time.monotonic()
    sock.settimeout(3)
    while sock.recv(4096):
        pass
    elapsed = time.monotonic() - started

if not 0.5 <= elapsed <= 2.5:
    raise AssertionError(f"idle timeout: expected about 1s, got {elapsed:.2f}s")

print("Protocol tests passed")
PY
