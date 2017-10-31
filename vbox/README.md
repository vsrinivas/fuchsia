# Fuchsia in VirtualBox

Contained herein are scripts that wrap `VBoxManage`, the VirtualBox command
line. They provide a set of conventional defaults and convenience wrappers for
building, running and working with Fuchsia running under VirtualBox. VirtualBox
main interest at present is in its similarity to cloud virtualization (similar
PCI, chipset, IO and feature sets) and increased performance over Qemu for
multi-core and non-KVM platforms.

## Common usage

```
fx full-build
fx box build-disk
fx box create
fx box start
fx box console
```

After running the above commands You now have Fuchsia running in a virtualbox
machine called "fuchsia", and have the serial console piped to your screen.

## Environment variables

Most of the configuration of the fbox scripts is handled by environment
variables rather than flags, so that the scripts can make use of their
inheritence model and avoid a lot of flag juggling. It is likely more variables
will be used over time, as the configuration properties for VirtualBox VMs are
extensive. All of the variables and their default values may be found in
`scripts/vbox/env.sh`. They may all be overridden.

The most frequently needed variable is likely to be `$FUCHSIA_VBOX_NAME` that
defines the name of the VirtualBox VM that the commands will operate on.

## Console

The console command connects to the VM serial port on `0x03f8` IRQ 4. The port
is attached on the host side to a unix socket at `$FUCHSIA_VBOX_CONSOLE_SOCK`.
The `fbox console` script connects to this in a way that provides a functional
terminal. It adjusts the host TTY and then uses `socat(1)` to pipe IO from your
terminal to and from this socket, reseting your TTY on exit.

## Running without X or on GCE

If you do not have a running graphical environment, the `fbox start` command
defaults will fail. In this case you will need to launch in headless mode with
the command `fbox start --type headless`. The script is just a wrapper around
`VBoxManage startvm`, more options are available and may be discovered with
`-h`.

## Interesting network configurations

The VMs configured by these scripts at time of writing simply get a NIC attached
to a NAT implemented on the host side. They will be able to connect out, but
connecting back to the VM through the NAT will require further configuration. At
present there is no extensive wrapping of the network configurations by these
scripts and further work will be needed to make good use of these capabilities.
Due to the nature and complexity of this, it may be better for users needing
that kind of configuration to move to a fuller virtualbox command and control
platform such as [vagrant](https://www.vagrantup.com/). For now, modifying the
VMs that are bootstrapped by this tool directly in the VirtualBox GUI is
probably the easiest approach.

## Debugging VMs

VirtualBox has some useful debugging features. In particular it provides the
ability to invoke nmi, set and get registers and dump the whole vm state. The
debugvm command is wrapped for convenience and available at `fbox debugvm`.
VirtualBox has [documentation for the dump
format](https://www.virtualbox.org/manual/ch12.html#ts_guest-core-format).

## Machine configuration

As mentioned elsewhere, VirtualBox offers a plethora of machine configuration
options. The configuration initially selected as a conventional default here has
the following properties (see create.sh for more details):
 * EFI
 * ACPI
 * IOAPIC
 * HPET
 * ACHI
 * SATA (emulating SSD)
 * Intel 82540EM NIC (same DID as the Qemu NIC)
 * USB 1 (USB 2 & 3 are supported, but require specific guest code)
 * Intel Audio controller (mapped to null, it won't make actual sounds)

## Further VirtualBox information

VirtualBox has a handful of hardware pass-through features that may later be
useful to us. In particular it can pass through USB devices, disks, and even PCI
devices.

Full documentation is online: https://www.virtualbox.org/manual
