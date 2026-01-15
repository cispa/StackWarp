## Run

``` bash
# Establish a baseline signature.
make all 

# Iterate signing operations until a fault triggers a signature mismatch.
make fault

<Fault Injection with StackWarp>
```


``` bash
# On signature change, execute the script below for key recovery.
python3 exploit.py
```