// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>
#include <string>

#include <inspector/inspector.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/exception.h>

#include "src/lib/fxl/logging.h"
#include "src/lib/fxl/strings/string_printf.h"

#include "garnet/lib/debugger_utils/threads.h"
#include "garnet/lib/debugger_utils/util.h"

namespace debugger_utils {

namespace {

// How much memory to dump, in bytes.
constexpr size_t kMemoryDumpSizeBytes = 256;

} // namespace

void DumpThread(zx_handle_t process, zx_handle_t thread, bool in_exception) {
  // TODO(dje): There's a bit of inefficiency here in the dso list is
  // recomputed for each thread in the process (when we're dumping multiple
  // threads of the process). Inspector's use of dso lists is in flux so we
  // KISS and wait until the dust settles.
  zx_thread_state_general_regs_t regs;
  const char* arch = "unknown";
  zx_vaddr_t pc = 0, sp = 0, fp = 0;

  if (inspector_read_general_regs(thread, &regs) != ZX_OK) {
    // Error message has already been printed.
    return;
  }

#if defined(__x86_64__)
  arch = "x86_64";
  pc = regs.rip;
  sp = regs.rsp;
  fp = regs.rbp;
#elif defined(__aarch64__)
  arch = "aarch64";
  pc = regs.pc;
  sp = regs.sp;
  fp = regs.r[29];
#else
  // It's unlikely we'll get here as trying to read the regs will likely
  // fail, but we don't assume that.
  printf("unsupported architecture .. coming soon.\n");
  return;
#endif

  zx_koid_t pid = GetKoid(process);
  std::string process_name = GetObjectName(process);
  if (process_name.empty()) {
    process_name = "unknown";
  }

  zx_koid_t tid = GetKoid(thread);
  std::string thread_name = GetObjectName(thread);
  if (thread_name.empty()) {
    thread_name = "unknown";
  }

  printf("<== Process %s[%lu] Thread %s[%lu] ==>\n",
         process_name.c_str(), pid, thread_name.c_str(), tid);
  printf("arch: %s\n", arch);

  zx_exception_report_t report;
  const inspector_excp_data_t* excp_data = nullptr;
  if (in_exception) {
    zx_status_t status =
      zx_object_get_info(thread, ZX_INFO_THREAD_EXCEPTION_REPORT,
                         &report, sizeof(report), nullptr, nullptr);
    if (status != ZX_OK) {
        printf("failed to get exception report for [%lu.%lu]: error %d\n",
               pid, tid, status);
        return;
    }

    printf("<== %s, PC at 0x%lx\n",
           ExceptionNameAsString(report.header.type).c_str(), pc);

#if defined(__x86_64__)
    excp_data = &report.context.arch.u.x86_64;
#elif defined(__aarch64__)
    excp_data = &report.context.arch.u.arm_64;
#endif
  }

  inspector_print_general_regs(stdout, &regs, excp_data);

#if defined(__aarch64__)
  // Only output the Fault address register and ESR if there's a data fault.
  if (in_exception && report.header.type == ZX_EXCP_FATAL_PAGE_FAULT) {
    printf(" far %#18lx esr %#18x\n",
           report.context.arch.u.arm_64.far, report.context.arch.u.arm_64.esr);
  }
#endif

  printf("bottom of user stack:\n");
  inspector_print_memory(stdout, process, sp, kMemoryDumpSizeBytes);

  // TOOD(dje): Here's an example of the flux. We need the dso list in the
  // backtrace, but the list of dsos is printed via elf-search.
  inspector_dsoinfo_t* dso_list = inspector_dso_fetch_list(process);
  inspector_print_markup_context(stdout, process);

  bool use_libunwind = true;
  inspector_print_backtrace_markup(stdout, process, thread,
                                   dso_list, pc, sp, fp, use_libunwind);
}

} // namespace debugger_utils
