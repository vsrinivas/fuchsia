// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <arch/ops.h>
#include <kernel/percpu.h>

#include <zircon/compiler.h>

__BEGIN_CDECLS

struct k_counter_desc {
    const char* name;
};
static_assert(sizeof(struct k_counter_desc) ==
              sizeof(((struct percpu){}).counters[0]),
              "the kernel.ld ASSERT knows that these sizes match");

// Define the descriptor and reserve the arena space for the counters.
// Because of -fdata-sections, each kcounter_arena_* array will be
// placed in a .bss.kcounter.* section; kernel.ld recognizes those names
// and places them all together to become the contiguous kcounters_arena
// array.  Note that each kcounter_arena_* does not correspond with the
// slots used for this particular counter (that would have terrible
// cache effects); it just reserves enough space for counters_init() to
// dole out in per-CPU chunks.
#define KCOUNTER(var, name)                                         \
    __USED uint64_t kcounter_arena_##var[SMP_MAX_CPUS]              \
        __asm__("kcounter." name);                                  \
    __USED __SECTION("kcountdesc." name)                            \
    static const struct k_counter_desc var[] = { { name } }

// Via magic in kernel.ld, all the descriptors wind up in a contiguous
// array bounded by these two symbols, sorted by name.
extern const struct k_counter_desc kcountdesc_begin[], kcountdesc_end[];

// The order of the descriptors is the order of the slots in each percpu array.
static inline size_t kcounter_index(const struct k_counter_desc* var) {
    return var - kcountdesc_begin;
}

// The counter, as named |var| and defined is just an offset into
// per-cpu table, therefore to add an atomic is not required.
static inline uint64_t* kcounter_slot(const struct k_counter_desc* var) {
    return &get_local_percpu()->counters[kcounter_index(var)];
}

static inline void kcounter_add(const struct k_counter_desc* var,
                                uint64_t add) {
    *kcounter_slot(var) += add;
}

__END_CDECLS
