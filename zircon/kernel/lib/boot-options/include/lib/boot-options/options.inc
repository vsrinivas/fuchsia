// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

// This file is #include'd multiple times with the DEFINE_OPTION macro defined.
//
// #define DEFINE_OPTION(name, type, member, {init}, docstring) ...
// #include "options.inc"
// #undef DEFINE_OPTION
//
// See boot-options.h for admonitions about what kinds of types can be used, as
// well as test-optons.inc for basic examples.

DEFINE_OPTION("aslr.disable", bool, aslr_disabled, {false}, R"""(
If this option is set, the system will not use Address Space Layout
Randomization.
)""")

DEFINE_OPTION("aslr.entropy_bits", uint8_t, aslr_entropy_bits, {30}, R"""(
For address spaces that use ASLR this controls the number of bits of entropy in
the randomization. Higher entropy results in a sparser address space and uses
more memory for page tables. Valid values range from 0-36.
)""")

DEFINE_OPTION("kernel.cprng-reseed-require.hw-rng", bool, cprng_reseed_require_hw_rng, {false},
              R"""(
When enabled and if HW RNG fails at reseeding, CPRNG panics.
)""")

DEFINE_OPTION("kernel.cprng-reseed-require.jitterentropy", bool, cprng_reseed_require_jitterentropy,
              {false}, R"""(
When enabled and if jitterentropy fails at reseeding, CPRNG panics.
)""")

DEFINE_OPTION("kernel.cprng-seed-require.hw-rng", bool, cprng_seed_require_hw_rng, {false}, R"""(
When enabled and if HW RNG fails at initial seeding, CPRNG panics.
)""")

DEFINE_OPTION("kernel.cprng-disable.jitterentropy", bool, cprng_disable_jitterentropy, {false},
              R"""(
Determines whether jitterentropy will be used as an entropy source (used for testing)
)""")

DEFINE_OPTION("kernel.cprng-disable.hw-rng", bool, cprng_disable_hw_rng, {false}, R"""(
Determines whether HW RNG will be used as an entropy source (used for testing)
)""")

DEFINE_OPTION("kernel.cprng-seed-require.jitterentropy", bool, cprng_seed_require_jitterentropy,
              {false}, R"""(
When enabled and if jitterentropy fails initial seeding, CPRNG panics.
)""")

DEFINE_OPTION("kernel.cprng-seed-require.cmdline", bool, cprng_seed_require_cmdline, {false}, R"""(
When enabled and if you do not provide entropy input from the kernel command
line, CPRNG panics.
)""")

// RedactedHex parses an arbitrary-length (but bounded like SmallString) string
// of ASCII hex digits, and then overwrites those digits in the original
// command line text in the ZBI's physical memory so the entropy_mixin string
// in the BootOptions struct is the only place that has those bits.
DEFINE_OPTION("kernel.entropy-mixin", RedactedHex, entropy_mixin, {}, R"""(
Provides entropy to be mixed into the kernel's CPRNG.  The value must be a
string of lowercase hexadecimal digits.

The original value will be scrubbed from memory as soon as possible and will be
redacted from all diagnostic output.
)""")

DEFINE_OPTION("kernel.jitterentropy.bs", uint32_t, jitterentropy_bs, {64}, R"""(
Sets the "memory block size" parameter for jitterentropy. When jitterentropy is
performing memory operations (to increase variation in CPU timing), the memory
will be accessed in blocks of this size.
)""")

DEFINE_OPTION("kernel.jitterentropy.bc", uint32_t, jitterentropy_bc, {512}, R"""(
Sets the "memory block count" parameter for jitterentropy. When jitterentropy
is performing memory operations (to increase variation in CPU timing), this
controls how many blocks (of size `kernel.jitterentropy.bs`) are accessed.
)""")

DEFINE_OPTION("kernel.jitterentropy.ml", uint32_t, jitterentropy_ml, {32}, R"""(
Sets the "memory loops" parameter for jitterentropy. When jitterentropy is
performing memory operations (to increase variation in CPU timing), this
controls how many times the memory access routine is repeated. This parameter
is only used when `kernel.jitterentropy.raw` is true. If the value of this
parameter is `0` or if `kernel.jitterentropy.raw` is `false`, then
jitterentropy chooses the number of loops is a random-ish way.
)""")

