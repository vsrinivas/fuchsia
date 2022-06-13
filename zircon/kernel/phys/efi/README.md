# kernel/phys/efi -- UEFI application support sharing code with Zircon

This directory provides support code for building UEFI applications using the
`efi_executable()` GN template.  The UEFI build environment provided here is
basically similar to the `phys` environment for `phys_executable()` and
`zbi_executable()`, but somewhat less constrained.  The kernel's basic libc
and the `ktl` subset of standard C++ library features are available.

The `//zircon/kernel/phys/efi:main` target provides entry code that calls the
standard C `main` function with `argc` and `argv`.  `main` will be called with
`argc == 0` when booted directly from the UEFI Boot Manager, but gets full
arguments when launched from the UEFI Shell.

UEFI code can use dynamic relocation freely, and can have static constructors
and destructors.  (Destructors are called only if `main` returns.  There is no
standard C `exit()` function.)

## Unit Testing

Though meant for low-level situations, the UEFI API is just a bunch of C data
structures and function pointers.  So it's eminently amenable to mocking for
unit tests.  The [`//zircon/kernel/lib/efi/testing`](../../lib/efi/testing)
library works with gmock and gtest to make this easy.  So most `phys/efi` code
is written to allow compilation on host platforms for the benefit of unit
tests.  To this end, it uses standard C++ headers and `std::` symbols directly
rather than using `<ktl/....h>` headers and `ktl::` symbols as kernel code
normally does.  Like library code shared between kernel and other contexts, it
must adhere to the strict subset of standard C++ library features that have
`ktl` counterparts defined.

## Running Tests Manually

UEFI supports standard disk partitioning and the VFAT filesystem format, which
is a traditional MS-DOS/Window filesystem format that's what's usually used on
USB thumb drives.  On most UEFI systems, any attached disk (USB, etc.) in this
format will be available to boot or run UEFI applications from.  VFAT file
names are case-insensitive but case-preserving.

### UEFI Boot Manager

When booting from a VFAT filesystem, UEFI firmware will look for the file
`\EFI\BOOT\BOOT$cpu.EFI` where `$cpu` is `X64` on x86 and `AA64` on ARM64.  If
this file exists, it will be run directly.  If no such file exists, some
systems will fall back to running the UEFI Shell.

### UEFI Shell

Some UEFI systems support the UEFI Shell, including the EDK2 reference
implementation used with QEMU and other emulators.  This provides a simple
command-line interface that can launch UEFI applications, pass them arguments,
etc. as well as simple MS-DOS/Unix-like commands for filesystem access and so
on.  When booted into the UEFI shell, UEFI applications can be run directly by
filesystem path, using the extension `.EFI` (explicit or implicit):

```
Shell> efi-hello-world-test
```

UEFI applications run this way can take command-line arguments:

```
Shell> efi-hello-world-test arg1 "argument 2"
```

When the application exits, it will return to the shell prompt.  However,
tests that crash may wedge the system so physical reset (or killing the
emulator) is required.

The UEFI Shell command `reset` can be used to reboot the machine or exit the
emulator.

### QEMU

The Fuchsia checkout includes UEFI firmware images usable with QEMU or other
emulators (with or without KVM).  To run UEFI binaries under QEMU, launch QEMU
manually as normal (e.g. using the same switches `fx qemu` prints out) but
with a few different switches.

First, discard any `-kernel ...`, `-initrd ...`, or `-append ...` switches, as
these will not have any effect.  Replace them with the `-bios` switch giving
the path to the UEFI firmware image:

```
-bios prebuilt/third_party/edk2/x64/OVMF.fd
```

or

```
-bios prebuilt/third_party/edk2/arm64/QEMU_EFI.fd
```

The QEMU virtual machine will attempt to boot from disk or network just like a
real UEFI-based PC would.  If you have a disk image as would go onto a USB
thumb drive, you can use QEMU switches such as `-hda ...` to point at it.
QEMU also offers a convenient feature to mock up a VFAT filesystem image on a
virtual disk device directly from your host filesystem.  To use this, first
populate a small directory with the files you want visible to UEFI.  For
example:

```
mkdir qemu-efi-root
(cd qemu-efi-root; ln -snf ../out/default/kernel.efi_x64/*.efi .)
```

or:

```
mkdir -p qemu-efi-root/efi/boot
ln out/default/kernel.efi_x64/efi-hello-world-test.efi qemu-efi-root/efi/boot/bootx64.efi
```

Then invoke QEMU with this additional switch, providing the path to your
directory after `file=fat:`.

```
-drive driver=vvfat,rw=true,snapshot=true,file=fat:qemu-efi-root
```

Don't put too much into the directory, or the virtual filesystem image will be
too big for QEMU to handle.  (It likely won't work to just point it at your
whole build directory, for example.)  Once QEMU starts, you shouldn't modify
the contents of that directory.  You'll probably need to restart QEMU after
any changes to the directory or the files in it.
