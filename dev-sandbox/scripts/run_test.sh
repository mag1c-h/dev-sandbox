#!/bin/bash

# Set CANN environment
source /usr/local/Ascend/ascend-toolkit/set_env.sh 2>/dev/null || true

# Build
echo "Building..."
mkdir -p build
cd build
cmake .. > /dev/null 2>&1
make -j > /dev/null 2>&1

if [ $? -ne 0 ]; then
    echo "❌ Build failed"
    exit 1
fi

echo "✅ Build succeeded"

# Run feasibility test
echo ""
echo "=== Feasibility Test ==="
./feasibility_test

# Run performance benchmark
echo ""
echo "=== Performance Benchmark ==="
./performance_benchmark