DEFINE_OPTION("kernel.jitterentropy.ll", uint32_t, jitterentropy_ll, {1}, R"""(
Sets the "LFSR loops" parameter for jitterentropy. When
jitterentropy is performing CPU-intensive LFSR operations (to increase variation
in CPU timing), this controls how many times the LFSR routine is repeated.  This
parameter is only used when `kernel.jitterentropy.raw` is true. If the value of
this parameter is `0` or if `kernel.jitterentropy.raw` is `false`, then
jitterentropy chooses the number of loops is a random-ish way.
)""")

DEFINE_OPTION("kernel.jitterentropy.raw", bool, jitterentropy_raw, {true}, R"""(
When true, the jitterentropy entropy collector will return raw,
unprocessed samples. When false, the raw samples will be processed by
jitterentropy, producing output data that looks closer to uniformly random. Note
that even when set to false, the CPRNG will re-process the samples, so the
processing inside of jitterentropy is somewhat redundant.
)""")

// TODO(maniscalco): Set a default threshold that is high enough that it won't erronously trigger
// under qemu.  Alternatively, set an aggressive default threshold in code and override in
// virtualized environments and scripts that start qemu.
DEFINE_OPTION("kernel.lockup-detector.critical-section-threshold-ms", uint64_t,
              lockup_detector_critical_section_threshold_ms, {3000}, R"""(
When a CPU remains in a designated critical section for longer than
this threshold, a KERNEL OOPS will be emitted.

See also `k lockup status` and
[lockup detector](/zircon/kernel/lib/lockup_detector/README.md).

When 0, critical section lockup detection is disabled.

When kernel.lockup-detector.heartbeat-period-ms is 0, critical section lockup
detection is disabled.
)""")

DEFINE_OPTION("kernel.lockup-detector.critical-section-fatal-threshold-ms", uint64_t,
              lockup_detector_critical_section_fatal_threshold_ms, {10000}, R"""(
When a CPU remains in a designated critical section for longer than this
threshold, a crashlog will be generated and the system will reboot, indicating a
reboot reason of `SOFTWARE_WATCHDOG` as it does.

See also `k lockup status` and
[lockup detector](/zircon/kernel/lib/lockup_detector/README.md).

When 0, critical section crashlog generation and reboot is disabled.

When kernel.lockup-detector.heartbeat-period-ms is 0, critical section lockup
detection is disabled.
)""")

DEFINE_OPTION("kernel.lockup-detector.heartbeat-period-ms", uint64_t,
              lockup_detector_heartbeat_period_ms, {1000}, R"""(
How frequently a secondary CPU should emit a heartbeat via kernel timer.  This
value should be large enough to not impact system performance, but should be
smaller than the heartbeat age threshold.  1000 is a reasonable value.

See also [lockup detector](/zircon/kernel/lib/lockup_detector/README.md).

When 0, heartbeat detection is disabled.
)""")

DEFINE_OPTION("kernel.lockup-detector.heartbeat-age-threshold-ms", uint64_t,
              lockup_detector_heartbeat_age_threshold_ms, {3000}, R"""(
The maximum age of a secondary CPU's last heartbeat before it is considered to
be locked up.  This value should be larger than the heartbeat peroid, but small
enough so as to not miss short-lived lockup events.  3000 is a reasonable value.

See also [lockup detector](/zircon/kernel/lib/lockup_detector/README.md).

When 0, heartbeat detection is disabled.
)""")

DEFINE_OPTION("kernel.lockup-detector.heartbeat-age-fatal-threshold-ms", uint64_t,
              lockup_detector_age_fatal_threshold_ms, {10000}, R"""(
The maximum age of a CPU's last heartbeat before it is considered to be locked
up, triggering generation of a crashlog indicating a reboot reason of
`SOFTWARE_WATCHDOG` followed by a reboot.

See also [lockup detector](/zircon/kernel/lib/lockup_detector/README.md).

When 0, heartbeat crashlog generation and reboot is disabled.
)""")

DEFINE_OPTION("kernel.lockup-detector.diagnostic-query-timeout-ms", uint64_t,
              lockup_detector_diagnostic_query_timeout_ms, {100}, R"""(
The maximum amount of time to spend trying to get diagnostic data from an
unresponsive CPU before giving up.

When 0, querying for diagnostic data is disabled.
)""")

