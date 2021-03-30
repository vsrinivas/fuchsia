# Zircon Kernel Commandline Options

TODO([fxbug.dev/53594](https://fxbug.dev/53594)): move kernel_cmdline.md verbiage here

## Options common to all machines

### aslr.disable=\<bool>
**Default:** `false`

If this option is set, the system will not use Address Space Layout
Randomization.

### aslr.entropy_bits=\<uint8_t>
**Default:** `0x1e`

For address spaces that use ASLR this controls the number of bits of entropy in
the randomization. Higher entropy results in a sparser address space and uses
more memory for page tables. Valid values range from 0-36.

### kernel.cprng-reseed-require.hw-rng=\<bool>
**Default:** `false`

When enabled and if HW RNG fails at reseeding, CPRNG panics.

### kernel.cprng-reseed-require.jitterentropy=\<bool>
**Default:** `false`

When enabled and if jitterentropy fails at reseeding, CPRNG panics.

### kernel.cprng-seed-require.hw-rng=\<bool>
**Default:** `false`

When enabled and if HW RNG fails at initial seeding, CPRNG panics.

### kernel.cprng-seed-require.jitterentropy=\<bool>
**Default:** `false`

When enabled and if jitterentrop fails initial seeding, CPRNG panics.

### kernel.cprng-seed-require.cmdline=\<bool>
**Default:** `false`

When enabled and if you do not provide entropy input from the kernel command
line, CPRNG panics.

### kernel.entropy-mixin=\<hexadecimal>

Provides entropy to be mixed into the kernel's CPRNG.  The value must be a
string of lowercase hexadecimal digits.

The original value will be scrubbed from memory as soon as possible and will be
redacted from all diagnostic output.

### kernel.jitterentropy.bs=\<uint32_t>
**Default:** `0x40`

Sets the "memory block size" parameter for jitterentropy. When jitterentropy is
performing memory operations (to increase variation in CPU timing), the memory
will be accessed in blocks of this size.

### kernel.jitterentropy.bc=\<uint32_t>
**Default:** `0x200`

Sets the "memory block count" parameter for jitterentropy. When jitterentropy
is performing memory operations (to increase variation in CPU timing), this
controls how many blocks (of size `kernel.jitterentropy.bs`) are accessed.

### kernel.jitterentropy.ml=\<uint32_t>
**Default:** `0x20`

Sets the "memory loops" parameter for jitterentropy. When jitterentropy is
performing memory operations (to increase variation in CPU timing), this
controls how many times the memory access routine is repeated. This parameter
is only used when `kernel.jitterentropy.raw` is true. If the value of this
parameter is `0` or if `kernel.jitterentropy.raw` is `false`, then
jitterentropy chooses the number of loops is a random-ish way.

### kernel.jitterentropy.ll=\<uint32_t>
**Default:** `0x1`

Sets the "LFSR loops" parameter for jitterentropy (the default is 1). When
jitterentropy is performing CPU-intensive LFSR operations (to increase variation
in CPU timing), this controls how many times the LFSR routine is repeated.  This
parameter is only used when `kernel.jitterentropy.raw` is true. If the value of
this parameter is `0` or if `kernel.jitterentropy.raw` is `false`, then
jitterentropy chooses the number of loops is a random-ish way.

### kernel.jitterentropy.raw=\<bool>
**Default:** `true`

When true (the default), the jitterentropy entropy collector will return raw,
unprocessed samples. When false, the raw samples will be processed by
jitterentropy, producing output data that looks closer to uniformly random. Note
that even when set to false, the CPRNG will re-process the samples, so the
processing inside of jitterentropy is somewhat redundant.

### kernel.lockup-detector.critical-section-threshold-ms=\<uint64_t>
**Default:** `0xbb8`

When a CPU remains in a designated critical section for longer than
this threshold, a KERNEL OOPS will be emitted.

See also `k lockup status` and
[lockup detector](/zircon/kernel/lib/lockup_detector/README.md).

When 0, critical section lockup detection is disabled.

When kernel.lockup-detector.heartbeat-period-ms is 0, critical section lockup
detection is disabled.

### kernel.lockup-detector.critical-section-fatal-threshold-ms=\<uint64_t>
**Default:** `0x2710`

When a CPU remains in a designated critical section for longer than this
threshold, a crashlog will be generated and the system will reboot, indicating a
reboot reason of `SOFTWARE_WATCHDOG` as it does.

See also `k lockup status` and
[lockup detector](/zircon/kernel/lib/lockup_detector/README.md).

When 0, critical section crashlog generation and reboot is disabled.

When kernel.lockup-detector.heartbeat-period-ms is 0, critical section lockup
detection is disabled.

### kernel.lockup-detector.heartbeat-period-ms=\<uint64_t>
**Default:** `0x3e8`

How frequently a secondary CPU should emit a heartbeat via kernel timer.  This
value should be large enough to not impact system performance, but should be
smaller than the heartbeat age threshold.  1000 is a reasonable value.

See also [lockup detector](/zircon/kernel/lib/lockup_detector/README.md).

When 0, heartbeat detection is disabled.

### kernel.lockup-detector.heartbeat-age-threshold-ms=\<uint64_t>
**Default:** `0xbb8`

The maximum age of a secondary CPU's last heartbeat before it is considered to
be locked up.  This value should be larger than the heartbeat peroid, but small
enough so as to not miss short-lived lockup events.  3000 is a reasonable value.

See also [lockup detector](/zircon/kernel/lib/lockup_detector/README.md).

When 0, heartbeat detection is disabled.

### kernel.lockup-detector.heartbeat-age-fatal-threshold-ms=\<uint64_t>
**Default:** `0x2710`

The maximum age of a CPU's last heartbeat before it is considered to be locked
up, triggering generation of a crashlog indicating a reboot reason of
`SOFTWARE_WATCHDOG` followed by a reboot.

See also [lockup detector](/zircon/kernel/lib/lockup_detector/README.md).

When 0, heartbeat crashlog generation and reboot is disabled.

### kernel.oom.behavior=[reboot | jobkill]
**Default:** `reboot`

This option can be used to configure the behavior of the kernel when
encountering an out-of-memory (OOM) situation. Valid values are `jobkill`, and
`reboot`.

If set to `jobkill`, when encountering OOM, the kernel attempts to kill jobs that
have the `ZX_PROP_JOB_KILL_ON_OOM` bit set to recover memory.

If set to `reboot`, when encountering OOM, the kernel signals an out-of-memory
event (see `zx_system_get_event()`), delays briefly, and then reboots the system.

### kernel.mexec-force-high-ramdisk=\<bool>
**Default:** `false`

This option is intended for test use only. When set to `true` it forces the
mexec syscall to place the ramdisk for the following kernel in high memory
(64-bit address space, >= 4GiB offset).

### kernel.mexec-pci-shutdown=\<bool>
**Default:** `true`

If false, this option leaves PCI devices running when calling mexec.

### kernel.oom.enable=\<bool>
**Default:** `true`

This option turns on the out-of-memory (OOM) kernel thread, which kills
processes or reboots the system (per `kernel.oom.behavior`), when the PMM has
less than `kernel.oom.outofmemory-mb` free memory.

An OOM can be manually triggered by the command `k pmm oom`, which will cause
free memory to fall below the `kernel.oom.outofmemory-mb` threshold. An
allocation rate can be provided with `k pmm oom <rate>`, where `<rate>` is in MB.
This will cause the specified amount of memory to be allocated every second,
which can be useful for observing memory pressure state transitions.

Refer to `kernel.oom.outofmemory-mb`, `kernel.oom.critical-mb`,
`kernel.oom.warning-mb`, and `zx_system_get_event()` for further details on
memory pressure state transitions.

The current memory availability state can be queried with the command
`k pmm mem_avail_state info`.

### kernel.oom.outofmemory-mb=\<uint64_t>
**Default:** `0x32`

This option specifies the free-memory threshold at which the out-of-memory (OOM)
thread will trigger an out-of-memory event and begin killing processes, or
rebooting the system.

### kernel.oom.critical-mb=\<uint64_t>
**Default:** `0x96`

This option specifies the free-memory threshold at which the out-of-memory
(OOM) thread will trigger a critical memory pressure event, signaling that
processes should free up memory.

### kernel.oom.warning-mb=\<uint64_t>
**Default:** `0x12c`

This option specifies the free-memory threshold at which the out-of-memory
(OOM) thread will trigger a warning memory pressure event, signaling that
processes should slow down memory allocations.

### kernel.oom.debounce-mb=\<uint64_t>
**Default:** `0x1`

This option specifies the memory debounce value used when computing the memory
pressure state based on the free-memory thresholds
(`kernel.oom.outofmemory-mb`, `kernel.oom.critical-mb` and
`kernel.oom.warning-mb`). Transitions between memory availability states are
debounced by not leaving a state until the amount of free memory is at least
`kernel.oom.debounce-mb` outside of that state.

For example, consider the case where `kernel.oom.critical-mb` is set to 100 MB
and `kernel.oom.debounce-mb` set to 5 MB. If we currently have 90 MB of free
memory on the system, i.e. we're in the Critical state, free memory will have to
increase to at least 105 MB (100 MB + 5 MB) for the state to change from
Critical to Warning.

### kernel.oom.evict-at-warning=\<bool>
**Default:** `false`

This option triggers eviction of file pages at the Warning pressure state,
in addition to the default behavior, which is to evict at the Critical and OOM
states.

### kernel.oom.hysteresis-seconds=\<uint64_t>
**Default:** `0xa`

This option specifies the hysteresis interval (in seconds) between memory
pressure state transitions. Note that hysteresis is only applicable for
transitions from a state with less free memory to a state with more free memory;
transitions in the opposite direction are not delayed.

### kernel.serial=[none | legacy | qemu | \<type>,\<base>,\<irq>]
**Default:** `none`

TODO(53594)

### vdso.ticks_get_force_syscall=\<bool>
**Default:** `false`

If this option is set, the `zx_ticks_get` vDSO call will be forced to be a true
syscall, even if the hardware cycle counter registers are accessible from
user-mode.

### vdso.clock_get_monotonic_force_syscall=\<bool>
**Default:** `false`

If this option is set, the `zx_clock_get_monotonic` vDSO call will be forced to
be a true syscall, instead of simply performing a transformation of the tick
counter in user-mode.

### kernel.userpager.overtime_wait_seconds=\<uint64_t>
**Default:** `0x14`

This option configures how long a user pager fault may block before being
considered overtime and printing an information message to the debuglog and
continuing to wait. A value of 0 indicates a wait is never considered to be
overtime.

### kernel.userpager.overtime_timeout_seconds=\<uint64_t>
**Default:** `0x12c`

This option configures how long a user pager fault may block before being
aborted. For a hardware page fault, the faulting thread will terminate with a
fatal page fault exception. For a software page fault triggered by a syscall,
the syscall will fail with `ZX_ERR_TIMED_OUT`. A value of 0 indicates a page
fault is never aborted due to a time out.


## Options available only on arm64 machines

### kernel.arm64.disable_spec_mitigations=\<bool>
**Default:** `false`

If set, disables all speculative execution information leak mitigations.

If unset, the per-mitigation defaults will be used.

### kernel.arm64.event-stream.enable=\<bool>
**Default:** `false`

When enabled, each ARM cpu will enable an event stream generator, which per-cpu
sets the hidden event flag at a particular rate. This has the effect of kicking
cpus out of any WFE states they may be sitting in.

### kernel.arm64.event-stream.freq-hz=\<uint32_t>
**Default:** `0x2710`

If the event stream is enabled, specifies the frequency at which it will attempt
to run. The resolution is limited, so the driver will only be able to pick the
nearest power of 2 from the cpu timer counter.

### kernel.arm64.debug.dap-rom-soc=\<string>

If set, tries to initialize the dap debug aperture at a hard coded address for the particular
system on chip. Currently accepted values are amlogic-t931g, amlogic-s905d2, and amlogic-s905d3g.


## Options available only on x86 machines

### kernel.x86.disable_spec_mitigations=\<bool>
**Default:** `false`

If set, disables all speculative execution information leak mitigations.

If unset, the per-mitigation defaults will be used.

### kernel.x86.hwp=\<bool>
**Default:** `true`

This settings enables HWP (hardware P-states) on supported chips. This feature
lets Intel CPUs automatically scale their own clock speed.

### kernel.x86.hwp_policy=[bios-specified | performance | balanced | power-save | stable-performance]
**Default:** `bios-specified`

Set a power/performance tradeoff policy of the CPU. x86 CPUs with HWP
(hardware P-state) support can be configured to autonomusly scale their
frequency to favour different policies.

Currently supported policies are:

*   `bios-specified`: Use the power/performance tradeoff policy
    specified in firmware/BIOS settings. If no policy is available, falls back
    to `balanced`.
*   `performance`: Maximise performance.
*   `balanced`: Balance performance / power savings.
*   `power-save`: Reduce power usage, at the cost of lower performance.
*   `stable-performance`: Use settings that keep system performance consistent.
    This may be useful for benchmarking, for example, where keeping performance
    predictable is more important than maximising performance.

### kernel.x86.md_clear_on_user_return=\<bool>
**Default:** `true`

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

*   If set to true (the default), structures are flushed if the processor is
    vulnerable.
*   If set to false, no flush is executed on structures.

### kernel.x86.pti.enable=\<uint32_t>
**Default:** `0x2`

Page table isolation configures user page tables to not have kernel text or
data mapped. This may impact performance negatively. This is a mitigation
for Meltdown (AKA CVE-2017-5754).

* If set to 1, this force-enables page table isolation.
* If set to 0, this force-disables page table isolation. This may be insecure.
* If set to 2 or unset (the default), this enables page table isolation on
CPUs vulnerable to Meltdown.

TODO(joshuaseaton): make this an enum instead of using magic integers.

### kernel.x86.spec_store_bypass_disable=\<bool>
**Default:** `false`

Spec-store-bypass (Spectre V4) is a speculative execution information leak
vulnerability that affects many Intel and AMD x86 CPUs. It targets memory
disambiguation hardware to infer the contents of recent stores. The attack
only affects same-privilege-level, intra-process data.

This command line option controls whether a mitigation is enabled. The
mitigation has negative performance impacts.

* If true, the mitigation is enabled on CPUs that need it.
* If false (the default), the mitigation is not enabled.

### kernel.x86.turbo=\<bool>
**Default:** `true`

Turbo Boost or Core Performance Boost are mechanisms that allow processors to
dynamically vary their performance at runtime based on available thermal and
electrical budget. This may provide improved interactive performance at the cost
of performance variability. Some workloads may benefit from disabling Turbo; if
this command line flag is set to false, turbo is disabled for all CPUs in the
system.

TODO: put something here
