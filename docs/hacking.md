# Notes for hacking on Magenta

This file contains a random collection of notes for hacking on Magenta.

[TOC]

## Building and testing

To ensure changes don't impact any of the builds it is recommended that
that one tests all targets, with gcc and clang, and in both release mode
and debug mode. This can all be executed with the `buildall` script:

```./scripts/buildall -q -c -r```

From the magenta shell run `k ut all` to execute all kernel tests, and
`runtests` to execute all userspace tests.

## Syscall generation

Syscall support is generated from
system/public/magenta/syscalls.sysgen.  A host tool called
[sysgen](../system/host/sysgen) consumes that file and produces output
for both the kernel and userspace in a variety of languages. This
output includes C or C++ headers for both the kernel and userspace,
syscall entry points, other language bindings, and so on.

This tool is invoked as a part of the build, rather than checking in
its output.

## Terminal navigation and keyboard shortcuts

* Alt+Tab switches between VTs
* Alt+F{1,2,...} switches directly to a VT
* Alt+Up/Down scrolls up and down by lines
* Shift+PgUp/PgDown scrolls up and down by half page
* Ctrl+Alt+Delete reboots

## Kernel panics

Since the kernel can't reliably draw to a framebuffer when the GPU is enabled,
the system will reboot by default if the kernel crashes or panics.

If the kernel crashes and the system reboots, the log from the kernel panic will
appear at `/boot/log/last-panic.txt`, suitable for viewing, downloading, etc.

> Please attach your `last-panic.txt` and `magenta.elf` files to any kernel
> panic bugs you file.

If there's a `last-panic.txt`, that indicates that this is the first successful
boot since a kernel panic occurred.

It is not "sticky" -- if you reboot cleanly, it will be gone, and if you crash
again it will be replaced.

To disable reboot-on-panic, pass the kernel commandline argument
[`kernel.halt_on_panic=true`](kernel_cmdline.md#kernel_halt_on_panic_bool).

## Low level kernel development

For kernel development it's not uncommon to need to monitor or break things
before the gfxconsole comes up.

To enable the early console before the graphical console comes up use the
``gfxconsole.early`` cmdline option. More information can be found in
[kernel_cmdline.md](kernel_cmdline.md).
Enabling ``startup.keep-log-visible``will ensure that the kernel log stays
visible if the gfxconsole comes up after boot. To disable the gfxconsole
entirely you can disable the video driver it is binding to via ``driver.<driver
name>.disable``.
On a skylake system, all these options together would look something like:

```
$ tools/build-magenta-x86_64/bootserver build-magenta-x86_64/magenta.bin -- gfxconsole.early driver.intel-i915-display.disable
```

To directly output to the console rather than buffering it (useful in the event
of kernel freezes) you can enable ``ENABLE_KERNEL_LL_DEBUG`` in your ``local.mk`` like so:

```
EXTERNAL_KERNEL_DEFINES := ENABLE_KERNEL_LL_DEBUG=1
```

More information on ``local.mk`` can be found via ``make help``

## Changing the compiler optimization level of a module

You can override the default `-On` level for a module by defining in its
`rules.mk`:

```
MODULE_OPTFLAGS := -O0
```

## Requesting a backtrace

### From within a user process

For debugging purposes, the system crashlogger can print backtraces by
request. It requires modifying your source, but in the absence of a
debugger, or as a general builtin debug mechanism, this can be useful.

```
#include <magenta/crashlogger.h>

void my_function() {
  crashlogger_request_backtrace();
}
```

When crashlogger\_request\_backtrace is called, it causes an
exception used by debuggers for breakpoint handling.
If a debugger is not attached, the system crashlogger will
process the exception, print a backtrace, and then resume the thread.

### From a kernel thread

```
#include <kernel/thread.h>

void my_function() {
  thread_print_backtrace(get_current_thread(), __GET_FRAME(0));
}
```

## Exporting debug data during boot

To support testing the system during early boot, there is a mechanism to export
data files from the kernel to the /boot filesystem. To export a data file,
create a VMO, give it a name, and pass it to userboot with handle\_info of type
PA\_VMO\_DEBUG\_FILE (and argument 0). Then userboot will automatically pass it
throough to devmgr, and devmgr will export the VMO as a file at the path

```
/boot/kernel/<name-of-vmo>
```

This mechanism is used by the entropy collector quality tests to export
relatively large (~1 Mbit) files full of random data.

## How to deprecate #define constants

One can create a deprecated typedef and have the constant definition
cast to that type.  The ensuing warning/error will include the name
of the deprecated typedef.

```
typedef int MX_RESUME_NOT_HANDLED_DEPRECATION __attribute__((deprecated));
#define MX_RESUME_NOT_HANDLED ((MX_RESUME_NOT_HANDLED_DEPRECATION)(2))
```
