// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2014 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#ifndef __ASSEMBLER__

#include <assert.h>
#include <stdbool.h>
#include <sys/types.h>

#include <syscalls/syscalls.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

__BEGIN_CDECLS

#define DSB __asm__ volatile("dsb sy" :: \
                                 : "memory")
#define DSB_ISHST __asm__ volatile("dsb ishst" :: \
                                       : "memory")
#define DMB __asm__ volatile("dmb sy" :: \
                                 : "memory")
#define DMB_ISHST __asm__ volatile("dmb ishst" :: \
                                       : "memory")
#define ISB __asm__ volatile("isb" :: \
                                 : "memory")

#define STRINGIFY(x) #x
#define TOSTRING(x) STRINGIFY(x)

#define ARM64_READ_SYSREG(reg)                   \
    ({                                           \
        uint64_t _val;                           \
        __asm__ volatile("mrs %0," TOSTRING(reg) \
                         : "=r"(_val));          \
        _val;                                    \
    })

#define ARM64_WRITE_SYSREG(reg, val)                               \
    ({                                                             \
        uint64_t _val = (val);                                     \
        __asm__ volatile("msr " TOSTRING(reg) ", %0" ::"r"(_val)); \
        ISB;                                                       \
    })

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

/* exception handling */
struct arm64_iframe_long {
    uint64_t r[30];
    uint64_t lr;
    uint64_t usp;
    uint64_t elr;
    uint64_t spsr;
    uint64_t mdscr;
    uint64_t pad2[1]; // Keep structure multiple of 16-bytes for stack alignment.
};

struct arm64_iframe_short {
    uint64_t r[20];
    // pad the short frame out so that it has the same general shape and size as a long
    uint64_t pad[10];
    uint64_t lr;
    uint64_t usp;
    uint64_t elr;
    uint64_t spsr;
    uint64_t pad2[2];
};

static_assert(sizeof(struct arm64_iframe_long) == sizeof(struct arm64_iframe_short), "");

struct arch_exception_context {
    struct arm64_iframe_long* frame;
    uint64_t far;
    uint32_t esr;
};

struct thread;
extern void arm64_el1_exception_base(void);
void arm64_el3_to_el1(void);
void arm64_sync_exception(struct arm64_iframe_long* iframe, uint exception_flags, uint32_t esr);
void arm64_thread_process_pending_signals(struct arm64_iframe_long* iframe);

typedef struct arm64_iframe_long iframe_t;
typedef struct arm64_iframe_short iframe;

void platform_irq(iframe* frame);
void platform_fiq(iframe* frame);

/* fpu routines */
void arm64_fpu_exception(struct arm64_iframe_long* iframe, uint exception_flags);
void arm64_fpu_context_switch(struct thread* oldthread, struct thread* newthread);

uint64_t arm64_get_boot_el(void);

void arm_reset(void);
/*
 * Sets the secondary stack pointer for the specified CPU.  |sp| and
 * |unsafe_sp| must point to the top (highest address, exclusive) of the
 * memory to use as the stacks.
 */
zx_status_t arm64_set_secondary_sp(uint cluster, uint cpu,
                                   void* sp, void* unsafe_sp);

__END_CDECLS

#endif // __ASSEMBLER__

/* used in above exception_flags arguments */
#define ARM64_EXCEPTION_FLAG_LOWER_EL (1 << 0)
#define ARM64_EXCEPTION_FLAG_ARM32 (1 << 1)
