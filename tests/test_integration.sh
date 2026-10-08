#!/bin/bash
set -e

PORT=9005
DOCROOT="./www"

echo "=== Starting Integration Tests on Port $PORT ==="

# Kill any existing server on this port
pkill -f "observe.*$PORT" || true
sleep 0.5

# Start server in background
./observe "$DOCROOT" "$PORT" 2>/dev/null &
SERVER_PID=$!

cleanup() {
    echo "=== Cleaning up server (PID $SERVER_PID) ==="
    kill $SERVER_PID 2>/dev/null || true
    wait $SERVER_PID 2>/dev/null || true
    rm -f test_out_*.tmp stderr.tmp stdout.tmp
}
trap cleanup EXIT

# Wait for server to be responsive
sleep 1

echo "[1] Testing basic GET /index.html via bcurl..."
./bcurl localhost:$PORT/index.html > test_out_index.tmp
diff -u "$DOCROOT/index.html" test_out_index.tmp
echo "    PASS: Body matches exactly"

echo "[2] Testing basic GET /hello.txt via bcurl..."
./bcurl localhost:$PORT/hello.txt > test_out_hello.tmp
diff -u "$DOCROOT/hello.txt" test_out_hello.tmp
echo "    PASS: Body matches exactly"

echo "[3] Testing basic GET /sample.json via bcurl..."
./bcurl localhost:$PORT/sample.json > test_out_json.tmp
diff -u "$DOCROOT/sample.json" test_out_json.tmp
echo "    PASS: Body matches exactly"

echo "[4] Testing 404 Not Found handling..."
set +e
./bcurl localhost:$PORT/does_not_exist.html > /dev/null 2>&1
EXIT_CODE=$?
set -e
if [ $EXIT_CODE -eq 0 ]; then
    echo "    FAIL: Expected non-zero exit for 404, got $EXIT_CODE"
    exit 1
fi
echo "    PASS: Non-zero exit code ($EXIT_CODE) on 404"

echo "[5] Testing Path Traversal rejection (400)..."
set +e
./bcurl localhost:$PORT/../../etc/passwd > /dev/null 2>&1
EXIT_CODE=$?
set -e
if [ $EXIT_CODE -eq 0 ]; then
    echo "    FAIL: Expected non-zero exit for traversal, got $EXIT_CODE"
    exit 1
fi
echo "    PASS: Non-zero exit code ($EXIT_CODE) on traversal attempt"

echo "[6] Testing Verbose Mode (-v) and stdout/stderr separation..."
./bcurl -v localhost:$PORT/index.html > stdout.tmp 2> stderr.tmp
diff -u "$DOCROOT/index.html" stdout.tmp
if ! grep -q "FRAME_REQUEST" stderr.tmp || ! grep -q "FRAME_RESPONSE" stderr.tmp; then
    echo "    FAIL: Verbose output missing frame information in stderr"
    exit 1
fi
echo "    PASS: Verbose diagnostics cleanly emitted to stderr without polluting stdout"

echo "[7] Running independent Python interoperability test suite..."
python3 tests/interop_test.py $PORT

echo "=== All Integration Tests Passed Successfully! ==="
