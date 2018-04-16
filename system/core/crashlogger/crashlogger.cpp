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

#include <zircon/assert.h>
#include <zircon/crashlogger.h>
#include <zircon/process.h>
#include <zircon/processargs.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/exception.h>
#include <zircon/syscalls/port.h>
#include <zircon/threads.h>
#include <fdio/util.h>
#include <inspector/inspector.h>
#include <pretty/hexdump.h>

#include "dump-pt.h"

static int verbosity_level = 0;

// The task that we are monitoring.
static zx_handle_t subject = ZX_HANDLE_INVALID;

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

// Same as basename, except will not modify |path|.
// This assumes there are no trailing /s.

static const char* cl_basename(const char* path) {
    const char* base = strrchr(path, '/');
    return base ? base + 1 : path;
}

void do_print_error(const char* file, int line, const char* fmt, ...) {
    const char* base = cl_basename(file);
    va_list args;
    va_start(args, fmt);
    fprintf(stderr, "crashlogger: %s:%d: ", base, line);
    vfprintf(stderr, fmt, args);
    fprintf(stderr, "\n");
    va_end(args);
}

void do_print_zx_error(const char* file, int line, const char* what, zx_status_t status) {
    do_print_error(file, line, "%s: %d (%s)",
                   what, status, zx_status_get_string(status));
}

#define print_error(fmt...) \
  do { \
    do_print_error(__FILE__, __LINE__, fmt); \
  } while (0)

#define print_zx_error(what, status) \
  do { \
    do_print_zx_error(__FILE__, __LINE__, \
                      (what), static_cast<zx_status_t>(status)); \
  } while (0)

// While this should never fail given a valid handle,
// returns ZX_KOID_INVALID on failure.

static zx_koid_t get_koid(zx_handle_t handle) {
    zx_info_handle_basic_t info;
    if (zx_object_get_info(handle, ZX_INFO_HANDLE_BASIC, &info, sizeof(info), NULL, NULL) < 0) {
        // This shouldn't ever happen, so don't just ignore it.
        fprintf(stderr, "Eh? ZX_INFO_HANDLE_BASIC failed\n");
        // OTOH we can't just fail, we have to be robust about reporting back
        // to the kernel that we handled the exception.
        // TODO: Provide ability to safely terminate at any point (e.g., for assert
        // failures and such).
        return ZX_KOID_INVALID;
    }
    return info.koid;
}

// Return true if the thread is to be resumed "successfully" (meaning the o/s
// won't kill it, and thus the kill process).

static bool is_resumable_swbreak(uint32_t excp_type) {
    if (excp_type == ZX_EXCP_SW_BREAKPOINT && swbreak_backtrace_enabled)
        return true;
    return false;
}

#if defined(__x86_64__)

int have_swbreak_magic(const zx_thread_state_general_regs_t* regs) {
    return regs->rax == CRASHLOGGER_RESUME_MAGIC;
}

#elif defined(__aarch64__)

int have_swbreak_magic(const zx_thread_state_general_regs_t* regs) {
    return regs->r[0] == CRASHLOGGER_RESUME_MAGIC;
}

#else

int have_swbreak_magic(const zx_thread_state_general_regs_t* regs) {
    return 0;
}

#endif

