# Debugging Tips

For general debugging info see the [Fuchsia Debugging Workflow][fuchsia-debugging-doc].

## Generating debug info

There are several make variables used to control the generation of debug info.

### GLOBAL_DEBUGFLAGS

GLOBAL\_DEBUGFLAGS specifies level of debug info to generate.
The default is -g.
A useful value for getting less debug info usable in backtraces is -g1.

### BOOTFS_DEBUG_MODULES

BOOTFS\_DEBUG\_INFO\_FILES allows one to specify which modules
(apps,libs,tests) have their associated debug info included
in the boot image.

The value is a comma-separated list of "module short names"
which are generally `parent_directory/module_directory`.
E.g., `ulib/launchpad,utest/debugger`
Make-style patterns (%) are allowed, e.g., `ulib/%,utest/debugger`.

The default is empty (meaning none).

## Adding debug info to boot image

By default the boot image does not contain debug info as it
can require a lot of extra space. Adding debug info is useful when
using tools like debuggers natively. Note that this does not apply
to cross debugging where the debugger is running on separate machine.
Adding debug info to the boot image is for when you are running debugging
tools on zircon itself.

Example:
```
$ make -j10 x86 BOOTFS_DEBUG_MODULES=ulib/%,utest/debugger GLOBAL_DEBUGFLAGS=-g1
```

This example will include in the boot image debug info files for all
shared libraries and for the "debugger" test program. To reduce the amount
of debug info to just that usable in backtraces `GLOBAL_DEBUGFLAGS=-g1`
is passed.

## Debugging the kernel with QEMU+GDB.

See "Debugging the kernel with GDB" in [QEMU](../qemu.md) for
documentation on debugging zircon with QEMU+GDB.

[fuchsia-debugging-doc]: https://fuchsia.googlesource.com/docs/+/master/development/workflows/debugging.md