DEFINE_OPTION("kernel.oom.behavior", OomBehavior, oom_behavior, {OomBehavior::kReboot}, R"""(
This option can be used to configure the behavior of the kernel when
encountering an out-of-memory (OOM) situation. Valid values are `jobkill`, and
`reboot`.

If set to `jobkill`, when encountering OOM, the kernel attempts to kill jobs that
have the `ZX_PROP_JOB_KILL_ON_OOM` bit set to recover memory.

If set to `reboot`, when encountering OOM, the kernel signals an out-of-memory
event (see `zx_system_get_event()`), waits some period, and then reboots the
system. The length of the wait period is set by the
`kernel.oom.reboot-timeout-ms` boot option.
)""")

DEFINE_OPTION("kernel.oom.reboot-timeout-ms", uint32_t, oom_timeout_ms, {50000}, R"""(
This option sets the amount of time the kernel will wait before rebooting the
system when it encounters an out-of-memory (OOM) situation. This option is
only relevant when `kernel.oom.behavior` is set to `reboot`.
)""")

DEFINE_OPTION("kernel.mexec-force-high-ramdisk", bool, mexec_force_high_ramdisk, {false}, R"""(
This option is intended for test use only. When set to `true` it forces the
mexec syscall to place the ramdisk for the following kernel in high memory
(64-bit address space, >= 4GiB offset).
)""")

DEFINE_OPTION("kernel.mexec-pci-shutdown", bool, mexec_pci_shutdown, {true}, R"""(
If false, this option leaves PCI devices running when calling mexec.
)""")

DEFINE_OPTION("kernel.oom.enable", bool, oom_enabled, {true}, R"""(
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
)""")

DEFINE_OPTION("kernel.oom.outofmemory-mb", uint64_t, oom_out_of_memory_threshold_mb, {50}, R"""(
This option specifies the free-memory threshold at which the out-of-memory (OOM)
thread will trigger an out-of-memory event and begin killing processes, or
rebooting the system.
)""")

DEFINE_OPTION("kernel.oom.critical-mb", uint64_t, oom_critical_threshold_mb, {150}, R"""(
This option specifies the free-memory threshold at which the out-of-memory
(OOM) thread will trigger a critical memory pressure event, signaling that
processes should free up memory.
)""")

DEFINE_OPTION("kernel.oom.warning-mb", uint64_t, oom_warning_threshold_mb, {300}, R"""(
This option specifies the free-memory threshold at which the out-of-memory
(OOM) thread will trigger a warning memory pressure event, signaling that
processes should slow down memory allocations.
)""")

DEFINE_OPTION("kernel.oom.debounce-mb", uint64_t, oom_debounce_mb, {1}, R"""(
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
)""")

DEFINE_OPTION("kernel.oom.evict-at-warning", bool, oom_evict_at_warning, {false}, R"""(
This option triggers eviction of file pages at the Warning pressure state,
in addition to the default behavior, which is to evict at the Critical and OOM
states.
)""")

DEFINE_OPTION("kernel.oom.evict-continuous", bool, oom_evict_continuous, {false}, R"""(
This option configures kernel eviction to run continually in the background to try and
keep the system out of memory pressure, as opposed to triggering one-shot eviction only at
memory pressure level transitions.
)""")

DEFINE_OPTION("kernel.oom.hysteresis-seconds", uint64_t, oom_hysteresis_seconds, {10}, R"""(
This option specifies the hysteresis interval (in seconds) between memory
pressure state transitions. Note that hysteresis is only applicable for
transitions from a state with less free memory to a state with more free memory;
transitions in the opposite direction are not delayed.
)""")

DEFINE_OPTION("kernel.oom.imminent-oom-delta-mb", uint64_t, oom_imminent_oom_delta_mb, {10}, R"""(
This option specifies the delta (in MB) above the out-of-memory threshold at which an
imminent-out-of-memory event will be signaled. This signal is intended to be used for
capturing diagnostic memory information close to the OOM, since capturing state exactly
at the OOM might not be possible.

For example, if `kernel.oom.outofmemory-mb` is set to 50 and `kernel.oom.imminent-oom-delta-mb`
is set to 20, an imminent-out-of-memory event will be signaled at 70MB (i.e. 50MB + 20MB)
free memory, while out-of-memory will be signaled at 50MB free memory.
)""")

