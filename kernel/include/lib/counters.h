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
    uint32_t* counter;
    const char* name;
};

// We have to define the slot in assembly to put it into a named SHT_NOBITS
// section.  Otherwise the compiler will make it a SHT_PROGBITS section.
#define KCOUNTER(var, name)                                         \
    extern uint32_t var __asm__(".kcounter.offset." name);          \
    __asm__(".pushsection \".kcounter.offsets\",\"aw\",%nobits\n"   \
            ".kcounter.offset." name ": .skip 4\n"                  \
            ".popsection");                                         \
    __USED __SECTION("kcountdesc." name)                            \
    static const struct k_counter_desc kcc_desc_##var = {           \
        &var, name }

// The counter, as named |var| and defined is just an offset into
// per-cpu table, therefore to add an atomic is not required.
static inline void kcounter_add(uint32_t var, uint64_t add) {
    get_local_percpu()->counters[var] += (add);
}

__END_CDECLS
