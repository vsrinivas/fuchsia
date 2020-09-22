// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

// TODO(fxbug.dev/30938): Need to be able to r/w MSRs.
// The thought is to use resources (as in ResourceDispatcher), at which point
// this will all get rewritten. Until such time, the goal here is KISS.
// This file contains the lower part of Intel Processor Trace support that must
// be done in the kernel (so that we can read/write msrs).
// The userspace driver is in system/dev/misc/cpu-trace/intel-pt.c.
//
// We currently only support Table of Physical Addresses mode:
// it supports discontiguous buffers and supports stop-on-full behavior
// in addition to wrap-around.
//
// IPT tracing has two "modes":
// - per-cpu tracing
// - thread-specific tracing
// Tracing can only be done in one mode at a time. This is because saving/
// restoring thread PT state via the xsaves/xrstors instructions is a global
// flag in the XSS msr.
// Plus once a trace has been done with IPT_MODE_THREAD one cannot go back
// to IPT_MODE_CPU: supporting this requires flushing trace state from all
// threads which is a bit of work. For now it's easy enough to just require
// the user to reboot. ZX-892
#include "arch/x86/proc_trace.h"

#include <err.h>
#include <lib/ktrace.h>
#include <lib/zircon-internal/device/cpu-trace/intel-pt.h>
#include <lib/zircon-internal/ktrace.h>
#include <lib/zircon-internal/mtrace.h>
#include <lib/zircon-internal/thread_annotations.h>
#include <pow2.h>
#include <string.h>
#include <trace.h>
#include <zircon/types.h>

#include <arch/arch_ops.h>
#include <arch/mmu.h>
#include <arch/x86.h>
#include <arch/x86/feature.h>
#include <arch/x86/mmu.h>
#include <fbl/auto_lock.h>
#include <fbl/macros.h>
#include <kernel/cpu.h>
#include <kernel/mp.h>
#include <kernel/mutex.h>
#include <kernel/thread.h>
#include <ktl/unique_ptr.h>
#include <vm/vm.h>
#include <vm/vm_aspace.h>

#define LOCAL_TRACE 0

// Control MSRs
#define IA32_RTIT_OUTPUT_BASE 0x560
#define IA32_RTIT_OUTPUT_MASK_PTRS 0x561
#define IA32_RTIT_CTL 0x570
#define IA32_RTIT_STATUS 0x571
#define IA32_RTIT_CR3_MATCH 0x572
#define IA32_RTIT_ADDR0_A 0x580
#define IA32_RTIT_ADDR0_B 0x581
#define IA32_RTIT_ADDR1_A 0x582
#define IA32_RTIT_ADDR1_B 0x583
#define IA32_RTIT_ADDR2_A 0x584
#define IA32_RTIT_ADDR2_B 0x585
#define IA32_RTIT_ADDR3_A 0x586
#define IA32_RTIT_ADDR3_B 0x587

// We need bits[15:8] to get the "maximum non-turbo ratio".
// See libipt:intel-pt.h:pt_config, and Intel Vol. 3 chapter 35.5.
#define IA32_PLATFORM_INFO 0xce

// Our own copy of what h/w supports, mostly for sanity checking.
static bool supports_pt = false;
static bool supports_cr3_filtering = false;
static bool supports_psb = false;
static bool supports_ip_filtering = false;
static bool supports_mtc = false;
static bool supports_ptwrite = false;
static bool supports_power_events = false;
static bool supports_output_topa = false;
static bool supports_output_topa_multi = false;
static bool supports_output_single = false;
static bool supports_output_transport = false;

struct ipt_trace_state_t {
  uint64_t ctl;
  uint64_t status;
  uint64_t output_base;
  uint64_t output_mask_ptrs;
  uint64_t cr3_match;
  struct {
    uint64_t a, b;
  } addr_ranges[IPT_MAX_NUM_ADDR_RANGES];
};

namespace {
DECLARE_SINGLETON_MUTEX(IptLock);
}  // namespace

