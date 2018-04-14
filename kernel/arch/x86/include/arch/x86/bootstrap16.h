// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <arch/x86/mmu.h>

#define BCD_PHYS_BOOTSTRAP_PML4_OFFSET 0
#define BCD_PHYS_KERNEL_PML4_OFFSET 4
#define BCD_PHYS_GDTR_OFFSET 8
#define BCD_PHYS_LM_ENTRY_OFFSET 20
#define BCD_LM_CS_OFFSET 24
#define BCD_CPU_COUNTER_OFFSET 28
#define BCD_CPU_WAITING_OFFSET 32
#define BCD_PER_CPU_BASE_OFFSET 40

#define RED_REGISTERS_OFFSET 28

#ifndef __ASSEMBLER__
#include <assert.h>
#include <vm/vm_aspace.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

__BEGIN_CDECLS

// Markers for the application processor bootstrap code region
extern void x86_bootstrap16_start(void);
extern void x86_bootstrap16_end(void);

// 64-bit entry points that bootstrap might transition to

// Entry point used for secondary CPU initialization
extern void _x86_secondary_cpu_long_mode_entry(void);

// Entry point used for suspend-to-RAM resume vector.
// Note that this does not restore %rdi, and it touches below the saved %rsp.
extern void _x86_suspend_wakeup(void);

__END_CDECLS

struct __PACKED x86_bootstrap16_data {
    // Physical address of identity PML4
    uint32_t phys_bootstrap_pml4;
    // Physical address of the kernel PML4
    uint32_t phys_kernel_pml4;
    // Physical address of GDTR
    uint16_t phys_gdtr_limit;
    uint64_t phys_gdtr_base;
    uint16_t __pad;

    // Ordering of these two matter; they should be usable by retfl
    // Physical address of long mode entry point
    uint32_t phys_long_mode_entry;
    // 64-bit code segment to use
    uint32_t long_mode_cs;
};

struct __PACKED x86_realmode_entry_data {
    struct x86_bootstrap16_data hdr;

    // Virtual address of the register dump (expected to be in
    // the form of x86_realmode_entry_data_registers)
    uint64_t registers_ptr;
};

struct x86_realmode_entry_data_registers {
    uint64_t rdi, rsi, rbp, rbx, rdx, rcx, rax;
    uint64_t r8, r9, r10, r11, r12, r13, r14, r15;
    uint64_t rsp, rip;
};


struct __PACKED x86_ap_bootstrap_data {
    struct x86_bootstrap16_data hdr;

    // Counter for APs to use to determine which stack to take
    uint32_t cpu_id_counter;
    // Pointer to value to use to determine when APs are done with boot
    volatile int *cpu_waiting_mask;

    // Per-cpu data
    struct __PACKED {
        // Virtual address of base of initial kstack
        uint64_t kstack_base;
        // Virtual address of initial thread_t
        uint64_t thread;
    } per_cpu[SMP_MAX_CPUS - 1];
};

// Initialize the bootstrap16 subsystem by giving it pages to work with.
// |bootstrap_base| must refer to two consecutive pages of RAM with addresses
// less than 1M that are available for the OS to use.
void x86_bootstrap16_init(paddr_t bootstrap_base);

// Upon success, returns a pointer to the bootstrap aspace, a pointer to the
// virtual address of the bootstrap data, and the physical address of the
// first instruction that should be executed in 16-bit mode.  It is the caller's
// responsibility to free the aspace once it is no longer needed.
//
// If this function returns success, x86_bootstrap16_release() must be called
// later, to allow the bootstrap16 module to be reused.
zx_status_t x86_bootstrap16_acquire(uintptr_t entry64, fbl::RefPtr<VmAspace> *temp_aspace,
                                    void **bootstrap_aperature, paddr_t* instr_ptr);

// To be called once the caller is done using the bootstrap16 module
void x86_bootstrap16_release(void* bootstrap_aperature);

static_assert(sizeof(struct x86_ap_bootstrap_data) <= PAGE_SIZE, "");
static_assert(sizeof(struct x86_realmode_entry_data) <= PAGE_SIZE, "");

static_assert(__offsetof(struct x86_bootstrap16_data, phys_bootstrap_pml4) == BCD_PHYS_BOOTSTRAP_PML4_OFFSET, "");
static_assert(__offsetof(struct x86_bootstrap16_data, phys_kernel_pml4) == BCD_PHYS_KERNEL_PML4_OFFSET, "");
static_assert(__offsetof(struct x86_bootstrap16_data, phys_gdtr_limit) == BCD_PHYS_GDTR_OFFSET, "");
static_assert(__offsetof(struct x86_bootstrap16_data, phys_gdtr_base) == BCD_PHYS_GDTR_OFFSET+2, "");
static_assert(__offsetof(struct x86_bootstrap16_data, phys_long_mode_entry) == BCD_PHYS_LM_ENTRY_OFFSET, "");
static_assert(__offsetof(struct x86_bootstrap16_data, long_mode_cs) == BCD_LM_CS_OFFSET, "");

static_assert(__offsetof(struct x86_ap_bootstrap_data, hdr) == 0, "");
static_assert(__offsetof(struct x86_ap_bootstrap_data, cpu_id_counter) == BCD_CPU_COUNTER_OFFSET, "");
static_assert(__offsetof(struct x86_ap_bootstrap_data, cpu_waiting_mask) == BCD_CPU_WAITING_OFFSET, "");
static_assert(__offsetof(struct x86_ap_bootstrap_data, per_cpu) == BCD_PER_CPU_BASE_OFFSET, "");

static_assert(__offsetof(struct x86_realmode_entry_data, hdr) == 0, "");
static_assert(__offsetof(struct x86_realmode_entry_data, registers_ptr) == RED_REGISTERS_OFFSET, "");

#endif // __ASSEMBLER__
