// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/counters.h>

#include <arch/ops.h>
#include <kernel/percpu.h>
#include <kernel/spinlock.h>
#include <lk/init.h>
#include <platform.h>
#include <stdlib.h>

// kernel.ld uses this and fills in the descriptor table size after it and then
// places the sorted descriptor table after that (and then pads to page size),
// so as to fully populate the counters::DescriptorVmo layout.
__USED __SECTION(".kcounter.desc.header") static const uint64_t vmo_header[] = {
    counters::DescriptorVmo::kMagic,
    SMP_MAX_CPUS,
};
static_assert(sizeof(vmo_header) ==
              offsetof(counters::DescriptorVmo, descriptor_table_size));

// This counter gets a constant value just as a sanity check.
KCOUNTER(magic, "counters.magic")

static void counters_init(unsigned level) {
    // Wire the memory defined in the .bss section to the counters.
    for (size_t ix = 0; ix != SMP_MAX_CPUS; ++ix) {
        percpu[ix].counters = CounterArena().CpuData(ix);
    }
    magic.Add(counters::DescriptorVmo::kMagic);
}

LK_INIT_HOOK(kcounters, counters_init, LK_INIT_LEVEL_PLATFORM_EARLY)
