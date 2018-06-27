# Zircon Kernel Commandline Options

The Zircon kernel receives a textual commandline from the bootloader,
which can be used to alter some behaviours of the system.  Kernel commandline
parameters are in the form of *option* or *option=value*, separated by
spaces, and may not contain spaces.

For boolean options, *option=0*, *option=false*, or *option=off* will
disable the option.  Any other form (*option*, *option=true*, *option=wheee*,
etc) will enable it.

The kernel commandline is passed from the kernel to the userboot process
and the device manager, so some of the options described below apply to
those userspace processes, not the kernel itself.

The devmgr reads the file /boot/config/devmgr (if it exists) at startup
and imports name=value lines into its environment, augmenting or overriding
the values from the kernel commandline.  Leading whitespace is ignored and
lines starting with # are ignored.  Whitespace is not allowed in names.

## aslr.disable

If this option is set, the system will not use Address Space Layout
Randomization.

## crashsvc.analyzer=\<service-host\>

If this is empty, the default crash analyzer in svchost will be used
which logs exception information and a backtrace to the system log. If
it is set, the crash analyzer will be found in the given service bundle.
The only valid non-empty value for this currently is "from-appmgr".

The analyzer process is passed two startup handles: the process and
thread that sustained the exception.

## devmgr\.epoch=\<seconds\>

Sets the initial offset (from the Unix epoch, in seconds) for the UTC clock.
This is useful for platforms lacking an RTC, where the UTC offset would
otherwise remain at 0.

## devmgr\.require-system=\<bool\>

Instructs the devmgr that a /system volume is required.  Without this,
devmgr assumes this is a standalone Zircon build and not a full Fuchsia
system.

## devmgr\.suspend-timeout-debug

If this option is set, the system prints out debugging when mexec, suspend,
reboot, or power off did not finish in 10 seconds.

## devmgr\.suspend-timeout-fallback

If this option is set, the system invokes kernel fallback to reboot or poweroff
the device when the operation did not finish in 10 seconds.

## devmgr\.devhost\.asan

This option must be set if any drivers not included directly in /boot are built
with `-fsanitize=address`.  If there are `-fsanitize=address` drivers in /boot,
then all `-fsanitize=address` drivers will be supported regardless of this
option.  If this option is not set and there are no such drivers in /boot, then
drivers built with `-fsanitize=address` cannot be loaded and will be rejected.

## driver.\<name>.disable

Disables the driver with the given name. The driver name comes from the
zircon\_driver\_info, and can be found as the first argument to the
ZIRCON\_DRIVER\_BEGIN macro.

Example: `driver.usb_audio.disable`

## driver.\<name>.log=\<flags>

Set the log flags for a driver.  Flags are one or more comma-separated
values which must be preceded by a "+" (in which case that flag is enabled)
or a "-" (in which case that flag is disabled).  The textual constants
"error", "warn", "info", "trace", "spew", "debug1", "debug2", "debug3", and "debug4"
may be used, and they map to the corresponding bits in DDK_LOG_... in `ddk/debug.h`
The default log flags for a driver is "error", "warn", and "info".

Individual drivers may define their own log flags beyond the eight mentioned
above.

Example: `driver.usb_audio.log=-error,+info,+0x1000`

Note again that the name of the driver is the "Driver" argument to the
ZIRCON\_DRIVER\_BEGIN macro. It is not, for example, the name of the device,
which for some drivers is almost identical, except that the device may be
named "foo-bar" whereas the driver name must use underscores, e.g., "foo_bar".

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

## iommu.enable=\<bool>

This option (disabled by default) allows the system to use a hardware IOMMU
if present.

## kernel.entropy-mixin=\<hex>

Provides entropy to be mixed into the kernel's CPRNG.

## kernel.entropy-test.len=\<len>

When running an entropy collector quality test, collect the provided number of
bytes. Defaults to the maximum value `ENTROPY_COLLECTOR_TEST_MAXLEN`.

The default value for the compile-time constant `ENTROPY_COLLECTOR_TEST_MAXLEN`
is 1MiB.

## kernel.entropy-test.src=\<source>

