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

If keys are repeated, the last value takes precedence, that is, later settings
override earlier ones.

The devmgr reads the file /boot/config/devmgr (if it exists) at startup
and imports name=value lines into its environment, augmenting or overriding
the values from the kernel commandline.  Leading whitespace is ignored and
lines starting with # are ignored.  Whitespace is not allowed in names.

## aslr.disable

If this option is set, the system will not use Address Space Layout
Randomization.

## aslr.entropy_bits=\<num\>

For address spaces that use ASLR this controls the number of bits of entropy in
the randomization. Higher entropy results in a sparser address space and uses
more memory for page tables. Valid values range from 0-36, with default being 30.

## bootsvc.next=\<bootfs path\>

Controls what program is executed by bootsvc to continue the boot process.
If this is not specified, the default next program will be used.

Arguments to the program can optionally be specified using a comma separator
between the program and individual arguments. For example,
'bootsvc.next=bin/mybin,arg1,arg2'.

## clock\.backstop=\<seconds\>

Sets the initial offset (from the Unix epoch, in seconds) for the UTC clock.
The clock will be set by the device coordinator at boot time, and then later,
if an RTC is present, the RTC clock will be sanitized to at least this time.

## cpu\.hwp=\<bool\>

This settings enables HWP (hardware P-states) on supported chips. This feature
lets Intel CPUs automatically scale their own clock speed. Defaults to false.

## devmgr\.require-system=\<bool\>

Instructs the devmgr that a /system volume is required. Without this, devmgr
assumes this is a standalone Zircon build and not a full Fuchsia system.

## devmgr\.suspend-timeout-fallback

If this option is set, the system invokes kernel fallback to reboot or poweroff
the device when the operation did not finish in 10 seconds.

## devmgr\.devhost\.asan

This option must be set if any drivers not included directly in /boot are built
with `-fsanitize=address`.  If there are `-fsanitize=address` drivers in /boot,
then all `-fsanitize=address` drivers will be supported regardless of this
option.  If this option is not set and there are no such drivers in /boot, then
drivers built with `-fsanitize=address` cannot be loaded and will be rejected.

## devmgr\.devhost\.strict-linking

If this option is set, devmgr will only allow `libasync-default.so`,
`libdriver.so`, and `libfdio.so` to be dynamically linked into a devhost. This
prevents drivers from dynamically linking with libraries that they should not.
All other libraries should be statically linked into a driver.

## devmgr\.verbose

Turn on verbose logging.

## driver.\<name>.compatibility-tests-enable

If this option is set, devmgr will run compatibility tests for the driver.
zircon\_driver\_info, and can be found as the first argument to the
ZIRCON\_DRIVER\_BEGIN macro.

## driver.\<name>.compatibility-tests-wait-time

This timeout lets you configure the wait time in milliseconds for each of
bind/unbind/suspend hooks to complete in compatibility tests.
zircon\_driver\_info, and can be found as the first argument to the
ZIRCON\_DRIVER\_BEGIN macro.

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

## driver.\<name>.tests.enable=\<bool>

Enable the unit tests for an individual driver. The unit tests will run before
the driver binds any devices. If `driver.tests.enable` is true then this
defaults to enabled, otherwise the default is disabled.

Note again that the name of the driver is the "Driver" argument to the
ZIRCON\_DRIVER\_BEGIN macro. It is not, for example, the name of the device,
which for some drivers is almost identical, except that the device may be
named "foo-bar" whereas the driver name must use underscores, e.g., "foo_bar".

## driver.sysmem.protected_memory_size=\<num>

Overrides the board-driver-specified size for sysmem's default protected memory
pool. Value is in bytes.

## driver.sysmem.protected_memory_size=\<num>

Overrides the board-driver-specified size for sysmem's contiguous memory pool.
Value is in bytes.

## driver.tests.enable=\<bool>

Enable the unit tests for all drivers. The unit tests will run before the
drivers bind any devices. It's also possible to enable tests for an individual
driver, see `driver.\<name>.enable_tests`. The default is disabled.

## driver.tracing.enable=\<bool>

Enable or disable support for tracing drivers.
When enabled drivers may participate in [Fuchsia tracing](/docs/concepts/drivers/tracing.md).

Implementation-wise, what this option does is tell each devhost whether to
register as "trace provider".

The default is enabled. This options exists to provide a quick fallback should
a problem arise.

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

## kernel.bypass-debuglog=\<bool>

When enabled, forces output to the console instead of buffering it. The reason
we have both a compile switch and a cmdline parameter is to facilitate prints
in the kernel before cmdline is parsed to be forced to go to the console.
The compile switch setting overrides the cmdline parameter (if both are present).
Note that both the compile switch and the cmdline parameter have the side effect
of disabling irq driven uart Tx.

## kernel.cprng-reseed-require.hw-rng=\<bool>

When enabled and if HW RNG fails at reseeding, CPRNG panics. Defaults to false.