static ipt_trace_state_t* ipt_trace_state TA_GUARDED(IptLock::Get());
static bool active TA_GUARDED(IptLock::Get()) = false;
static zx_insntrace_trace_mode_t trace_mode TA_GUARDED(IptLock::Get()) = IPT_MODE_CPU;

// In cpu mode this arch_max_num_cpus.
// In thread mode this is provided by the user.
static uint32_t ipt_num_traces TA_GUARDED(IptLock::Get());

void x86_processor_trace_init(void) {
  if (!x86_feature_test(X86_FEATURE_PT)) {
    return;
  }

  struct cpuid_leaf leaf;
  if (!x86_get_cpuid_subleaf(X86_CPUID_PT, 0, &leaf)) {
    return;
  }

  supports_pt = true;

  // Keep our own copy of these flags, mostly for potential sanity checks.
  supports_cr3_filtering = !!(leaf.b & (1 << 0));
  supports_psb = !!(leaf.b & (1 << 1));
  supports_ip_filtering = !!(leaf.b & (1 << 2));
  supports_mtc = !!(leaf.b & (1 << 3));
  supports_ptwrite = !!(leaf.b & (1 << 4));
  supports_power_events = !!(leaf.b & (1 << 5));

  supports_output_topa = !!(leaf.c & (1 << 0));
  supports_output_topa_multi = !!(leaf.c & (1 << 1));
  supports_output_single = !!(leaf.c & (1 << 2));
  supports_output_transport = !!(leaf.c & (1 << 3));
}

// Intel Processor Trace support needs to be able to map cr3 values that
// appear in the trace to pids that ld.so uses to dump memory maps.
void arch_trace_process_create(uint64_t pid, paddr_t pt_phys) {
  // The cr3 value that appears in Intel PT h/w tracing.
  uint64_t cr3 = pt_phys;
  ktrace(TAG_IPT_PROCESS_CREATE, (uint32_t)pid, (uint32_t)(pid >> 32), (uint32_t)cr3,
         (uint32_t)(cr3 >> 32));
}

// Worker for x86_ipt_alloc_trace to be executed on all cpus.
// This is invoked via mp_sync_exec which thread safety analysis cannot follow.
static void x86_ipt_set_mode_task(void* raw_context) TA_REQ(IptLock::Get()) {
  DEBUG_ASSERT(arch_ints_disabled());
  DEBUG_ASSERT(!active);

  // When changing modes make sure all PT MSRs are in the init state.
  // We don't want a value to appear in the xsave buffer and have xrstors
  // #gp because XCOMP_BV has the PT bit set that's not set in XSS.
  // We still need to do this, even with ZX-892, when transitioning
  // from IPT_MODE_CPU to IPT_MODE_THREAD.
  write_msr(IA32_RTIT_CTL, 0);
  write_msr(IA32_RTIT_STATUS, 0);
  write_msr(IA32_RTIT_OUTPUT_BASE, 0);
  write_msr(IA32_RTIT_OUTPUT_MASK_PTRS, 0);
  if (supports_cr3_filtering)
    write_msr(IA32_RTIT_CR3_MATCH, 0);
  // TODO(dje): addr range msrs

  zx_insntrace_trace_mode_t new_mode =
      static_cast<zx_insntrace_trace_mode_t>(reinterpret_cast<uintptr_t>(raw_context));

  // PT state saving, if supported, was enabled during boot so there's no
  // need to recalculate the xsave space needed.
  x86_set_extended_register_pt_state(new_mode == IPT_MODE_THREAD);
}

