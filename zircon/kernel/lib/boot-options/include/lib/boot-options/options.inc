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
Sets the "LFSR loops" parameter for jitterentropy (the default is 1). When
jitterentropy is performing CPU-intensive LFSR operations (to increase variation
in CPU timing), this controls how many times the LFSR routine is repeated.  This
parameter is only used when `kernel.jitterentropy.raw` is true. If the value of
this parameter is `0` or if `kernel.jitterentropy.raw` is `false`, then
jitterentropy chooses the number of loops is a random-ish way.
)""")

DEFINE_OPTION("kernel.jitterentropy.raw", bool, jitterentropy_raw, {true}, R"""(
When true (the default), the jitterentropy entropy collector will return raw,
unprocessed samples. When false, the raw samples will be processed by
jitterentropy, producing output data that looks closer to uniformly random. Note
that even when set to false, the CPRNG will re-process the samples, so the
processing inside of jitterentropy is somewhat redundant.
)""")

DEFINE_OPTION("kernel.oom.behavior", OomBehavior, oom_behavior, {OomBehavior::kReboot}, R"""(
This option can be used to configure the behavior of the kernel when
encountering an out-of-memory (OOM) situation. Valid values are `jobkill`, and
`reboot`.

If set to `jobkill`, when encountering OOM, the kernel attempts to kill jobs that
have the `ZX_PROP_JOB_KILL_ON_OOM` bit set to recover memory.

If set to `reboot`, when encountering OOM, the kernel signals an out-of-memory
event (see `zx_system_get_event()`), delays briefly, and then reboots the system.
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

DEFINE_OPTION("kernel.serial", uart::all::Driver, serial, {}, R"""(
TODO(53594)
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