When running an entropy collector quality test, use the provided entropy source.
Currently recognized sources: `hw_rng`, `jitterentropy`. This option is ignored
unless the kernel was built with `ENABLE_ENTROPY_COLLECTOR_TEST=1`.

## kernel.halt-on-panic=\<bool>
If this option is set (disabled by default), the system will halt on
a kernel panic instead of rebooting.

## kernel.jitterentropy.bs=\<num>

Sets the "memory block size" parameter for jitterentropy (the default is 64).
When jitterentropy is performing memory operations (to increase variation in CPU
timing), the memory will be accessed in blocks of this size.

## kernel.jitterentropy.bc=\<num>

Sets the "memory block count" parameter for jitterentropy (the default is 512).
When jitterentropy is performing memory operations (to increase variation in CPU
timing), this controls how many blocks (of size `kernel.jitterentropy.bs`) are
accessed.

## kernel.jitterentropy.ml=\<num>

Sets the "memory loops" parameter for jitterentropy (the default is 32). When
jitterentropy is performing memory operations (to increase variation in CPU
timing), this controls how many times the memory access routine is repeated.
This parameter is only used when `kernel.jitterentropy.raw` is true. If the
value of this parameter is `0` or if `kernel.jitterentropy.raw` is `false`,
then jitterentropy chooses the number of loops is a random-ish way.

## kernel.jitterentropy.ll=\<num>

Sets the "LFSR loops" parameter for jitterentropy (the default is 1). When
jitterentropy is performing CPU-intensive LFSR operations (to increase variation
in CPU timing), this controls how many times the LFSR routine is repeated.  This
parameter is only used when `kernel.jitterentropy.raw` is true. If the value of
this parameter is `0` or if `kernel.jitterentropy.raw` is `false`, then
jitterentropy chooses the number of loops is a random-ish way.

## kernel.jitterentropy.raw=\<bool>

When true (the default), the jitterentropy entropy collector will return raw,
unprocessed samples. When false, the raw samples will be processed by
jitterentropy, producing output data that looks closer to uniformly random. Note
that even when set to false, the CPRNG will re-process the samples, so the
processing inside of jitterentropy is somewhat redundant.

## kernel.memory-limit-mb=\<num>

This option tells the kernel to limit system memory to the MB value specified
by 'num'. Using this effectively allows a user to simulate the system having
less physical memory than physically present.

## kernel.oom.enable=\<bool>

This option (true by default) turns on the out-of-memory (OOM) kernel thread,
which kills processes when the PMM has less than `kernel.oom.redline_mb` free
memory, sleeping for `kernel.oom.sleep_sec` between checks.

The OOM thread can be manually started/stopped at runtime with the `k oom start`
and `k oom stop` commands, and `k oom info` will show the current state.

See `k oom` for a list of all OOM kernel commands.

## kernel.oom.redline-mb=\<num>

This option (50 MB by default) specifies the free-memory threshold at which the
out-of-memory (OOM) thread will trigger a low-memory event and begin killing
processes.

The `k oom info` command will show the current value of this and other
parameters.

## kernel.oom.sleep-sec=\<num>

This option (1 second by default) specifies how long the out-of-memory (OOM)
kernel thread should sleep between checks.

The `k oom info` command will show the current value of this and other
parameters.

## kernel.mexec-pci-shutdown=\<bool>

If false, this option leaves PCI devices running when calling mexec. Defaults
to true.

## kernel.serial=\<string\>

This controls what serial port is used.  If provided, it overrides the serial
port described by the system's bootdata.

If set to "none", the kernel debug serial port will be disabled.

### x64 specific values

On x64, some additional values are supported for configuring 8250-like UARTs:
- If set to "legacy", the legacy COM1 interface is used.
- A port-io UART can be specified using "ioport,\<portno>,\<irq>".
- An MMIO UART can be specified using "mmio,\<physaddr>,\<irq>".

For example, "ioport,0x3f8,4" would describe the legacy COM1 interface.

All numbers may be in any base accepted by *strtoul*().

All other values are currently undefined.

## kernel.shell=\<bool>

This option tells the kernel to start its own shell on the kernel console
instead of a userspace sh.

## kernel.smp.maxcpus=\<num>

