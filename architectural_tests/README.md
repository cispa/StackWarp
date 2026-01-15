# Architectural Behavior Tests

Tools for testing instruction behavior while changing MSR settings.

### Preparation
The list of MSRs used in the tests is hard-coded.
To add a custom set of MSRs, add an array containing the MSR IDs and a mask of writable bits to `msr_lists.h`.
For instance, the following adds bit 19 of the undocumented MSR behind StackWarp: 
```C
static const struct msr_mask msr_list_stackwarp[] = {
    {/* MSR ID */ 0xc0011029, /* Bit Mask */ 0x80000},
    // ...
};
```
Then, change the definition of the `MSR_LIST` macro to the name of your array. Example:
```C
#define MSR_LIST msr_list_stackwarp
```
An example list for Intel Coffee Lake processors is already in the file.

### Building
Install the build dependencies as follows:
```shell
sudo apt-get install cmake libasmjit-dev libzstd-dev linux-headers-amd64
```
Then, build the tests with
```shell
cmake -B build && make -j$(nproc) -C build
```
A full rebuild is required after changing the MSR list.

### Running
Before running the tests, insert the MSR access kernel module as follows:
```shell
sudo insmod msr_flipper.ko
```

There are three main test programs:
 * `all_instructions` runs a single sequence of all instructions that your system supports, and that produces a deterministic program state. Expect a long runtime.
 * `single_instructions` tests the behavior of individual instructions.
 * `sequences` tests a collection of hand-crafted test instruction sequences. See `tests/control_flow.s` and `tests/data_flow.s` for their definitions.

Execute these programs as root to start a test run. Differences in instruction behavior are logged to the console output.

## Warning
 * Modifying MSRs can endanger the stability of your system and potentially damage data in use. Run this tool at your own risk.
 * Running the tool on the affected MSR in StackWarp is not recommended, as the tool can cause stack corruption in the kernel, leading to a system crash.