## kernel.cprng-reseed-require.jitterentropy=\<bool>

When enabled and if jitterentropy fails at reseeding, CPRNG panics. Defaults to
false.

## kernel.cprng-seed-require.hw-rng=\<bool>

When enabled and if HW RNG fails at initial seeding, CPRNG panics. Defaults to
false.

## kernel.cprng-seed-require.jitterentropy=\<bool>

When enabled and if jitterentrop fails initial seeding, CPRNG panics. Defaults
to false.

## kernel.cprng-seed-require.cmdline=\<bool>

When enabled and if you do not provide entropy input from the kernel command
line, CPRNG panics. Defaults to false.

## kernel.enable-debugging-syscalls=\<bool>

When disabled, certain debugging-related syscalls will fail with
`ZX_ERR_NOT_SUPPORTED`. Defaults to false (debugging syscalls disabled).

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
a kernel panic instead of rebooting. To enable halt-on-panic,
pass the kernel commandline argument `kernel.halt-on-panic=false`.

Since the kernel can't reliably draw to a framebuffer when the GPU is enabled,
the system will reboot by default if the kernel crashes or panics.

If the kernel crashes and the system reboots, the log from the kernel panic will
appear at `/boot/log/last-panic.txt`, suitable for viewing, downloading, etc.

> Please attach your `last-panic.txt` and `zircon.elf` files to any kernel
> panic bugs you file.

If there's a `last-panic.txt`, that indicates that this is the first successful
boot since a kernel panic occurred.

It is not "sticky" -- if you reboot cleanly, it will be gone, and if you crash
again it will be replaced.

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

## kernel.memory-limit-dbg=\<bool>

This option enables verbose logging from the memory limit library.

## kernel.memory-limit-mb=\<num>

This option tells the kernel to limit system memory to the MB value specified
by 'num'. Using this effectively allows a user to simulate the system having
less physical memory than physically present.

## kernel.mexec-force-high-ramdisk=\<bool>

This option is intended for test use only. When set to `true` it forces the
mexec syscall to place the ramdisk for the following kernel in high memory
(64-bit address space, >= 4GiB offset). The default value is `false`.

## kernel.oom.behavior=\<string>

This option can be used to configure the behavior of the kernel when
encountering an OOM situation. Valid values are `jobkill`, and `reboot`. If
unset or set to an invalid value, defaults to `reboot`.

If set to `jobkill`, when encountering OOM, the kernel attempts to kill jobs that
have the `ZX_PROP_JOB_KILL_ON_OOM` bit set to recover memory.

If set to `reboot`, when encountering OOM, the kernel signals an event (see
`zx_system_get_event()`), delays briefly, and then reboots the system.

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

## kernel.x86.disable_spec_mitigations=\<bool>

If set, disable all speculative execution information leak mitigations.

If clear, the per-mitigation defaults will be used.

This option only affects x86 systems.

## kernel.x86.pti.enable=\<int>

Page table isolation configures user page tables to not have kernel text or
data mapped. This may impact performance negatively. This is a mitigation
for Meltdown (AKA CVE-2017-5754).

* If set to 1, this force-enables page table isolation.
* If set to 0, this force-disables page table isolation. This may be insecure.
* If set to 2 or unset (the default), this enables page table isolation on
CPUs vulnerable to Meltdown.

This option only affects x86 systems.

## kernel.x86.spec_store_bypass_disable=\<bool>

Spec-store-bypass (Spectre V4) is a speculative execution information leak
vulnerability that affects many Intel and AMD x86 CPUs. It targets memory
disambiguation hardware to infer the contents of recent stores. The attack
only affects same-privilege-level, intra-process data.

This command line option controls whether a mitigation is enabled. The
mitigation has negative performance impacts.

* If true, the mitigation is enabled on CPUs that need it.
* If false (the default), the mitigation is not enabled.

## kernel.x86.md_clear_on_user_return=\<bool>

MDS (Microarchitectural Data Sampling) is a family of speculative execution
information leak bugs that allow the contents of recent loads or stores to be
inferred by hostile code, regardless of privilege level (CVE-2019-11091,
CVE-2018-12126, CVE-2018-12130, CVE-2018-12127). For example, this could allow
user code to read recent kernel loads/stores.

To avoid this bug, it is required that all microarchitectural structures
that could leak data be flushed on trust level transitions. Also, it is
important that trust levels do not concurrently execute on a single physical
processor core.

This option controls whether microarchitectual structures are flushed on
the kernel to user exit path, if possible. It may have a negative performance
impact.

* If set to true, structures are flushed if the processor is vulnerable.
* If set to false (the default), no flush is executed on structures.

This option only affects x86 systems.

## kernel.mexec-pci-shutdown=\<bool>

If false, this option leaves PCI devices running when calling mexec. Defaults
to true.

## kernel.serial=\<string\>

