// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>

#include <magenta/assert.h>
#include <magenta/crashlogger.h>
#include <magenta/process.h>
#include <magenta/processargs.h>
#include <magenta/syscalls.h>
#include <magenta/syscalls/debug.h>
#include <magenta/syscalls/exception.h>
#include <magenta/syscalls/port.h>
#include <magenta/threads.h>
#include <mxio/util.h>
#include <pretty/hexdump.h>

#include "backtrace.h"
#include "dso-list.h"
#include "dump-pt.h"
#include "utils.h"

#if defined(__x86_64__)
using gregs_type = mx_x86_64_general_regs_t;
#elif defined(__aarch64__)
using gregs_type = mx_arm64_general_regs_t;
#else
using gregs_type = int; // unsupported arch
#endif

// If true then s/w breakpoint instructions do not kill the process.
// After the backtrace is printed the thread quietly resumes.
// TODO: The default is on for now for development purposes.
// Ultimately will want to switch this to off.
static bool swbreak_backtrace_enabled = true;

#ifdef __x86_64__
// If true then an attempt is made to dump processor trace data.
// Requires processor tracing turned on in the kernel.
static bool pt_dump_enabled = false;
#endif

// Return true if the thread is to be resumed "successfully" (meaning the o/s
// won't kill it, and thus the kill process).

static bool is_resumable_swbreak(const mx_exception_report_t* report) {
    if (report->header.type == MX_EXCP_SW_BREAKPOINT && swbreak_backtrace_enabled)
        return true;
    return false;
}

#if defined(__x86_64__)

int have_swbreak_magic(const gregs_type* regs) {
    return regs->rax == CRASHLOGGER_RESUME_MAGIC;
}

#elif defined(__aarch64__)

int have_swbreak_magic(const gregs_type* regs) {
    return regs->r[0] == CRASHLOGGER_RESUME_MAGIC;
}

#else

int have_swbreak_magic(const gregs_type* regs) {
    return 0;
}

#endif

const char* excp_type_to_str(uint32_t type) {
    MX_DEBUG_ASSERT(MX_EXCP_IS_ARCH(type));
    switch (type) {
    case MX_EXCP_GENERAL:
        return "general fault";
    case MX_EXCP_FATAL_PAGE_FAULT:
        return "fatal page fault";
    case MX_EXCP_UNDEFINED_INSTRUCTION:
        return "undefined instruction";
    case MX_EXCP_SW_BREAKPOINT:
        return "sw breakpoint";
    case MX_EXCP_HW_BREAKPOINT:
        return "hw breakpoint";
    case MX_EXCP_UNALIGNED_ACCESS:
        return "alignment fault";
    default:
        // Note: To get a compilation failure when a new exception type has
        // been added without having also updated this function, compile with
        // -Wswitch-enum.
        return "unknown fault";
    }
}

constexpr uint64_t kSysExceptionKey = 1166444u;
constexpr uint64_t kSelfExceptionKey = 0x646a65u;

// How much memory to dump, in bytes.
// Space for this is allocated on the stack, so this can't be too large.
constexpr size_t kMemoryDumpSize = 256;

// Handle of the thread we're dumping.
// This is used by both the main thread and the self-dumper thread.
// However there is no need to lock it as the self-dumper thread only runs
// when the main thread has crashed.
mx_handle_t crashed_thread = MX_HANDLE_INVALID;

// The exception that |crashed_thread| got.
mx_exception_report_t crashed_thread_report;

void output_frame_x86_64(const x86_64_exc_data_t& exc_data,
                         const mx_x86_64_general_regs_t& regs) {
    printf(" CS:  %#18llx RIP: %#18" PRIx64 " EFL: %#18" PRIx64 " CR2: %#18" PRIx64 "\n",
           0ull, regs.rip, regs.rflags, exc_data.cr2);
    printf(" RAX: %#18" PRIx64 " RBX: %#18" PRIx64 " RCX: %#18" PRIx64 " RDX: %#18" PRIx64 "\n",
           regs.rax, regs.rbx, regs.rcx, regs.rdx);
    printf(" RSI: %#18" PRIx64 " RDI: %#18" PRIx64 " RBP: %#18" PRIx64 " RSP: %#18" PRIx64 "\n",
           regs.rsi, regs.rdi, regs.rbp, regs.rsp);
    printf("  R8: %#18" PRIx64 "  R9: %#18" PRIx64 " R10: %#18" PRIx64 " R11: %#18" PRIx64 "\n",
           regs.r8, regs.r9, regs.r10, regs.r11);
    printf(" R12: %#18" PRIx64 " R13: %#18" PRIx64 " R14: %#18" PRIx64 " R15: %#18" PRIx64 "\n",
           regs.r12, regs.r13, regs.r14, regs.r15);
    // errc value is 17 on purpose, errc is 4 characters
    printf(" errc: %#17" PRIx64 "\n", exc_data.err_code);
}

