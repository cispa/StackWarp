# NOTE: exit with C-a x

desktop=$(current-desktop)

set -e
if [ -z "$LINUX_SRC" ]; then
    echo "Error: LINUX_SRC not set"
    exit 1
fi
if [ -z "$MY_SSH_KEY" ]; then
    echo "Error: MY_SSH_KEY not set"
    exit 1
fi
nix build .#nixosConfigurations.vm.config.system.build.vm --impure
sleep 1
terminal --class "qemu-gdb" --desktop "$desktop" --no-focus --no-follow -e gef -ex "set auto-load safe-path /" -ex "file $LINUX_SRC/vmlinux" -ex "gef-remote localhost 1234 --qemu-user --qemu-binary $LINUX_SRC/vmlinux" -x exploit-gdb.gdb
NIXPKGS_QEMU_KERNEL_vm=$(realpath "$LINUX_SRC/arch/x86/boot/bzImage") ./result/bin/run-vm-vm -s -serial mon:stdio -nographic
