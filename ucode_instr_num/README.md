# PMC Analysis

### Run 
```
# Pin it to a specific core, e.g., 14
taskset -c 14 ./test 
```

### Disable Stack Engine
```
# Offline the SMT first to avoid crash, e.g., core 15
echo 0 | sudo tee /sys/devices/system/cpu/cpu15/online

# Write to MSR
sudo bash -c 'CUR=$(rdmsr 0xc0011029); DISABLED=$(printf "%x" $((0x$ENABLED & ~524288))); wrmsr -p 14 0xc0011029 0x$DISABLED'
```

Then re-run the test, or change the instruction again.