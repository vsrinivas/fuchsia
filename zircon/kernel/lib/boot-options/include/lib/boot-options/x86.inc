// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

// This file is #include'd multiple times with the DEFINE_OPTION macro defined.
// See kernel/lib/boot-options/README.md for details.
//
// These are the x86-specific boot options.

DEFINE_OPTION("kernel.x86.disable_spec_mitigations", bool, disable_x86_spec_mitigations, {false}, R"""(
If set, disables all speculative execution information leak mitigations.

If unset, the per-mitigation defaults will be used.
)""")

DEFINE_OPTION("kernel.x86.md_clear_on_user_return", bool, md_clear_on_user_return, {true}, R"""(
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

DEFINE_OPTION("kernel.x86.pti.enable", uint32_t, pti_enable, {2}, R"""(
Page table isolation configures user page tables to not have kernel text or
data mapped. This may impact performance negatively. This is a mitigation
for Meltdown (AKA CVE-2017-5754).

* If set to 1, this force-enables page table isolation.
* If set to 0, this force-disables page table isolation. This may be insecure.
* If set to 2 or unset (the default), this enables page table isolation on
CPUs vulnerable to Meltdown.

TODO(joshuaseaton): make this an enum instead of using magic integers.
)""")

DEFINE_OPTION("kernel.x86.spec_store_bypass_disable", bool, spec_store_bypass_disable, {false},
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
