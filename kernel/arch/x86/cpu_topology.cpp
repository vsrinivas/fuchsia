// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <arch/ops.h>
#include <arch/x86/cpu_topology.h>
#include <arch/x86/feature.h>
#include <pow2.h>
#include <bits.h>
#include <stdio.h>
#include <string.h>
#include <trace.h>

#define LOCAL_TRACE 0

static uint32_t smt_mask = 0;

static uint32_t core_mask = 0;
static uint32_t core_shift = 0;

// Default to all bits, so that the topology system fails towards
// distinguishing all CPUs
static uint32_t package_mask = ~0;
static uint32_t package_shift = 0;

static int initialized;

static void legacy_topology_init(void);
static void modern_intel_topology_init(void);
static void extended_amd_topology_init(void);

void x86_cpu_topology_init(void)
{
    if (atomic_swap(&initialized, 1)) {
        return;
    }

    if (x86_vendor == X86_VENDOR_INTEL && x86_get_cpuid_leaf(X86_CPUID_BASE)->a >= X86_CPUID_TOPOLOGY) {
        modern_intel_topology_init();
    } else if (x86_vendor == X86_VENDOR_AMD) {
        extended_amd_topology_init();
    } else {
        legacy_topology_init();
    }
}

static void modern_intel_topology_init(void)
{
    // This is based off of Intel 3A's Example 8-18 "Support Routine for
    // Identifying Package, Core, and Logical Processors from 32-bit x2APIC ID"
    struct x86_topology_level info;
    if (!x86_topology_enumerate(0, &info)) {
        return;
    }

    if (info.type == X86_TOPOLOGY_SMT) {
        smt_mask = (1 << info.right_shift) - 1;
        core_shift = info.right_shift;
    }

    for (uint8_t level = 0; x86_topology_enumerate(level, &info); ++level) {
        if (info.type == X86_TOPOLOGY_CORE) {
            core_mask = ((1 << info.right_shift) - 1) ^ smt_mask;
            package_shift = info.right_shift;
            package_mask = ~(core_mask | smt_mask);
            break;
        }
    }
}

static void extended_amd_topology_init(void)
{
    // Described in AMD CPUID Specification, version 2.34, section 3.2
    const struct cpuid_leaf *leaf = x86_get_cpuid_leaf(X86_CPUID_ADDR_WIDTH);
    if (!leaf)
        return;

    // width of the core part of the apic id
    uint32_t apic_id_core_id_size = BITS_SHIFT(leaf->c, 15, 12);
    if (apic_id_core_id_size == 0) {
        legacy_topology_init();
        return;
    }

    core_shift = 0;
    core_mask = (1u << apic_id_core_id_size) - 1;

    package_shift = apic_id_core_id_size;
    package_mask = ~core_mask;

    // check to see if AMD topology extensions are enabled
    if (x86_feature_test(X86_FEATURE_AMD_TOPO)) {
        leaf = x86_get_cpuid_leaf(X86_CPUID_AMD_TOPOLOGY);
        if (!leaf)
            return;

        uint32_t cores_per_compute_unit = BITS_SHIFT(leaf->b, 15, 8) + 1;
        if (cores_per_compute_unit == 2) {
            // SMT is enabled, the bottom bit of the APIC id is the SMT id
            // This is according to the BKDG and PPR for family 15h-17h
            smt_mask = 1;
            core_shift = 1;
        } else if (cores_per_compute_unit > 2) {
            // not sure how to handle this, display message and move on
            TRACEF("WARNING: cores per compute unit > 2 (%u), unhandled\n", cores_per_compute_unit);
        }

        // TODO: handle multiple nodes per processor in 0x8000001e/ecx
    }

}

static void legacy_topology_init(void)
{
    const struct cpuid_leaf *leaf = x86_get_cpuid_leaf(X86_CPUID_MODEL_FEATURES);
    if (!leaf) {
        return;
    }

    bool pkg_size_valid = !!(leaf->d & (1<<28));
    if (!pkg_size_valid) {
        return;
    }

    // Get the maximum number of addressable IDs at the sub-package level.
    uint8_t max_num_subpackage = (leaf->b >> 16) & 0xff;

    leaf = x86_get_cpuid_leaf(X86_CPUID_CACHE_V2);
    if (!leaf) {
        return;
    }

    // Get the maximum number of addressable cores with a package.
    uint max_num_core = (leaf->a >> 26) + 1;
    uint max_num_ht = max_num_subpackage / max_num_core;

    package_mask = ~(max_num_subpackage - 1);
    package_shift = __builtin_ctz(package_mask);

    smt_mask = max_num_ht - 1;
    core_shift = __builtin_ctz(~smt_mask);
    core_mask = ~package_mask ^ smt_mask;
}

void x86_cpu_topology_decode(uint32_t apic_id, x86_cpu_topology_t *topo) {
    memset(topo, 0, sizeof(*topo));

    LTRACEF("id 0x%x: package_shift %u package_mask 0x%x core_shift %u core_mask 0x%x smt_mask %u\n",
        apic_id, package_shift, package_mask, core_shift, core_mask, smt_mask);

    topo->package_id = (apic_id & package_mask) >> package_shift;
    topo->core_id = (apic_id & core_mask) >> core_shift;
    topo->smt_id = apic_id & smt_mask;
}
