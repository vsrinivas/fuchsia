// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2016 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <assert.h>
#include <debug.h>
#include <err.h>
#include <stdio.h>
#include <string.h>
#include <trace.h>
#include <zircon/compiler.h>

#include <arch/mp.h>
#include <arch/ops.h>
#include <arch/x86.h>
#include <arch/x86/apic.h>
#include <arch/x86/cpu_topology.h>
#include <arch/x86/descriptor.h>
#include <arch/x86/feature.h>
#include <arch/x86/interrupts.h>
#include <arch/x86/mmu.h>
#include <arch/x86/mp.h>
#include <arch/x86/tsc.h>
#include <dev/hw_rng.h>
#include <dev/interrupt.h>
#include <kernel/event.h>
#include <kernel/timer.h>
#include <platform.h>
#include <zircon/types.h>

#define LOCAL_TRACE 0

struct x86_percpu* ap_percpus;
uint8_t x86_num_cpus = 1;
static bool use_monitor = false;

extern struct idt _idt;

zx_status_t x86_allocate_ap_structures(uint32_t* apic_ids, uint8_t cpu_count) {
    ASSERT(ap_percpus == nullptr);

    DEBUG_ASSERT(cpu_count >= 1);
    if (cpu_count == 0) {
        return ZX_ERR_INVALID_ARGS;
    }

    if (cpu_count > 1) {
        size_t len = sizeof(*ap_percpus) * (cpu_count - 1);
        ap_percpus = (x86_percpu*)memalign(MAX_CACHE_LINE, len);
        if (ap_percpus == nullptr) {
            return ZX_ERR_NO_MEMORY;
        }
        memset(ap_percpus, 0, len);

        if ((use_monitor = x86_feature_test(X86_FEATURE_MON))) {
            uint16_t monitor_size = x86_get_cpuid_leaf(X86_CPUID_MON)->b & 0xffff;
            if (monitor_size < MAX_CACHE_LINE) {
                monitor_size = MAX_CACHE_LINE;
            }
            uint8_t* monitors = (uint8_t*)memalign(monitor_size, monitor_size * cpu_count);
            if (monitors == nullptr) {
                return ZX_ERR_NO_MEMORY;
            }
            bp_percpu.monitor = monitors;
            for (uint i = 1; i < cpu_count; ++i) {
                ap_percpus[i - 1].monitor = monitors + (i * monitor_size);
            }
        }
    }

    uint32_t bootstrap_ap = apic_local_id();
    DEBUG_ASSERT(bootstrap_ap == apic_bsp_id());

    uint apic_idx = 0;
    for (uint i = 0; i < cpu_count; ++i) {
        if (apic_ids[i] == bootstrap_ap) {
            continue;
        }
        DEBUG_ASSERT(apic_idx != (uint)(cpu_count - 1));
        if (apic_idx == (uint)cpu_count - 1) {
            /* Never found bootstrap CPU in apic id list */
            return ZX_ERR_BAD_STATE;
        }
        ap_percpus[apic_idx].cpu_num = apic_idx + 1;
        ap_percpus[apic_idx].apic_id = apic_ids[i];
        ap_percpus[apic_idx].direct = &ap_percpus[apic_idx];
        apic_idx++;
    }

    x86_num_cpus = cpu_count;
    return ZX_OK;
}

