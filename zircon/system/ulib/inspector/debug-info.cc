// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <inttypes.h>
#include <lib/backtrace-request/backtrace-request-utils.h>
#include <lib/zx/process.h>
#include <lib/zx/thread.h>
#include <string.h>
#include <zircon/assert.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/exception.h>
#include <zircon/types.h>

#include <string>
#include <vector>

#include "inspector/inspector.h"
#include "utils-impl.h"

namespace inspector {

#if defined(__x86_64__)
static const char* kArch = "x86_64";
#elif defined(__aarch64__)
static const char* kArch = "aarch64";
#else
#error unsupported architecture
#endif

static const char* excp_type_to_str(const zx_excp_type_t type) {
  switch (type) {
    case ZX_EXCP_GENERAL:
      return "general fault";
    case ZX_EXCP_FATAL_PAGE_FAULT:
      return "fatal page fault";
    case ZX_EXCP_UNDEFINED_INSTRUCTION:
      return "undefined instruction";
    case ZX_EXCP_SW_BREAKPOINT:
      return "sw breakpoint";
    case ZX_EXCP_HW_BREAKPOINT:
      return "hw breakpoint";
    case ZX_EXCP_UNALIGNED_ACCESS:
      return "alignment fault";
    case ZX_EXCP_POLICY_ERROR:
      return "policy error";
    default:
      // Note: To get a compilation failure when a new exception type has
      // been added without having also updated this function, compile with
      // -Wswitch-enum.
      return "<unknown fault>";
  }
}

// Globs the general registers and the interpretation of them as IP, SP, FP, etc.
typedef struct decoded_registers_t {
  zx_vaddr_t pc = 0;
  zx_vaddr_t sp = 0;
  zx_vaddr_t fp = 0;
} decoded_registers;

decoded_registers_t decode_registers(const zx_thread_state_general_regs* regs) {
  decoded_registers decoded;
#if defined(__x86_64__)
  decoded.pc = regs->rip;
  decoded.sp = regs->rsp;
  decoded.fp = regs->rbp;
#elif defined(__aarch64__)
  decoded.pc = regs->pc;
  decoded.sp = regs->sp;
  decoded.fp = regs->r[29];
#else
#error unsupported architecture
#endif

  return decoded;
}

// How much memory to dump, in bytes.
static constexpr size_t kMemoryDumpSize = 256;

static zx_koid_t get_koid(zx_handle_t handle) {
  zx_info_handle_basic_t info;
  zx_status_t status =
      zx_object_get_info(handle, ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr);
  if (status != ZX_OK) {
    printf("failed to get koid\n");
    return ZX_HANDLE_INVALID;
  }
  return info.koid;
}

static void get_name(zx_handle_t handle, char* buf, size_t size) {
  zx_status_t status = zx_object_get_property(handle, ZX_PROP_NAME, buf, size);
  if (status != ZX_OK) {
    strlcpy(buf, "<unknown>", size);
  }
}

static void print_exception_report(FILE* out, const zx_exception_report_t& report,
                                   const zx_thread_state_general_regs* regs) {
  inspector::decoded_registers decoded = inspector::decode_registers(regs);

  if (report.header.type == ZX_EXCP_FATAL_PAGE_FAULT) {
    const char* access_type;
    const char* violation;
#if defined(__x86_64__)
    static constexpr uint32_t kErrCodeInstrFetch = (1 << 4);
    static constexpr uint32_t kErrCodeWrite = (1 << 1);
    static constexpr uint32_t kErrCodeProtectionViolation = (1 << 0);
    if (report.context.arch.u.x86_64.err_code & kErrCodeInstrFetch) {
      access_type = "execute";
    } else if (report.context.arch.u.x86_64.err_code & kErrCodeWrite) {
      access_type = "write";
    } else {
      access_type = "read";
    }

    if (report.context.arch.u.x86_64.err_code & kErrCodeProtectionViolation) {
      violation = "protection";
    } else {
      violation = "not-present";
    }
#elif defined(__aarch64__)
    // The one ec bit that's different between a data and instruction abort
    static constexpr uint32_t kEcDataAbortBit = (1 << 28);
    static constexpr uint32_t kIssCacheOp = (1 << 8);
    static constexpr uint32_t kIssWrite = (1 << 6);
    static constexpr uint32_t kDccNoLvlMask = 0b111100;
    static constexpr uint32_t kDccPermissionFault = 0b001100;
    static constexpr uint32_t kDccTranslationFault = 0b000100;
    static constexpr uint32_t kDccAddressSizeFault = 0b000000;
    static constexpr uint32_t kDccAccessFlagFault = 0b001000;
    static constexpr uint32_t kDccSynchronousExternalFault = 0b010000;

    if (report.context.arch.u.arm_64.esr & kEcDataAbortBit) {
      if (report.context.arch.u.arm_64.esr & kIssWrite &&
          !(report.context.arch.u.arm_64.esr & kIssCacheOp)) {
        access_type = "write";
      } else {
        access_type = "read";
      }
    } else {
      access_type = "execute";
    }

    switch ((report.context.arch.u.arm_64.esr & kDccNoLvlMask)) {
      case kDccPermissionFault:
        violation = "protection";
        break;
      case kDccTranslationFault:
        violation = "not-present";
        break;
      case kDccAddressSizeFault:
        violation = "address-size";
        break;
      case kDccAccessFlagFault:
        violation = "access-flag";
        break;
      case kDccSynchronousExternalFault:
        violation = "external-abort";
        break;
      default:
        violation = "undecoded";
        break;
    }
#else
#error unsupported architecture
#endif
    fprintf(out, "<== %s %s page fault, PC at 0x%" PRIxPTR "\n", access_type, violation,
            decoded.pc);
  } else {
    fprintf(out, "<== %s, PC at 0x%" PRIxPTR "\n", inspector::excp_type_to_str(report.header.type),
            decoded.pc);
  }
}

}  // namespace inspector

