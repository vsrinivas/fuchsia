// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <assert.h>
#include <stdint.h>
#include <string.h>

#include <err.h>
#include <trace.h>
#include <arch/mmu.h>
#include <arch/x86.h>
#include <arch/x86/bootstrap16.h>
#include <arch/x86/apic.h>
#include <arch/x86/descriptor.h>
#include <arch/x86/mmu.h>
#include <arch/x86/mp.h>
#include <lk/main.h>
#include <kernel/thread.h>
#include <kernel/vm.h>

struct bp_bootstrap_data {
    // Counter of number of APs that have booted
    volatile int aps_still_booting;
    vmm_aspace_t *bootstrap_aspace;

    uint32_t num_apics;
    uint32_t *apic_ids;
};

void x86_bringup_smp(uint32_t *apic_ids, uint32_t num_cpus);

void x86_init_smp(uint32_t *apic_ids, uint32_t num_cpus)
{
    status_t status = x86_allocate_ap_structures(apic_ids, num_cpus);
    if (status != NO_ERROR) {
        TRACEF("Failed to allocate structures for APs");
        return;
    }

    lk_init_secondary_cpus(num_cpus - 1);

    x86_bringup_smp(apic_ids, num_cpus);
}

void x86_bringup_smp(uint32_t *apic_ids, uint32_t num_cpus) {
    struct x86_ap_bootstrap_data *bootstrap_data = NULL;

    struct bp_bootstrap_data config = {
        .aps_still_booting = num_cpus - 1,
        .bootstrap_aspace = NULL,
        .apic_ids = apic_ids,
        .num_apics = num_cpus,
    };

    status_t status = x86_bootstrap16_prep(
            PHYS_BOOTSTRAP_PAGE,
            (uintptr_t)_x86_secondary_cpu_long_mode_entry,
            &config.bootstrap_aspace,
            (void **)&bootstrap_data);
    if (status != NO_ERROR) {
        goto finish;
    }

    bootstrap_data->cpu_id_counter = 0;
    bootstrap_data->cpu_waiting_counter = &config.aps_still_booting;
    // Zero the kstack list so if we have to bail, we can safely free the
    // resources.
    memset(&bootstrap_data->per_cpu, 0, sizeof(bootstrap_data->per_cpu));
    // Allocate kstacks and threads for all processors
    for (unsigned int i = 0; i < config.num_apics - 1; ++i) {
        void *thread_addr = memalign(16, PAGE_SIZE + ROUNDUP(sizeof(thread_t), 16));
        if (!thread_addr) {
            goto cleanup_allocationss;
        }
        bootstrap_data->per_cpu[i].kstack_base =
                (uint64_t)thread_addr + ROUNDUP(sizeof(thread_t), 16);
        bootstrap_data->per_cpu[i].thread = (uint64_t)thread_addr;
    }

    // Memory fence to ensure all writes to the bootstrap region are
    // visible on the APs when they come up
    smp_wmb();

    uint32_t bsp_apic_id = apic_local_id();
    for (unsigned int i = 0; i < config.num_apics; ++i) {
        uint32_t apic_id = config.apic_ids[i];
        // Don't attempt to initialize the bootstrap processor
        if (apic_id == bsp_apic_id) {
            continue;
        }
        apic_send_ipi(0, apic_id, DELIVERY_MODE_INIT);
    }

    // Wait 10 ms and then send the startup signals
    thread_sleep(10);

    // Actually send the startups
    ASSERT(PHYS_BOOTSTRAP_PAGE < 1 * MB);
    uint8_t vec = PHYS_BOOTSTRAP_PAGE >> PAGE_SIZE_SHIFT;
    // Try up to two times per core
    for (int tries = 0; tries < 2; ++tries) {
        for (unsigned int i = 0; i < config.num_apics; ++i) {
            uint32_t apic_id = config.apic_ids[i];
            // Don't attempt to initialize the bootstrap processor
            if (apic_id == bsp_apic_id) {
                continue;
            }
            // This will cause the APs to begin executing at PHYS_BOOTSTRAP_PAGE in
            // physical memory.
            apic_send_ipi(vec, apic_id, DELIVERY_MODE_STARTUP);
        }

        if (config.aps_still_booting == 0) {
            break;
        }
        // Wait 2ms for cores to boot.  The docs recommend 200us, but we do a
        // bit more work before we mark an AP as booted.
        thread_sleep(2);
    }

    if (config.aps_still_booting != 0) {
        printf("Failed to boot %d cores\n", config.aps_still_booting);
        // If we failed to boot some cores, leak the shared resources.
        // There is the risk the cores will try to access them after
        // we free them.
        // TODO: Can we do better here
        goto finish;
    }

    // Now that everything is booted, cleanup all temporary structures (e.g.
    // everything except the kstacks).
    goto cleanup_aspace;
cleanup_allocationss:
    for (unsigned int i = 0; i < config.num_apics - 1; ++i) {
        if (bootstrap_data->per_cpu[i].thread) {
            free((void *)bootstrap_data->per_cpu[i].thread);
        }
    }
cleanup_aspace:
    status = vmm_free_aspace(config.bootstrap_aspace);
    DEBUG_ASSERT(status == NO_ERROR);
    vmm_aspace_t *kernel_aspace = vmm_get_kernel_aspace();
    vmm_free_region(kernel_aspace, (vaddr_t)bootstrap_data);
finish:
    // Get all CPUs to agree on the PATs
    x86_pat_sync();
}
