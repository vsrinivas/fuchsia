// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "inspector/inspector.h"

#include <inttypes.h>
#include <string.h>

#include <lib/backtrace-request/backtrace-request-utils.h>
#include <zircon/assert.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/exception.h>
#include <zircon/types.h>

#include "utils-impl.h"

namespace inspector {

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
        return "unknown fault";
    }
}

// How much memory to dump, in bytes.
static constexpr size_t kMemoryDumpSize = 256;

static void resume_thread(zx_handle_t thread, zx_handle_t exception_port, bool handled) {
    uint32_t options = 0;
    if (!handled)
        options |= ZX_RESUME_TRY_NEXT;
    auto status = zx_task_resume_from_exception(thread, exception_port, options);
    if (status != ZX_OK) {
        print_zx_error("unable to \"resume\" thread", status);
        // This shouldn't happen (unless someone killed it already).
        // The task is now effectively hung (until someone kills it).
        // TODO: Try to forcefully kill it ourselves?
    }
}

static void resume_thread_from_exception(zx_handle_t thread, zx_handle_t exception_port,
                                         zx_excp_type_t excp_type,
                                         const zx_thread_state_general_regs_t* gregs) {
    if (is_backtrace_request(excp_type, gregs)) {
        zx_thread_state_general_regs_t regs = *gregs;
        if (cleanup_backtrace_request(thread, &regs) == ZX_OK) {
            resume_thread(thread, exception_port, true);
            return;
        }
    }

    // Tell the o/s to "resume" the thread by killing the process, the
    // exception has not been handled.
    resume_thread(thread, exception_port, false);
}

static zx_koid_t get_koid(zx_handle_t handle) {
    zx_info_handle_basic_t info;
    zx_status_t status = zx_object_get_info(handle, ZX_INFO_HANDLE_BASIC, &info, sizeof(info), NULL, NULL);
    if (status != ZX_OK) {
        printf("failed to get koid\n");
        return ZX_HANDLE_INVALID;
    }
    return info.koid;
}

