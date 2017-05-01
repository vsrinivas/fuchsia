// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2014 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#ifndef ASSEMBLY

#include <stdbool.h>
#include <sys/types.h>
#include <magenta/compiler.h>

__BEGIN_CDECLS

#define DSB __asm__ volatile("dsb sy" ::: "memory")
#define ISB __asm__ volatile("isb" ::: "memory")

#define STRINGIFY(x) #x
#define TOSTRING(x) STRINGIFY(x)

#define ARM64_READ_SYSREG(reg) \
({ \
    uint64_t _val; \
    __asm__ volatile("mrs %0," TOSTRING(reg) : "=r" (_val)); \
    _val; \
})

#define ARM64_WRITE_SYSREG(reg, val) \
({ \
    uint64_t _val = (val); \
    __asm__ volatile("msr " TOSTRING(reg) ", %0" :: "r" (_val)); \
    ISB; \
})

void arm64_context_switch(vaddr_t *old_sp, vaddr_t new_sp);
void arm64_uspace_entry(uintptr_t arg1, uintptr_t arg2,
                        uintptr_t pc, uintptr_t sp,
                        vaddr_t kstack, uint32_t spsr) __NO_RETURN;


typedef struct {
    uint8_t     ctype;
    bool        write_through;
    bool        write_back;
    bool        read_alloc;
    bool        write_alloc;
    uint32_t    num_sets;
    uint32_t    associativity;
    uint32_t    line_size;
} arm64_cache_desc_t;

typedef struct {
    uint8_t                 inner_boundary;
    uint8_t                 lou_u;
    uint8_t                 loc;
    uint8_t                 lou_is;
    arm64_cache_desc_t      level_data_type[7];
    arm64_cache_desc_t      level_inst_type[7];
} arm64_cache_info_t;

/* exception handling */
struct arm64_iframe_long {
    uint64_t r[30];
    uint64_t lr;
    uint64_t usp;
    uint64_t elr;
    uint64_t spsr;
};

struct arm64_iframe_short {
    uint64_t pad;
    uint64_t r[19];
    uint64_t lr;
    uint64_t usp;
    uint64_t elr;
    uint64_t spsr;
};

struct arch_exception_context {
    struct arm64_iframe_long *frame;
    uint64_t far;
    uint32_t esr;
};

struct thread;
extern void arm64_exception_base(void);
void arm64_el3_to_el1(void);
void arm64_sync_exception(struct arm64_iframe_long *iframe, uint exception_flags);

typedef struct arm64_iframe_short iframe;

enum handler_return platform_irq(iframe* frame);
enum handler_return platform_fiq(iframe* frame);

void arm64_thread_process_pending_signals(struct arm64_iframe_long *frame);

/* fpu routines */
void arm64_fpu_exception(struct arm64_iframe_long *iframe, uint exception_flags);
void arm64_fpu_context_switch(struct thread *oldthread, struct thread *newthread);

/* overridable syscall handler */
void arm64_syscall(struct arm64_iframe_long *iframe, bool is_64bit, uint64_t pc);
uint64_t arm64_get_boot_el(void);
void arm64_get_cache_info(arm64_cache_info_t* info);
void arm64_dump_cache_info(uint32_t cpu);

void arm_reset(void);
/*
 * Sets the secondary stack pointer for the specified CPU.  |sp| and
 * |unsafe_sp| must point to the top (highest address, exclusive) of the
 * memory to use as the stacks.
 */
status_t arm64_set_secondary_sp(uint cluster, uint cpu,
                                void* sp, void* unsafe_sp);

/* block size of the dc zva instruction */
extern uint32_t arm64_zva_shift;

__END_CDECLS

#endif // __ASSEMBLY__

/* used in above exception_flags arguments */
#define ARM64_EXCEPTION_FLAG_LOWER_EL (1<<0)
#define ARM64_EXCEPTION_FLAG_ARM32    (1<<1)