DEFINE_OPTION("kernel.oom.trigger-on-alloc-failure", bool, oom_trigger_on_alloc_failure, {true},
R"""(
This option controls whether potentially user-visible PMM allocation failures due to running out of
memory trigger an OOM response.
)""")

DEFINE_OPTION("kernel.phys.verbose", bool, phys_verbose, {true}, R"""(
This controls the degree of logging to the serial console in the kernel's early
boot phase; if false, only error-related logging will take place.

One utility of this option is for benchmarking: synchronous, single-threaded
UART writing can be relatively costly (10 chars/ms) to the entire time
spent in physboot and it is desirable to exclude this sort of work from
holistic time measurements.
)""")

DEFINE_OPTION("kernel.phys.print-stack-max", uint32_t, phys_print_stack_max, {1024}, R"""(
When there is a crash in the kernel's early boot phase, it can print out stack
contents on the serial console.  This is the maximum size (in bytes) of stack
memory that will be dumped; the actual amount dumped depends on stack depth.
Each 16 bytes of stack is printed on one line using up to 75 characters.
)""")

DEFINE_OPTION("kernel.ppb.borrow-in-supplypages", bool, ppb_borrow_in_supplypages, {false}, R"""(
This controls whether zx_pager_supply_pages can borrow loaned pages.  If true,
zx_pager_supply_pages will copy supplied page contents into borrowed pages, if
any loaned pages are available and the supplied pages aren't already loaned,
else zx_pager_supply_pages will install the supplied pages into the VMO.  If
false, zx_pager_supply_pages will install the supplied pages into the VMO
(regardless of whether they are already loaned or not).
)""")

DEFINE_OPTION("kernel.ppb.borrow-on-mru", bool, ppb_borrow_on_mru, {false}, R"""(
This controls whether non-loaned pages will be replaced with loaned pages (if
any loaned pages are available) when non-loaned pages are moved to the MRU
queue.  This moving is done lazily under normal circumstances, but near OOM as
eviction proceeds, any pages that should be in the MRU queue are moved to the
MRU queue.
)""")

DEFINE_OPTION("kernel.ppb.loan", bool, ppb_loan, {false}, R"""(
This controls whether ZX_VMO_OP_DECOMMIT is enabled on a contiguous VMO.  If
true, decommit on a contiguous VMO can work and return ZX_OK.  The pages are
loaned to the rest of the system for potential borrowing.  If false, decommit
will return ZX_ERR_NOT_SUPPORTED.
)""")

DEFINE_OPTION("kernel.ppb.replace-on-unloan", bool, ppb_replace_on_unloan, {false}, R"""(
This controls what happens when a loaned page is re-committed to its original
contiguous VMO. If false, the page is evicted from where it is loaned from,
requiring that content to be faulted back in. If true, the contents are copied
to a new page that is swapped in to replace the loaned page.
)""")

DEFINE_OPTION("kernel.render-dlog-to-crashlog", bool, render_dlog_to_crashlog, {false}, R"""(
When set to true, as much of the recent kernel debuglog as will fit will be
appended to the generated crashlog during a kernel panic to assist in debugging.
)""")

DEFINE_OPTION("kernel.serial", uart::all::Driver, serial, {}, R"""(
This controls what serial port is used.  If provided, it overrides the serial
port described by the system's bootdata.  The kernel debug serial port is
a reserved resource and may not be used outside of the kernel.

If set to "none", the kernel debug serial port will be disabled and will not
be reserved, allowing the default serial port to be used outside the kernel.
)""")

DEFINE_OPTION("kernel.vdso.always_use_next", bool, always_use_next_vdso, {false}, R"""(
If this option is set, the kernel will supply userspace with the "next" vDSO
rather than the "stable" vDSO as the default vDSO.
)""")

DEFINE_OPTION("vdso.ticks_get_force_syscall", bool, vdso_ticks_get_force_syscall, {false}, R"""(
If this option is set, the `zx_ticks_get` vDSO call will be forced to be a true
syscall, even if the hardware cycle counter registers are accessible from
user-mode.
)""")

