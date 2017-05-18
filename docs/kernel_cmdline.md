# Magenta Kernel Commandline Options

The Magenta kernel receives a textual commandline from the bootloader,
which can be used to alter some behaviours of the system.  Kernel commandline
parameters are in the form of *option* or *option=value*, separated by
spaces, and may not contain spaces.

For boolean options, *option=0*, *option=false*, or *option=off* will
disable the option.  Any other form (*option*, *option=true*, *option=wheee*,
etc) will enable it.

The kernel commandline is passed from the kernel to the userboot process
and the device manager, so some of the options described below apply to
those userspace processes, not the kernel itself.

## aslr.disable

If this option is set, the system will not use Address Space Layout
Randomization.

## crashlogger.disable

If this option is set, the crashlogger is not started. You should leave this
option off unless you suspect the crashlogger is causing problems.

## crashlogger.pt=true

If this option is set, the crashlogger will attempt to generate a
"processor trace" dump along with the crash report. The dump files
are written as /tmp/crash-pt.*. This option requires processor tracing
to be enabled in the kernel. This can be done by running "ipt" program after
the system has booted. E.g., add this to /system/autorun

```
ipt --circular --control init start
```

After the files are written, copy them to the host and print them
with the "ipt-dump" program. See its docs for more info.

This option is only supported on Intel x86 platforms.

## driver.\<name>.disable

Disables the driver with the given name. The driver name comes from the
magenta\_driver\_info, and can be found as the second argument to the
MAGENTA\_DRIVER\_BEGIN macro.

Example: `driver.usb-audio.disable`

## kernel.entropy=\<hex>

Provides entropy to be mixed into the kernel's CPRNG.

## kernel.watchdog=\<bool>
If this option is set (disabled by default), the system will attempt
to detect hangs/crashes and reboot upon detection.

## kernel.halt_on_panic=\<bool>
If this option is set (disabled by default), the system will halt on
a kernel panic instead of rebooting.

## kernel.memory-limit-mb=\<num>

This option tells the kernel to limit system memory to the MB value specified
by 'num'. Using this effectively allows a user to simulate the system having
less physical memory than physically present.

## gfxconsole.early=\<bool>

This option (disabled by default) requests that the kernel start a graphics
console during early boot (if possible), to display kernel debug print
messages while the system is starting.  When userspace starts up, a usermode
graphics console driver takes over.

The early kernel console can be slow on some platforms, so if it is not
needed for debugging it may speed up boot to disable it.

## gfxconsole.font=\<name>

This option asks the graphics console to use a specific font.  Currently
only "9x16" (the default) and "18x32" (a double-size font) are supported.

## ktrace.bufsize

This option specifies the size of the buffer for ktrace records, in megabytes.
The default is 32MB.

## ktrace.grpmask

This option specifies what ktrace records are emitted.
The value is a bitmask of KTRACE\_GRP\_\* values from magenta/ktrace.h.
Hex values may be specified as 0xNNN.

## ldso.trace

This option (disabled by default) turns on dynamic linker trace output.
The output is in a form that is consumable by clients like Intel
Processor Trace support.

## magenta.autorun.boot=\<path>\

This option requests that the executable at *path* be launched at boot,
after devmgr starts up.

## magenta.autorun.system=\<path>\

This option requests that the executable at *path* be launched once the
system partition is mounted and *init* is launched.  If there is no system
bootfs or system partition, it will never be launched.

## smp.maxcpus=\<num>

This option caps the number of CPUs to initialize.  It cannot be greater than
*SMP\_MAX\_CPUS* for a specific architecture.

## smp.ht=\<bool>

This option can be used to disable the initialization of hyperthread logical
CPUs.  Defaults to true.

## startup.keep-log-visible=\<bool>

If this option is set, devmgr will not activate the first interactive
console. It is useful for scenarios in which user input handling (and
the ability to switch vcs) is not available. Defaults to false.

## timer.wallclock=\<name>

This option can be used to force the selection of a particular wall clock.  It
only is used on pc builds.  Options are "tsc", "hpet", and "pit".

## userboot=\<path>

This option instructs the userboot process (the first userspace process) to
execute the specified binary within the bootfs, instead of following the
normal userspace startup process (launching the device manager, etc).

It is useful for alternate boot modes (like a factory test or system
unit tests).

The pathname used here is relative to `/boot`, so it should not start with
a `/` prefix.

Note that this option does not work for executables that are linked with
libraries other than libc and the dynamic linker.

Example: `userboot=bin/core-tests`

## userboot.shutdown

If this option is set, userboot will attempt to power off the machine
when the process it launches exits.

## vdso.soft_ticks=\<bool>

If this option is set, the `mx_ticks_get` and `mx_ticks_per_second` system
calls will use `mx_time_get(MX_CLOCK_MONOTONIC)` in nanoseconds rather than
hardware cycle counters in a hardware-based time unit.  Defaults to false.

# Additional Gigaboot Commandline Options

## bootloader.timeout=\<num>
This option sets the boot timeout in the bootloader, with a default of 3
seconds. Set to zero to skip the boot menu.

## bootloader.fbres=\<w>x\<h>
This option sets the framebuffer resolution. Use the bootloader menu to display
available resolutions for the device.

Example: `bootloader.fbres=640x480`

## bootloader.default=\<network|local>
This option sets the default boot device to netboot or local magenta.bin.

# How to pass the commandline to the kernel

## in Qemu, using scripts/run-magenta*

Pass each option using -c, for example:
```
./scripts/run-magenta-x86-64 -c gfxconsole.font=18x32 -c gfxconsole.early=false
```

## in GigaBoot20x6, when netbooting

Pass the kernel commandline at the end, after a -- separator, for example:
```
bootserver magenta.bin bootfs.bin -- gfxconsole.font=18x32 gfxconsole.early=false
```

## in GigaBoot20x6, when booting from USB flash

Create a text file named "cmdline" in the root of the USB flash drive's
filesystem containing the command line.
