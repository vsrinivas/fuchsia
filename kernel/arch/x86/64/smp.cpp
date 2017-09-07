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
#include <arch/x86/mmu_mem_types.h>
#include <arch/x86/mp.h>
#include <lk/main.h>
#include <kernel/mp.h>
#include <kernel/thread.h>
#include <vm/vm_aspace.h>

void x86_init_smp(uint32_t *apic_ids, uint32_t num_cpus)
{
    DEBUG_ASSERT(num_cpus <= UINT8_MAX);
    status_t status = x86_allocate_ap_structures(apic_ids, (uint8_t)num_cpus);
    if (status != MX_OK) {
        TRACEF("Failed to allocate structures for APs");
        return;
    }

    lk_init_secondary_cpus(num_cpus - 1);
}

status_t x86_bringup_aps(uint32_t *apic_ids, uint32_t count)
{
    volatile int aps_still_booting = 0;
    status_t status = MX_ERR_INTERNAL;

    // if being asked to bring up 0 cpus, move on
    if (count == 0) {
        return MX_OK;
    }

    // Sanity check the given ids
    for (uint i = 0; i < count; ++i) {
        int cpu = x86_apic_id_to_cpu_num(apic_ids[i]);
        DEBUG_ASSERT(cpu > 0);
        if (cpu <= 0) {
            return MX_ERR_INVALID_ARGS;
        }
        if (mp_is_cpu_online(cpu)) {
            return MX_ERR_BAD_STATE;
        }
        aps_still_booting |= 1U << cpu;
    }

    struct x86_ap_bootstrap_data *bootstrap_data = NULL;
    fbl::RefPtr<VmAspace> bootstrap_aspace;

    status = x86_bootstrap16_prep(
            PHYS_BOOTSTRAP_PAGE,
            (uintptr_t)_x86_secondary_cpu_long_mode_entry,
            &bootstrap_aspace,
            (void **)&bootstrap_data);
    if (status != MX_OK) {
        return status;
    }

    bootstrap_data->cpu_id_counter = 0;
    bootstrap_data->cpu_waiting_mask = &aps_still_booting;
    // Zero the kstack list so if we have to bail, we can safely free the
    // resources.
    memset(&bootstrap_data->per_cpu, 0, sizeof(bootstrap_data->per_cpu));
    // Allocate kstacks and threads for all processors
    for (unsigned int i = 0; i < count; ++i) {
        thread_t *thread = (thread_t *)memalign(16,
                                    ROUNDUP(sizeof(thread_t), 16) +
#if __has_feature(safe_stack)
                                    PAGE_SIZE +
#endif
                                    PAGE_SIZE);
        if (!thread) {
            status = MX_ERR_NO_MEMORY;
            goto cleanup_allocations;
        }
        uintptr_t kstack_base =
                (uint64_t)thread + ROUNDUP(sizeof(thread_t), 16);
        bootstrap_data->per_cpu[i].kstack_base = kstack_base;
        bootstrap_data->per_cpu[i].thread = (uint64_t)thread;
#if __has_feature(safe_stack)
        thread->unsafe_stack = (void *) (kstack_base + PAGE_SIZE);
        thread->stack_size = PAGE_SIZE;
#endif
    }

    // Memory fence to ensure all writes to the bootstrap region are
    // visible on the APs when they come up
    smp_mb();

    dprintf(INFO, "booting apic ids: ");
    for (unsigned int i = 0; i < count; ++i) {
        uint32_t apic_id = apic_ids[i];
        dprintf(INFO, "%#x ", apic_id);
        apic_send_ipi(0, apic_id, DELIVERY_MODE_INIT);
    }
    dprintf(INFO, "\n");

    // Wait 10 ms and then send the startup signals
    thread_sleep_relative(LK_MSEC(10));

    // Actually send the startups
    ASSERT(PHYS_BOOTSTRAP_PAGE < 1 * MB);
    uint8_t vec;
    vec = PHYS_BOOTSTRAP_PAGE >> PAGE_SIZE_SHIFT;
    // Try up to two times per CPU, as Intel 3A recommends.
    for (int tries = 0; tries < 2; ++tries) {
        for (unsigned int i = 0; i < count; ++i) {
            uint32_t apic_id = apic_ids[i];

            // This will cause the APs to begin executing at PHYS_BOOTSTRAP_PAGE in
            // physical memory.
            apic_send_ipi(vec, apic_id, DELIVERY_MODE_STARTUP);
        }

        if (aps_still_booting == 0) {
            break;
        }
        // Wait 1ms for cores to boot.  The docs recommend 200us between STARTUP
        // IPIs.
        thread_sleep_relative(LK_MSEC(1));
    }

    // The docs recommend waiting 200us for cores to boot.  We do a bit more
    // work before the cores report in, so wait longer (up to 1 second).
    for (int tries_left = 200;
         aps_still_booting != 0 && tries_left > 0;
         --tries_left) {

        thread_sleep_relative(LK_MSEC(5));
    }

    uint failed_aps;
    failed_aps = (uint)atomic_swap(&aps_still_booting, 0);
    if (failed_aps != 0) {
        printf("Failed to boot CPUs: mask %x\n", failed_aps);
        for (uint i = 0; i < count; ++i) {
            int cpu = x86_apic_id_to_cpu_num(apic_ids[i]);
            uint mask = 1U << cpu;
            if ((failed_aps & mask) == 0) {
                continue;
            }

            // Shut the failed AP down
            apic_send_ipi(0, apic_ids[i], DELIVERY_MODE_INIT);

            // It shouldn't have been possible for it to have been in the
            // scheduler...
            ASSERT(!mp_is_cpu_active(cpu));

            // Make sure the CPU is not marked online
            atomic_and((volatile int *)&mp.online_cpus, ~mask);

            // Free the failed AP's thread, it was cancelled before it could use
            // it.
            free((void *)bootstrap_data->per_cpu[i].thread);

            failed_aps &= ~mask;
        }
        DEBUG_ASSERT(failed_aps == 0);

        status = MX_ERR_TIMED_OUT;

        goto finish;
    }

    // Now that everything is booted, cleanup all temporary structures (e.g.
    // everything except the kstacks).
    goto cleanup_aspace;
cleanup_allocations:
    for (unsigned int i = 0; i < count; ++i) {
        if (bootstrap_data->per_cpu[i].thread) {
            free((void *)bootstrap_data->per_cpu[i].thread);
        }
    }
cleanup_aspace:
    bootstrap_aspace->Destroy();
    VmAspace::kernel_aspace()->FreeRegion(reinterpret_cast<vaddr_t>(bootstrap_data));
finish:
    return status;
}
