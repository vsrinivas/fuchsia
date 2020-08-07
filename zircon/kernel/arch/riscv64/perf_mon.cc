// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

// A note on terminology: "events" vs "counters": A "counter" is an
// "event", but some events are not counters. Internally, we use the
// term "counter" when we know the event is a counter.
//
// TODO(ZX-3304): combine common parts with x86 (after things settle)
// TODO(ZX-3305): chain event handling

#include <assert.h>
#include <lib/perfmon.h>
#include <lib/zircon-internal/mtrace.h>
#include <lib/zircon-internal/thread_annotations.h>
#include <platform.h>
#include <string.h>
#include <trace.h>
#include <zircon/errors.h>

#include <new>

#include <arch/arch_ops.h>
#include <arch/riscv64.h>
#include <arch/regs.h>
#include <fbl/algorithm.h>
#include <fbl/alloc_checker.h>
#include <fbl/auto_lock.h>
#include <fbl/ref_ptr.h>
#include <kernel/align.h>
#include <kernel/mp.h>
#include <kernel/mutex.h>
#include <kernel/stats.h>
#include <kernel/thread.h>
#include <ktl/move.h>
#include <ktl/unique_ptr.h>
#include <lk/init.h>
#include <vm/vm.h>
#include <vm/vm_address_region.h>
#include <vm/vm_aspace.h>
#include <vm/vm_object_physical.h>

namespace {

struct PerfmonState : public PerfmonStateBase {
  static zx_status_t Create(unsigned n_cpus, ktl::unique_ptr<PerfmonState>* out_state);
  explicit PerfmonState(unsigned n_cpus);
};

DECLARE_SINGLETON_MUTEX(PerfmonLock);

}  // namespace

zx_status_t PerfmonState::Create(unsigned n_cpus, ktl::unique_ptr<PerfmonState>* out_state) {
  return ZX_OK;
}

PerfmonState::PerfmonState(unsigned n_cpus) : PerfmonStateBase(n_cpus) {}

static void riscv64_perfmon_init_once(uint level) {
}

LK_INIT_HOOK(riscv64_perfmon, riscv64_perfmon_init_once, LK_INIT_LEVEL_ARCH)

zx_status_t arch_perfmon_get_properties(ArchPmuProperties* props) {
  return ZX_OK;
}

zx_status_t arch_perfmon_init() {
  return ZX_OK;
}

zx_status_t arch_perfmon_assign_buffer(uint32_t cpu, fbl::RefPtr<VmObject> vmo) {
  return ZX_OK;
}

// Stage the configuration for later activation by START.
// One of the main goals of this function is to verify the provided config
// is ok, e.g., it won't cause us to crash.
zx_status_t arch_perfmon_stage_config(ArchPmuConfig* config) {
  return ZX_OK;
}

// Begin collecting data.

zx_status_t arch_perfmon_start() {
  return ZX_OK;
}

void arch_perfmon_stop_locked() TA_REQ(PerfmonLock::Get()) {
}

// Stop collecting data.
void arch_perfmon_stop() {
}

// Finish data collection, reset h/w back to initial state and undo
// everything riscv64_perfmon_init did.
void arch_perfmon_fini() {
}