This option caps the number of CPUs to initialize.  It cannot be greater than
*SMP\_MAX\_CPUS* for a specific architecture.

## kernel.smp.ht=\<bool>

This option can be used to disable the initialization of hyperthread logical
CPUs.  Defaults to true.

## kernel.wallclock=\<name>

This option can be used to force the selection of a particular wall clock.  It
only is used on pc builds.  Options are "tsc", "hpet", and "pit".

## ktrace.bufsize

This option specifies the size of the buffer for ktrace records, in megabytes.
The default is 32MB.

## ktrace.grpmask

This option specifies what ktrace records are emitted.
The value is a bitmask of KTRACE\_GRP\_\* values from zircon/ktrace.h.
Hex values may be specified as 0xNNN.

## ldso.trace

This option (disabled by default) turns on dynamic linker trace output.
The output is in a form that is consumable by clients like Intel
Processor Trace support.

## thread.set.priority.disable=\<bool>

This option (false by default) prevents the syscall that increases
a thread priority (`zx_thread_set_priority`) from taking effect.

## zircon.autorun.boot=\<command>

This option requests that *command* be run at boot, after devmgr starts up.

Any `+` characters in *command* are treated as argument separators, allowing
you to pass arguments to an executable.

### Sidebar: Injecting a personal autorun script <a name="autorun"></a>

It's often useful to inject a personal autorun script into the boot filesystem,
especially for running tests or setup tasks. You can do this with the zircon
build system's `EXTRA_USER_MANIFEST_LINES` make variable.

1.  Create a local shell script with the commands you want to run; for this
    example, call it `${HOME}/my_local_script.sh`. \
    **NOTE**: The first line must be a `#!` line, typically `#!/boot/bin/sh`.
2.  Add your script to the boot filesystem by invoking `make` (or one of the
    `//zircon/scripts/build-*` scripts) with \
    `EXTRA_USER_MANIFEST_LINES="my_installed_script=${HOME}/my_local_script.sh"`
    \
    This will copy your local script into the boot filesystem at the path
    `/boot/my_installed_script`. (You can change the `my_installed_script` part
    to change the basename of the installed script, though it will always appear
    under `/boot`.)
3.  Tell `devmgr` to invoke your script by passing a commandline like
    `zircon.autorun.boot=/boot/my_installed_script`

## zircon.autorun.system=\<command>

This option requests that *command* be run once the system partition is mounted
and *init* is launched.  If there is no system bootfs or system partition, it
will never be launched.

Any `+` characters in *command* are treated as argument separators, allowing
you to pass arguments to an executable.

## zircon.system.blob-init=\<command>

