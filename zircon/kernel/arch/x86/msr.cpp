// Copyright 2019 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <assert.h>
#include <debug.h>
#include <err.h>
#include <stdio.h>
#include <string.h>
#include <trace.h>
#include <zircon/compiler.h>

#include <arch/mp.h>
#include <arch/ops.h>
#include <arch/x86.h>
#include <arch/x86/apic.h>
#include <arch/x86/descriptor.h>
#include <arch/x86/feature.h>
#include <arch/x86/interrupts.h>
#include <arch/x86/mp.h>
#include <arch/x86/tsc.h>
#include <dev/hw_rng.h>
#include <dev/interrupt.h>
#include <kernel/event.h>
#include <kernel/timer.h>
#include <platform.h>
#include <zircon/types.h>


struct read_msr_context {
    uint32_t msr;
    uint64_t val;
};

static void read_msr_on_cpu_task(void* raw_context) {
    auto* const context = reinterpret_cast<struct read_msr_context*>(raw_context);
    context->val = read_msr(context->msr);
}

uint64_t read_msr_on_cpu(cpu_num_t cpu, uint32_t msr_id) {
    struct read_msr_context context = {};
    cpu_mask_t mask = {};

    if (!mp_is_cpu_online(cpu)) {
        return -1;
    }

    context.msr = msr_id;
    mask |= cpu_num_to_mask(cpu);
    mp_sync_exec(MP_IPI_TARGET_MASK, mask, read_msr_on_cpu_task,
                 reinterpret_cast<void*>(&context));
    return context.val;
}