void output_frame_arm64(const arm64_exc_data_t& exc_data,
                        const mx_arm64_general_regs_t& regs) {
    printf(" x0  %#18" PRIx64 " x1  %#18" PRIx64 " x2  %#18" PRIx64 " x3  %#18" PRIx64 "\n",
           regs.r[0], regs.r[1], regs.r[2], regs.r[3]);
    printf(" x4  %#18" PRIx64 " x5  %#18" PRIx64 " x6  %#18" PRIx64 " x7  %#18" PRIx64 "\n",
           regs.r[4], regs.r[5], regs.r[6], regs.r[7]);
    printf(" x8  %#18" PRIx64 " x9  %#18" PRIx64 " x10 %#18" PRIx64 " x11 %#18" PRIx64 "\n",
           regs.r[8], regs.r[9], regs.r[10], regs.r[11]);
    printf(" x12 %#18" PRIx64 " x13 %#18" PRIx64 " x14 %#18" PRIx64 " x15 %#18" PRIx64 "\n",
           regs.r[12], regs.r[13], regs.r[14], regs.r[15]);
    printf(" x16 %#18" PRIx64 " x17 %#18" PRIx64 " x18 %#18" PRIx64 " x19 %#18" PRIx64 "\n",
           regs.r[16], regs.r[17], regs.r[18], regs.r[19]);
    printf(" x20 %#18" PRIx64 " x21 %#18" PRIx64 " x22 %#18" PRIx64 " x23 %#18" PRIx64 "\n",
           regs.r[20], regs.r[21], regs.r[22], regs.r[23]);
    printf(" x24 %#18" PRIx64 " x25 %#18" PRIx64 " x26 %#18" PRIx64 " x27 %#18" PRIx64 "\n",
           regs.r[24], regs.r[25], regs.r[26], regs.r[27]);
    printf(" x28 %#18" PRIx64 " x29 %#18" PRIx64 " lr  %#18" PRIx64 " sp  %#18" PRIx64 "\n",
           regs.r[28], regs.r[29], regs.lr, regs.sp);
    printf(" pc  %#18" PRIx64 " psr %#18" PRIx64 "\n",
           regs.pc, regs.cpsr);
};

bool read_general_regs(mx_handle_t thread, void* buf, size_t buf_size) {
    // The syscall takes a uint32_t.
    auto to_xfer = static_cast<uint32_t>(buf_size);
    uint32_t bytes_read;
    auto status = mx_thread_read_state(thread, MX_THREAD_STATE_REGSET0, buf, to_xfer, &bytes_read);
    if (status < 0) {
        print_mx_error("unable to access general regs", status);
        return false;
    }
    if (bytes_read != buf_size) {
        print_error("general regs size mismatch: %u != %zu\n", bytes_read, buf_size);
        return false;
    }
    return true;
}

bool write_general_regs(mx_handle_t thread, void* buf, size_t buf_size) {
    // The syscall takes a uint32_t.
    auto to_xfer = static_cast<uint32_t> (buf_size);
    auto status = mx_thread_write_state(thread, MX_THREAD_STATE_REGSET0, buf, to_xfer);
    if (status < 0) {
        print_mx_error("unable to access general regs", status);
        return false;
    }
    return true;
}

void dump_memory(mx_handle_t proc, uintptr_t start, size_t len) {
    // Make sure we're not allocating an excessive amount of stack.
    MX_DEBUG_ASSERT(len <= kMemoryDumpSize);

    uint8_t buf[len];
    auto res = mx_process_read_memory(proc, start, buf, len, &len);
    if (res < 0) {
        printf("failed reading %p memory; error : %d\n", (void*)start, res);
    } else if (len != 0) {
        hexdump_ex(buf, len, start);
    }
}

