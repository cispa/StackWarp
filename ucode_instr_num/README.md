# PMC Analysis

### Offline the SMT
``` bash
# Offline the SMT first to avoid crash: 
echo 0 | sudo tee /sys/devices/system/cpu/cpu<CORE2>/online
```

### Denoise
``` bash
# Disable Op Cache to reduce noices.
sudo bash -c 'CUR=$(rdmsr 0xc0011021); DISOP=$(printf "%x" $((0x$CUR | 32))); wrmsr -p <CORE1> 0xc0011021 0x$DISOP'
```

### Run 
``` bash
# Pin it to a specific core
taskset -c <CORE1> ./test 

# Expected Output:
# Instruction Pair       | Base   | Total  | Delta 
# ----------------------------------------------------------
# push REG               | 110    | 111    | 1     
# pop REG                | 113    | 114    | 1     
# mov rsp, rbp           | 111    | 113    | 2     
# add rsp / sub rsp      | 110    | 112    | 2     
# add rbp / sub rbp      | 112    | 113    | 1     
# add rcx / sub rcx      | 112    | 113    | 1     
# mfence (extra)         | 110    | 116    | 6
```

### Disable Stack Engine
``` bash
# Write to MSR
sudo bash -c 'CUR=$(rdmsr 0xc0011029); DISABLED=$(printf "%x" $((0x$CUR | 524288))); wrmsr -p <CORE1> 0xc0011029 0x$DISABLED'
```

### Re-run 
Then run the test again.
``` bash
taskset -c <CORE1> ./test 

# Expected Output:
# Instruction Pair       | Base   | Total  | Delta 
# ----------------------------------------------------------
# push REG               | 116    | 118    | 2     
# pop REG                | 119    | 121    | 2     
# mov rsp, rbp           | 118    | 119    | 1     
# add rsp / sub rsp      | 116    | 117    | 1     
# add rbp / sub rbp      | 118    | 119    | 1     
# add rcx / sub rcx      | 118    | 119    | 1     
# mfence (extra)         | 116    | 121    | 5
```