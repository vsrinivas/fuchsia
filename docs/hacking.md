# Notes for hacking on Magenta

This file contains a random collection of notes for hacking on Magenta.

## syscall generation

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

## Requesting a backtrace from within a program

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