**DEPRECATED** See [`zircon.system.pkgfs.cmd`](#zircon.system.pkgfs.cmd).

This option requests that *command* be run once the blob partition is
mounted. The given command is expected to mount /system, and then signal its
process handle with `ZX_USER_SIGNAL_0`.

*command* may not assume that any other filesystem has been mounted. If
`zircon.system.blob-init-arg` is set, it will be provided as the first
argument.

A ramdisk containing `/system` takes precedence over
`zircon.system.blob-init` and *command* will not be run if a system
ramdisk is present. blob init will take precedence over a minfs
partition with the system GUID, and the minfs partition will not be mounted
if `zircon.system.blob-init` is set.

## zircon.system.disable-automount=\<bool>

This option prevents the fshost from auto-mounting any disk filesystems
(/system, /data, etc), which can be useful for certain low level test setups.
It is false by default.  It is implied by **netsvc.netboot=true**

## zircon.system.pkgfs.cmd=\<command>

This option requests that *command* be run once the blob partition is mounted.
Any `+` characters in *command* are treated as argument separators, allowing
you to pass arguments to an executable.

The executable and its dependencies (dynamic linker and shared libraries) are
found in the blob filesystem.  The executable *path* is *command* before the
first `+`.  The dynamic linker (`PT_INTERP`) and shared library (`DT_NEEDED`)
name strings sent to the loader service are prefixed with `lib/` to produce a
*path*.  Each such *path* is resolved to a blob ID (i.e. merkleroot in ASCII
hex) using the `zircon.system.pkgfs.file.`*path* command line argument.  In
this way, `/boot/config/devmgr` contains a fixed manifest of files used to
start the process.

The new process receives a `PA_USER0` channel handle at startup that will be
used as the client filesystem handle mounted at `/pkgfs`.  The command is
expected to start serving on this channel and then signal its process handle
with `ZX_USER_SIGNAL_0`.  Then `/pkgfs/system` will be mounted as `/system`.

## zircon.system.pkgfs.file.*path*=\<blobid>

Used with [`zircon.system.pkgfs.cmd`](#zircon.system.pkgfs.cmd), above.

## zircon.system.writable=\<bool>

This option requests that if a minfs partition with the system type GUID is
found, it is to be mounted read-write rather than read-only.

## zircon.system.volume=\<arg>

This option specifies where to find the "/system" volume.

It may be set to:
"any", in which case the first volume of the appropriate type will be used.
"local" in which the first volume that's non-removable of the appropriate type
  will be used.
"none" (default) which avoids mounting anything.

A "/system" ramdisk provided by bootdata always supersedes this option.

## netsvc.netboot=\<bool>

If true, zircon will attempt to netboot into another instance of zircon upon
booting.

More specifically, zircon will fetch a new zircon system from a bootserver on
the local link and attempt to kexec into the new image, thereby replacing the
currently running instance of zircon.

This setting implies **zircon.system.disable-automount=true**

## netsvc.advertise=\<bool>

If true, netsvc will seek a bootserver by sending netboot advertisements.
Defaults to true.

## netsvc.interface=\<path>

This option instructs netsvc to use only the ethernet device at the given
topological path. All other ethernet devices are ignored by netsvc. The
topological path for a device can be determined from the shell by running the
`lsdev` command on the ethernet class device (e.g., `/dev/class/ethernet/000`).

This is useful for configuring network booting for a device with multiple
ethernet ports which may be enumerated in a non-deterministic order.

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

If this option is set, the `zx_ticks_get` and `zx_ticks_per_second` system
calls will use `zx_clock_get_monotonic()` in nanoseconds rather than
hardware cycle counters in a hardware-based time unit.  Defaults to false.

## virtcon.disable

Do not launch the virtual console service if this option is present.

## virtcon.hide-on-boot

If this option is present, the virtual console will not take ownership of any
displays until the user switches to it with a device control key combination.

## virtcon.keep-log-visible

If this option is present, the virtual console service will keep the
debug log (vc0) visible instead of switching to the first shell (vc1) at startup.

## virtcon.keymap=\<name>

Specify the keymap for the virtual console.  "qwerty" and "dvorak" are supported.

## virtcon.font=\<name>

Specify the font for the virtual console.  "9x16" and "18x32" are supported.

## zircon.nodename=\<name>

Set the system nodename, as used by `bootserver`, `loglistener`, and the
`net{addr,cp,ls,runcmd}` tools.  If omitted, the system will generate a
human-readable nodename from its MAC address.  This cmdline is honored by
GigaBoot and Zircon.

## console.path=\<path>

Specify console device path. If not specified device manager will open
`/dev/misc/console`. Only has effect if kernel.shell=false.

# Additional Gigaboot Commandline Options

## bootloader.timeout=\<num>
This option sets the boot timeout in the bootloader, with a default of 3
seconds. Set to zero to skip the boot menu.

## bootloader.fbres=\<w>x\<h>
This option sets the framebuffer resolution. Use the bootloader menu to display
available resolutions for the device.

Example: `bootloader.fbres=640x480`

## bootloader.default=\<network|local|zedboot>
This option sets the default boot device to netboot, use a local zircon.bin or to netboot via zedboot.

# How to pass the commandline to the kernel

## in Qemu, using scripts/run-zircon*

Pass each option using -c, for example:
```
./scripts/run-zircon-x64 -c gfxconsole.font=18x32 -c gfxconsole.early=false
```

## in GigaBoot20x6, when netbooting

Pass the kernel commandline at the end, after a -- separator, for example:
```
bootserver zircon.bin bootfs.bin -- gfxconsole.font=18x32 gfxconsole.early=false
```

## in GigaBoot20x6, when booting from USB flash

Create a text file named "cmdline" in the root of the USB flash drive's
filesystem containing the command line.
