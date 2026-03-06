#!/bin/bash

# Configuration
BUILD_DIR="../build"
TEST_DIR="/tmp/g04_system_test_cpp"
ROUTER_BIN="$BUILD_DIR/bin/router_server"
NODE_BIN="$BUILD_DIR/bin/node_server"
CLIENT_BIN="$BUILD_DIR/bin/system_test_client"

# Cleanup Function
cleanup() {
    echo "Stopping processes..."
    kill $ROUTER_PID 2>/dev/null
    for pid in "${NODE_PIDS[@]}"; do
        kill $pid 2>/dev/null
    done
}

# Cleanup previous run
rm -rf "$TEST_DIR"
mkdir -p "$TEST_DIR"

# Create Router Config
cat > "$TEST_DIR/router.json" <<EOF
{
    "endpoint": "tcp://*:5555",
    "heartbeat_interval_ms": 1000,
    "heartbeat_timeout_ms": 3000,
    "retry_after_ms": 500,
    "request_timeout_ms": 2000
}
EOF

# Create Node Configs
for i in {0..2}; do
    mkdir -p "$TEST_DIR/node_${i}_data"
    cat > "$TEST_DIR/node_${i}.json" <<EOF
{
    "router_endpoint": "tcp://127.0.0.1:5555",
    "data_dir": "$TEST_DIR/node_${i}_data",
    "num_vnodes": 4,
    "heartbeat_interval_ms": 1000
}
EOF
done

# Start Router
echo "Starting Router..."
"$ROUTER_BIN" "$TEST_DIR/router.json" > "$TEST_DIR/router.log" 2>&1 &
ROUTER_PID=$!
sleep 1

# Start Nodes
NODE_PIDS=()
for i in {0..2}; do
    echo "Starting Node $i..."
    "$NODE_BIN" "$TEST_DIR/node_${i}.json" > "$TEST_DIR/node_${i}.log" 2>&1 &
    NODE_PIDS+=($!)
done

sleep 3

# 1. Run Client PUT
echo "Running Client PUT..."
"$CLIENT_BIN" PUT
if [ $? -ne 0 ]; then
    echo "Client PUT FAILED"
    cleanup
    exit 1
fi

# 2. Kill a Node (Fault Tolerance)
echo "Killing Node 0..."
kill ${NODE_PIDS[0]}
sleep 2

# 3. Run Client GET
echo "Running Client GET (expecting success despite node failure)..."
"$CLIENT_BIN" GET
if [ $? -ne 0 ]; then
    echo "Client GET FAILED"
    cleanup
    exit 1
fi

# 4. Kill another Node (Quorum Failure)
echo "Killing Node 1 (Simulating Quorum Failure)..."
kill ${NODE_PIDS[1]}
sleep 2

# 5. Run Client GET (expecting FAILURE)
echo "Running Client GET (expecting FAILURE due to lack of quorum)..."
"$CLIENT_BIN" GET
if [ $? -eq 0 ]; then
    echo "Client GET SUCCEEDED (Unexpected! Should have failed)"
    cleanup
    exit 1
fi

echo "Quorum Failure Test PASSED (Client correctly failed)"

# Cleanup
cleanup
exit 0