zx_status_t x86_ipt_alloc_trace(zx_insntrace_trace_mode_t mode, uint32_t num_traces) {
  Guard<Mutex> guard(IptLock::Get());

  DEBUG_ASSERT(mode == IPT_MODE_CPU || mode == IPT_MODE_THREAD);
  if (mode == IPT_MODE_CPU) {
    if (num_traces != arch_max_num_cpus())
      return ZX_ERR_INVALID_ARGS;
  } else {
    return ZX_ERR_NOT_SUPPORTED;
  }

  if (!supports_pt)
    return ZX_ERR_NOT_SUPPORTED;
  if (active)
    return ZX_ERR_BAD_STATE;
  if (ipt_trace_state)
    return ZX_ERR_BAD_STATE;

  // ZX-892: We don't support changing the mode from IPT_MODE_THREAD to
  // IPT_MODE_CPU: We can't turn off XSS.PT until we're sure all threads
  // have no PT state, and that's too tricky to do right now. Instead,
  // require the developer to reboot.
  if (trace_mode == IPT_MODE_THREAD && mode == IPT_MODE_CPU)
    return ZX_ERR_NOT_SUPPORTED;

  ipt_trace_state =
      reinterpret_cast<ipt_trace_state_t*>(calloc(num_traces, sizeof(*ipt_trace_state)));
  if (!ipt_trace_state)
    return ZX_ERR_NO_MEMORY;

  mp_sync_exec(MP_IPI_TARGET_ALL, 0, x86_ipt_set_mode_task,
               reinterpret_cast<void*>(static_cast<uintptr_t>(mode)));

  trace_mode = mode;
  ipt_num_traces = num_traces;
  return ZX_OK;
}

// Free resources obtained by x86_ipt_alloc_trace().
// This doesn't care if resources have already been freed to save callers
// from having to care during any cleanup.

zx_status_t x86_ipt_free_trace() {
  Guard<Mutex> guard(IptLock::Get());

  // Terminating tracing in thread mode is done differently: Tracing state
  // is recorded, in part, with traced threads.
  // This is the only situation where this fails.
  // TODO(fxbug.dev/30840): We could take a more heavy-handed approach here and
  // do the work necessary to clear out tracing on all threads. It's a bit
  // of work, but the resulting functionality would simplify the u/i.
  if (trace_mode == IPT_MODE_THREAD) {
    return ZX_ERR_BAD_STATE;
  }

  if (!supports_pt) {
    // If tracing is not supported we're already terminated.
    return ZX_OK;
  }
  if (active) {
    [[maybe_unused]] zx_status_t status = x86_ipt_stop();
    // This should succeed. The only time it can fail is in thread-mode,
    // but we've already checked for that.
    DEBUG_ASSERT(status == ZX_OK);
    DEBUG_ASSERT(!active);
  }

  free(ipt_trace_state);
  ipt_trace_state = nullptr;
  return ZX_OK;
}

static void x86_ipt_start_cpu_task(void* raw_context) TA_REQ(IptLock::Get()) {
  DEBUG_ASSERT(arch_ints_disabled());
  DEBUG_ASSERT(active && raw_context);

  ipt_trace_state_t* context = reinterpret_cast<ipt_trace_state_t*>(raw_context);
  cpu_num_t cpu = arch_curr_cpu_num();
  ipt_trace_state_t* state = &context[cpu];

  DEBUG_ASSERT(!(read_msr(IA32_RTIT_CTL) & IPT_CTL_TRACE_EN_MASK));

  // Load the ToPA configuration
  write_msr(IA32_RTIT_OUTPUT_BASE, state->output_base);
  write_msr(IA32_RTIT_OUTPUT_MASK_PTRS, state->output_mask_ptrs);

  // Load all other msrs, prior to enabling tracing.
  write_msr(IA32_RTIT_STATUS, state->status);
  if (supports_cr3_filtering)
    write_msr(IA32_RTIT_CR3_MATCH, state->cr3_match);

  // Enable the trace
  write_msr(IA32_RTIT_CTL, state->ctl);
}

// Begin the trace.