DEFINE_OPTION("vdso.clock_get_monotonic_force_syscall", bool,
              vdso_clock_get_monotonic_force_syscall, {false}, R"""(
If this option is set, the `zx_clock_get_monotonic` vDSO call will be forced to
be a true syscall, instead of simply performing a transformation of the tick
counter in user-mode.
)""")

DEFINE_OPTION("kernel.userpager.overtime_wait_seconds", uint64_t, userpager_overtime_wait_seconds,
              {20}, R"""(
This option configures how long a user pager fault may block before being
considered overtime and printing an information message to the debuglog and
continuing to wait. A value of 0 indicates a wait is never considered to be
overtime.
)""")

DEFINE_OPTION("kernel.userpager.overtime_timeout_seconds", uint64_t,
              userpager_overtime_timeout_seconds, {300}, R"""(
This option configures how long a user pager fault may block before being
aborted. For a hardware page fault, the faulting thread will terminate with a
fatal page fault exception. For a software page fault triggered by a syscall,
the syscall will fail with `ZX_ERR_TIMED_OUT`. A value of 0 indicates a page
fault is never aborted due to a time out.
)""")

DEFINE_OPTION("kernel.heap-max-size-mb", uint64_t, heap_max_size_mb, {2048}, R"""(
This option configures the maximum size of the heap. Only has effect if kernel
has been compiled to use a virtual heap.
)""")

DEFINE_OPTION("kernel.heap.alloc_fill_threshold", uint64_t, alloc_fill_threshold, {0}, R"""(
When set, the kernel heap will fill allocations below this size (in bytes).
)""")

DEFINE_OPTION("kernel.bufferchain.reserve-pages", uint64_t, bufferchain_reserve_pages, {32}, R"""(
Specifies the number of pages per CPU to reserve for buffer chain allocations
(channel messages). Higher values reduce contention on the PMM when the
system is under load at the cost of using more memory when the system is
idle.

TODO(fxbug.dev/68456): Determine an upper bound for this value to prevent
consuming too much memory.
)""")

DEFINE_OPTION("kernel.bypass-debuglog", bool, bypass_debuglog, {false}, R"""(
When enabled, forces output to the console instead of buffering it. The reason
we have both a compile switch and a cmdline parameter is to facilitate prints
in the kernel before cmdline is parsed to be forced to go to the console.
The compile switch setting overrides the cmdline parameter (if both are present).
Note that both the compile switch and the cmdline parameter have the side effect
of disabling irq driven uart Tx.
)""")

DEFINE_OPTION("kernel.debug_uart_poll", bool, debug_uart_poll, {false}, R"""(
If true, will periodically poll UART and forwards its contents into the console.
)""")

DEFINE_OPTION("kernel.enable-debugging-syscalls", bool, enable_debugging_syscalls, {false}, R"""(
When disabled, certain debugging-related syscalls will fail with
`ZX_ERR_NOT_SUPPORTED`. These are:

* `zx_debug_send_command()`
* `zx_ktrace_control()`
* `zx_ktrace_init()`
* `zx_ktrace_read()`
* `zx_ktrace_write()`
* `zx_mtrace_control()`
* `zx_object_get_property()` with `ZX_PROP_PROCESS_HW_TRACE_CONTEXT_ID`
* `zx_process_write_memory()`
* `zx_system_mexec()`
* `zx_system_mexec_payload_get()`
* `zx_thread_write_state()` (When using the `ZX_THREAD_STATE_DEBUG_REGS` kind.)
* `zx_vmo_op_range()` with `ZX_VMO_OP_CACHE_INVALIDATE`
)""")

DEFINE_OPTION("kernel.enable-serial-syscalls", SerialDebugSyscalls, enable_serial_syscalls,
              {SerialDebugSyscalls::kDisabled}, R"""(
When `false`, both `zx_debug_read()` and `zx_debug_write()` will fail with
`ZX_ERR_NOT_SUPPORTED`.

When `output-only`, `zx_debug_read()` will fail with `ZX_ERR_NOT_SUPPORTED`, but `zx_debug_write()`
will work normally.

When `true`, both will work normally.
)""")

