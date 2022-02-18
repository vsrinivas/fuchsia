// Copyright 2022 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "lib/lockup_detector/diagnostics.h"

#include <inttypes.h>
#include <lib/boot-options/boot-options.h>
#include <lib/version.h>

#include <ktl/bit.h>
#include <object/process_dispatcher.h>
#include <object/thread_dispatcher.h>

#if defined(__aarch64__)
#include <arch/arm64/dap.h>
#endif

#if defined(__x86_64__)
#include <lib/backtrace/global_cpu_context_exchange.h>
#endif

namespace lockup_internal {

#if defined(__aarch64__)
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
    constexpr const char* bt_fmt = "{{{bt:%u:%p}}}\n";
    uint32_t n = 0;

    // Don't attempt to do any backtracking unless this looks like the thread is
    // in the kernel right now.  The PC might be completely bogus, but even if
    // it is in a legit user mode process, I'm not sure of a good way to print
    // the symbolizer context for that process, or to figure out if the process
    // is using a shadow call stack or not.
    if (state.get_el_level() != 1u) {
      fprintf(output_target, "Skipping backtrace, CPU-%u EL is %u, not 1\n", cpu,
              state.get_el_level());
      return;
    }

    // Print the symbolizer context, and then the PC as frame 0's address, and
    // the LR as frame 1's address.
    PrintSymbolizerContext(output_target);
    fprintf(output_target, bt_fmt, n++, reinterpret_cast<void*>(state.pc));
    fprintf(output_target, bt_fmt, n++, reinterpret_cast<void*>(state.r[30]));

    constexpr size_t PtrSize = sizeof(void*);
    uintptr_t ret_addr_ptr = state.r[18];
    if (ret_addr_ptr & (PtrSize - 1)) {
      fprintf(output_target, "Halting backtrace, x18 (0x%" PRIu64 ") is not %zu byte aligned.\n",
              ret_addr_ptr, PtrSize);
      return;
    }

    constexpr uint32_t MAX_BACKTRACE = 32;
    for (; n < MAX_BACKTRACE; ++n) {
      // Attempt to back up one level.  Never cross a page boundary when we do this.
      static_assert(ktl::has_single_bit(static_cast<uint64_t>(PAGE_SIZE)),
                    "PAGE_SIZE is not a power of 2!  Wut??");
      if ((ret_addr_ptr & (PAGE_SIZE - 1)) == 0) {
        break;
      }

      ret_addr_ptr -= PtrSize;

      // Print out this level
      fprintf(output_target, bt_fmt, n, reinterpret_cast<void**>(ret_addr_ptr)[0]);
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
      printf("CPU-%u context follows\n", target_cpu);
      context.backtrace.PrintWithoutVersion(output_target);
      PrintFrame(output_target, context.frame);
      printf("end of CPU-%u context\n", target_cpu);
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
