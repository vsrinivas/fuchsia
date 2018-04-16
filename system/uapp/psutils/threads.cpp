// Copyright 2017 The Fuchsia Authors. All rights reserved.
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
#include <zircon/process.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/exception.h>
#include <zircon/syscalls/port.h>
#include <zircon/threads.h>

#include <fbl/unique_ptr.h>
#include <fbl/vector.h>
#include <inspector/inspector.h>
#include <pretty/hexdump.h>

static int verbosity_level = 0;

void print_error(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    fprintf(stderr, "ERROR: ");
    vfprintf(stderr, fmt, args);
    fprintf(stderr, "\n");
    va_end(args);
}

void print_zx_error(zx_status_t status, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    fprintf(stderr, "ERROR: ");
    vfprintf(stderr, fmt, args);
    fprintf(stderr, ": %d(%s)", status, zx_status_get_string(status));
    fprintf(stderr, "\n");
    va_end(args);
}

// While this should never fail given a valid handle,
// returns ZX_KOID_INVALID on failure.
zx_koid_t get_koid(zx_handle_t handle) {
    zx_info_handle_basic_t info;
    if (zx_object_get_info(handle, ZX_INFO_HANDLE_BASIC, &info, sizeof(info), NULL, NULL) < 0) {
        // This shouldn't ever happen, so don't just ignore it.
        print_error("Eh? ZX_INFO_HANDLE_BASIC failed");
        return ZX_KOID_INVALID;
    }
    return info.koid;
}

constexpr uint64_t kSelfExceptionKey = 0x646a65u;

// How much memory to dump, in bytes.
// Space for this is allocated on the stack, so this can't be too large.
constexpr size_t kMemoryDumpSize = 256;

// Handle of the thread we're dumping.
// This is used by both the main thread and the self-dumper thread.
// However there is no need to lock it as the self-dumper thread only runs
// when the main thread has crashed.
zx_handle_t suspended_thread = ZX_HANDLE_INVALID;

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

void resume_thread(zx_handle_t thread) {
    uint32_t options = 0;
    auto status = zx_task_resume(thread, options);
    if (status != ZX_OK) {
        print_zx_error(status, "unable to resume thread");
        // This couldn happen if someone killed the thread already.
    }
}

void resume_thread_from_exception(zx_handle_t thread) {
    uint32_t options = ZX_RESUME_EXCEPTION | ZX_RESUME_TRY_NEXT;
    auto status = zx_task_resume(thread, options);
    if (status != ZX_OK) {
        print_zx_error(status, "unable to resume thread");
        // This could happen if someone killed the thread already.
        // We crashed, but we can't resume exception processing, so just exit.
        exit (1);
    }
}

void dump_thread(zx_handle_t process, inspector_dsoinfo_t* dso_list,
                 uint64_t tid, zx_handle_t thread) {
    zx_thread_state_general_regs_t regs;
    zx_vaddr_t pc = 0, sp = 0, fp = 0;

    if (inspector_read_general_regs(thread, &regs) != ZX_OK) {
        // Error message has already been printed.
        return;
    }

#if defined(__x86_64__)
    pc = regs.rip;
    sp = regs.rsp;
    fp = regs.rbp;
#elif defined(__aarch64__)
    pc = regs.pc;
    sp = regs.sp;
    fp = regs.r[29];
#else
    // It's unlikely we'll get here as trying to read the regs will likely
    // fail, but we don't assume that.
    printf("unsupported architecture .. coming soon.\n");
    return;
#endif

    char thread_name[ZX_MAX_NAME_LEN];
    auto status = zx_object_get_property(thread, ZX_PROP_NAME, thread_name, sizeof(thread_name));
    if (status < 0) {
        strlcpy(thread_name, "unknown", sizeof(thread_name));
    }

    printf("<== Thread %s[%" PRIu64 "] ==>\n", thread_name, tid);

    inspector_print_general_regs(stdout, &regs, nullptr);

    printf("bottom of user stack:\n");
    dump_memory(process, sp, kMemoryDumpSize);

    inspector_print_backtrace(stdout, process, thread, dso_list, pc, sp, fp, true);

    if (verbosity_level >= 1)
        printf("Done handling thread %" PRIu64 ".%" PRIu64 ".\n", get_koid(process), get_koid(thread));
}

