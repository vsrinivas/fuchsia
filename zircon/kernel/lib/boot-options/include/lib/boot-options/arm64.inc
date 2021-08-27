// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

// This file is #include'd multiple times with the DEFINE_OPTION macro defined.
// See kernel/lib/boot-options/README.md for details.
//
// These are the arm64-specific boot options.

DEFINE_OPTION("kernel.arm64.disable_spec_mitigations", bool, arm64_disable_spec_mitigations,
              {false}, R"""(
If set, disables all speculative execution information leak mitigations.

If unset, the per-mitigation defaults will be used.
)""")

DEFINE_OPTION("kernel.arm64.event-stream.enable", bool, arm64_event_stream_enabled, {true}, R"""(
When enabled, each ARM cpu will enable an event stream generator, which per-cpu
sets the hidden event flag at a particular rate. This has the effect of kicking
cpus out of any WFE states they may be sitting in.
)""")

DEFINE_OPTION("kernel.arm64.event-stream.freq-hz", uint32_t, arm64_event_stream_freq_hz, {10000},
              R"""(
If the event stream is enabled, specifies the frequency at which it will attempt
to run. The resolution is limited, so the driver will only be able to pick the
nearest power of 2 from the cpu timer counter.
)""")

DEFINE_OPTION("kernel.arm64.debug.dap-rom-soc", SmallString, arm64_debug_dap_rom_soc, {""}, R"""(
If set, tries to initialize the dap debug aperture at a hard coded address for the particular
system on chip. Currently accepted values are amlogic-t931g, amlogic-s905d2, and amlogic-s905d3g.
)""")

// TODO(fxb/80082): Unify arch agnostic options.
DEFINE_OPTION("kernel.smp.maxcpus", uint32_t, arm64_smp_max_cpus, {16}, R"""(
This option caps the number of CPUs to initialize.  It cannot be greater than *SMP\_MAX\_CPUS*
for a specific architecture.

Note: The default value may vary for each architechture.
)""")

DEFINE_OPTION("kernel.arm64.phys.mmu", bool, phys_mmu, {true}, R"""(
This enables use of the MMU and caches during the kernel's early boot phase.
)""")