const char* excp_type_to_str(uint32_t type) {
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

constexpr uint64_t kSysExceptionKey = 1166444u;
constexpr uint64_t kSelfExceptionKey = 0x646a65u;

// How much memory to dump, in bytes.
// Space for this is allocated on the stack, so this can't be too large.
constexpr size_t kMemoryDumpSize = 256;

// Handle of the thread we're dumping.
// This is used by both the main thread and the self-dumper thread.
// However there is no need to lock it as the self-dumper thread only runs
// when the main thread has crashed.
zx_handle_t crashed_thread = ZX_HANDLE_INVALID;

// The exception that |crashed_thread| got.
uint32_t crashed_thread_excp_type;

bool write_general_regs(zx_handle_t thread, void* buf, size_t buf_size) {
    // The syscall takes a uint32_t.
    auto to_xfer = static_cast<uint32_t> (buf_size);
    auto status = zx_thread_write_state(thread, ZX_THREAD_STATE_GENERAL_REGS, buf, to_xfer);
    if (status < 0) {
        print_zx_error("unable to access general regs", status);
        return false;
    }
    return true;
}

void dump_memory(zx_handle_t proc, uintptr_t start, size_t len) {
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

void resume_thread(zx_handle_t thread, bool handled) {
    uint32_t options = ZX_RESUME_EXCEPTION;
    if (!handled)
        options |= ZX_RESUME_TRY_NEXT;
    auto status = zx_task_resume(thread, options);
    if (status != ZX_OK) {
        print_zx_error("unable to \"resume\" thread", status);
        // This shouldn't happen (unless someone killed it already).
        // The task is now effectively hung (until someone kills it).
        // TODO: Try to forcefully kill it ourselves?
    }
}

void resume_thread_from_exception(zx_handle_t thread,
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

void process_report(uint64_t pid, uint64_t tid, uint32_t type, bool use_libunwind) {
    if (!ZX_EXCP_IS_ARCH(type) && type != ZX_EXCP_POLICY_ERROR)
        return;

    zx_handle_t process;
    zx_status_t status = zx_object_get_child(ZX_HANDLE_INVALID, pid, ZX_RIGHT_SAME_RIGHTS, &process);
    if (status < 0) {
        printf("failed to get a handle to [%" PRIu64 "] : error %d\n", pid, status);
        return;
    }
    zx_handle_t thread;
    status = zx_object_get_child(process, tid, ZX_RIGHT_SAME_RIGHTS, &thread);
    if (status < 0) {
        printf("failed to get a handle to [%" PRIu64 ".%" PRIu64 "] : error %d\n", pid, tid, status);
        zx_handle_close(process);
        return;
    }

    // Record the crashed thread so that if we crash then self_dump_func
    // can (try to) "resume" the thread so that it's not left hanging.
    crashed_thread = thread;
    crashed_thread_excp_type = type;

    zx_exception_report_t report;
    status = zx_object_get_info(thread, ZX_INFO_THREAD_EXCEPTION_REPORT,
                                &report, sizeof(report), NULL, NULL);
    if (status < 0) {
        printf("failed to get exception report for [%" PRIu64 ".%" PRIu64 "] : error %d\n", pid, tid, status);
        zx_handle_close(process);
        zx_handle_close(thread);
        return;
    }
    auto context = report.context;

    zx_thread_state_general_regs_t reg_buf;
    zx_thread_state_general_regs_t *regs = nullptr;
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
    // CRASHLOGGER_RESUME_MAGIC isn't set. Big deal.
    if (is_resumable_swbreak(type))
        fatal = "";
    // TODO(MA-922): Remove this and make policy exceptions fatal.
    if (type == ZX_EXCP_POLICY_ERROR)
        fatal = "";

    char process_name[ZX_MAX_NAME_LEN];
    status = zx_object_get_property(process, ZX_PROP_NAME, process_name, sizeof(process_name));
    if (status < 0) {
        strlcpy(process_name, "unknown", sizeof(process_name));
    }

    char thread_name[ZX_MAX_NAME_LEN];
    status = zx_object_get_property(thread, ZX_PROP_NAME, thread_name, sizeof(thread_name));
    if (status < 0) {
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

#ifdef __x86_64__
    if (pt_dump_enabled) {
        try_dump_pt_data();
    }
#endif

Fail:
    if (verbosity_level >= 1)
        printf("Done handling thread %" PRIu64 ".%" PRIu64 ".\n", get_koid(process), get_koid(thread));

    // allow the thread (and then process) to die, unless the exception is
    // to just trigger a backtrace (if enabled)
    resume_thread_from_exception(thread, type, regs);
    crashed_thread = ZX_HANDLE_INVALID;
    crashed_thread_excp_type = 0u;

    zx_handle_close(thread);
    zx_handle_close(process);
}

zx_status_t bind_subject_exception_port(zx_handle_t eport) {
    return zx_task_bind_exception_port(subject, eport, kSysExceptionKey, 0);
}

zx_status_t unbind_subject_exception_port() {
    return zx_task_bind_exception_port(subject, ZX_HANDLE_INVALID, kSysExceptionKey, 0);
}

int self_dump_func(void* arg) {
    zx_handle_t ex_port = static_cast<zx_handle_t> (reinterpret_cast<uintptr_t> (arg));

    // TODO: There may be exceptions we can recover from, but for now KISS
    // and just terminate on any exception.

    zx_port_packet_t packet;
    zx_port_wait(ex_port, ZX_TIME_INFINITE, &packet, 1);
    if (packet.key != kSelfExceptionKey) {
        print_error("invalid crash key");
        return 1;
    }

    fprintf(stderr, "crashlogger: crashed!\n");

    // The main thread got an exception.
    // Try to print a dump of it before we shutdown.

    // Disable subject exception handling ASAP: If we get another exception
    // we're hosed.
    auto unbind_status = unbind_subject_exception_port();

    // Also, before we do anything else, "resume" the original crashing thread.
    // Otherwise whomever is waiting on its process to terminate will hang.
    // And best do this ASAP in case we ourselves crash.
    // If this was a resumable exception we'll instead kill the process,
    // but we only get here if crashlogger itself crashed.
    if (crashed_thread != ZX_HANDLE_INVALID) {
        resume_thread_from_exception(crashed_thread, crashed_thread_excp_type, nullptr);
    }

    // Now we can check the return code of the unbinding. We don't want to
    // terminate until the original crashing thread is "resumed".
    // This could be an assert, but we don't want the check disabled in
    // release builds.
    if (unbind_status != ZX_OK) {
        print_zx_error("WARNING: unable to unbind subject exception port", unbind_status);
        // This "shouldn't happen", safer to just terminate.
        exit(1);
    }

    // Pass false for use_libunwind on the assumption that if we crashed
    // because of libunwind then we might crash again (which is ok, we'll
    // handle it appropriately). In order to get a useful backtrace in this
    // situation crashlogger,libunwind,libbacktrace are compiled with frame
    // pointers. This decision needs to be revisited if/when we need/want
    // to compile any of these without frame pointers.
    process_report(packet.exception.pid, packet.exception.tid, packet.type, false);

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
    fprintf(stderr, "\n");
    fprintf(stderr, "The task to monitor must be passed as PA_HND(PA_USER0, 0).\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "The exception port may be passed in as an argument,\n"
            "as PA_HND(PA_USER0, 1). The port must be bound to the provided task's\n"
            "exception port. (Note that the port key value must match the one used\n"
            "by crashlogger.)\n");
}

int main(int argc, char** argv) {
    zx_status_t status;
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

    subject = zx_get_startup_handle(PA_HND(PA_USER0, 0));
    if (subject == ZX_HANDLE_INVALID) {
        fprintf(stderr, "error: unable to find a task to monitor in PA_USER0.\n");
        return 1;
    }

    // If asked, undo any previously installed exception port.
    // This is useful if the system gets in a state where we want to replace
    // an existing crashlogger with this one.
    if (force) {
        status = unbind_subject_exception_port();
        if (status != ZX_OK) {
            print_zx_error("unable to unbind subject exception port", status);
            return 1;
        }
    }

    zx_handle_t thread_self = thrd_get_zx_handle(thrd_current());
    if (thread_self == ZX_HANDLE_INVALID) {
        print_zx_error("unable to get thread self", thread_self);
        return 1;
    }

    zx_handle_t self_dump_port;
    if ((status = zx_port_create(0, &self_dump_port)) < 0) {
        print_zx_error("zx_port_create failed", status);
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
    status = zx_task_bind_exception_port(thread_self, self_dump_port,
                                           kSelfExceptionKey, 0);
    if (status < 0) {
        print_zx_error("unable to set self exception port", status);
        return 1;
    }

    // The exception port may be passed in from the parent process.  If it
    // wasn't, we bind the subject exception port.
    zx_handle_t ex_port = zx_get_startup_handle(PA_HND(PA_USER0, 1));
    if (ex_port == ZX_HANDLE_INVALID) {
        if ((status = zx_port_create(0, &ex_port)) < 0) {
            print_zx_error("zx_port_create failed", status);
            return 1;
        }

        status = bind_subject_exception_port(ex_port);
        if (status < 0) {
            print_zx_error("unable to bind subject exception port", status);
            return 1;
        }
    }

    printf("crashlogger service ready\n");

    while (true) {
        zx_port_packet_t packet;
        zx_port_wait(ex_port, ZX_TIME_INFINITE, &packet, 1);
        if (packet.key != kSysExceptionKey) {
            print_error("invalid crash key");
            return 1;
        }

        process_report(packet.exception.pid, packet.exception.tid, packet.type, use_libunwind);
    }

    return 0;
}
