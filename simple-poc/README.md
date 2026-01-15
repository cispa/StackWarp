# Simple Userspace PoC

Run the victim pinned to one core (with root to make it more stable, but this is not strictly necessary)

```
sudo taskset -c 15 ./victim
```

Run the attacker (with root) on any core, with the core ID as argument that is the sibling of the victim core

```
sudo ./attacker 14
```

(Here, 14 and 15 are the two hyperthreads)

## Requirements

Ideally, isolate the hyperthreads and allow all MSR writes.

Add the following to the kernel command line (assuming 14 and 15 are the two hyperthreads of one core)

```
isolcpus=domain,managed_irq,14,15 nohz_full=14,15 msr.allow_writes=on
```