DEFINE_OPTION("kernel.entropy-test.src", EntropyTestSource, entropy_test_src,
              {EntropyTestSource::kHwRng}, R"""(
When running an entropy collector quality test, use the provided entropy source.
This option is ignored unless the kernel was built with `ENABLE_ENTROPY_COLLECTOR_TEST=1`.
)""")

DEFINE_OPTION("kernel.entropy-test.len", uint64_t, entropy_test_len, {kMaxEntropyLength}, R"""(
When running an entropy collector quality test, collect the provided number of
bytes.

The maximum value can be increased by defining `ENTROPY_COLLECTOR_TEST_MAXLEN` as such value.
)""")

DEFINE_OPTION("kernel.force-watchdog-disabled", bool, force_watchdog_disabled, {false}, R"""(
When set, the system will attempt to disable any hardware watchdog timer armed
and passed by the bootloader as soon as it possibly can in the boot sequence,
presuming that the bootloader provides enough information to know how to disable
the WDT at all.
)""")

DEFINE_OPTION("gfxconsole.early", bool, gfx_console_early, {false}, R"""(
This option requests that the kernel start a graphics console
during early boot (if possible), to display kernel debug print
messages while the system is starting.  When userspace starts up, a usermode
graphics console driver takes over.

The early kernel console can be slow on some platforms, so if it is not
needed for debugging it may speed up boot to disable it.
)""")

DEFINE_OPTION("gfxconsole.font", GfxConsoleFont, gfx_console_font, {GfxConsoleFont::k9x16}, R"""(
This option asks the graphics console to use a specific font.
)""")

DEFINE_OPTION("kernel.halt-on-panic", bool, halt_on_panic, {false}, R"""(
If this option is set, the system will halt on a kernel panic instead
of rebooting. To enable halt-on-panic, pass the kernel command line
argument `kernel.halt-on-panic=true`.

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
)""")

DEFINE_OPTION("ktrace.bufsize", uint32_t, ktrace_bufsize, {32}, R"""(
This option specifies the number of megabytes allocated for ktrace records.
)""")

DEFINE_OPTION("ktrace.grpmask", uint32_t, ktrace_grpmask, {0xFFF}, R"""(
This option specifies what ktrace records are emitted.
The value is a bitmask of KTRACE\_GRP\_\* values from zircon/ktrace.h.
Hex values may be specified as 0xNNN.
)""")

DEFINE_OPTION("kernel.memory-limit-dbg", bool, memory_limit_dbg, {true}, R"""(
This option enables verbose logging from the memory limit library.
)""")

DEFINE_OPTION("kernel.memory-limit-mb", uint64_t, memory_limit_mb, {0}, R"""(
This option sets an upper-bound in megabytes for the system memory.
If set to zero, then no upper-bound is set.

For example, choosing a low enough value would allow a user simulating a system with
less physical memory than it actually has.
)""")

DEFINE_OPTION("kernel.page-scanner.enable-eviction", bool, page_scanner_enable_eviction, {true},
              R"""(
When set, allows the page scanner to evict user pager backed pages. Eviction can
reduce memory usage and prevent out of memory scenarios, but removes some
timing predictability from system behavior.
)""")

DEFINE_OPTION("kernel.page-scanner.discardable-evictions-percent", uint32_t,
              page_scanner_discardable_evictions_percent, {20}, R"""(
Percentage of page evictions, that should be satisfied from
discardable VMOs, as opposed to pager-backed VMOs. For example, if this value
is set to `X` and the kernel needs to reclaim `N` pages to relieve memory
pressure, it will evict `(N * X / 100)` pages from discardable VMOs, and the
remaining `(N * (100 - X) / 100)` pages from pager-backed VMOs.

Note that the kernel will try its best to honor this ratio between discardable
and pager-backed pages evicted, but the actual numbers might not be exact.
)""")

DEFINE_OPTION("kernel.page-scanner.page-table-eviction-policy", PageTableEvictionPolicy,
              page_scanner_page_table_eviction_policy, {PageTableEvictionPolicy::kAlways}, R"""(
Sets the reclamation policy for user page tables that are not accessed.

When `on_request`, only performs eviction on request, such as in response to a
low memory scenario.

When `never`, page tables are never evicted.

When `always`, Unused page tables are evicted periodically. The period can be controlled by
`kernel.page-scanner.page-table-eviction-period`.
)""")

