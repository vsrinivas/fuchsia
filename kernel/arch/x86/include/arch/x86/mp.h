// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2016 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

/* describes the per cpu structure pointed to by gs: in the kernel */

/* offsets into this structure, used by assembly */
#define PERCPU_DIRECT_OFFSET           0x0
#define PERCPU_CURRENT_THREAD_OFFSET   0x8
//      MX_TLS_STACK_GUARD_OFFSET      0x10
//      MX_TLS_UNSAFE_SP_OFFSET        0x18
#define PERCPU_SAVED_USER_SP_OFFSET    0x20
#define PERCPU_IN_IRQ_OFFSET           0x28
#define PERCPU_GPF_RETURN_OFFSET       0x30
#define PERCPU_CPU_NUM_OFFSET          0x38
#define PERCPU_DEFAULT_TSS_OFFSET      0x40

/* offset of default_tss.rsp0 */
#define PERCPU_KERNEL_SP_OFFSET        (PERCPU_DEFAULT_TSS_OFFSET + 4)

#ifndef ASSEMBLY

#include <arch/spinlock.h>
#include <arch/ops.h>
#include <arch/x86.h>
#include <arch/x86/idt.h>
#include <assert.h>
#include <magenta/compiler.h>
#include <magenta/tls.h>
#include <stdint.h>

__BEGIN_CDECLS

struct thread;

struct x86_percpu {
    /* a direct pointer to ourselves */
    struct x86_percpu *direct;

    /* the current thread */
    struct thread *current_thread;

    // The offsets of these two slots are published in
    // system/public/magenta/tls.h and known to the compiler.
    uintptr_t stack_guard;
    uintptr_t kernel_unsafe_sp;

    /* temporarily saved during a syscall */
    uintptr_t saved_user_sp;

    /* are we currently in an irq handler */
    uint32_t in_irq;

    /* local APIC id */
    uint32_t apic_id;

    /* If nonzero and we receive a GPF, change the return IP to this value. */
    uintptr_t gpf_return_target;

    /* CPU number */
    uint32_t cpu_num;

    /* This CPU's default TSS */
    tss_t default_tss __ALIGNED(16);

    /* Reserved space for interrupt stacks */
    uint8_t interrupt_stacks[NUM_ASSIGNED_IST_ENTRIES][PAGE_SIZE] __ALIGNED(16);
} __CPU_MAX_ALIGN;

static_assert(__offsetof(struct x86_percpu, direct) == PERCPU_DIRECT_OFFSET, "");
static_assert(__offsetof(struct x86_percpu, current_thread) == PERCPU_CURRENT_THREAD_OFFSET, "");
static_assert(__offsetof(struct x86_percpu, stack_guard) == MX_TLS_STACK_GUARD_OFFSET, "");
static_assert(__offsetof(struct x86_percpu, kernel_unsafe_sp) == MX_TLS_UNSAFE_SP_OFFSET, "");
static_assert(__offsetof(struct x86_percpu, saved_user_sp) == PERCPU_SAVED_USER_SP_OFFSET, "");
static_assert(__offsetof(struct x86_percpu, in_irq) == PERCPU_IN_IRQ_OFFSET, "");
static_assert(__offsetof(struct x86_percpu, gpf_return_target) == PERCPU_GPF_RETURN_OFFSET, "");
static_assert(__offsetof(struct x86_percpu, cpu_num) == PERCPU_CPU_NUM_OFFSET, "");
static_assert(__offsetof(struct x86_percpu, default_tss) == PERCPU_DEFAULT_TSS_OFFSET, "");
static_assert(__offsetof(struct x86_percpu, default_tss.rsp0) == PERCPU_KERNEL_SP_OFFSET, "");

extern struct x86_percpu bp_percpu;
extern struct x86_percpu *ap_percpus;

// This needs to be run very early in the boot process from start.S and as
// each CPU is brought up.
void x86_init_percpu(uint cpu_num);

/* used to set the bootstrap processor's apic_id once the APIC is initialized */
void x86_set_local_apic_id(uint32_t apic_id);

int x86_apic_id_to_cpu_num(uint32_t apic_id);

// Allocate all of the necessary structures for all of the APs to run.
status_t x86_allocate_ap_structures(uint32_t *apic_ids, uint8_t cpu_count);

static inline struct x86_percpu *x86_get_percpu(void)
{
    return (struct x86_percpu *)x86_read_gs_offset64(PERCPU_DIRECT_OFFSET);
}

static inline uint arch_curr_cpu_num(void)
{
    return x86_get_percpu()->cpu_num;
}

extern uint8_t x86_num_cpus;
static uint arch_max_num_cpus(void)
{
    return x86_num_cpus;
}

static bool arch_in_int_handler(void)
{
    return (bool)x86_read_gs_offset32(PERCPU_IN_IRQ_OFFSET);
}

static void arch_set_in_int_handler(bool in_irq)
{
    x86_write_gs_offset32(PERCPU_IN_IRQ_OFFSET, in_irq);
}

enum handler_return x86_ipi_generic_handler(void);
enum handler_return x86_ipi_reschedule_handler(void);
void x86_ipi_halt_handler(void) __NO_RETURN;
void x86_secondary_entry(volatile int *aps_still_booting, thread_t *thread);

__END_CDECLS

#endif // !ASSEMBLY
