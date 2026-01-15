# Athena Tracer (ptrace)
This tracer uses the ptrace utilities to attach to a program and create a memory trace.

## Usage
Compile using `make` and execute the following command:
```
./main <target-function> <target-binary>
```
Hereby, target function is the function from which the memory trace should start.

### Example
`./main main ./tracee`
