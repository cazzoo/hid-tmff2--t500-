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

# Check if the binary exists
if [ ! -f "builddir/t500rs-test" ]; then
    echo -e "${RED}Error: Application not built${NC}"
    echo "Please run: sudo ./build.sh"
    exit 1
fi

# Check if the module is loaded
if ! lsmod | grep -q "hid_tmff2"; then
    echo -e "${RED}Error: T500RS kernel module not loaded${NC}"
    echo "Please load the module first:"
    echo "sudo insmod ../hid-tmff2.ko"
    exit 1
fi

# Check if the T500RS device exists and is accessible
if [ ! -e "/dev/input/by-id/usb-Thrustmaster_T500RS_Racing_Wheel-event-joystick" ]; then
    echo -e "${RED}Error: T500RS device not found${NC}"
    echo "Please ensure:"
    echo "1. The T500RS is connected via USB"
    echo "2. The kernel module is loaded correctly"
    echo "3. You have the correct permissions to access the device"
    exit 1
fi

# Run the application
echo -e "${GREEN}Starting T500RS Test Application...${NC}"
./builddir/t500rs-test