void x86_init_percpu(cpu_num_t cpu_num) {
    struct x86_percpu* const percpu =
        cpu_num == 0 ? &bp_percpu : &ap_percpus[cpu_num - 1];
    DEBUG_ASSERT(percpu->cpu_num == cpu_num);
    DEBUG_ASSERT(percpu->direct == percpu);

    // Assembly code has already set up %gs.base so that this function's
    // own code can use it implicitly for stack-protector or safe-stack.
    DEBUG_ASSERT(read_msr(X86_MSR_IA32_GS_BASE) == (uintptr_t)percpu);

    /* set the KERNEL_GS_BASE MSR to 0 */
    /* when we enter user space, this will be populated via a swapgs */
    write_msr(X86_MSR_IA32_KERNEL_GS_BASE, 0);

    x86_feature_init();

    x86_cpu_topology_init();
    x86_extended_register_init();
    x86_extended_register_enable_feature(X86_EXTENDED_REGISTER_SSE);
    x86_extended_register_enable_feature(X86_EXTENDED_REGISTER_AVX);

    // This can be turned on/off later by the user. Turn it on here so that
    // the buffer size assumes it's on.
    x86_extended_register_enable_feature(X86_EXTENDED_REGISTER_PT);
    // But then set the default mode to off.
    x86_set_extended_register_pt_state(false);

    x86_initialize_percpu_tss();

    // Setup the post early boot IDT
    if (cpu_num == 0) {
        idt_setup(&_idt);
        // Setup alternate stacks to guarantee stack sanity when handling these
        // interrupts
        idt_set_ist_index(&_idt, X86_INT_NMI, NMI_IST_INDEX);
        idt_set_ist_index(&_idt, X86_INT_MACHINE_CHECK, MCE_IST_INDEX);
        idt_set_ist_index(&_idt, X86_INT_DOUBLE_FAULT, DBF_IST_INDEX);
        idt_load(&_idt);
    } else {
        // Load the read-only IDT setup on arch initialization.
        idt_load(idt_get_readonly());
    }

    // Apply any timestamp counter adjustment to keep a continuous clock across
    // suspend/resume.
    x86_tsc_adjust();

    /* load the syscall entry point */
    write_msr(X86_MSR_IA32_LSTAR, (uint64_t)&x86_syscall);

    /* set the STAR MSR to load the appropriate kernel code selector on syscall
     * and the appropriate user code selector on return.
     * on syscall entry the following are loaded into segment registers:
     *   CS = CODE_64_SELECTOR      (STAR[47:32])
     *   SS = DATA_SELECTOR         (STAR[47:32] + 0x8)
     * on syscall exit:
     *   CS = USER_CODE_64_SELECTOR (STAR[63:48] + 0x16)
     *   SS = USER_DATA_SELECTOR    (STAR[63:48] + 0x8)
     */
    write_msr(X86_MSR_IA32_STAR, (uint64_t)USER_CODE_SELECTOR << 48 | (uint64_t)CODE_64_SELECTOR << 32);

    // Set the FMASK register to mask off certain bits in RFLAGS on syscall
    // entry.  See docs/kernel_invariants.md.
    uint64_t mask =
        X86_FLAGS_AC |         /* disable alignment check/access control (this
                                * prevents ring 0 from performing data access
                                * to ring 3 if SMAP is available) */
        X86_FLAGS_NT |         /* clear nested task */
        X86_FLAGS_IOPL_MASK |  /* set iopl to 0 */
        X86_FLAGS_STATUS_MASK; /* clear all status flags, interrupt disabled, trap flag */
    write_msr(X86_MSR_IA32_FMASK, mask);

    // Apply the same mask to our current flags, to ensure that flags are
    // set to known-good values, because some flags may be inherited by
    // later kernel threads.  We do this just in case any bad values were
    // left behind by firmware or the bootloader.
    x86_restore_flags(x86_save_flags() & ~mask);

    /* enable syscall instruction */
    uint64_t efer_msr = read_msr(X86_MSR_IA32_EFER);
    efer_msr |= X86_EFER_SCE;
    write_msr(X86_MSR_IA32_EFER, efer_msr);

    uint64_t cr4 = x86_get_cr4();
    // Enable {rd,wr}{fs,gs}base instructions.
    if (x86_feature_test(X86_FEATURE_FSGSBASE)) {
        cr4 |= X86_CR4_FSGSBASE;
    }
    if (x86_feature_test(X86_FEATURE_UMIP)) {
        cr4 |= X86_CR4_UMIP;
    }
    x86_set_cr4(cr4);

    // Some intel cpus support auto-entering C1E state when all cores are at C1. In
    // C1E state the voltage is reduced on all cores as well as clock gated. There is
    // a latency associated with ramping the voltage on wake. Disable this feature here
    // to save time on the irq path from idle. (5-10us on skylake nuc from kernel irq
    // handler to user space handler).
    if (!x86_feature_test(X86_FEATURE_HYPERVISOR) &&
        x86_get_microarch_config()->disable_c1e) {
        uint64_t power_ctl_msr = read_msr(0x1fc);
        write_msr(0x1fc, power_ctl_msr & ~0x2);
    }

    mp_set_curr_cpu_online(true);
}

