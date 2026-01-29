#!/bin/bash
# Check for MSR access
if sudo rdmsr 0xC0011029 > /dev/null 2>&1; then
    echo "[+] MSR access: SUCCESS"
else
    echo "[-] MSR access: FAILED (Check if 'msr' module is loaded and you have sudo)"
fi

# Check if Userspace RDPMC is enabled
# On many systems, /sys/devices/cpu/rdpmc must be 1 or 2
RDPMC_VAL=$(sudo cat /sys/devices/cpu/rdpmc 2>/dev/null)
if [[ "$RDPMC_VAL" == "2" ]]; then
    echo "[+] RDPMC userspace: SUCCESS"
else
    echo "[-] RDPMC userspace: DISABLED (Run 'echo 2 | sudo tee /sys/devices/cpu/rdpmc')"
fi