# Guest

The `vmm` app enables booting a guest operating system using the Zircon
hypervisor. The hypervisor and VMM are collectively referred to as "Machina".

These instructions will guide you through creating minimal Zircon and Linux
guests. For instructions on building a more comprehensive Linux guest system
see the [debian_guest](../packages/debian_guest/README.md) package.

These instructions assume familiarity with how to build Fuchsia images and
boot them on your target device.

## Build host system with the guest package

Configure, build, and boot the guest package as follows:
``` sh
fx set core.x64 --with-base //src/virtualization
fx build
```
For ARM64 targets, replace `x64` with `arm64` or the appropriate board name.

### Note for external developers

***_Googlers: You don't need to do this, the Linux images are downloaded from
CIPD by Jiri.***

The `debian_guest` package expects the Linux kernel binaries and userspace
image to be in `prebuilt/virtualization/packages/debian_guest`. You should
create them before running `fx build` by following the instructions in
`debian_guest/README.md`.

## Running guests

After booting the target device, to run Zircon:
```sh
guest launch zircon_guest
```

Likewise, to launch a Debian guest:
```sh
guest launch debian_guest
```

## Running on QEMU

Running a guest on QEMU on x64 requires KVM (i.e. pass `-k` to `fx qemu`):
```sh
fx qemu -k
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

## Running from Workstation

To run from Workstation, configure the guest package as follows:
```sh
fx set workstation.x64 --with-base //src/virtualization
```

After booting the guest packages can be launched from the system launcher as
`debian_guest` and `zircon_guest`.

## Integration tests

Machina has a set of integration tests that launch Zircon and Debian guests to test the VMM,
hypervisor, and each of the virtio devices. To run the tests:
```sh
fx set core.x64 --with-base //src/virtualization:tests
fx build
fx test //src/virtualization/tests
```

For ARM64 targets, replace `x64` with `arm64` or the appropriate board name.

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

# Monitor Guest exit statistics

`kstats -v`  can print the number of Guest exits per second and the reason.
For example, the output on ARM64 appears as follows:

```
cpu   vm_entry vm_exit inst_abt data_abt wfx_inst sys_inst smc_inst ints
  0      43        43        0      6      27        1        0      9
  1     226       225        0    111       3       17        0     94
  2     109       109        0     60       8        7        0     35
  3      58        58        0     21      12        2        0     23
```

The fields in the output are as follows:
- `inst_abt`: The amount of instruction abort exit.
- `data_abt`: The amount of data abort exit.
- `wfx_inst`: The amount of instruction wfe/wfi exit.
- `sys_inst`: The amount of systen register access exit.
- `smc_inst`: The amount of instruction smc exit.
- `ints`    : The amount of interrupt exit.

For x86_64, the output is as follows:

```
 cpu    vm_entry vm_exit ints ints_win ept ctrl_reg msr(rd wr) inst(io hlt cpuid ple vmcall xsetbv)
  0       40       40     10      0     0      0        10 10        5  5    0    0    0      0
```

With the following fields:
- `ints`     : The amount of interrupt exit.
- `ints_win` : The amount of interrupt window exit.
- `ept`      : The amount of EPT violation exit.
- `ctrl_reg` : The amount of control register(CRx) access exit.
- `msr`      : The amount of MSR register access exit (`rd/wr` is read/write).
- `inst`     : the amount of some kinds of instruction exit.
