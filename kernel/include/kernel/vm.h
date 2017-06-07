// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2014 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <arch.h>
#include <assert.h>
#include <list.h>
#include <magenta/compiler.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/types.h>

#define PAGE_ALIGN(x) ALIGN((x), PAGE_SIZE)
#define ROUNDUP_PAGE_SIZE(x) ROUNDUP((x), PAGE_SIZE)
#define IS_PAGE_ALIGNED(x) IS_ALIGNED((x), PAGE_SIZE)

// kernel address space
#ifndef KERNEL_ASPACE_BASE
#define KERNEL_ASPACE_BASE ((vaddr_t)0x80000000UL)
#endif
#ifndef KERNEL_ASPACE_SIZE
#define KERNEL_ASPACE_SIZE ((vaddr_t)0x80000000UL)
#endif

static_assert(KERNEL_ASPACE_BASE + (KERNEL_ASPACE_SIZE - 1) > KERNEL_ASPACE_BASE, "");

static inline bool is_kernel_address(vaddr_t va) {
    return (va >= (vaddr_t)KERNEL_ASPACE_BASE &&
            va <= ((vaddr_t)KERNEL_ASPACE_BASE + ((vaddr_t)KERNEL_ASPACE_SIZE - 1)));
}

// user address space, defaults to below kernel space with a 16MB guard gap on either side
#ifndef USER_ASPACE_BASE
#define USER_ASPACE_BASE ((vaddr_t)0x01000000UL)
#endif
#ifndef USER_ASPACE_SIZE
#define USER_ASPACE_SIZE ((vaddr_t)KERNEL_ASPACE_BASE - USER_ASPACE_BASE - 0x01000000UL)
#endif

static_assert(USER_ASPACE_BASE + (USER_ASPACE_SIZE - 1) > USER_ASPACE_BASE, "");

static inline bool is_user_address(vaddr_t va) {
    return (va >= USER_ASPACE_BASE && va <= (USER_ASPACE_BASE + (USER_ASPACE_SIZE - 1)));
}

static inline bool is_user_address_range(vaddr_t va, size_t len) {
    return va + len >= va &&
           is_user_address(va) &&
           (len == 0 || is_user_address(va + len - 1));
}

#ifndef GUEST_PHYSICAL_ASPACE_BASE
#define GUEST_PHYSICAL_ASPACE_BASE 0UL
#endif
#ifndef GUEST_PHYSICAL_ASPACE_SIZE
#define GUEST_PHYSICAL_ASPACE_SIZE (1UL << 48)
#endif

__BEGIN_CDECLS

// C friendly opaque handle to the internals of the VMM.
// Never defined, just used as a handle for C apis.
typedef struct vmm_aspace vmm_aspace_t;

// internal kernel routines below, do not call directly

// internal routine by the scheduler to swap mmu contexts
void vmm_context_switch(vmm_aspace_t* oldspace, vmm_aspace_t* newaspace);

// set the current user aspace as active on the current thread.
// NULL is a valid argument, which unmaps the current user address space
void vmm_set_active_aspace(vmm_aspace_t* aspace);

__END_CDECLS
