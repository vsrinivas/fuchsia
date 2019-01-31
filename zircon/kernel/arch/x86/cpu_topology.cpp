// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <arch/ops.h>
#include <arch/x86/cpu_topology.h>
#include <arch/x86/feature.h>
#include <bits.h>
#include <pow2.h>
#include <stdio.h>
#include <string.h>
#include <trace.h>

#define LOCAL_TRACE 0

// Use various methods to decode the apic id to different levels of cpu topology.
//
// The heirarchy is currently
// package (socket) : node (die within the socket) : core (within the die) : thread

// Default to all bits, so that the topology system fails towards
// distinguishing all CPUs
static uint32_t package_mask = ~0;
static uint32_t package_shift = 0;

static uint32_t node_mask = 0;
static uint32_t node_shift = 0;

static uint32_t core_mask = 0;
static uint32_t core_shift = 0;

static uint32_t smt_mask = 0;

static void legacy_topology_init();
static void modern_intel_topology_init();
static void extended_amd_topology_init();

void x86_cpu_topology_init() {
    static int initialized;

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

static void modern_intel_topology_init() {
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

static void extended_amd_topology_init() {
    // Described in AMD CPUID Specification, version 2.34, section 3.2
    const struct cpuid_leaf* leaf = x86_get_cpuid_leaf(X86_CPUID_ADDR_WIDTH);
    if (!leaf)
        return;

    // width of the core part of the apic id
    uint32_t apic_id_core_id_size = BITS_SHIFT(leaf->c, 15, 12);
    if (apic_id_core_id_size == 0) {
        legacy_topology_init();
        return;
    }

    // initial state of variables that are optionally set below
    // smt_mask = 0;
    // core_shift = 0;
    // node_shift = 0;
    // node_mask = 0;

    uint32_t node_size = 0;

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

        uint32_t nodes_per_processor = BITS_SHIFT(leaf->c, 10, 8) + 1;
        if (nodes_per_processor > 0 && ispow2(nodes_per_processor)) {
            // a pow2 number of bits between the core number and package number refer to node
            node_size = log2_uint_floor(nodes_per_processor);
            node_shift = apic_id_core_id_size - node_size;
            node_mask = (nodes_per_processor - 1) << node_shift;

            // node number chews in to the core number, so subtract node_size off
            // apic_id_core_id_size so that it computes the core mask properly
            apic_id_core_id_size -= node_size;
        }
    }

    // core is the mask of the bottom half of the APIC id space
    core_mask = (1u << apic_id_core_id_size) - 1;

    // package soaks up all the high bits of APIC id space
    package_shift = node_size + apic_id_core_id_size;
    package_mask = UINT32_MAX << (node_size + apic_id_core_id_size);
}

static void legacy_topology_init() {
    const struct cpuid_leaf* leaf = x86_get_cpuid_leaf(X86_CPUID_MODEL_FEATURES);
    if (!leaf) {
        return;
    }

    bool pkg_size_valid = !!(leaf->d & (1 << 28));
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

void x86_cpu_topology_decode(uint32_t apic_id, x86_cpu_topology_t* topo) {
    *topo = {};

    LTRACEF("id 0x%x: package shift/mask %u:%#x node shift/mask %u:%#x "
            "core shift/mask %u:%#x smt mask %#x\n",
            apic_id, package_shift, package_mask, node_shift, node_mask,
            core_shift, core_mask, smt_mask);

    topo->package_id = (apic_id & package_mask) >> package_shift;
    topo->node_id = (apic_id & node_mask) >> node_shift;
    topo->core_id = (apic_id & core_mask) >> core_shift;
    topo->smt_id = apic_id & smt_mask;
}