void x86_set_local_apic_id(uint32_t apic_id) {
    struct x86_percpu* percpu = x86_get_percpu();
    DEBUG_ASSERT(percpu->cpu_num == 0);
    percpu->apic_id = apic_id;
}

int x86_apic_id_to_cpu_num(uint32_t apic_id) {
    if (bp_percpu.apic_id == apic_id) {
        return (int)bp_percpu.cpu_num;
    }

    for (uint i = 0; i < (uint)x86_num_cpus - 1; ++i) {
        if (ap_percpus[i].apic_id == apic_id) {
            return (int)ap_percpus[i].cpu_num;
        }
    }
    return -1;
}

zx_status_t arch_mp_reschedule(cpu_mask_t mask) {
    DEBUG_ASSERT(spin_lock_held(&thread_lock));

    cpu_mask_t needs_ipi = 0;
    if (use_monitor) {
        while (mask) {
            cpu_num_t cpu_id = lowest_cpu_set(mask);
            cpu_mask_t cpu_mask = cpu_num_to_mask(cpu_id);
            struct x86_percpu* percpu = cpu_id ? &ap_percpus[cpu_id - 1] : &bp_percpu;

            // When a cpu see that it is about to start the idle thread, it sets its own
            // monitor flag. When a cpu is rescheduling another cpu, if it sees the monitor flag
            // set, it can clear the flag to wake up the other cpu w/o an IPI. When the other
            // cpu wakes up, the idle thread sees the cleared flag and preempts itself. Both of
            // these operations are under the scheduler lock, so there are no races where the
            // wrong signal can be sent.
            uint8_t old_val = *percpu->monitor;
            *percpu->monitor = 0;
            if (!old_val) {
                needs_ipi |= cpu_mask;
            }
            mask &= ~cpu_mask;
        }
    } else {
        needs_ipi = mask;
    }

    return needs_ipi ? arch_mp_send_ipi(MP_IPI_TARGET_MASK, needs_ipi, MP_IPI_RESCHEDULE) : ZX_OK;
}

void arch_prepare_current_cpu_idle_state(bool idle) {
    DEBUG_ASSERT(spin_lock_held(&thread_lock));

    if (use_monitor) {
        *x86_get_percpu()->monitor = idle;
    }
}

__NO_RETURN int arch_idle_thread_routine(void*) {
    if (use_monitor) {
        struct x86_percpu* percpu = x86_get_percpu();
        for (;;) {
            while (*percpu->monitor) {
                x86_monitor(percpu->monitor);
                // Check percpu->monitor in case it was cleared between the first check and
                // the monitor being armed. Any writes after arming the monitor will trigger
                // it and cause mwait to return, so there aren't races after this check.
                if (*percpu->monitor) {
                    x86_mwait();
                }
            }
            thread_preempt();
        }
    } else {
        for (;;) {
            x86_idle();
        }
    }
}

zx_status_t arch_mp_send_ipi(mp_ipi_target_t target, cpu_mask_t mask, mp_ipi_t ipi) {
    uint8_t vector = 0;
    switch (ipi) {
    case MP_IPI_GENERIC:
        vector = X86_INT_IPI_GENERIC;
        break;
    case MP_IPI_RESCHEDULE:
        vector = X86_INT_IPI_RESCHEDULE;
        break;
    case MP_IPI_HALT:
        vector = X86_INT_IPI_HALT;
        break;
    default:
        panic("Unexpected MP IPI value: %u", (uint)ipi);
    }

    if (target == MP_IPI_TARGET_ALL_BUT_LOCAL) {
        apic_send_broadcast_ipi(vector, DELIVERY_MODE_FIXED);
        return ZX_OK;
    } else if (target == MP_IPI_TARGET_ALL) {
        apic_send_broadcast_self_ipi(vector, DELIVERY_MODE_FIXED);
        return ZX_OK;
    }

    ASSERT(x86_num_cpus <= sizeof(mask) * CHAR_BIT);

    cpu_mask_t remaining = mask;
    uint cpu_id = 0;
    while (remaining && cpu_id < x86_num_cpus) {
        if (remaining & 1) {
            struct x86_percpu* percpu;
            if (cpu_id == 0) {
                percpu = &bp_percpu;
            } else {
                percpu = &ap_percpus[cpu_id - 1];
            }
            /* Reschedule IPIs may occur before all CPUs are fully up.  Just
             * ignore attempts to send them to down CPUs. */
            if (ipi != MP_IPI_RESCHEDULE) {
                DEBUG_ASSERT(percpu->apic_id != INVALID_APIC_ID);
            }
            /* Make sure the CPU is actually up before sending the IPI */
            if (percpu->apic_id != INVALID_APIC_ID) {
                apic_send_ipi(vector, (uint8_t)percpu->apic_id, DELIVERY_MODE_FIXED);
            }
        }
        remaining >>= 1;
        cpu_id++;
    }

    return ZX_OK;
}