This controls what serial port is used.  If provided, it overrides the serial
port described by the system's bootdata.  The kernel debug serial port is
a reserved resource and may not be used outside of the kernel.

If set to "none", the kernel debug serial port will be disabled and will not
be reserved, allowing the default serial port to be used outside the kernel.

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

## zircon.autorun.boot=\<command>

This option requests that *command* be run at boot, after devmgr starts up.

Any `+` characters in *command* are treated as argument separators, allowing
you to pass arguments to an executable.

## zircon.autorun.system=\<command>

This option requests that *command* be run once the system partition is mounted
and *init* is launched.  If there is no system bootfs or system partition, it
will never be launched.

Any `+` characters in *command* are treated as argument separators, allowing
you to pass arguments to an executable.

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

## zircon.system.volume=\<arg>

This option specifies where to find the "/system" volume.

It may be set to:
"any", in which case the first volume of the appropriate type will be used.
"local" in which the first volume that's non-removable of the appropriate type
  will be used.
"none" (default) which avoids mounting anything.

A "/system" ramdisk provided by bootdata always supersedes this option.

## zircon.system.filesystem-check=\<bool>

This option requests that filesystems automatically mounted by the system
are pre-verified using a filesystem consistency checker before being mounted.

By default, this option is set to false.

## zircon.system.wait-for-data=\<bool>

This option initializes `pkgfs` and `appmgr` only after a persistent data
partition appears.

By default, this option is set to true.

## netsvc.netboot=\<bool>

If true, zircon will attempt to netboot into another instance of zircon upon
booting.

More specifically, zircon will fetch a new zircon system from a bootserver on
the local link and attempt to kexec into the new image, thereby replacing the
currently running instance of zircon.

This setting implies **zircon.system.disable-automount=true**

## netsvc.disable=\<bool>

If set to true (default), `netsvc` is disabled.

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

## netsvc.all-features=\<bool>

This option makes `netsvc` work normally and support all features. By default,
`netsvc` starts in a minimal mode where only device discovery is supported.

## userboot=\<path>

This option instructs the userboot process (the first userspace process) to
execute the specified binary within the bootfs, instead of following the
normal userspace startup process (launching the device manager, etc).

It is useful for alternate boot modes (like a factory test or system
unit tests).

The pathname used here is relative to `userboot.root` (below), if set,
or else relative to the root of the BOOTFS (which later is ordinarily
seen at `/boot`).  It should not start with a `/` prefix.

If this executable uses `PT_INTERP` (i.e. the dynamic linker), the userboot
process provides a [loader service](/docs/concepts/booting/program_loading.md#the-loader-service) to
resolve the `PT_INTERP` (dynamic linker) name and any shared library names it
may request.  That service simply looks in the `lib/` directory (under
`userboot.root`) in the BOOTFS.

Example: `userboot=bin/core-tests`

## userboot.root=\<path>

This sets a "root" path prefix within the BOOTFS where the `userboot` path and
the `lib/` directory for the loader service will be found.  By default, there
is no prefix so paths are treated as exact relative paths from the root of the
BOOTFS.  e.g. with `userboot.root=pkg/foo` and `userboot=bin/app`, the names
found in the BOOTFS will be `pkg/foo/bin/app`, `pkg/foo/lib/ld.so.1`, etc.

## userboot.reboot

If this option is set, userboot will attempt to reboot the machine after
waiting 3 seconds when the process it launches exits.

*If running a "ZBI test" image in QEMU, this will cause the system to
continually run tests and reboot.*  For QEMU, `userboot.shutdown` is usually
preferable.

## userboot.shutdown

If this option is set, userboot will attempt to power off the machine
when the process it launches exits.  Note if `userboot.reboot` is set
then `userboot.shutdown` will be ignored.

## vdso.ticks_get_force_syscall=\<bool>

If this option is set, the `zx_ticks_get` vDSO call will be forced to be a true
syscall, even if the hardware cycle counter registers are accessible from
user-mode.  Defaults to false.

## vdso.clock_get_monotonic_force_syscall=\<bool>

If this option is set, the `zx_clock_get_monotonic` vDSO call will be forced to
be a true syscall, instead of simply performing a transformation of the tick
counter in user-mode.  Defaults to false.

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

## console.is_virtio=\<bool>

Specify if the device given with `console.path` is a virtio-console device.
Defaults to false.  This is needed as a workaround due to drivers not being able
to implement fuchsia.io.File themselves.

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

## in the emulator or Qemu, using fx emu or fx qemu

Pass each option using -c, for example:

```
fx qemu -c gfxconsole.font=18x32 -c gfxconsole.early=false
```

## in GigaBoot20x6, when netbooting

Pass the kernel commandline at the end, after a -- separator, for example:

```
bootserver zircon.bin bootfs.bin -- gfxconsole.font=18x32 gfxconsole.early=false
```

## in GigaBoot20x6, when booting from USB flash

Create a text file named "cmdline" in the root of the USB flash drive's
filesystem containing the command line.