static void print_debug_info(zx_handle_t process, zx_handle_t thread, zx_excp_type_t* type, zx_thread_state_general_regs_t* regs) {
    zx_koid_t pid = get_koid(process);
    zx_koid_t tid = get_koid(thread);

    zx_exception_report_t report;
    zx_status_t status = zx_object_get_info(thread, ZX_INFO_THREAD_EXCEPTION_REPORT,
                                            &report, sizeof(report), NULL, NULL);
    if (status != ZX_OK) {
        printf("failed to get exception report for [%" PRIu64 ".%" PRIu64 "] : error %d\n", pid, tid, status);
        return;
    }

    *type = report.header.type;

    if (!ZX_EXCP_IS_ARCH(*type) && *type != ZX_EXCP_POLICY_ERROR) {
        return;
    }

    auto context = report.context;

    zx_vaddr_t pc = 0, sp = 0, fp = 0;
    const char* arch = "unknown";

    if (inspector_read_general_regs(thread, regs) != ZX_OK) {
        return;
    }

#if defined(__x86_64__)
    arch = "x86_64";
    pc = regs->rip;
    sp = regs->rsp;
    fp = regs->rbp;
#elif defined(__aarch64__)
    arch = "aarch64";
    pc = regs->pc;
    sp = regs->sp;
    fp = regs->r[29];
#else
#error unsupported architecture
#endif

    const char* fatal = "fatal ";
    // We don't want to print "fatal" when we are printing the debug info from a
    // backtrace request as we will resume the thread at the end.
    if (is_backtrace_request(*type, regs)) {
        fatal = "";
    }

    char process_name[ZX_MAX_NAME_LEN];
    status = zx_object_get_property(process, ZX_PROP_NAME, process_name, sizeof(process_name));
    if (status != ZX_OK) {
        strlcpy(process_name, "unknown", sizeof(process_name));
    }

    char thread_name[ZX_MAX_NAME_LEN];
    status = zx_object_get_property(thread, ZX_PROP_NAME, thread_name, sizeof(thread_name));
    if (status != ZX_OK) {
        strlcpy(thread_name, "unknown", sizeof(thread_name));
    }

    printf("<== %sexception: process %s[%" PRIu64 "] thread %s[%" PRIu64 "]\n", fatal,
           process_name, pid, thread_name, tid);

    if (report.header.type == ZX_EXCP_FATAL_PAGE_FAULT) {
        const char* access_type;
        const char* violation;
#if defined(__x86_64__)
        static constexpr uint32_t kErrCodeInstrFetch = (1 << 4);
        static constexpr uint32_t kErrCodeWrite = (1 << 1);
        static constexpr uint32_t kErrCodeProtectionViolation = (1 << 0);
        if (context.arch.u.x86_64.err_code & kErrCodeInstrFetch) {
            access_type = "execute";
        } else if (context.arch.u.x86_64.err_code & kErrCodeWrite) {
            access_type = "write";
        } else {
            access_type = "read";
        }

        if (context.arch.u.x86_64.err_code & kErrCodeProtectionViolation) {
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
        static constexpr uint32_t kDccPermissionFault = 0b1100;

        if (context.arch.u.arm_64.esr & kEcDataAbortBit) {
            if (context.arch.u.arm_64.esr & kIssWrite
                    && !(context.arch.u.arm_64.esr & kIssCacheOp)) {
                access_type = "write";
            } else {
                access_type = "read";
            }
        } else {
            access_type = "execute";
        }

        if ((context.arch.u.arm_64.esr & kDccNoLvlMask) == kDccPermissionFault) {
            violation = "protection";
        } else {
            violation = "not-present";
        }
#else
#error unsupported architecture
#endif
        printf("<== %s %s page fault, PC at 0x%" PRIxPTR "\n", access_type, violation , pc);
    } else {
        printf("<== %s, PC at 0x%" PRIxPTR "\n", excp_type_to_str(report.header.type), pc);
    }

#if defined(__x86_64__)
    inspector_print_general_regs(stdout, regs, &context.arch.u.x86_64);
#elif defined(__aarch64__)
    inspector_print_general_regs(stdout, regs, &context.arch.u.arm_64);

    // Only output the Fault address register and ESR if there's a data or
    // alignment fault.
    if (ZX_EXCP_FATAL_PAGE_FAULT == report.header.type ||
        ZX_EXCP_UNALIGNED_ACCESS == report.header.type) {
        printf(" far %#18" PRIx64 " esr %#18x\n",
               context.arch.u.arm_64.far, context.arch.u.arm_64.esr);
    }
#else
#error unsupported architecture
#endif

    printf("bottom of user stack:\n");
    inspector_print_memory(stdout, process, sp, kMemoryDumpSize);

    printf("arch: %s\n", arch);

    {
        // Whether to use libunwind or not.
        // If not then we use a simple algorithm that assumes ABI-specific
        // frame pointers are present.
        const bool use_libunwind = true;

        // TODO (jakehehrlich): Remove old dso format.
        inspector_dsoinfo_t* dso_list = inspector_dso_fetch_list(process);
        inspector_dso_print_list(stdout, dso_list);
        inspector_print_markup_context(stdout, process);
        // TODO (jakehehrlich): Remove the old backtrace format.
        inspector_print_backtrace(stdout, process, thread, dso_list,
                                  pc, sp, fp, use_libunwind);
        inspector_print_backtrace_markup(stdout, process, thread, dso_list,
                                         pc, sp, fp, use_libunwind);
    }

    // TODO(ZX-588): Print a backtrace of all other threads in the process.

    if (verbosity_level >= 1)
        printf("Done handling thread %" PRIu64 ".%" PRIu64 ".\n", pid, tid);
}

} // namespace inspector

 __EXPORT void inspector_print_debug_info(zx_handle_t process,
                                          zx_handle_t thread) {
    zx_excp_type_t type = 0;
    zx_thread_state_general_regs_t regs;
    inspector::print_debug_info(process, thread, &type, &regs);
}

 __EXPORT void inspector_print_debug_info_and_resume_thread(
     zx_handle_t process, zx_handle_t thread, zx_handle_t exception_port) {
    zx_excp_type_t type = 0;
    zx_thread_state_general_regs_t regs;
    inspector::print_debug_info(process, thread, &type, &regs);

    // allow the thread (and then process) to die, unless the exception is
    // to just trigger a backtrace (if enabled).
    inspector::resume_thread_from_exception(thread, exception_port, type, &regs);
}
