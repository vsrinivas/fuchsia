// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <inttypes.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>

#include <fuchsia/crash/c/fidl.h>
#include <inspector/inspector.h>
#include <lib/async/cpp/wait.h>
#include <lib/crashanalyzer/crashanalyzer.h>
#include <lib/fdio/util.h>
#include <lib/fidl/cpp/message_buffer.h>
#include <pretty/hexdump.h>
#include <zircon/assert.h>
#include <zircon/crashlogger.h>
#include <zircon/process.h>
#include <zircon/processargs.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/exception.h>
#include <zircon/syscalls/port.h>
#include <zircon/threads.h>

static int verbosity_level = 0;

// If true then s/w breakpoint instructions do not kill the process.
// After the backtrace is printed the thread quietly resumes.
// TODO: The default is on for now for development purposes.
// Ultimately will want to switch this to off.
static bool swbreak_backtrace_enabled = true;

// Same as basename, except will not modify |path|.
// This assumes there are no trailing /s.

static const char* cl_basename(const char* path) {
    const char* base = strrchr(path, '/');
    return base ? base + 1 : path;
}

static void do_print_error(const char* file, int line, const char* fmt, ...) {
    const char* base = cl_basename(file);
    va_list args;
    va_start(args, fmt);
    fprintf(stderr, "crashanalyzer: %s:%d: ", base, line);
    vfprintf(stderr, fmt, args);
    fprintf(stderr, "\n");
    va_end(args);
}

static void do_print_zx_error(const char* file, int line, const char* what, zx_status_t status) {
    do_print_error(file, line, "%s: %d (%s)",
                   what, status, zx_status_get_string(status));
}

#define PRINT_ZX_ERROR(what, status)                                 \
    do {                                                             \
        do_print_zx_error(__FILE__, __LINE__,                        \
                          (what), static_cast<zx_status_t>(status)); \
    } while (0)

// Return true if the thread is to be resumed "successfully" (meaning the o/s
// won't kill it, and thus the kill process).

static bool is_resumable_swbreak(uint32_t excp_type) {
    if (excp_type == ZX_EXCP_SW_BREAKPOINT && swbreak_backtrace_enabled)
        return true;
    return false;
}

#if defined(__x86_64__)

static int have_swbreak_magic(const zx_thread_state_general_regs_t* regs) {
    return regs->rax == CRASHLOGGER_REQUEST_SELF_BT_MAGIC;
}

#elif defined(__aarch64__)

static int have_swbreak_magic(const zx_thread_state_general_regs_t* regs) {
    return regs->r[0] == CRASHLOGGER_REQUEST_SELF_BT_MAGIC;
}

#else

static int have_swbreak_magic(const zx_thread_state_general_regs_t* regs) {
    return 0;
}

#endif

