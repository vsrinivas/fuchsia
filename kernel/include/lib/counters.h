// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <arch/ops.h>
#include <kernel/atomic.h>
#include <kernel/percpu.h>

#include <zircon/compiler.h>

__BEGIN_CDECLS

// Kernel counters are a facility designed to help field diagnostics and
// to help devs properly dimension the load/clients/size of the kernel
// constructs. It answers questions like:
//   - after N seconds how many outstanding <x> things are allocated?
//   - up to this point has <Y> ever happened?
//
// Currently the only query interface to the counters is the console
// k counters command. Issue 'k counters help' to learn what it can do.
//
// Kernel counters public API:
// 1- define a new counter.
//      KCOUNTER(counter_name, "<counter name>");
//
// 2- counters start at zero, increment the counter:
//      kcounter_add(counter_name, 1u);
//
//
// Naming the counters
// The naming convention is "kernel.subsystem.thing_or_action"
// for example "kernel.dispatcher.destroy"
//             "kernel.exceptions.fpu"
//             "kernel.handles.new"
//
//  Reading the counters in code
//  Don't. The counters are mantained in a per-cpu arena and
//  atomic operations are never used to set their value so
//  they are both imprecise and reflect only the operations
//  on a particular core.

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
#if defined(__aarch64__)
    // use a relaxed atomic load/store for arm64 to avoid a potentially nasty
    // race between the regular load/store operations on for a +1. Relaxed
    // atomic load/stores are about as efficient as a regular load/store.
    atomic_add_u64_relaxed(kcounter_slot(var), add);
#else
    // x86 can do the add in a single non atomic instruction, so the data loss
    // of a preemption in the middle of this sequence is fairly minimal.
    *kcounter_slot(var) += add;
#endif
}

__END_CDECLS
