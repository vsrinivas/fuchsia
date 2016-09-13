// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2014 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <kernel/mutex.h>
#include <kernel/vm.h>
#include <kernel/vm/vm_aspace.h>
#include <stdint.h>
#include <sys/types.h>
#include <mxtl/limits.h>

#define VM_GLOBAL_TRACE 0

/* simple boot time allocator */
extern "C" void* boot_alloc_mem(size_t len) __MALLOC;
extern uintptr_t boot_alloc_start;
extern uintptr_t boot_alloc_end;

void vmm_init_preheap(void);
void vmm_init(void);

// global vmm lock (for now)
extern mutex_t vmm_lock;

// utility function to test that offset + len is entirely within a range
// returns false if out of range
// NOTE: only use unsigned lengths
template <typename O, typename L>
static inline bool InRange(O offset, L len, O trim_to_len) {
    static_assert(mxtl::numeric_limits<O>::is_signed == false, "TrimRange requires unsigned type O");
    static_assert(mxtl::numeric_limits<L>::is_signed == false, "TrimRange requires unsigned type L");

    // trim offset/len to the range
    if (offset + len < offset)
        return false; // offset + len wrapped

    // we started off the end of the range
    if (offset > trim_to_len)
        return false;

    // does the end exceed the range?
    if (offset + len > trim_to_len)
        return false;

    return true;
}

// utility function to trim offset + len to trim_to_len, modifying offset and len
// returns false if out of range
// may return length 0 if it precisely trims
// NOTE: only use unsigned lengths
template <typename O, typename L>
static inline bool TrimRange(O& offset, L& len, O trim_to_len) {
    static_assert(mxtl::numeric_limits<O>::is_signed == false, "TrimRange requires unsigned type O");
    static_assert(mxtl::numeric_limits<L>::is_signed == false, "TrimRange requires unsigned type L");

    // trim offset/len to the range
    if (offset + len < offset)
        return false;  // offset + len wrapped

    // we started off the end of the range
    if (offset > trim_to_len)
        return false;

    // trim to the range
    if (offset + len > trim_to_len)
        len = static_cast<L>(trim_to_len - offset);

    return true;
}

