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

## crashlogger.disable

If this option is set, the crashlogger is not started. You should leave this
option off unless you suspect the crashlogger is causing problems.

## kernel.watchdog=<bool>
If this option is set (disabled by default), the system will attempt
to detect hangs/crashes and reboot upon detection.

## gfxconsole.early=<bool>

This option (disabled by default) requests that the kernel start a graphics
console during early boot (if possible), to display kernel debug print
messages while the system is starting.  When userspace starts up, a usermode
graphics console driver takes over.

The early kernel console can be slow on some platforms, so if it is not
needed for debugging it may speed up boot to disable it.

## gfxconsole.font=<name>

This option asks the graphics console to use a specific font.  Currently
only "9x16" (the default) and "18x32" (a double-size font) are supported.

## smp.maxcpus=<num>

This option caps the number of CPUs to initialize.  It cannot be greater than
*SMP\_MAX\_CPUS* for a specific architecture.

## smp.ht=<bool>

This option can be used to disable the initialization of hyperthread logical
CPUs.  Defaults to true.

## timer.wallclock=<name>

This option can be used to force the selection of a particular wall clock.  It
only is used on pc builds.  Options are "tsc", "hpet", and "pit".

## userboot=<path>

This option instructs the userboot process (the first userspace process) to
execute the specified binary within the bootfs, instead of following the
normal userspace startup process (launching the device manager, etc).

It is useful for alternate boot modes (like a factory test or system
unit tests).

## userboot.shutdown

If this option is set, userboot will attempt to power off the machine
when the process it launches exits.


# How to get pass the commandline to the kernel

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
