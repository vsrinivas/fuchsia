// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2014 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_ARCH_ARM64_INCLUDE_ARCH_ARM64_H_
#define ZIRCON_KERNEL_ARCH_ARM64_INCLUDE_ARCH_ARM64_H_

#ifndef __ASSEMBLER__

#include <assert.h>
#include <stdbool.h>
#include <sys/types.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

#include <kernel/cpu.h>

__BEGIN_CDECLS

struct iframe_t;

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
  uint32_t user_synth_code;
};

// Register state layout used by arm64_context_switch().
struct arm64_context_switch_frame {
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

struct Thread;

#if __has_feature(shadow_call_stack)
void arm64_context_switch(vaddr_t* old_sp, vaddr_t new_sp, uintptr_t** old_scsp,
                          uintptr_t* new_scsp);
#else
void arm64_context_switch(vaddr_t* old_sp, vaddr_t new_sp);
#endif
void arm64_uspace_entry(iframe_t* iframe, vaddr_t kstack) __NO_RETURN;
arm64_context_switch_frame* arm64_get_context_switch_frame(Thread* thread);

extern void arm64_el1_exception_base(void);
void arm64_el3_to_el1(void);
void arm64_sync_exception(iframe_t* iframe, uint exception_flags, uint32_t esr);

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
void arm64_fpu_exception(iframe_t* iframe, uint exception_flags);
void arm64_fpu_context_switch(Thread* oldthread, Thread* newthread);
void arm64_fpu_save_state(Thread* t);
void arm64_fpu_restore_state(Thread* t);

uint64_t arm64_get_boot_el(void);

void arm_reset(void);

/*
 * Creates a stack and sets the stack pointer for the specified secondary CPU.
 */
zx_status_t arm64_create_secondary_stack(cpu_num_t cpu_num, uint64_t mpid);

/*
 * Frees a stack created by |arm64_create_secondary_stack|.
 */
zx_status_t arm64_free_secondary_stack(cpu_num_t cpu_num);

__END_CDECLS

#endif  // __ASSEMBLER__

/* used in above exception_flags arguments */
#define ARM64_EXCEPTION_FLAG_LOWER_EL (1 << 0)
#define ARM64_EXCEPTION_FLAG_ARM32 (1 << 1)

#endif  // ZIRCON_KERNEL_ARCH_ARM64_INCLUDE_ARCH_ARM64_H_