void dump_all_threads(uint64_t pid, zx_handle_t process) {
    // First get the thread count so that we can allocate an appropriately
    // sized buffer. This is racy but it's the nature of the beast.
    size_t num_threads;
    zx_status_t status =
        zx_object_get_info(process, ZX_INFO_PROCESS_THREADS, nullptr, 0,
                           nullptr, &num_threads);
    if (status != ZX_OK) {
        print_zx_error(status, "failed to get process thread info (#threads)");
        exit(1);
    }

    auto threads = fbl::unique_ptr<zx_koid_t[]>(new zx_koid_t[num_threads]);
    size_t records_read;
    status = zx_object_get_info(process, ZX_INFO_PROCESS_THREADS,
                                threads.get(),
                                num_threads * sizeof(threads[0]),
                                &records_read, nullptr);
    if (status != ZX_OK) {
        print_zx_error(status, "failed to get process thread info");
        exit(1);
    }
    ZX_DEBUG_ASSERT(records_read == num_threads);

    const char* arch = "unknown";
#if defined(__x86_64__)
    arch = "x86_64";
#elif defined(__aarch64__)
    arch = "aarch64";
#endif
    printf("arch: %s\n", arch);

    printf("%zu thread(s)\n", num_threads);

    inspector_dsoinfo_t* dso_list = inspector_dso_fetch_list(process);
    inspector_dso_print_list(stdout, dso_list);

    // TODO(dje): Move inspector's DebugInfoCache here, so that we can use it
    // across all threads.

    for (size_t i = 0; i < num_threads; ++i) {
        zx_koid_t tid = threads[i];
        zx_handle_t thread;
        // TODO(dje): There is value in specifying exactly the rights we need,
        // but an explicit list this early has a higher risk of bitrot.
        status = zx_object_get_child(process, tid, ZX_RIGHT_SAME_RIGHTS, &thread);
        if (status < 0) {
            printf("WARNING: failed to get a handle to [%" PRIu64 ".%" PRIu64 "] : error %d\n", pid, tid, status);
            continue;
        }

        status = zx_task_suspend(thread);
        if (status != ZX_OK) {
            print_zx_error(status, "unable to suspend thread, skipping");
            zx_handle_close(thread);
            continue;
        }

        // Record the thread so that if we crash then self_dump_func
        // can "resume" the thread so that it's not left hanging.
        suspended_thread = thread;

        zx_signals_t observed = 0u;
        // Try to be robust and don't wait forever. The timeout is a little
        // high as we want to work well in really loaded systems.
        auto deadline = zx_deadline_after(ZX_SEC(5));
        // Currently, asking to wait for suspended means only waiting for the
        // thread to suspend. If the thread terminates instead this will wait
        // forever (or until the timeout). Thus we need to explicitly wait for
        // ZX_THREAD_TERMINATED too.
        zx_signals_t signals = ZX_THREAD_SUSPENDED | ZX_THREAD_TERMINATED;
        status = zx_object_wait_one(thread, signals, deadline, &observed);
        if (status == ZX_OK) {
            if (observed & ZX_THREAD_TERMINATED) {
                printf("Unable to print backtrace of thread %" PRIu64 ".%" PRIu64 ": terminated\n",
                       pid, tid);
            } else {
                dump_thread(process, dso_list, tid, thread);
            }
        } else {
            print_zx_error(status,
                           "failure waiting for thread %" PRIu64 ".%" PRIu64 " to suspend, skipping",
                           pid, tid);
        }

        resume_thread(thread);
        suspended_thread = ZX_HANDLE_INVALID;
        zx_handle_close(thread);
    }

    inspector_dso_free_list(dso_list);
}

// To pass data from main to self_dump_func.
struct SelfDumpData {
    zx_handle_t main_thread;
    zx_handle_t excp_port;
};