__EXPORT void inspector_print_stack_trace(FILE* out, zx_handle_t process, zx_handle_t thread,
                                          const zx_thread_state_general_regs* regs) {
  // Whether to use libunwind or not.
  // If not then we use a simple algorithm that assumes ABI-specific
  // frame pointers are present.
  const bool use_libunwind = true;

  // TODO (jakehehrlich): Remove old dso format.
  inspector_dsoinfo_t* dso_list = inspector_dso_fetch_list(process);
  inspector_dso_print_list(out, dso_list);
  inspector_print_markup_context(out, process);

  inspector::decoded_registers decoded = inspector::decode_registers(regs);
  inspector_print_backtrace_markup(out, process, thread, dso_list, decoded.pc, decoded.sp,
                                   decoded.fp, use_libunwind);
  inspector_dso_free_list(dso_list);
}

__EXPORT void inspector_print_debug_info(FILE* out, zx_handle_t process_handle,
                                         zx_handle_t thread_handle) {
  zx_status_t status;
  // If the caller didn't supply |regs| use a local copy.
  zx_thread_state_general_regs_t regs;

  zx::unowned<zx::process> process(process_handle);
  zx_koid_t pid = inspector::get_koid(process->get());
  char process_name[ZX_MAX_NAME_LEN];
  inspector::get_name(process->get(), process_name, sizeof(process_name));

  zx::unowned<zx::thread> thread(thread_handle);
  zx_koid_t tid = inspector::get_koid(thread->get());
  char thread_name[ZX_MAX_NAME_LEN];
  inspector::get_name(thread->get(), thread_name, sizeof(thread_name));

  // Attempt to obtain the registers. If this fails, it means that the thread wasn't provided in a
  // valid state.
  status = inspector_read_general_regs(thread->get(), &regs);
  if (status != ZX_OK) {
    printf("[Process %s, Thread %s] Could not get general registers: %s.\n", process_name,
           thread_name, zx_status_get_string(status));
    return;
  }
  inspector::decoded_registers decoded = inspector::decode_registers(&regs);

  // Backtrace requests are special software breakpoints that get resumed. They need to be clearly
  // differentiable from other exceptions.
  bool backtrace_requested = false;

  // Check if the process is on an exception.
  zx_exception_report_t report;
  if (thread->get_info(ZX_INFO_THREAD_EXCEPTION_REPORT, &report, sizeof(report), nullptr,
                       nullptr) == ZX_OK) {
    // The thread is in a valid exception state.
    if (!ZX_EXCP_IS_ARCH(report.header.type) && report.header.type != ZX_EXCP_POLICY_ERROR) {
      return;
    }

    backtrace_requested = is_backtrace_request(report.header.type, &regs);
    if (backtrace_requested) {
      fprintf(out, "<== BACKTRACE REQUEST: process %s[%" PRIu64 "] thread %s[%" PRIu64 "]\n",
              process_name, pid, thread_name, tid);
    } else {
      // Normal exception.
      fprintf(out, "<== CRASH: process %s[%" PRIu64 "] thread %s[%" PRIu64 "]\n", process_name, pid,
              thread_name, tid);
      inspector::print_exception_report(out, report, &regs);

#if defined(__x86_64__)
      inspector_print_general_regs(out, &regs, &report.context.arch.u.x86_64);
#elif defined(__aarch64__)
      inspector_print_general_regs(out, &regs, &report.context.arch.u.arm_64);

      // Only output the Fault address register and ESR if there's a data or
      // alignment fault.
      if (ZX_EXCP_FATAL_PAGE_FAULT == report.header.type ||
          ZX_EXCP_UNALIGNED_ACCESS == report.header.type) {
        fprintf(out, " far %#18" PRIx64 " esr %#18x\n", report.context.arch.u.arm_64.far,
                report.context.arch.u.arm_64.esr);
      }
#else
#error unsupported architecture
#endif
    }
  } else {
    // The thread is suspended so we can safely print the stack trace.
    fprintf(out, "<== process %s[%" PRIu64 "] thread %s[%" PRIu64 "]\n", process_name, pid,
            thread_name, tid);
    fprintf(out, "<== PC at 0x%" PRIxPTR "\n", decoded.pc);
    inspector_print_general_regs(out, &regs, nullptr);
  }

  if (!backtrace_requested) {
    // Print the common stack part of the thread.
    fprintf(out, "bottom of user stack:\n");
    inspector_print_memory(out, process->get(), decoded.sp, inspector::kMemoryDumpSize);

    fprintf(out, "arch: %s\n", inspector::kArch);
  }

  inspector_print_stack_trace(out, process->get(), thread->get(), &regs);

  if (inspector::verbosity_level >= 1)
    printf("Done handling thread %" PRIu64 ".%" PRIu64 ".\n", pid, tid);
}

