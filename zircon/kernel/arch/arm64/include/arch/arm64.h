// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2014 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_ARCH_ARM64_INCLUDE_ARCH_ARM64_H_
#define ZIRCON_KERNEL_ARCH_ARM64_INCLUDE_ARCH_ARM64_H_

#ifndef __ASSEMBLER__

#include <arm_acle.h>
#include <assert.h>
#include <stdbool.h>
#include <sys/types.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

#include <arch/arm64/iframe.h>
#include <syscalls/syscalls.h>

// Constants from ACLE section 8.3, used as the argument for __dmb(), __dsb(), and __isb()
// in arm_acle.h. Values are the architecturally defined immediate values encoded in barrier
// instructions DMB, DSB, and ISB.
#define ARM_MB_OSHLD 0x1
#define ARM_MB_OSHST 0x2
#define ARM_MB_OSH 0x3

#define ARM_MB_NSHLD 0x5
#define ARM_MB_NSHST 0x6
#define ARM_MB_NSH 0x7

#define ARM_MB_ISHLD 0x9
#define ARM_MB_ISHST 0xa
#define ARM_MB_ISH 0xb

#define ARM_MB_LD 0xd
#define ARM_MB_ST 0xe
#define ARM_MB_SY 0xf

__BEGIN_CDECLS

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

struct arch_exception_context {
  struct iframe_t* frame;
  uint64_t far;
  uint32_t esr;
};

// Register state layout used by arm64_context_switch().
struct arm64_context_switch_frame {
  uint64_t tpidr_el0;
  uint64_t tpidrro_el0;
  uint64_t r19;
  uint64_t r20;
  uint64_t r21;
  uint64_t r22;
  uint64_t r23;
  uint64_t r24;
  uint64_t r25;
  uint64_t r26;
  uint64_t r27;
  uint64_t r28;
  uint64_t r29;
  uint64_t lr;
};

struct thread_t;

#if __has_feature(shadow_call_stack)
void arm64_context_switch(vaddr_t* old_sp, vaddr_t new_sp, uintptr_t** old_scsp, uintptr_t* new_scsp);
#else
void arm64_context_switch(vaddr_t* old_sp, vaddr_t new_sp);
#endif
void arm64_uspace_entry(iframe_t* iframe, vaddr_t kstack) __NO_RETURN;
arm64_context_switch_frame* arm64_get_context_switch_frame(thread_t* thread);

extern void arm64_el1_exception_base(void);
void arm64_el3_to_el1(void);
void arm64_sync_exception(arm64_iframe_t* iframe, uint exception_flags, uint32_t esr);

void platform_irq(iframe_t* frame);
void platform_fiq(iframe_t* frame);

/* Local per-cpu cache flush routines.
 * These routines clean or invalidate the cache from the point of view
 * of a single cpu to the point of coherence.
 */
void arm64_local_invalidate_cache_all();
void arm64_local_clean_invalidate_cache_all();
void arm64_local_clean_cache_all();

/* fpu routines */
void arm64_fpu_exception(arm64_iframe_t* iframe, uint exception_flags);
void arm64_fpu_context_switch(thread_t* oldthread, thread_t* newthread);

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

#endif  // __ASSEMBLER__

/* used in above exception_flags arguments */
#define ARM64_EXCEPTION_FLAG_LOWER_EL (1 << 0)
#define ARM64_EXCEPTION_FLAG_ARM32 (1 << 1)

#endif  // ZIRCON_KERNEL_ARCH_ARM64_INCLUDE_ARCH_ARM64_H_
