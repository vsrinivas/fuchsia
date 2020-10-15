# Zircon Kernel Boot Options

This library provides a single source of truth and centralized definition for
all boot options that the kernel parses from the "kernel command line" passed
via `ZBI_TYPE_CMDLINE`.

See [`options.inc`](include/lib/boot-options/options.inc) for details on how to
define a boot option.  All options are defined in that file or in an additional
[`$cpu.inc` file](include/lib/boot-options/x86.inc) for architecture-specific
options.

## BootOptions class

Code in the kernel uses the
[`BootOptions` class](include/lib/boot-options/boot-options.h) to
read boot option values.  Each option is a simple member held in the internal
form of its type (`uint32_t`, `bool`, `enum Something`, etc.), so just
accessing `gBootOptions->my_option` is all that's required.

In ['physboot`](../../phys) startup, each `ZBI_TYPE_CMDLINE` item is fed to
`BootOptions::Set` to parse any recognized options and update the members.

[**TODO(53594):**](https://fxbug.dev/53594) Currently only `physboot` makes use of
these options.  Eventually, `physboot` will hand off the `BootOptions`
structure to the kernel proper and that will be the only means of accessing
boot options inside the kernel.

To support this hand-off between `physboot` and the kernel proper, all data
types used in `BootOptions` members are restricted to trivially-copyable data
with no pointers.

## Documentation and JSON generation

The build generates two files, `boot-options.json` and `boot-options.md` in the
(Zircon) root build directory.  The JSON data provides information on each
option supported by `BootOptions` (and thus by the kernel) in a simple
machine-readable form.

This information is formatted into Markdown along with a fixed header and
footer to produce `boot-options.md`.  The build compares the generated file to
[`//docs/gen/boot-options.md`](../../../../docs/gen/boot-options.md).  If they
differ, the build breaks and shows a recommended `cp` command to update the
file in the source tree.  Hence every update to `options.inc` must be
accompanied by a re-generation of the documentation.