// The approach of |inspector_print_debug_info_for_all_threads| is to suspend the process, obtain
// all threads, go over the ones in an exception first and print them and only then print all the
// other threads. This permits to have a clearer view between logs and the crash report.
__EXPORT void inspector_print_debug_info_for_all_threads(FILE* out, zx_handle_t process_handle) {
  zx_status_t status = ZX_ERR_NOT_SUPPORTED;

  zx::unowned<zx::process> process(process_handle);
  char process_name[ZX_MAX_NAME_LEN];
  inspector::get_name(process->get(), process_name, sizeof(process_name));
  zx_koid_t process_koid = inspector::get_koid(process->get());

  // Suspend the process so that each thread is suspended and no more threads get spawned.
  // NOTE: A process cannot suspend itself, so this could fail on some environments (like calling
  //       this function on your process). To support that usecase, this logic will also try to
  //       suspend each thread individually.
  //
  //       The advantages of suspending the process vs each thread individually are:
  //       1. Threads get suspended at a single point in time, which gives a more accurate
  //          representation of what the process is doing at the moment of printing.
  //       2. When a process is suspended, no more threads will be spawned.
  zx::suspend_token process_suspend_token;
  status = process->suspend(&process_suspend_token);
  if (status != ZX_OK) {
    printf("[Process %s (%" PRIu64 ")] Could not suspend process: %s. Continuing anyway.\n ",
           process_name, process_koid, zx_status_get_string(status));
  }

  // Get the thread list.
  // NOTE: This could be skipping threads being created at the moment of this call.
  //       This is an inherent race between suspending a process and a thread being created.
  size_t actual, avail;
  // This is an outrageous amount of threads to output. We mark them all as invalid first.
  constexpr size_t kMaxThreadHandles = 128;
  std::vector<zx_koid_t> thread_koids(kMaxThreadHandles);
  status = process->get_info(ZX_INFO_PROCESS_THREADS, thread_koids.data(),
                             thread_koids.size() * sizeof(zx_koid_t), &actual, &avail);
  if (status != ZX_OK) {
    printf("[Process %s (%" PRIu64 ")] Could not get list of threads: %s.\n", process_name,
           process_koid, zx_status_get_string(status));
    return;
  }

  std::vector<zx::thread> thread_handles(actual);
  std::vector<std::string> thread_names(actual);
  std::vector<zx_info_thread_t> thread_infos(actual);

  // Get the thread associated data.
  for (size_t i = 0; i < actual; i++) {
    // Get the handles.
    zx::thread& child = thread_handles[i];
    status = process->get_child(thread_koids[i], ZX_RIGHT_SAME_RIGHTS, &child);
    if (status != ZX_OK) {
      printf("[Process %s (%" PRIu64 ")] Could not obtain thread handle: %s.\n", process_name,
             process_koid, zx_status_get_string(status));
      continue;
    }

    // Get the name.
    char thread_name[ZX_MAX_NAME_LEN];
    inspector::get_name(child.get(), thread_name, sizeof(thread_name));
    thread_names[i] = thread_name;

    // Get the thread infos.
    zx_info_thread_t thread_info = {};
    status = child.get_info(ZX_INFO_THREAD, &thread_info, sizeof(thread_info), nullptr, nullptr);
    if (status != ZX_OK) {
      printf("[Process %s (%" PRIu64 "), Thread %s (%" PRIu64 ")] Could not obtain info: %s\n",
             process_name, process_koid, thread_names[i].c_str(), thread_koids[i],
             zx_status_get_string(status));
      continue;
    }

    thread_infos[i] = std::move(thread_info);
  }

  // Print the threads in an exception first.
  for (size_t i = 0; i < actual; i++) {
    zx::thread& child = thread_handles[i];
    if (!child.is_valid())
      continue;

    // If the thread is not in an exception, it will be printed on the next loop.
    if (thread_infos[i].state != ZX_THREAD_STATE_BLOCKED_EXCEPTION)
      continue;

    // We print the thread and then mark this koid as empty, so that it won't be printed on the
    // suspended pass. This means we can free the handle after this.
    inspector_print_debug_info(out, process->get(), child.get());
    thread_handles[i].reset();
  }

  // Go over each thread and print them.
  for (size_t i = 0; i < actual; i++) {
    if (!thread_handles[i].is_valid())
      continue;

    // If the thread is in an exception, it was already printed by the previous loop.
    if (thread_infos[i].state == ZX_THREAD_STATE_BLOCKED_EXCEPTION) {
      continue;
    }

    zx::thread& child = thread_handles[i];

    // Wait for the thread to be suspended.
    // We do this regardless of the process suspension. There are legitimate cases where the process
    // suspension would fail, like trying to suspend one's own process. If the process suspension
    // was successful, this is a no-op.
    zx::suspend_token suspend_token;
    status = child.suspend(&suspend_token);
    if (status != ZX_OK) {
      printf("[Process %s (%" PRIu64 "), Thread %s (%" PRIu64 ")] Could not suspend thread: %s.\n",
             process_name, process_koid, thread_names[i].c_str(), thread_koids[i],
             zx_status_get_string(status));
      continue;
    }

    status = child.wait_one(ZX_THREAD_SUSPENDED, zx::deadline_after(zx::msec(100)), nullptr);
    if (status != ZX_OK) {
      printf("[Process %s (%" PRIu64 "), Thread %s (%" PRIu64 ")] Didn't get suspend signal: %s.\n",
             process_name, process_koid, thread_names[i].c_str(), thread_koids[i],
             zx_status_get_string(status));
      continue;
    }

    // We can now print the thread.
    inspector_print_debug_info(out, process->get(), child.get());
  }
}