void resume_thread(mx_handle_t thread, bool handled) {
    uint32_t options = MX_RESUME_EXCEPTION;
    if (!handled)
        options |= MX_RESUME_TRY_NEXT;
    auto status = mx_task_resume(thread, options);
    if (status != NO_ERROR) {
        print_mx_error("unable to \"resume\" thread", status);
        // This shouldn't ever happen. The task is now effectively hung.
        // TODO: Try to forcefully kill it?
    }
}

void resume_thread_from_exception(mx_handle_t thread,
                                  const mx_exception_report_t* report,
                                  const gregs_type* gregs) {
    if (is_resumable_swbreak(report) &&
        gregs != nullptr && have_swbreak_magic(gregs)) {
#if defined(__x86_64__)
        // On x86, the pc is left at one past the s/w break insn,
        // so there's nothing more we need to do.
#elif defined(__aarch64__)
        gregs_type regs = *gregs;
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

  Fail:
    // Tell the o/s to "resume" the thread by killing the process, the
    // exception has not been handled.
    resume_thread(thread, false);
}

void process_report(const mx_exception_report_t* report, bool use_libunwind) {
    if (!MX_EXCP_IS_ARCH(report->header.type))
        return;

    auto context = report->context;
    const char* fatal = "fatal ";
    // This won't print "fatal" in the case where this is a s/w bkpt but
    // CRASHLOGGER_RESUME_MAGIC isn't set. Big deal.
    if (is_resumable_swbreak(report))
        fatal = "";
    printf("<== %sexception: process [%" PRIu64 "] thread [%" PRIu64 "]\n", fatal, context.pid, context.tid);
    printf("<== %s, PC at 0x%" PRIxPTR "\n", excp_type_to_str(report->header.type), context.arch.pc);

    mx_handle_t process;
    mx_status_t status = mx_object_get_child(0, context.pid, MX_RIGHT_SAME_RIGHTS, &process);
    if (status < 0) {
        printf("failed to get a handle to [%" PRIu64 "] : error %d\n", context.pid, status);
        return;
    }
    mx_handle_t thread;
    status = mx_object_get_child(process, context.tid, MX_RIGHT_SAME_RIGHTS, &thread);
    if (status < 0) {
        printf("failed to get a handle to [%" PRIu64 ".%" PRIu64 "] : error %d\n", context.pid, context.tid, status);
        mx_handle_close(process);
        return;
    }

    // Record the crashed thread so that if we crash then self_dump_func
    // can (try to) "resume" the thread so that it's not left hanging.
    crashed_thread = thread;
    crashed_thread_report = *report;

    gregs_type reg_buf;
    gregs_type *regs = nullptr;
    mx_vaddr_t pc = 0, sp = 0, fp = 0;
    const char* arch = "unknown";

    if (!read_general_regs(thread, &reg_buf, sizeof(reg_buf)))
        goto Fail;
    // Delay setting this until here so Fail will know we now have the regs.
    regs = &reg_buf;

    if (context.arch_id == ARCH_ID_X86_64) {
#if defined(__x86_64__)
        arch = "x86_64";
        output_frame_x86_64(context.arch.u.x86_64, *regs);
        pc = regs->rip;
        sp = regs->rsp;
        fp = regs->rbp;
#endif
    } else if (context.arch_id == ARCH_ID_ARM_64) {
#if defined(__aarch64__)
        arch = "aarch64";
        output_frame_arm64(context.arch.u.arm_64, *regs);

        // Only output the Fault address register if there's a data fault.
        if (MX_EXCP_FATAL_PAGE_FAULT == report->header.type)
            printf(" far %#18" PRIx64 "\n", context.arch.u.arm_64.far);

        pc = regs->pc;
        sp = regs->sp;
        fp = regs->r[29];
#endif
    } else {
        // TODO: support other architectures. It's unlikely we'll get here as
        // trying to read the regs will likely fail, but we don't assume that.
        printf("unsupported architecture .. coming soon.\n");
        goto Fail;
    }

    printf("bottom of user stack:\n");
    dump_memory(process, sp, kMemoryDumpSize);
    printf("arch: %s\n", arch);
    backtrace(process, thread, pc, sp, fp, use_libunwind);

    // TODO(dje): Print a backtrace of all other threads in the process.
    // Need to be able to suspend/resume threads first. MG-588

#ifdef __x86_64__
    if (pt_dump_enabled) {
        try_dump_pt_data();
    }
#endif

Fail:
    debugf(1, "Done handling thread %" PRIu64 ".%" PRIu64 ".\n", get_koid(process), get_koid(thread));

    // allow the thread (and then process) to die, unless the exception is
    // to just trigger a backtrace (if enabled)
    resume_thread_from_exception(thread, report, regs);
    crashed_thread = MX_HANDLE_INVALID;
    crashed_thread_report = { };

    mx_handle_close(thread);
    mx_handle_close(process);
}

// A small wrapper to provide a useful name to the API call used to effect
// the request.

mx_status_t bind_system_exception_port(mx_handle_t eport) {
    return mx_task_bind_exception_port(MX_HANDLE_INVALID, eport, kSysExceptionKey, 0);
}

// A small wrapper to provide a useful name to the API call used to effect
// the request.

mx_status_t unbind_system_exception_port() {
    return mx_task_bind_exception_port(MX_HANDLE_INVALID, MX_HANDLE_INVALID,
                                         kSysExceptionKey, 0);
}

int self_dump_func(void* arg) {
    mx_handle_t ex_port = static_cast<mx_handle_t> (reinterpret_cast<uintptr_t> (arg));

    // TODO: There may be exceptions we can recover from, but for now KISS
    // and just terminate on any exception.

    mx_exception_packet_t packet;
    mx_port_wait(ex_port, MX_TIME_INFINITE, &packet, sizeof(packet));
    if (packet.hdr.key != kSelfExceptionKey) {
        print_error("invalid crash key");
        return 1;
    }

    fprintf(stderr, "crashlogger: crashed!\n");

    // The main thread got an exception.
    // Try to print a dump of it before we shutdown.

    // Disable system exception handling ASAP: If we get another exception
    // we're hosed. This is also a workaround to MG-307.
    auto unbind_status = unbind_system_exception_port();

    // Also, before we do anything else, "resume" the original crashing thread.
    // Otherwise whomever is waiting on its process to terminate will hang.
    // And best do this ASAP in case we ourselves crash.
    // If this was a resumable exception we'll instead kill the process,
    // but we only get here if crashlogger itself crashed.
    if (crashed_thread != MX_HANDLE_INVALID) {
        resume_thread_from_exception(crashed_thread, &crashed_thread_report, nullptr);
    }

    // Now we can check the return code of the unbinding. We don't want to
    // terminate until the original crashing thread is "resumed".
    // This could be an assert, but we don't want the check disabled in
    // release builds.
    if (unbind_status != NO_ERROR) {
        print_mx_error("WARNING: unable to unbind system exception port", unbind_status);
        // This "shouldn't happen", safer to just terminate.
        exit(1);
    }

    // Pass false for use_libunwind on the assumption that if we crashed
    // because of libunwind then we might crash again (which is ok, we'll
    // handle it appropriately). In order to get a useful backtrace in this
    // situation crashlogger,libunwind,libbacktrace are compiled with frame
    // pointers. This decision needs to be revisited if/when we need/want
    // to compile any of these without frame pointers.
    process_report(&packet.report, false);

    exit(1);
}

void usage() {
    fprintf(stderr, "Usage: crashlogger [options]\n");
    fprintf(stderr, "Options:\n");
    fprintf(stderr, "  -v[n] = set verbosity level to N\n");
    fprintf(stderr, "  -f = force replacement of existing crashlogger\n");
#ifdef __x86_64__
    fprintf(stderr, "  -pt[on|off] = enable processor trace dumps,\n");
    fprintf(stderr, "      requires PT turned on in the kernel\n");
#endif
    fprintf(stderr, "  -n = do not use libunwind\n");
    fprintf(stderr, "  -s[on|off] = enable s/w breakpoints to trigger\n");
    fprintf(stderr, "      a backtrace without terminating the process\n");
}

int main(int argc, char** argv) {
    mx_status_t status;
    bool force = false;
    // Whether to use libunwind or not.
    // If not then we use a simple algorithm that assumes ABI-specific
    // frame pointers are present.
    bool use_libunwind = true;

    for (int i = 1; i < argc; ++i) {
        const char* arg = argv[i];
        if (strncmp(arg, "-v", 2) == 0) {
            if (arg[2] != '\0') {
                verbosity_level = atoi(arg + 2);
            } else {
                verbosity_level = 1;
            }
        } else if (strcmp(arg, "-f") == 0) {
            force = true;
#ifdef __x86_64__
        } else if (strncmp(arg, "-pt", 2) == 0) {
            if (arg[2] == '\0' || strcmp(arg, "-pton") == 0) {
                pt_dump_enabled = true;
            } else if (strcmp(arg, "-ptoff") == 0) {
                pt_dump_enabled = false;
            } else {
                usage();
                return 1;
            }
#endif
        } else if (strcmp(arg, "-n") == 0) {
            use_libunwind = false;
        } else if (strncmp(arg, "-s", 2) == 0) {
            if (arg[2] == '\0') {
                swbreak_backtrace_enabled = true;
            } else if (strcmp(arg, "-son") == 0) {
                swbreak_backtrace_enabled = true;
            } else if (strcmp(arg, "-soff") == 0) {
                swbreak_backtrace_enabled = false;
            } else {
                usage();
                return 1;
            }
        } else {
            usage();
            return 1;
        }
    }

    // At debugging level 1 print our dso list (in case we crash in a way
    // that prevents printing it later).
    if (verbosity_level >= 1) {
        mx_handle_t self = mx_process_self();
        dsoinfo_t* dso_list = dso_fetch_list(self, "crashlogger");
        printf("Crashlogger dso list:\n");
        dso_print_list(dso_list);
        dso_free_list(dso_list);
    }

    // If asked, undo any previously installed exception port.
    // This is useful if the system gets in a state where we want to replace
    // an existing crashlogger with this one.
    if (force) {
        status = unbind_system_exception_port();
        if (status != NO_ERROR) {
            print_mx_error("unable to unbind system exception port", status);
            return 1;
        }
    }

    mx_handle_t thread_self = thrd_get_mx_handle(thrd_current());
    if (thread_self < 0) {
        print_mx_error("unable to get thread self", thread_self);
        return 1;
    }

    mx_handle_t self_dump_port;
    if ((status = mx_port_create(0u, &self_dump_port)) < 0) {
        print_mx_error("mx_port_create failed", status);
        return 1;
    }

    // A thread to wait for and process internal exceptions.
    // This is done so that we can recognize when we ourselves have
    // crashed: We still want a dump, and we need to still mark the original
    // crashing thread as resumed.
    thrd_t self_dump_thread;
    void* self_dump_arg =
        reinterpret_cast<void*> (static_cast<uintptr_t> (self_dump_port));
    int ret = thrd_create_with_name(&self_dump_thread, self_dump_func,
                                    self_dump_arg, "self-dump-thread");
    if (ret != thrd_success) {
        print_error("thrd_create_with_name failed");
        return 1;
    }

    // Bind this exception handler to the main thread instead of the process
    // so that the crashlogger crash dumper doesn't get its own exceptions.
    status = mx_task_bind_exception_port(thread_self, self_dump_port,
                                           kSelfExceptionKey, 0);
    if (status < 0) {
        print_mx_error("unable to set self exception port", status);
        return 1;
    }

    mx_handle_t ex_port;
    if ((status = mx_port_create(0u, &ex_port)) < 0) {
        print_mx_error("mx_port_create failed", status);
        return 1;
    }

    status = bind_system_exception_port(ex_port);
    if (status < 0) {
        print_mx_error("unable to bind system exception port", status);
        return 1;
    }

    printf("crashlogger service ready\n");

    while (true) {
        mx_exception_packet_t packet;
        mx_port_wait(ex_port, MX_TIME_INFINITE, &packet, sizeof(packet));
        if (packet.hdr.key != kSysExceptionKey) {
            print_error("invalid crash key");
            return 1;
        }

        process_report(&packet.report, use_libunwind);
    }

    return 0;
}
