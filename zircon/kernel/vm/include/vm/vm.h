// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2014 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_VM_INCLUDE_VM_VM_H_
#define ZIRCON_KERNEL_VM_INCLUDE_VM_VM_H_

#include <align.h>
#include <arch.h>
#include <lib/ktrace.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/types.h>
#include <zircon/compiler.h>
#include <zircon/listnode.h>

#include <arch/kernel_aspace.h>
#include <ktl/span.h>
#include <vm/arch_vm_aspace.h>

#ifndef VM_TRACING_LEVEL
#define VM_TRACING_LEVEL 0
#endif

// Evaluates to true if tracing is enabled for the given level.
#define VM_KTRACE_LEVEL_ENABLED(level) ((VM_TRACING_LEVEL) >= (level))

#define VM_KTRACE_DURATION(level, string, args...)                             \
  TraceDuration<TraceEnabled<VM_KTRACE_LEVEL_ENABLED(level)>, KTRACE_GRP_VM, \
                TraceContext::Thread>                                          \
      duration{KTRACE_STRING_REF(string), ##args};

#define VM_KTRACE_FLOW_BEGIN(level, string, flow_id, args...)                         \
  ktrace_flow_begin(LocalTrace<VM_KTRACE_LEVEL_ENABLED(level)>, TraceContext::Thread, \
                    KTRACE_GRP_VM, KTRACE_STRING_REF(string), flow_id, ##args);

#define VM_KTRACE_FLOW_END(level, string, flow_id, args...)                         \
  ktrace_flow_end(LocalTrace<VM_KTRACE_LEVEL_ENABLED(level)>, TraceContext::Thread, \
                  KTRACE_GRP_VM, KTRACE_STRING_REF(string), flow_id, ##args);

class VmAspace;

// kernel address space
static_assert(KERNEL_ASPACE_BASE + (KERNEL_ASPACE_SIZE - 1) > KERNEL_ASPACE_BASE, "");

static inline bool is_kernel_address(vaddr_t va) {
  return (va >= (vaddr_t)KERNEL_ASPACE_BASE &&
          va - (vaddr_t)KERNEL_ASPACE_BASE < (vaddr_t)KERNEL_ASPACE_SIZE);
}

// user address space, defaults to below kernel space with a 16MB guard gap on either side
static_assert(USER_ASPACE_BASE + (USER_ASPACE_SIZE - 1) > USER_ASPACE_BASE, "");

static inline bool is_user_address(vaddr_t va) {
  return (va >= USER_ASPACE_BASE && va <= (USER_ASPACE_BASE + (USER_ASPACE_SIZE - 1)));
}

static inline bool is_user_address_range(vaddr_t va, size_t len) {
  return va + len >= va && is_user_address(va) && (len == 0 || is_user_address(va + len - 1));
}

// linker script provided variables for various virtual kernel addresses
extern char __code_start[];
extern char __code_end[];
extern char __rodata_start[];
extern char __rodata_end[];
extern char __data_start[];
extern char __data_end[];
extern char __bss_start[];
extern char _end[];

// return the physical address corresponding to _start
static inline paddr_t get_kernel_base_phys() {
  extern paddr_t kernel_base_phys;

  return kernel_base_phys;
}

static inline size_t get_kernel_size() { return _end - __code_start; }

// return a pointer to the zero page
static inline vm_page_t* vm_get_zero_page(void) {
  extern vm_page_t* zero_page;
  return zero_page;
}

// return the physical address of the zero page
static inline paddr_t vm_get_zero_page_paddr(void) {
  extern paddr_t zero_page_paddr;

  return zero_page_paddr;
}

// List of the kernel program's various segments.
struct kernel_region {
  const char* name;
  vaddr_t base;
  size_t size;
  uint arch_mmu_flags;
};
extern const ktl::span<const kernel_region> kernel_regions;

// internal kernel routines below, do not call directly

// internal routine by the scheduler to swap mmu contexts
void vmm_context_switch(VmAspace* oldspace, VmAspace* newaspace);

// set the current user aspace as active on the current thread.
// NULL is a valid argument, which unmaps the current user address space
void vmm_set_active_aspace(VmAspace* aspace);

// specialized version of above function that must be called with the thread_lock already held.
// This is only intended for use by panic handlers.
void vmm_set_active_aspace_locked(VmAspace* aspace);

#endif  // ZIRCON_KERNEL_VM_INCLUDE_VM_VM_H_
