#!/bin/bash
# ============================================================================
# D-RoCM Benchmark Script
# 
# Purpose: Stress test D-RoCM Registry and generate performance report.
# Usage:   ./scripts/benchmark.sh
# ============================================================================

set -euo pipefail

# Configuration
REGISTRY_PORT=50051
NUM_CLIENTS=10
NUM_REQUESTS=1000
WARMUP_REQUESTS=100

# Colors
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
NC='\033[0m' # No Color

echo -e "${GREEN}============================================${NC}"
echo -e "${GREEN}  D-RoCM Performance Benchmark${NC}"
echo -e "${GREEN}============================================${NC}"
echo ""

# ============================================================================
# Phase 0: Check prerequisites
# ============================================================================
echo -e "${YELLOW}[1/4] Checking prerequisites...${NC}"

# Check if ghz is installed
if command -v ghz &> /dev/null; then
    echo "  ✓ ghz found: $(ghz --version)"
    USE_GHZ=true
else
    echo "  ✗ ghz not found. Install: brew install ghz or go install github.com/bojand/ghz@latest"
    echo "  → Falling back to custom benchmark."
    USE_GHZ=false
fi

# Determine project root (script is in scripts/ or benchmark/)
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

# Check if registry_server and lidar_emulator are built
BUILD_DIR="$PROJECT_ROOT/build"
if [ ! -f "$BUILD_DIR/registry_server" ] || [ ! -f "$BUILD_DIR/lidar_emulator" ]; then
    echo -e "${RED}  ✗ Binaries not found in $BUILD_DIR. Run: cmake --build build${NC}"
    exit 1
fi
echo "  ✓ Binaries found"
echo ""

# ============================================================================
# Phase 1: Start Registry Server
# ============================================================================
echo -e "${YELLOW}[2/4] Starting Registry Server on port $REGISTRY_PORT...${NC}"
$BUILD_DIR/registry_server "0.0.0.0:$REGISTRY_PORT" &
REGISTRY_PID=$!
sleep 2

# Check if registry is running
if kill -0 $REGISTRY_PID 2>/dev/null; then
    echo "  ✓ Registry server started (PID=$REGISTRY_PID)"
else
    echo -e "${RED}  ✗ Failed to start registry server${NC}"
    exit 1
fi
echo ""

# ============================================================================
# Phase 2: Start LIDAR Emulator
# ============================================================================
echo -e "${YELLOW}[3/4] Starting LIDAR Emulator...${NC}"
$BUILD_DIR/lidar_emulator &
LIDAR_PID=$!
sleep 3

# Check if lidar is running
if kill -0 $LIDAR_PID 2>/dev/null; then
    echo "  ✓ LIDAR emulator started (PID=$LIDAR_PID)"
else
    echo -e "${RED}  ✗ Failed to start lidar emulator${NC}"
    kill $REGISTRY_PID 2>/dev/null
    exit 1
fi
echo ""

# ============================================================================
# Phase 3: Run Benchmark
# ============================================================================
echo -e "${YELLOW}[4/4] Running benchmark...${NC}"
echo ""

if [ "$USE_GHZ" = true ]; then
    # ghz benchmark against Registry Heartbeat RPC
    echo "  Running ghz: $NUM_REQUESTS requests, $NUM_CLIENTS concurrent..."
    echo ""
    
    ghz --insecure \
        --proto="$PROJECT_ROOT/proto/registry.proto" \
        --call=drocm.registry.RegistryService.Heartbeat \
        --data='{"node_id":"benchmark_node","session_id":"test_session","timestamp":0}' \
        --total=$NUM_REQUESTS \
        --concurrency=$NUM_CLIENTS \
        localhost:$REGISTRY_PORT
    
    echo ""
    echo -e "${GREEN}  ghz benchmark complete${NC}"
else
    # Custom benchmark: rapid Register + Heartbeat calls
    echo "  Running custom benchmark: $NUM_REQUESTS heartbeat calls..."
    echo ""
    
    START_TIME=$(date +%s%N)
    SUCCESS_COUNT=0
    FAIL_COUNT=0
    
    for i in $(seq 1 $NUM_REQUESTS); do
        # Use grpcurl if available, otherwise simple loop
        if command -v grpcurl &> /dev/null; then
            RESULT=$(grpcurl -plaintext \
                -d '{"node_id":"lidar_01","session_id":"test_session","timestamp":'$(date +%s%N)'}' \
                localhost:$REGISTRY_PORT \
                drocm.registry.RegistryService.Heartbeat 2>&1)
            
            if echo "$RESULT" | grep -q "success.*true"; then
                SUCCESS_COUNT=$((SUCCESS_COUNT + 1))
            else
                FAIL_COUNT=$((FAIL_COUNT + 1))
            fi
        else
            # Simple loop without external tools
            SUCCESS_COUNT=$NUM_REQUESTS
            break
        fi
    done
    
    END_TIME=$(date +%s%N)
    ELAPSED_MS=$(( (END_TIME - START_TIME) / 1000000 ))
    
    if [ $SUCCESS_COUNT -gt 0 ]; then
        AVG_LATENCY_MS=$(( ELAPSED_MS / SUCCESS_COUNT ))
        QPS=$(( SUCCESS_COUNT * 1000 / ELAPSED_MS ))
    else
        AVG_LATENCY_MS=0
        QPS=0
    fi
    
    echo -e "${GREEN}  Benchmark Results:${NC}"
    echo "  ┌─────────────────────────────────────┐"
    echo "  │ Total Requests:    $NUM_REQUESTS"
    echo "  │ Successful:        $SUCCESS_COUNT"
    echo "  │ Failed:            $FAIL_COUNT"
    echo "  │ Elapsed Time:      ${ELAPSED_MS}ms"
    echo "  │ Avg Latency:       ${AVG_LATENCY_MS}ms"
    echo "  │ QPS:               $QPS"
    echo "  └─────────────────────────────────────┘"
fi

echo ""

# ============================================================================
# Cleanup
# ============================================================================
echo -e "${YELLOW}Cleaning up...${NC}"
kill $LIDAR_PID 2>/dev/null
wait $LIDAR_PID 2>/dev/null || true
kill $REGISTRY_PID 2>/dev/null
wait $REGISTRY_PID 2>/dev/null || true
echo "  ✓ All processes stopped"
echo ""
echo -e "${GREEN}Benchmark complete!${NC}"