zx_status_t x86_ipt_start() {
  Guard<Mutex> guard(IptLock::Get());

  if (!supports_pt)
    return ZX_ERR_NOT_SUPPORTED;
  if (trace_mode == IPT_MODE_THREAD)
    return ZX_ERR_BAD_STATE;
  if (active)
    return ZX_ERR_BAD_STATE;
  if (!ipt_trace_state)
    return ZX_ERR_BAD_STATE;

  uint64_t kernel_cr3 = x86_kernel_cr3();
  TRACEF("Starting processor trace, kernel cr3: 0x%" PRIxPTR "\n", kernel_cr3);

  if (LOCAL_TRACE && trace_mode == IPT_MODE_CPU) {
    uint32_t num_cpus = ipt_num_traces;
    for (uint32_t cpu = 0; cpu < num_cpus; ++cpu) {
      TRACEF("Cpu %u: ctl 0x%" PRIx64 ", status 0x%" PRIx64 ", base 0x%" PRIx64 ", mask 0x%" PRIx64
             "\n",
             cpu, ipt_trace_state[cpu].ctl, ipt_trace_state[cpu].status,
             ipt_trace_state[cpu].output_base, ipt_trace_state[cpu].output_mask_ptrs);
    }
  }

  active = true;

  // Sideband info needed by the trace reader.
  uint64_t platform_msr = read_msr(IA32_PLATFORM_INFO);
  unsigned nom_freq = (platform_msr >> 8) & 0xff;
  ktrace(TAG_IPT_START, (uint32_t)nom_freq, 0, (uint32_t)kernel_cr3, (uint32_t)(kernel_cr3 >> 32));
  const struct x86_model_info* model_info = x86_get_model();
  ktrace(TAG_IPT_CPU_INFO, model_info->processor_type, model_info->display_family,
         model_info->display_model, model_info->stepping);

  if (trace_mode == IPT_MODE_CPU) {
    mp_sync_exec(MP_IPI_TARGET_ALL, 0, x86_ipt_start_cpu_task, ipt_trace_state);
  }

  return ZX_OK;
}

static void x86_ipt_stop_cpu_task(void* raw_context) TA_REQ(IptLock::Get()) {
  DEBUG_ASSERT(arch_ints_disabled());
  DEBUG_ASSERT(raw_context);

  ipt_trace_state_t* context = reinterpret_cast<ipt_trace_state_t*>(raw_context);
  cpu_num_t cpu = arch_curr_cpu_num();
  ipt_trace_state_t* state = &context[cpu];

  // Disable the trace
  write_msr(IA32_RTIT_CTL, 0);

  // Retrieve msr values for later providing to userspace
  state->ctl = 0;
  state->status = read_msr(IA32_RTIT_STATUS);
  state->output_base = read_msr(IA32_RTIT_OUTPUT_BASE);
  state->output_mask_ptrs = read_msr(IA32_RTIT_OUTPUT_MASK_PTRS);

  // Zero all MSRs so that we are in the XSAVE initial configuration.
  // This allows h/w to do some optimizations regarding the state.
  write_msr(IA32_RTIT_STATUS, 0);
  write_msr(IA32_RTIT_OUTPUT_BASE, 0);
  write_msr(IA32_RTIT_OUTPUT_MASK_PTRS, 0);
  if (supports_cr3_filtering)
    write_msr(IA32_RTIT_CR3_MATCH, 0);

  // TODO(dje): Make it explicit that packets have been completely written.
  // See Intel Vol 3 chapter 36.2.4.

  // TODO(teisenbe): Clear ADDR* MSRs depending on leaf 1
}

// This can be called while not active, so the caller doesn't have to care
// during any cleanup.