DEFINE_OPTION("kernel.page-scanner.page-table-eviction-period", uint32_t,
              page_scanner_page_table_eviction_period, {10}, R"""(
Sets the rate, in seconds, that page tables will be scanned. Any page tables not used between two
successive scans are candidates for eviction.

This option only has an effect if `kernel.page-scanner.page-table-eviction-policy=always`.
)""")

DEFINE_OPTION("kernel.page-scanner.min-aging-interval", uint32_t, page_scanner_min_aging_interval,
              {2}, R"""(
Sets the minimum time, in seconds, between successive aging events. Higher values here will provide
a more stable active set with less chance of thrashing like behavior and less time spent harvesting
page access information. Lower values will allow for smaller active sets, increasing opportunities
for eviction.

This value should be less than or equal to `kernel.page-scanner.max-aging-interval`.
)""")

DEFINE_OPTION("kernel.page-scanner.max-aging-interval", uint32_t, page_scanner_max_aging_interval,
              {15}, R"""(
Sets the maximum time, in seconds, between successive aging events. This time is the potential worst
case coarseness of page age information, and higher values can result in not having sufficient age
information to perform eviction if system behavior rapidly changes. Lower values will cause pages to
lose fidelity information by accumulating in the oldest bucket increasing the chance that when
eviction happens a sub-optimal page is chosen.

This value should be greater than or equal to `kernel.page-scanner.min-aging-interval`.
)""")

DEFINE_OPTION("kernel.page-scanner.active-ratio-multiplier", uint32_t,
              page_scanner_active_ratio_multiplier, {2}, R"""(
Controls the allowable ratio of active pages, compared to inactive pages, before aging is triggered.
The ratio is represented as a multiplier to simplify the kernel algorithm which is
`should_age = active_page_count * active_ratio_multiplier > inactive_page_count`. Higher multipliers
will result in more frequent aging events and hence less pages being in the active set. A multiplier
of 0 will disable aging based on the active page ratio.

The active ratio only triggers aging in the period between the `kernel.page-scanner.min-aging-interval`
and the `kernel.page-scanner.max-aging-interval`, and as such has no effect if the intervals are
equal.
)""")

DEFINE_OPTION("kernel.page-scanner.eviction-interval-seconds", uint32_t,
              page_scanner_eviction_interval_seconds, {10}, R"""(
This option specifies the periodic interval (in seconds) at which kernel eviction
will run in the background to try and keep the system out of memory pressure.
This will only take effect if continuous eviction is enabled with
`kernel.oom.evict-continuous`. If this value is lower than the page queue
rotation / aging interval (10s by default), it will be ignored and the eviction
interval will instead be set equal to the page queue rotation interval.
)""")

DEFINE_OPTION("kernel.page-scanner.start-at-boot", bool, page_scanner_start_at_boot, {true},
              R"""(
This option causes the kernels active memory scanner to be initially
enabled on startup. You can also enable and disable it using the kernel
console. If you disable the scanner, you can have additional system
predictability since it removes time based and background memory eviction.

Every action the scanner performs can be individually configured and disabled.
If all actions are disabled then enabling the scanner has no effect.
)""")

DEFINE_OPTION("kernel.page-scanner.zero-page-scans-per-second", uint64_t,
              page_scanner_zero_page_scans_per_second, {20000}, R"""(
This option configures the maximal number of candidate pages the zero
page scanner will consider every second.

Setting to zero means no zero page scanning will occur. This can provide
additional system predictability for benchmarking or other workloads.

The page scanner must be running for this option to have any effect. It can be
enabled at boot with the `kernel.page-scanner.start-at-boot` option.

This value was chosen to consume, in the worst case, 5% CPU on a lower-end
arm device. Individual configurations may wish to tune this higher (or lower)
as needed.
)""")

DEFINE_OPTION("kernel.pmm-checker.action", SmallString, pmm_checker_action, {"oops"}, R"""(
Supported actions:
- `oops`
- `panic`

This option specifies which action is taken when the PMM checker detects
corruption.

When `oops`, a non-fatal kernel OOPS will be emitted when corruption is detected.

When `panic` a fatal kernel panic will occur when corruption is detected.
)""")

