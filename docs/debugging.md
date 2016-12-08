# Debugging Tips

## Adding debug info to boot image

By default the boot image does not contain debug info as it
can require a lot of extra space. Adding debug info is useful when
using tools like debuggers natively.

There are two make variables used to control the generation of debug info
and its inclusion in the boot image.

`GLOBAL_DEBUGFLAGS`: Specifies level of debug info to generate.
The default is `-g`.
A useful value for getting less debug info usable in backtraces is `-g1`.

`USER_DEBUG_MODULES`: Allows one to specify which modules
(apps,libs,tests) have their associated debug info included
in the boot image.
The value is a comma-separated list of "module short names"
which are generally `parent_directory/module_directory`.
E.g., `ulib/launchpad,utest/debugger`
The default is empty (meaning none).
Make-style patterns (%) are allowed, e.g., `ulib/%,utest/debugger`

Example:
```
$ make -j10 magenta-pc-x86-64 USER_DEBUG_MODULES=ulib/%,utest/debugger GLOBAL_DEBUGFLAGS=-g1
```

## Debugging the kernel with QEMU+GDB.

See "Debugging the kernel with GDB" in [QEMU](qemu.md) for
documentation on debugging magenta with QEMU+GDB.
