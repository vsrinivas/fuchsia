// Copyright 2022 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "lib/lockup_detector/diagnostics.h"

#include <inttypes.h>
#include <lib/boot-options/boot-options.h>

#include <ktl/bit.h>
#include <object/process_dispatcher.h>
#include <object/thread_dispatcher.h>

#if defined(__aarch64__)
#include <arch/arm64/dap.h>
#include <arch/arm64/mmu.h>
#endif

#if defined(__x86_64__)
#include <lib/backtrace/global_cpu_context_exchange.h>
#endif

namespace lockup_internal {

#if defined(__aarch64__)

zx_status_t GetBacktraceFromDapState(const arm64_dap_processor_state& state, Backtrace& out_bt) {
  // Don't attempt to do any backtracing unless this looks like the thread is in the kernel right
  // now.  The PC might be completely bogus, but even if it is in a legit user mode process, I'm not
  // sure of a good way to print the symbolizer context for that process, or to figure out if the
  // process is using a shadow call stack or not.
  if (state.get_el_level() != 1u) {
    return ZX_ERR_BAD_STATE;
  }

  // Build a backtrace using the PC as frame 0's address and the LR as frame 1's address.
  out_bt.reset();
  out_bt.push_back(state.pc);
  out_bt.push_back(state.r[30]);

  // Is the Shadow Call Stack Pointer (x18) properly aligned?
  constexpr size_t kPtrSize = sizeof(void*);
  uintptr_t scsp = state.r[18];
  if (scsp & (kPtrSize - 1)) {
    return ZX_ERR_INVALID_ARGS;
  }

  // SCSP has post-increment semantics so back up one slot so that it points to a stored value.
  if (scsp == 0) {
    return ZX_ERR_INVALID_ARGS;
  }
  scsp -= kPtrSize;

  // Is the address in the kernel's address space?
  if (!is_kernel_address(scsp)) {
    return ZX_ERR_OUT_OF_RANGE;
  }

  // And is it mapped?
  paddr_t pa_unused;
  zx_status_t status = arm64_mmu_translate(scsp, &pa_unused, /*user=*/false, /*write=*/false);
  if (status != ZX_OK) {
    return status;
  }

  // The SCSP looks legit.  Copy the return address values, but don't cross a page boundary.
  while (out_bt.size() < Backtrace::kMaxSize) {
    vaddr_t ret_addr = *reinterpret_cast<vaddr_t*>(scsp);
    out_bt.push_back(ret_addr);

    // Are we about to cross a page boundary?
    static_assert(ktl::has_single_bit(static_cast<uint64_t>(PAGE_SIZE)),
                  "PAGE_SIZE is not a power of 2!  Wut??");
    if ((scsp & (PAGE_SIZE - 1)) == 0) {
      break;
    }
    scsp -= kPtrSize;
  }

  return ZX_OK;
}

void DumpRegistersAndBacktrace(cpu_num_t cpu, FILE* output_target) {
  arm64_dap_processor_state state;
  // TODO(maniscalco): Update the DAP to make use of lockup_detector_diagnostic_query_timeout_ms.
  zx_status_t result = arm64_dap_read_processor_state(cpu, &state);

  if (result != ZX_OK) {
    fprintf(output_target, "Failed to read DAP state (res %d)\n", result);
    return;
  }

  fprintf(output_target, "DAP state:\n");
  state.Dump(output_target);
  fprintf(output_target, "\n");

  if constexpr (__has_feature(shadow_call_stack)) {
    Backtrace bt;
    zx_status_t status = GetBacktraceFromDapState(state, bt);
    switch (status) {
      case ZX_ERR_BAD_STATE:
        fprintf(output_target, "DAP backtrace: CPU-%u not in kernel mode.\n", cpu);
        break;
      case ZX_ERR_INVALID_ARGS:
        fprintf(output_target, "DAP backtrace: invalid SCSP.\n");
        break;
      case ZX_ERR_OUT_OF_RANGE:
        fprintf(output_target, "DAP backtrace: not a kernel address.\n");
        break;
      case ZX_ERR_NOT_FOUND:
        fprintf(output_target, "DAP backtrace: not mapped.\n");
        break;
      default:
        fprintf(output_target, "DAP backtrace: %d\n", status);
    }
    if (bt.size() > 0) {
      bt.PrintWithoutVersion(output_target);
    }
  }
}

#elif defined(__x86_64__)

void DumpRegistersAndBacktrace(cpu_num_t cpu, FILE* output_target) {
  DEBUG_ASSERT(arch_ints_disabled());

  zx_duration_t timeout = ZX_MSEC(gBootOptions->lockup_detector_diagnostic_query_timeout_ms);
  if (timeout == 0) {
    fprintf(output_target, "diagnostic query disabled (timeout is 0)\n");
    return;
  }

  // First, dump the context for the unresponsive CPU.  Then, dump the contexts of the other CPUs.
  cpu_num_t target_cpu = cpu;
  cpu_mask_t remaining_cpus = mp_get_active_mask() & ~cpu_num_to_mask(target_cpu);
  do {
    CpuContext context;
    zx_status_t status = g_cpu_context_exchange.RequestContext(target_cpu, timeout, context);
    if (status != ZX_OK) {
      fprintf(output_target, "failed to get context of CPU-%u: %d\n", target_cpu, status);
    } else {
      fprintf(output_target, "CPU-%u context follows\n", target_cpu);
      context.backtrace.PrintWithoutVersion(output_target);
      PrintFrame(output_target, context.frame);
      fprintf(output_target, "end of CPU-%u context\n", target_cpu);
    }
  } while ((target_cpu = remove_cpu_from_mask(remaining_cpus)) != INVALID_CPU);
}

#else
#error "Unknown architecture! Neither __aarch64__ nor __x86_64__ are defined"
#endif

void DumpCommonDiagnostics(cpu_num_t cpu, FILE* output_target, FailureSeverity severity) {
  DEBUG_ASSERT(arch_ints_disabled());

  auto& percpu = percpu::Get(cpu);
  fprintf(output_target, "timer_ints: %lu, interrupts: %lu\n", percpu.stats.timer_ints,
          percpu.stats.interrupts);

  if (ThreadLock::Get()->lock().HolderCpu() == cpu) {
    fprintf(output_target,
            "thread lock is held by cpu %u, skipping thread and scheduler diagnostics\n", cpu);
    return;
  }

  Guard<MonitoredSpinLock, IrqSave> thread_lock_guard{ThreadLock::Get(), SOURCE_TAG};
  percpu.scheduler.Dump(output_target);
  Thread* thread = percpu.scheduler.active_thread();
  if (thread != nullptr) {
    fprintf(output_target, "thread: pid=%lu tid=%lu\n", thread->pid(), thread->tid());
    ThreadDispatcher* user_thread = thread->user_thread();
    if (user_thread != nullptr) {
      ProcessDispatcher* process = user_thread->process();
      char name[ZX_MAX_NAME_LEN]{};
      process->get_name(name);
      fprintf(output_target, "process: name=%s\n", name);
    }
  }

  if (severity == FailureSeverity::Fatal) {
    fprintf(output_target, "\n");
    DumpRegistersAndBacktrace(cpu, output_target);
  }
}

}  // namespace lockup_internal