zx_status_t x86_ipt_stop() {
  Guard<Mutex> guard(IptLock::Get());

  // Stopping tracing in thread mode is done differently: Tracing state
  // is recorded, in part, with traced threads.
  // This is the only situation where this fails.
  // TODO(fxbug.dev/30840): We could take a more heavy-handed approach here and
  // do the work necessary to clear out tracing on all threads. It's a bit
  // of work, but the resulting functionality would simplify the u/i.
  if (trace_mode == IPT_MODE_THREAD) {
    return ZX_ERR_BAD_STATE;
  }

  if (!supports_pt) {
    // If tracing is not supported we're already stopped.
    return ZX_OK;
  }
  if (!ipt_trace_state) {
    // If tracing is not enabled we're already stopped.
    return ZX_OK;
  }

  TRACEF("Stopping processor trace\n");

  if (trace_mode == IPT_MODE_CPU) {
    mp_sync_exec(MP_IPI_TARGET_ALL, 0, x86_ipt_stop_cpu_task, ipt_trace_state);
  }

  ktrace(TAG_IPT_STOP, 0, 0, 0, 0);
  active = false;

  if (LOCAL_TRACE && trace_mode == IPT_MODE_CPU) {
    uint32_t num_cpus = ipt_num_traces;
    for (uint32_t cpu = 0; cpu < num_cpus; ++cpu) {
      TRACEF("Cpu %u: ctl 0x%" PRIx64 ", status 0x%" PRIx64 ", base 0x%" PRIx64 ", mask 0x%" PRIx64
             "\n",
             cpu, ipt_trace_state[cpu].ctl, ipt_trace_state[cpu].status,
             ipt_trace_state[cpu].output_base, ipt_trace_state[cpu].output_mask_ptrs);
    }
  }

  return ZX_OK;
}

zx_status_t x86_ipt_stage_trace_data(zx_insntrace_buffer_descriptor_t descriptor,
                                     const zx_x86_pt_regs_t* regs) {
  Guard<Mutex> guard(IptLock::Get());

  if (!supports_pt)
    return ZX_ERR_NOT_SUPPORTED;
  if (trace_mode == IPT_MODE_CPU && active)
    return ZX_ERR_BAD_STATE;
  if (!ipt_trace_state)
    return ZX_ERR_BAD_STATE;
  if (descriptor >= ipt_num_traces)
    return ZX_ERR_INVALID_ARGS;

  ipt_trace_state[descriptor].ctl = regs->ctl;
  ipt_trace_state[descriptor].status = regs->status;
  ipt_trace_state[descriptor].output_base = regs->output_base;
  ipt_trace_state[descriptor].output_mask_ptrs = regs->output_mask_ptrs;
  ipt_trace_state[descriptor].cr3_match = regs->cr3_match;
  static_assert(sizeof(ipt_trace_state[descriptor].addr_ranges) == sizeof(regs->addr_ranges),
                "addr_ranges size mismatch");
  memcpy(ipt_trace_state[descriptor].addr_ranges, regs->addr_ranges, sizeof(regs->addr_ranges));

  return ZX_OK;
}

zx_status_t x86_ipt_get_trace_data(zx_insntrace_buffer_descriptor_t descriptor,
                                   zx_x86_pt_regs_t* regs) {
  Guard<Mutex> guard(IptLock::Get());

  if (!supports_pt)
    return ZX_ERR_NOT_SUPPORTED;
  if (trace_mode == IPT_MODE_CPU && active)
    return ZX_ERR_BAD_STATE;
  if (!ipt_trace_state)
    return ZX_ERR_BAD_STATE;
  if (descriptor >= ipt_num_traces)
    return ZX_ERR_INVALID_ARGS;

  regs->ctl = ipt_trace_state[descriptor].ctl;
  regs->status = ipt_trace_state[descriptor].status;
  regs->output_base = ipt_trace_state[descriptor].output_base;
  regs->output_mask_ptrs = ipt_trace_state[descriptor].output_mask_ptrs;
  regs->cr3_match = ipt_trace_state[descriptor].cr3_match;
  static_assert(sizeof(regs->addr_ranges) == sizeof(ipt_trace_state[descriptor].addr_ranges),
                "addr_ranges size mismatch");
  memcpy(regs->addr_ranges, ipt_trace_state[descriptor].addr_ranges, sizeof(regs->addr_ranges));

  return ZX_OK;
}