int self_dump_func(void* arg) {
    auto data = reinterpret_cast<SelfDumpData*>(arg);

    while (true) {
        zx_port_packet_t packet;
        zx_port_wait(data->excp_port, ZX_TIME_INFINITE, &packet, 1);
        if (packet.key != kSelfExceptionKey) {
            print_error("invalid crash key");
            return 1;
        }

        fprintf(stderr, "FATAL: threads crashed!\n");

        // The main thread got an exception.
        // Resume any thread we were working on and resume the main thread,
        // letting crashlogger dump it.
        if (suspended_thread != ZX_HANDLE_INVALID) {
            resume_thread(suspended_thread);
            suspended_thread = ZX_HANDLE_INVALID;
        }

        resume_thread_from_exception(data->main_thread);

        // The kernel will kill us after crashlogger is done, but we don't
        // want to exit here to give crashlogger time to print the report.
    }
}

void usage(FILE* f) {
    fprintf(f, "Usage: threads [options] pid\n");
    fprintf(f, "Options:\n");
    fprintf(f, "  -v[n] = set verbosity level to N\n");
}

int main(int argc, char** argv) {
    zx_status_t status;
    zx_koid_t pid = ZX_KOID_INVALID;

    int i;
    for (i = 1; i < argc; ++i) {
        const char* arg = argv[i];
        if (arg[0] == '-') {
            if (strcmp(arg, "-h") == 0 || strcmp(arg, "--help") == 0) {
                usage(stdout);
                return 0;
            } else if (strncmp(arg, "-v", 2) == 0) {
                if (arg[2] != '\0') {
                    verbosity_level = atoi(arg + 2);
                } else {
                    verbosity_level = 1;
                }
            } else {
                usage(stderr);
                return 1;
            }
        } else {
            break;
        }
    }
    if (i == argc || i + 1 != argc) {
            usage(stderr);
            return 1;
    }
    char *endptr;
    const char* pidstr = argv[i];
    pid = strtoull(pidstr, &endptr, 0);
    if (!(pidstr[0] != '\0' && *endptr == '\0')) {
        fprintf(stderr, "ERROR: invalid pid: %s", pidstr);
        return 1;
    }

    inspector_set_verbosity(verbosity_level);

    zx_handle_t thread_self = thrd_get_zx_handle(thrd_current());
    if (thread_self == ZX_HANDLE_INVALID) {
        print_error("unable to get thread self");
        return 1;
    }

    zx_handle_t self_dump_port;
    if ((status = zx_port_create(0, &self_dump_port)) < 0) {
        print_zx_error(status, "zx_port_create failed");
        return 1;
    }

    // A thread to wait for and process internal exceptions.
    // This is done so that we can recognize when we ourselves have
    // crashed: We need to resume the process we're dumping.
    thrd_t self_dump_thread;
    SelfDumpData self_dump_data;
    self_dump_data.main_thread = thread_self;
    self_dump_data.excp_port = self_dump_port;
    auto self_dump_arg = reinterpret_cast<void*>(&self_dump_data);
    int ret = thrd_create_with_name(&self_dump_thread, self_dump_func,
                                    self_dump_arg, "self-dump-thread");
    if (ret != thrd_success) {
        print_error("thrd_create_with_name failed");
        return 1;
    }

    // Bind this exception handler to the main thread instead of the process
    // so that our crash dumper doesn't get its own exceptions.
    status = zx_task_bind_exception_port(thread_self, self_dump_port,
                                         kSelfExceptionKey, 0);
    if (status < 0) {
        print_zx_error(status, "unable to set self exception port");
        return 1;
    }

    zx_handle_t process;
    status = zx_object_get_child(ZX_HANDLE_INVALID, pid, ZX_RIGHT_SAME_RIGHTS, &process);
    if (status < 0) {
        print_zx_error(status, "unable to get a handle to %" PRIu64, pid);
        return 1;
    }

    char process_name[ZX_MAX_NAME_LEN];
    status = zx_object_get_property(process, ZX_PROP_NAME, process_name, sizeof(process_name));
    if (status < 0) {
        strlcpy(process_name, "unknown", sizeof(process_name));
    }

    printf("Backtrace of threads of process %" PRIu64 ": %s\n",
           pid, process_name);

    dump_all_threads(pid, process);
    zx_handle_close(process);

    return 0;
}