DEFINE_OPTION("kernel.pmm-checker.enable", bool, pmm_checker_enabled, {false},
              R"""(
This controls whether the PMM's use-after-free checker is enabled.
The PMM checker can be expensive and is intended for use in debug and
development builds.  See also "k pmm checker".
)""")

DEFINE_OPTION("kernel.pmm-checker.fill-size", uint64_t, pmm_checker_fill_size, {ZX_PAGE_SIZE},
              R"""(
This option specifies how many bytes of each free page is filled or checked when
the PMM's use-after-free checker is enabled.  Valid values are multiples of 8,
between 8 and PAGE_SIZE, inclusive.
)""")

DEFINE_OPTION("kernel.pmm.alloc-random-should-wait", bool, pmm_alloc_random_should_wait, {false},
              R"""(
Enables a testing option that causes the PMM to randomly fail wait-able page allocations with
ZX_ERR_SHOULD_WAIT, regardless of the current memory level. This is intended to facilitate testing
of the waiting paths without needing to put the system into a low memory state. It is an error to
enable this option if the kernel is not built with debugging assertions enabled.
)""")

DEFINE_OPTION("kernel.portobserver.reserve-pages", uint64_t, port_observer_reserve_pages, {8},
              R"""(
Specifies the number of pages per CPU to reserve for port observer (async
wait) allocations. Higher values reduce contention on the PMM when the system
is under load at the cost of using more memory when the system is idle.
)""")

DEFINE_OPTION("kernel.portpacket.reserve-pages", uint64_t, port_packet_reserve_pages, {1},
              R"""(
Specifies the number of pages per CPU to reserve for port packet (port_queue)
allocations. Higher values reduce contention on the PMM when the system is
under load at the cost of using more memory when the system is idle.
)""")

DEFINE_OPTION("kernel.root-job.behavior", RootJobBehavior, root_job_behavior,
              {RootJobBehavior::kReboot}, R"""(
This option specifies what action the kernel should take when the root job is
either terminated, or has no jobs and no processes.

When `halt`, will halt the system.

When `reboot`, will reboot the system.

When `bootloader`, will reboot the system into the bootloader.

When `recovery`, will reboot the system into the recovery partition.

When `shutdown`, will shutdown the system.
)""")

DEFINE_OPTION("kernel.root-job.notice", SmallString, root_job_notice, {}, R"""(
The option allows a notice to be printed when the root job is either terminated, or has no jobs
 and no processes.
)""")

DEFINE_OPTION("kernel.shell", bool, shell, {false}, R"""(
Tells the kernel to start its own shell on the kernel console instead of a userspace sh.
)""")

DEFINE_OPTION("kernel.smp.ht", bool, smp_ht_enabled, {true}, R"""(
This option specifies whether the HyperThreading (HT) logical CPUs should be enabled or not.
)""")

DEFINE_OPTION("kernel.test.ram.reserve", std::optional<RamReservation>, test_ram_reserve, {}, R"""(
Specifies a range of physical RAM to be reserved for testing purposes.
This should be written as just SIZE (an integer byte quantity, which should
be page-aligned) but will be read back as SIZE,ADDRESS after the kernel
assigns an address in early boot.
)""")

DEFINE_OPTION("kernel.port.max-observers", uint64_t, max_port_observers, {50000},
R"""(
Specifies the maximum number of observers any single port may have. When this limit is reached, a
Zircon exception is raised in the process that crosses the limit. This value should be high enough
that well behaved programs will not hit the limit, but low enough to terminate misbehaving programs
before they impact the system.
)""")

DEFINE_OPTION("kernel.experimental.serial_migration", bool, experimental_serial_migration, {false}, R"""(
**TODO(fxbug.dev/89182)**

Temporary flag to assist on serial driver migration from kernel's implementation to
ulib/uart implementation.
)""")

// Machine-specific options are included here for all the kernel places.
// In the generator program, they're included separately.

#if defined(__x86_64__) && !BOOT_OPTIONS_GENERATOR
#include "x86.inc"
#elif defined(__aarch64__) && !BOOT_OPTIONS_GENERATOR
#include "arm64.inc"
#endif

#if BOOT_OPTIONS_TESTONLY_OPTIONS
#include "test-options.inc"
#endif
