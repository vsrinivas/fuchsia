// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

// This file is #include'd multiple times with the DEFINE_OPTION macro defined.
// See kernel/lib/boot-options/README.md for details.
//
// These are the x86-specific boot options.

DEFINE_OPTION("kernel.x86.disable_spec_mitigations", bool, x86_disable_spec_mitigations, {false},
              R"""(
If set, disables all speculative execution information leak mitigations.

If unset, the per-mitigation defaults will be used.
)""")

DEFINE_OPTION("kernel.x86.hwp", bool, x86_hwp, {true}, R"""(
This settings enables HWP (hardware P-states) on supported chips. This feature
lets Intel CPUs automatically scale their own clock speed.
)""")

DEFINE_OPTION("kernel.x86.hwp_policy", IntelHwpPolicy, x86_hwp_policy,
              {IntelHwpPolicy::kBiosSpecified}, R"""(
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
)""")

DEFINE_OPTION("kernel.x86.md_clear_on_user_return", bool, x86_md_clear_on_user_return, {true}, R"""(
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
)""")

DEFINE_OPTION("kernel.x86.spec_store_bypass_disable", bool, x86_spec_store_bypass_disable, {false},
              R"""(
Spec-store-bypass (Spectre V4) is a speculative execution information leak
vulnerability that affects many Intel and AMD x86 CPUs. It targets memory
disambiguation hardware to infer the contents of recent stores. The attack
only affects same-privilege-level, intra-process data.

This command line option controls whether a mitigation is enabled. The
mitigation has negative performance impacts.

* If true, the mitigation is enabled on CPUs that need it.
* If false (the default), the mitigation is not enabled.
)""")

DEFINE_OPTION("kernel.x86.turbo", bool, x86_turbo, {true}, R"""(
Turbo Boost or Core Performance Boost are mechanisms that allow processors to
dynamically vary their performance at runtime based on available thermal and
electrical budget. This may provide improved interactive performance at the cost
of performance variability. Some workloads may benefit from disabling Turbo; if
this command line flag is set to false, turbo is disabled for all CPUs in the
system.
)""")

// TODO(fxb/80082): Unify arch agnostic options.
DEFINE_OPTION("kernel.smp.maxcpus", uint32_t, x86_smp_max_cpus, {32}, R"""(
This option caps the number of CPUs to initialize.  It cannot be greater than *SMP\_MAX\_CPUS* 
for a specific architecture.

Note: The default value may vary for each architecture.
)""")

DEFINE_OPTION("kernel.enable_suspend", bool, x86_enable_suspend, {false}, R"""(
Enable the kernel to go to the S3 (Suspend to RAM) ACPI power state.
)""")

DEFINE_OPTION("kernel.wallclock", WallclockType, x86_wallclock, {WallclockType::kAutoDetect}, R"""(
This option can be used to force the selection of a particular wall clock on pc builds.
)""")
