#!/bin/bash
# env_setup.sh - D-RoCM Environment Setup Script
# Installs all required dependencies for building D-RoCM
# Usage: chmod +x env_setup.sh && ./env_setup.sh

set -e

echo "========================================="
echo "D-RoCM Environment Setup"
echo "========================================="

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

log_info() {
    echo -e "${GREEN}[INFO]${NC} $1"
}

log_warn() {
    echo -e "${YELLOW}[WARN]${NC} $1"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# Check if running as root
if [ "$EUID" -ne 0 ]; then 
    log_warn "Please run as root (use sudo)"
    exit 1
fi

# Update package lists
log_info "Updating package lists..."
apt-get update -y

# Install build essentials
log_info "Installing build essentials..."
apt-get install -y build-essential cmake pkg-config

# Install gRPC and Protobuf dependencies
log_info "Installing gRPC and Protobuf dependencies..."
apt-get install -y \
    libprotobuf-dev \
    protobuf-compiler \
    protobuf-compiler-grpc \
    libgrpc++-dev \
    libgrpc-dev \
    grpc-tools

# Install GTest
log_info "Installing Google Test..."
apt-get install -y libgtest-dev

# Build GTest from source (required on some systems)
if [ -d "/usr/src/gtest" ] && [ ! -f "/usr/lib/libgtest.a" ]; then
    log_info "Building GTest from source..."
    cd /usr/src/gtest
    cmake CMakeLists.txt
    make
    cp lib/*.a /usr/lib/
    cd -
fi

# Install spdlog for logging
log_info "Installing spdlog..."
apt-get install -y libspdlog-dev

# Optional: Install benchmark tools
log_info "Installing benchmark dependencies..."
apt-get install -y \
    linux-tools-common \
    linux-tools-generic \
    perf

# Verify installations
log_info "Verifying installations..."

check_command() {
    if command -v $1 &> /dev/null; then
        log_info "$1 installed successfully: $($1 --version 2>&1 | head -n 1)"
    else
        log_error "$1 installation failed!"
        exit 1
    fi
}

check_command "cmake"
check_command "protoc"
check_command "grpc_cpp_plugin"

log_info "========================================="
log_info "Environment setup completed successfully!"
log_info "========================================="
log_info "Next steps:"
log_info "  1. mkdir -p build && cd build"
log_info "  2. cmake .."
log_info "  3. make -j$(nproc)"
log_info "========================================="
