# Guest

The `vmm` app enables booting a guest operating system using the Zircon
hypervisor. The hypervisor and VMM are collectively referred to as "Machina".

These instructions will guide you through creating minimal Zircon and Linux
guests. For instructions on building a more comprehensive Linux guest system
see the [debian_guest](../packages/debian_guest/README.md) package.

These instructions assume a general familiarity with how to netboot the target
device.

## Build host system with the guest package

Configure, build, and boot the guest package as follows:
``` sh
fx set core.${ARCH} --with-base //src/virtualization
fx build
```
Where `${ARCH}` is one of `x64` or `arm64`.

### Note for external developers

(Googlers: You don't need to do this, the Linux images are downloaded from CIPD
by jiri.)

The `linux_guest` package expects the Linux kernel binaries to be in
`prebuilt/virtualization/packages/linux_guest`. You should create them before
running `fx build` by running the following scripts:
```sh
./src/virtualization/packages/linux_guest/mklinux.sh \
  -l /tmp/linux/source \
  -o prebuilt/virtualization/packages/linux_guest/images/${ARCH}/Image \
  -b machina-4.18 \
  ${ARCH}
./src/virtualization/packages/linux_guest/mksysroot.sh \
  -p  prebuilt/virtualization/packages/linux_guest/images/${ARCH}/disk.img \
  -d /tmp/toybox \
  -s /tmp/dash \
  -u \
  ${ARCH}
```

Note: `-b` specifies the branch of `zircon_guest` to use. You can modify this
value if you need a different version or omit it to use a local version.

## Running guests
After netbooting the target device, to run Zircon:
```sh
guest launch zircon_guest
```

Likewise, to launch a Linux guest:
```sh
guest launch linux_guest
```

## Running on QEMU

Running a guest on QEMU on x64 requires kvm (i.e. pass `-k` to `fx run`):
```sh
fx run -k
```

You may also need to enable nested KVM on your host machine. The following
instructions assume a Linux host machine with an Intel processor.

To check whether nested virtualization is enabled, run the following command:
```sh
cat /sys/module/kvm_intel/parameters/nested
```

An output of `Y` indicates nested virtualization is enabled, `0` or `N`
indicates not enabled.

To enable nested virtualization until the next reboot:

```sh
modprobe -r kvm_intel
modprobe kvm_intel nested=1
```

To make the change permanent add the following line to
`/etc/modprobe.d/kvm.conf`:

```
options kvm_intel nested=1
```

Running an arm64 guest on QEMU requires either using GICv2 (pass `-G 2`).

```sh
fx run -G 2
```

Or using a more recent version of QEMU (try 2.12.0). Older versions of QEMU do
not correctly emulate GICv3 when running with multiple guest VCPUs.

```sh
fx run -q /path/to/recent/qemu/aarch64-softmmu
...
guest launch (linux_guest|zircon_guest)
```

## Running from Workstation

To run from Workstation, configure the guest package as follows:
```sh
fx set workstation.x64 --with-base //src/virtualization
```

After booting the guest packages can be launched from the system launcher as
`linux_guest` and `zircon_guest`.

# Guest Configuration

Guest systems can be configured by including a config file inside the guest
package:

```json
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
