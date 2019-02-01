// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2014 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#ifndef __ASSEMBLER__

#include <arm_acle.h>
#include <assert.h>
#include <stdbool.h>
#include <sys/types.h>

#include <syscalls/syscalls.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

// Constants from ACLE section 8.3, used as the argument for __dmb(), __dsb(), and __isb()
// in arm_acle.h. Values are the architecturally defined immediate values encoded in barrier
// instructions DMB, DSB, and ISB.
#define ARM_MB_OSHLD    0x1
#define ARM_MB_OSHST    0x2
#define ARM_MB_OSH      0x3

#define ARM_MB_NSHLD    0x5
#define ARM_MB_NSHST    0x6
#define ARM_MB_NSH      0x7

#define ARM_MB_ISHLD    0x9
#define ARM_MB_ISHST    0xa
#define ARM_MB_ISH      0xb

#define ARM_MB_LD       0xd
#define ARM_MB_ST       0xe
#define ARM_MB_SY       0xf

__BEGIN_CDECLS

void arm64_context_switch(vaddr_t* old_sp, vaddr_t new_sp);
void arm64_uspace_entry(uintptr_t arg1, uintptr_t arg2,
                        uintptr_t pc, uintptr_t sp,
                        vaddr_t kstack, uint32_t spsr,
                        uint32_t mdscr) __NO_RETURN;

typedef struct {
    uint8_t ctype;
    bool write_through;
    bool write_back;
    bool read_alloc;
    bool write_alloc;
    uint32_t num_sets;
    uint32_t associativity;
    uint32_t line_size;
} arm64_cache_desc_t;

typedef struct {
    uint8_t inner_boundary;
    uint8_t lou_u;
    uint8_t loc;
    uint8_t lou_is;
    arm64_cache_desc_t level_data_type[7];
    arm64_cache_desc_t level_inst_type[7];
} arm64_cache_info_t;

// exception handling
// This is the main struct used by architecture-independent code.
// It can be forward declared thus this is the "real" type and
// arm64_iframe_t is the alias.
struct iframe_t {
    uint64_t r[30];
    uint64_t lr;
    uint64_t usp;
    uint64_t elr;
    uint64_t spsr;
    uint64_t mdscr;
    uint64_t pad2[1]; // Keep structure multiple of 16-bytes for stack alignment.
};

// This is an arm-specific iframe for IRQs.
struct iframe_short_t {
    uint64_t r[20];
    // pad the short frame out so that it has the same general shape and size as a long
    uint64_t pad[10];
    uint64_t lr;
    uint64_t usp;
    uint64_t elr;
    uint64_t spsr;
    uint64_t pad2[2];
};

static_assert(sizeof(iframe_t) == sizeof(iframe_short_t), "");

// Lots of the code uses this name.
typedef struct iframe_t arm64_iframe_t;

struct arch_exception_context {
    struct iframe_t* frame;
    uint64_t far;
    uint32_t esr;
};

struct thread;
extern void arm64_el1_exception_base(void);
void arm64_el3_to_el1(void);
void arm64_sync_exception(arm64_iframe_t* iframe, uint exception_flags, uint32_t esr);
void arm64_thread_process_pending_signals(arm64_iframe_t* iframe);

void platform_irq(iframe_short_t* frame);
void platform_fiq(iframe_short_t* frame);

/* fpu routines */
void arm64_fpu_exception(arm64_iframe_t* iframe, uint exception_flags);
void arm64_fpu_context_switch(struct thread* oldthread, struct thread* newthread);

uint64_t arm64_get_boot_el(void);

void arm_reset(void);

/*
 * Creates a stack and sets the stack pointer for the specified secondary CPU.
 */
zx_status_t arm64_create_secondary_stack(uint cpu_num, uint64_t mpid);

/*
 * Frees a stack created by |arm64_create_secondary_stack|.
 */
zx_status_t arm64_free_secondary_stack(uint cpu_num);

__END_CDECLS

#endif // __ASSEMBLER__

/* used in above exception_flags arguments */
#define ARM64_EXCEPTION_FLAG_LOWER_EL (1 << 0)
#define ARM64_EXCEPTION_FLAG_ARM32 (1 << 1)
