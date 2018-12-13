# Guest
The `vmm` app enables booting a guest operating system using the Zircon
hypervisor. The hypervisor and VMM are collectively referred to as "Machina".

These instructions will guide you through creating minimal Zircon and Linux
guests. For instructions on building a more comprehensive linux guest system
see the [debian_guest](pkg/debian_guest/README.md) package.

These instructions assume a general familiarity with how to netboot the target
device.

## Build host system with the guest package
Configure, build, and boot the guest package as follows:
```
$ fx set S{ARCH} --packages garnet/packages/default
$ fx full-build
$ fx pave
```
Where `${ARCH}` is one of `x64` or `arm64`.

### Note for external developers
The linux_guest package expects the Linux kernel binaries to be in
`garnet/bin/guest/pkg/linux_guest`, you should create them before running
`fx full-build` by running the following scripts:
```
$ ./garnet/bin/guest/pkg/linux_guest/mklinux.sh \
    -l /tmp/linux/source \
    -o garnet/bin/guest/pkg/linux_guest/images/${ARCH}/Image \
    -b machina-4.18 \
    ${ARCH}
$ ./garnet/bin/guest/pkg/linux_guest/mksysroot.sh \
    -r \
    -p garnet/bin/guest/pkg/linux_guest/images/${ARCH}/disk.img \
    -d /tmp/toybox \
    -s /tmp/dash \
    S{ARCH}
```

Note: `-b` specifies the branch of zircon_guest to use. You can modify this
value if you need a different version or omit it to use a local version.

## Running guests
After netbooting the target device, to run Zircon:
```
$ guest launch zircon_guest
```

Likewise, to launch a Linux guest:
```
$ guest launch linux_guest
```

## Running on QEMU
Running a guest on QEMU on x64 requires kvm (i.e. pass `-k` to fx run):
```
$ fx run -k
```

Running a guest on QEMU on arm64 requires either using GICv2 (pass `-G 2`).
```
$ fx run -G 2
```

Or using a more recent version of QEMU (try 2.12.0). Older versions of QEMU do
not correctly emulate GICv3 when running with multiple guest VCPUs. If you do
this, then you will need to launch the guest with `gic=3`.
```
$ fx run -q /path/to/recent/qemu/aarch64-softmmu
...
$ guest launch (linux_guest|zircon_guest) --gic=3
```

## Running from Topaz
To run from Topaz, configure the guest package as follows:
```
$ fx set ${ARCH} --packages topaz/packages/topaz,garnet/packages/default
```

After netbooting the guest packages can be launched from the system launcher as
`linux_guest` and `zircon_guest`.

# Guest Configuration
Guest systems can be configured by including a config file inside the guest
package:
```
{
    "type": "object",
    "properties": {
        "kernel": {
            "type": "string"
        },
        "ramdisk": {
            "type": "string"
        },
        "block": {
            "type": "string"
        },
        "cmdline": {
            "type": "string"
        }
    }
}
```