void x86_ipi_generic_handler(void) {
    LTRACEF("cpu %u\n", arch_curr_cpu_num());
    mp_mbx_generic_irq();
}

void x86_ipi_reschedule_handler(void) {
    LTRACEF("cpu %u\n", arch_curr_cpu_num());
    mp_mbx_reschedule_irq();
}

void x86_ipi_halt_handler(void) {
    printf("halting cpu %u\n", arch_curr_cpu_num());

    platform_halt_cpu();

    for (;;) {
        x86_cli();
        x86_hlt();
    }
}

// Forcibly stops all other CPUs except the current one and the BSP (which is
// cpu 0)
void x86_force_halt_all_but_local_and_bsp(void) {
    cpu_num_t self = arch_curr_cpu_num();
    for (cpu_num_t i = 1; i < x86_num_cpus; ++i) {
        if (i == self) {
            continue;
        }
        uint32_t dst_apic_id = ap_percpus[i - 1].apic_id;
        apic_send_ipi(0, static_cast<uint8_t>(dst_apic_id),
                      DELIVERY_MODE_INIT);
    }
}

zx_status_t arch_mp_prep_cpu_unplug(uint cpu_id) {
    if (cpu_id == 0 || cpu_id >= x86_num_cpus) {
        return ZX_ERR_INVALID_ARGS;
    }
    return ZX_OK;
}

zx_status_t arch_mp_cpu_unplug(uint cpu_id) {
    /* we do not allow unplugging the bootstrap processor */
    if (cpu_id == 0 || cpu_id >= x86_num_cpus) {
        return ZX_ERR_INVALID_ARGS;
    }

    uint32_t dst_apic_id = ap_percpus[cpu_id - 1].apic_id;
    if (dst_apic_id == INVALID_APIC_ID) {
        /* This is a transient state that can occur during CPU onlining */
        return ZX_ERR_UNAVAILABLE;
    }

    DEBUG_ASSERT(dst_apic_id < UINT8_MAX);
    apic_send_ipi(0, (uint8_t)dst_apic_id, DELIVERY_MODE_INIT);
    return ZX_OK;
}

zx_status_t arch_mp_cpu_hotplug(uint cpu_id) {
    if (cpu_id >= x86_num_cpus) {
        return ZX_ERR_INVALID_ARGS;
    }
    if (mp_is_cpu_online(cpu_id)) {
        return ZX_ERR_BAD_STATE;
    }
    DEBUG_ASSERT(cpu_id != 0);
    if (cpu_id == 0) {
        /* We shouldn't be able to shutoff the bootstrap CPU, so
         * no reason to be able to bring it back via this route. */
        return ZX_ERR_INVALID_ARGS;
    }

    struct x86_percpu* percpu = &ap_percpus[cpu_id - 1];
    DEBUG_ASSERT(percpu->apic_id != INVALID_APIC_ID);
    return x86_bringup_aps(&percpu->apic_id, 1);
}

/* Used to suspend work on a CPU until it is further shutdown */
void arch_flush_state_and_halt(event_t* flush_done) {
    DEBUG_ASSERT(arch_ints_disabled());

    __asm__ volatile("wbinvd"
                     :
                     :
                     : "memory");

    event_signal(flush_done, false);
    while (1) {
        __asm__ volatile("cli; hlt"
                         :
                         :
                         : "memory");
    }
}