static const char* excp_type_to_str(uint32_t type) {
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
// Space for this is allocated on the stack, so this can't be too large.
static constexpr size_t kMemoryDumpSize = 256;

// Handle of the thread we're dumping.
// This is used by both the main thread and the self-dumper thread.
// However there is no need to lock it as the self-dumper thread only runs
// when the main thread has crashed.
static zx_handle_t crashed_thread = ZX_HANDLE_INVALID;

// The exception that |crashed_thread| got.
static uint32_t crashed_thread_excp_type;

#if defined(__aarch64__)
static bool write_general_regs(zx_handle_t thread, void* buf, size_t buf_size) {
    // The syscall takes a uint32_t.
    auto to_xfer = static_cast<uint32_t>(buf_size);
    auto status = zx_thread_write_state(thread, ZX_THREAD_STATE_GENERAL_REGS, buf, to_xfer);
    if (status != ZX_OK) {
        PRINT_ZX_ERROR("unable to access general regs", status);
        return false;
    }
    return true;
}
#endif

static void dump_memory(zx_handle_t proc, uintptr_t start, size_t len) {
    // Make sure we're not allocating an excessive amount of stack.
    ZX_DEBUG_ASSERT(len <= kMemoryDumpSize);

    uint8_t buf[len];
    auto res = zx_process_read_memory(proc, start, buf, len, &len);
    if (res < 0) {
        printf("failed reading %p memory; error : %d\n", (void*)start, res);
    } else if (len != 0) {
        hexdump_ex(buf, len, start);
    }
}

static void resume_thread(zx_handle_t thread, bool handled) {
    uint32_t options = ZX_RESUME_EXCEPTION;
    if (!handled)
        options |= ZX_RESUME_TRY_NEXT;
    auto status = zx_task_resume(thread, options);
    if (status != ZX_OK) {
        PRINT_ZX_ERROR("unable to \"resume\" thread", status);
        // This shouldn't happen (unless someone killed it already).
        // The task is now effectively hung (until someone kills it).
        // TODO: Try to forcefully kill it ourselves?
    }
}

static void resume_thread_from_exception(zx_handle_t thread,
                                         uint32_t excp_type,
                                         const zx_thread_state_general_regs_t* gregs) {
    if (is_resumable_swbreak(excp_type) &&
        gregs != nullptr && have_swbreak_magic(gregs)) {
#if defined(__x86_64__)
// On x86, the pc is left at one past the s/w break insn,
// so there's nothing more we need to do.
#elif defined(__aarch64__)
        zx_thread_state_general_regs_t regs = *gregs;
        // Skip past the brk instruction.
        regs.pc += 4;
        if (!write_general_regs(thread, &regs, sizeof(regs)))
            goto Fail;
#else
        goto Fail;
#endif
        resume_thread(thread, true);
        return;
    }

    // For now, we turn policy exceptions into non-fatal warnings, by
    // resuming the thread when these exceptions occur.  TODO(ZX-922):
    // Remove this and make these exceptions fatal after the system has
    // received some amount of testing with ZX_POL_BAD_HANDLE enabled as a
    // warning.
    if (excp_type == ZX_EXCP_POLICY_ERROR) {
        resume_thread(thread, true);
        return;
    }

#if !defined(__x86_64__)
Fail:
#endif
    // Tell the o/s to "resume" the thread by killing the process, the
    // exception has not been handled.
    resume_thread(thread, false);
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

static void process_report(zx_handle_t process, zx_handle_t thread, bool use_libunwind) {
    zx_koid_t pid = get_koid(process);
    zx_koid_t tid = get_koid(thread);

    // Record the crashed thread so that if we crash then self_dump_func
    // can (try to) "resume" the thread so that it's not left hanging.
    crashed_thread = thread;

    zx_exception_report_t report;
    zx_status_t status = zx_object_get_info(thread, ZX_INFO_THREAD_EXCEPTION_REPORT,
                                            &report, sizeof(report), NULL, NULL);
    if (status != ZX_OK) {
        printf("failed to get exception report for [%" PRIu64 ".%" PRIu64 "] : error %d\n", pid, tid, status);
        zx_handle_close(process);
        zx_handle_close(thread);
        return;
    }

    uint32_t type = report.header.type;

    if (!ZX_EXCP_IS_ARCH(type) && type != ZX_EXCP_POLICY_ERROR)
        return;

    crashed_thread_excp_type = type;
    auto context = report.context;

    zx_thread_state_general_regs_t reg_buf;
    zx_thread_state_general_regs_t* regs = nullptr;
    zx_vaddr_t pc = 0, sp = 0, fp = 0;
    const char* arch = "unknown";
    const char* fatal = "fatal ";

    if (inspector_read_general_regs(thread, &reg_buf) != ZX_OK)
        goto Fail;
    // Delay setting this until here so Fail will know we now have the regs.
    regs = &reg_buf;

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
    // It's unlikely we'll get here as trying to read the regs will likely
    // fail, but we don't assume that.
    printf("unsupported architecture .. coming soon.\n");
    goto Fail;
#endif

    // This won't print "fatal" in the case where this is a s/w bkpt but
    // CRASHLOGGER_REQUEST_SELF_BT_MAGIC isn't set. Big deal.
    if (is_resumable_swbreak(type))
        fatal = "";
    // TODO(MA-922): Remove this and make policy exceptions fatal.
    if (type == ZX_EXCP_POLICY_ERROR)
        fatal = "";

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
    printf("<== %s, PC at 0x%" PRIxPTR "\n", excp_type_to_str(report.header.type), pc);

#if defined(__x86_64__)
    inspector_print_general_regs(stdout, regs, &context.arch.u.x86_64);
#elif defined(__aarch64__)
    inspector_print_general_regs(stdout, regs, &context.arch.u.arm_64);

    // Only output the Fault address register and ESR if there's a data fault.
    if (ZX_EXCP_FATAL_PAGE_FAULT == report.header.type) {
        printf(" far %#18" PRIx64 " esr %#18x\n",
               context.arch.u.arm_64.far, context.arch.u.arm_64.esr);
    }
#else
    __UNREACHABLE;
#endif

    printf("bottom of user stack:\n");
    dump_memory(process, sp, kMemoryDumpSize);

    printf("arch: %s\n", arch);

    {
        inspector_dsoinfo_t* dso_list = inspector_dso_fetch_list(process);
        inspector_dso_print_list(stdout, dso_list);
        inspector_print_backtrace(stdout, process, thread, dso_list,
                                  pc, sp, fp, use_libunwind);
    }

// TODO(ZX-588): Print a backtrace of all other threads in the process.

Fail:
    if (verbosity_level >= 1)
        printf("Done handling thread %" PRIu64 ".%" PRIu64 ".\n", pid, tid);

    // allow the thread (and then process) to die, unless the exception is
    // to just trigger a backtrace (if enabled)
    resume_thread_from_exception(thread, type, regs);
    crashed_thread = ZX_HANDLE_INVALID;
    crashed_thread_excp_type = 0u;

    zx_handle_close(thread);
    zx_handle_close(process);
}

static zx_status_t handle_message(zx_handle_t channel, fidl::MessageBuffer* buffer) {
    fidl::Message message = buffer->CreateEmptyMessage();
    zx_status_t status = message.Read(channel, 0);
    if (status != ZX_OK)
        return status;
    if (!message.has_header())
        return ZX_ERR_INVALID_ARGS;
    switch (message.ordinal()) {
    case fuchsia_crash_AnalyzerAnalyzeOrdinal: {
        const char* error_msg = nullptr;
        zx_status_t status = message.Decode(&fuchsia_crash_AnalyzerAnalyzeRequestTable, &error_msg);
        if (status != ZX_OK) {
            fprintf(stderr, "crashanalyzer: error: %s\n", error_msg);
            return status;
        }
        auto* request = message.GetBytesAs<fuchsia_crash_AnalyzerAnalyzeRequest>();

        // Whether to use libunwind or not.
        // If not then we use a simple algorithm that assumes ABI-specific
        // frame pointers are present.
        bool use_libunwind = true;

        fuchsia_crash_AnalyzerAnalyzeResponse response;
        memset(&response, 0, sizeof(response));
        response.hdr.txid = request->hdr.txid;
        response.hdr.ordinal = request->hdr.ordinal;
        status = zx_channel_write(channel, 0, &response, sizeof(response), nullptr, 0);

        process_report(request->process, request->thread, use_libunwind);

        return status;
    }
    default:
        fprintf(stderr, "crashanalyzer: error: Unknown message ordinal: %d\n", message.ordinal());
        return ZX_ERR_NOT_SUPPORTED;
    }
}

static void handle_ready(async_dispatcher_t* dispatcher,
                         async::Wait* wait,
                         zx_status_t status,
                         const zx_packet_signal_t* signal) {
    if (status != ZX_OK)
        goto done;

    if (signal->observed & ZX_CHANNEL_READABLE) {
        fidl::MessageBuffer buffer;
        for (uint64_t i = 0; i < signal->count; i++) {
            status = handle_message(wait->object(), &buffer);
            if (status == ZX_ERR_SHOULD_WAIT)
                break;
            if (status != ZX_OK)
                goto done;
        }
        status = wait->Begin(dispatcher);
        if (status != ZX_OK)
            goto done;
        return;
    }

    ZX_DEBUG_ASSERT(signal->observed & ZX_CHANNEL_PEER_CLOSED);
done:
    zx_handle_close(wait->object());
    delete wait;
}

static zx_status_t init(void** out_ctx) {
    inspector_set_verbosity(verbosity_level);

    // At debugging level 1 print our dso list (in case we crash in a way
    // that prevents printing it later).
    if (verbosity_level >= 1) {
        zx_handle_t self = zx_process_self();
        inspector_dsoinfo_t* dso_list = inspector_dso_fetch_list(self);
        printf("Crashlogger dso list:\n");
        inspector_dso_print_list(stdout, dso_list);
        inspector_dso_free_list(dso_list);
    }

    *out_ctx = nullptr;
    return ZX_OK;
}

static zx_status_t connect(void* ctx, async_dispatcher_t* dispatcher, const char* service_name,
                           zx_handle_t request) {
    if (!strcmp(service_name, fuchsia_crash_Analyzer_Name)) {
        auto wait = new async::Wait(request,
                                    ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED,
                                    handle_ready);
        zx_status_t status = wait->Begin(dispatcher);

        if (status != ZX_OK) {
            delete wait;
            zx_handle_close(request);
            return status;
        }

        return ZX_OK;
    }

    zx_handle_close(request);
    return ZX_ERR_NOT_SUPPORTED;
}

static constexpr const char* crashanalyzer_services[] = {
    fuchsia_crash_Analyzer_Name,
    nullptr,
};

static constexpr zx_service_ops_t crashanalyzer_ops = {
    .init = init,
    .connect = connect,
    .release = nullptr,
};

static constexpr zx_service_provider_t crashanalyzer_service_provider = {
    .version = SERVICE_PROVIDER_VERSION,
    .services = crashanalyzer_services,
    .ops = &crashanalyzer_ops,
};

const zx_service_provider_t* crashanalyzer_get_service_provider() {
    return &crashanalyzer_service_provider;
}
