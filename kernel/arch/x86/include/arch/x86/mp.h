// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2016 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

/* describes the per cpu structure pointed to by gs: in the kernel */

/* offsets into this structure, used by assembly */
#if ARCH_X86_32
#define PERCPU_DIRECT_OFFSET           0x0
#define PERCPU_CURRENT_THREAD_OFFSET   0x4
#define PERCPU_KERNEL_SP_OFFSET        0x8
#define PERCPU_SAVED_USER_SP_OFFSET    0xc
#define PERCPU_IN_IRQ_OFFSET           0x10
#define PERCPU_DEFAULT_TSS_OFFSET      0x20
#elif ARCH_X86_64
#define PERCPU_DIRECT_OFFSET           0x0
#define PERCPU_CURRENT_THREAD_OFFSET   0x8
#define PERCPU_KERNEL_SP_OFFSET        0x10
#define PERCPU_SAVED_USER_SP_OFFSET    0x18
#define PERCPU_IN_IRQ_OFFSET           0x20
#define PERCPU_DEFAULT_TSS_OFFSET      0x30
#endif

#ifndef ASSEMBLY

#include <arch/spinlock.h>
#include <arch/x86.h>
#include <arch/x86/idt.h>
#include <assert.h>
#include <magenta/compiler.h>
#include <stdint.h>

__BEGIN_CDECLS

struct thread;

struct x86_percpu {
    /* a direct pointer to ourselves */
    struct x86_percpu *direct;

    /* the current thread */
    struct thread *current_thread;

    /* our current kernel sp, to be loaded by syscall */
    // TODO: Remove this and replace with a fetch from
    // the current tss?
    uintptr_t kernel_sp;

    /* temporarily saved during a syscall */
    uintptr_t saved_user_sp;

    /* are we currently in an irq handler */
    uint32_t in_irq;

    /* local APIC id */
    uint32_t apic_id;

    /* CPU number */
    uint8_t cpu_num;

    /* This CPU's default TSS */
    tss_t __ALIGNED(16) default_tss;

    /* The IDT for this CPU */
    struct idt idt;
#ifdef ARCH_X86_64
    /* Reserved space for interrupt stacks */
    uint8_t interrupt_stacks[NUM_ASSIGNED_IST_ENTRIES][PAGE_SIZE];
#endif
};

static_assert(__offsetof(struct x86_percpu, direct) == PERCPU_DIRECT_OFFSET, "");
static_assert(__offsetof(struct x86_percpu, current_thread) == PERCPU_CURRENT_THREAD_OFFSET, "");
static_assert(__offsetof(struct x86_percpu, kernel_sp) == PERCPU_KERNEL_SP_OFFSET, "");
static_assert(__offsetof(struct x86_percpu, saved_user_sp) == PERCPU_SAVED_USER_SP_OFFSET, "");
static_assert(__offsetof(struct x86_percpu, in_irq) == PERCPU_IN_IRQ_OFFSET, "");
static_assert(__offsetof(struct x86_percpu, default_tss) == PERCPU_DEFAULT_TSS_OFFSET, "");

/* needs to be run very early in the boot process from start.S and as each cpu is brought up */
void x86_init_percpu(uint8_t cpu_num);

/* used to set the bootstrap processor's apic_id once the APIC is initialized */
void x86_set_local_apic_id(uint32_t apic_id);

int x86_apic_id_to_cpu_num(uint32_t apic_id);

// Allocate all of the necessary structures for all of the APs to run.
status_t x86_allocate_ap_structures(uint32_t *apic_ids, uint8_t cpu_count);

static inline struct x86_percpu *x86_get_percpu(void)
{
#if ARCH_X86_64
    return (struct x86_percpu *)x86_read_gs_offset64(PERCPU_DIRECT_OFFSET);
#else
    /* x86-32 does not yet support SMP and thus does not need a gs: pointer to point
     * at the percpu structure
     */
    static_assert(SMP_MAX_CPUS == 1, "");
    extern struct x86_percpu bp_percpu;
    return &bp_percpu;
#endif
}

static inline struct thread *get_current_thread(void)
{
#if ARCH_X86_64
    /* Read directly from gs, rather than via x86_get_percpu()->current_thread,
     * so that this is atomic.  Otherwise, we could context switch between the
     * read of percpu from gs and the read of the current_thread pointer, and
     * discover the current thread on a different CPU */
    return (struct thread *)x86_read_gs_offset64(PERCPU_CURRENT_THREAD_OFFSET);
#else
    spin_lock_saved_state_t state;
    arch_interrupt_save(&state, 0);
    struct thread *t = x86_get_percpu()->current_thread;
    arch_interrupt_restore(state, 0);
    return t;
#endif
}

static inline void set_current_thread(struct thread *t)
{
#if ARCH_X86_64
    /* See above for why this is a direct gs write */
    x86_write_gs_offset64(PERCPU_CURRENT_THREAD_OFFSET, (uint64_t)t);
#else
    spin_lock_saved_state_t state;
    arch_interrupt_save(&state, 0);
    x86_get_percpu()->current_thread = t;
    arch_interrupt_restore(state, 0);
#endif
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

/* set on every context switch and before entering user space */
static inline void x86_set_percpu_kernel_sp(uintptr_t sp)
{
#if ARCH_X86_64
    x86_write_gs_offset64(PERCPU_KERNEL_SP_OFFSET, sp);
#else
    spin_lock_saved_state_t state;
    arch_interrupt_save(&state, 0);
    x86_get_percpu()->kernel_sp = sp;
    arch_interrupt_restore(state, 0);
#endif
}

static bool arch_in_int_handler(void)
{
#if ARCH_X86_64
    return (bool)x86_read_gs_offset32(PERCPU_IN_IRQ_OFFSET);
#else
    spin_lock_saved_state_t state;
    arch_interrupt_save(&state, 0);
    bool in_irq = x86_get_percpu()->in_irq;
    arch_interrupt_restore(state, 0);
    return in_irq;
#endif
}

static void arch_set_in_int_handler(bool in_irq)
{
#if ARCH_X86_64
    x86_write_gs_offset32(PERCPU_IN_IRQ_OFFSET, in_irq);
#else
    /* should only be setting this inside an irq handler with interrupts disabled */
    x86_get_percpu()->in_irq = in_irq;
#endif
}

enum handler_return x86_ipi_generic_handler(void);
enum handler_return x86_ipi_reschedule_handler(void);
void x86_ipi_halt_handler(void) __NO_RETURN;
void x86_secondary_entry(volatile int *aps_still_booting, thread_t *thread);

__END_CDECLS

#endif

