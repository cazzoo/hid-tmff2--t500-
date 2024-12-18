#!/bin/bash

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Check if running on Linux
if [ "$(uname)" != "Linux" ]; then
    echo -e "${RED}Error: This application requires a native Linux system${NC}"
    echo "It cannot run on Windows or WSL as it needs direct access to kernel modules"
    exit 1
fi

# Check if running as root (needed for module operations)
if [ "$EUID" -ne 0 ]; then
    echo -e "${RED}Error: Please run as root${NC}"
    echo "This script needs root privileges to check kernel modules"
    echo "Use: sudo ./build.sh"
    exit 1
fi

echo -e "${YELLOW}Checking kernel module...${NC}"

# Check if the module is loaded
if ! lsmod | grep -q "hid_tmff2"; then
    echo -e "${YELLOW}T500RS kernel module not loaded${NC}"
    echo "Please ensure the module is built and loaded first:"
    echo "1. Build the kernel module in the main directory"
    echo "2. Load it using: sudo insmod hid-tmff2.ko"
    exit 1
fi

echo -e "${YELLOW}Checking dependencies...${NC}"

# Check for required tools
for cmd in meson ninja pkg-config; do
    if ! command -v $cmd &> /dev/null; then
        echo -e "${RED}Error: $cmd is not installed${NC}"
        echo "Please install it using: sudo apt install $cmd"
        exit 1
    fi
done

# Check for required development packages
required_pkgs="gtk4 libevdev cairo"
missing_pkgs=""

for pkg in $required_pkgs; do
    if ! pkg-config --exists $pkg; then
        missing_pkgs="$missing_pkgs $pkg"
    fi
done

if [ ! -z "$missing_pkgs" ]; then
    echo -e "${RED}Error: Missing development packages:${NC}$missing_pkgs"
    echo "Please install them using: sudo apt install$(echo "$missing_pkgs" | sed 's/\b\([^ ]*\)\b/lib\1-dev/g')"
    exit 1
fi

echo -e "${GREEN}All dependencies are satisfied${NC}"

# Create build directory if it doesn't exist
if [ ! -d "builddir" ]; then
    echo -e "${YELLOW}Setting up build directory...${NC}"
    meson setup builddir || exit 1
fi

# Build the project
echo -e "${YELLOW}Building project...${NC}"
meson compile -C builddir || exit 1

# Set appropriate permissions for the built binary
chmod +s builddir/t500rs-test

echo -e "${GREEN}Build completed successfully!${NC}"
echo "You can now run the application using: ./run.sh"
