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

// This counter tracks how long it takes for Zircon to reach the last init level
// It also can show if the target does not reset the internal clock upon reboot
// which is true also for mexec (netboot) scenario.
KCOUNTER(init_time, "init.target.time.msec")

static void counters_init(unsigned level) {
    init_time.Add(current_time() / 1000000LL);
}

LK_INIT_HOOK(kcounters, counters_init, LK_INIT_LEVEL_TARGET)
