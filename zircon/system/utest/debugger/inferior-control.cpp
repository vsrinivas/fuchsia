// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <elf.h>
#include <inttypes.h>
#include <link.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include <fbl/algorithm.h>
#include <launchpad/launchpad.h>
#include <launchpad/vmo.h>
#include <test-utils/test-utils.h>
#include <unittest/unittest.h>
#include <zircon/compiler.h>
#include <zircon/process.h>
#include <zircon/processargs.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/debug.h>
#include <zircon/syscalls/object.h>
#include <zircon/syscalls/port.h>

#include "inferior.h"
#include "inferior-control.h"
#include "utils.h"

constexpr uint64_t kExceptionPortKey = 0x6b6579; // "key"

void dump_gregs(zx_handle_t thread_handle, const zx_thread_state_general_regs_t* regs) {
    unittest_printf("Registers for thread %d\n", thread_handle);

#define DUMP_NAMED_REG(name)                                                                       \
    unittest_printf("  %8s      %24ld  0x%lx\n", #name, (long)regs->name, (long)regs->name)

#if defined(__x86_64__)

    DUMP_NAMED_REG(rax);
    DUMP_NAMED_REG(rbx);
    DUMP_NAMED_REG(rcx);
    DUMP_NAMED_REG(rdx);
    DUMP_NAMED_REG(rsi);
    DUMP_NAMED_REG(rdi);
    DUMP_NAMED_REG(rbp);
    DUMP_NAMED_REG(rsp);
    DUMP_NAMED_REG(r8);
    DUMP_NAMED_REG(r9);
    DUMP_NAMED_REG(r10);
    DUMP_NAMED_REG(r11);
    DUMP_NAMED_REG(r12);
    DUMP_NAMED_REG(r13);
    DUMP_NAMED_REG(r14);
    DUMP_NAMED_REG(r15);
    DUMP_NAMED_REG(rip);
    DUMP_NAMED_REG(rflags);

#elif defined(__aarch64__)

    for (int i = 0; i < 30; i++) {
        unittest_printf("  r[%2d]     %24ld  0x%lx\n", i, (long)regs->r[i], (long)regs->r[i]);
    }
    DUMP_NAMED_REG(lr);
    DUMP_NAMED_REG(sp);
    DUMP_NAMED_REG(pc);
    DUMP_NAMED_REG(cpsr);

#endif

#undef DUMP_NAMED_REG
}

void dump_inferior_regs(zx_handle_t thread) {
    zx_thread_state_general_regs_t regs;
    read_inferior_gregs(thread, &regs);
    dump_gregs(thread, &regs);
}

// N.B. It is assumed |buf_size| is large enough.

void read_inferior_gregs(zx_handle_t thread, zx_thread_state_general_regs_t* in) {
    zx_status_t status = zx_thread_read_state(
        thread, ZX_THREAD_STATE_GENERAL_REGS, in, sizeof(zx_thread_state_general_regs_t));
    // It's easier to just terminate if this fails.
    if (status != ZX_OK)
        tu_fatal("read_inferior_gregs: zx_thread_read_state", status);
}

void write_inferior_gregs(zx_handle_t thread, const zx_thread_state_general_regs_t* out) {
    zx_status_t status = zx_thread_write_state(thread, ZX_THREAD_STATE_GENERAL_REGS, out,
                                               sizeof(zx_thread_state_general_regs_t));
    // It's easier to just terminate if this fails.
    if (status != ZX_OK)
        tu_fatal("write_inferior_gregs: zx_thread_write_state", status);
}

size_t read_inferior_memory(zx_handle_t proc, uintptr_t vaddr, void* buf, size_t len) {
    zx_status_t status = zx_process_read_memory(proc, vaddr, buf, len, &len);
    if (status < 0)
        tu_fatal("read_inferior_memory", status);
    return len;
}

size_t write_inferior_memory(zx_handle_t proc, uintptr_t vaddr, const void* buf, size_t len) {
    zx_status_t status = zx_process_write_memory(proc, vaddr, buf, len, &len);
    if (status < 0)
        tu_fatal("write_inferior_memory", status);
    return len;
}

// This does everything that launchpad_launch_fdio_etc does except
// start the inferior. We want to attach to it first.
// TODO(dje): Are there other uses of such a wrapper? Move to launchpad?
// Plus there's a fair bit of code here. IWBN to not have to update it as
// launchpad_launch_fdio_etc changes.

zx_status_t create_inferior(const char* name, int argc, const char* const* argv,
                            const char* const* envp, size_t hnds_count, zx_handle_t* handles,
                            uint32_t* ids, launchpad_t** out_launchpad) {
    launchpad_t* lp = NULL;

    const char* filename = argv[0];
    if (name == NULL)
        name = filename;

    zx_status_t status;
    launchpad_create(0u, name, &lp);
    launchpad_load_from_file(lp, filename);
    launchpad_set_args(lp, argc, argv);
    launchpad_set_environ(lp, envp);
    launchpad_clone(lp, LP_CLONE_FDIO_ALL);
    status = launchpad_add_handles(lp, hnds_count, handles, ids);

    if (status < 0) {
        launchpad_destroy(lp);
    } else {
        *out_launchpad = lp;
    }
    return status;
}

bool setup_inferior(const char* name, launchpad_t** out_lp, zx_handle_t* out_inferior,
                    zx_handle_t* out_channel) {
    BEGIN_HELPER;

    zx_status_t status;
    zx_handle_t channel1, channel2;
    tu_channel_create(&channel1, &channel2);

    const char verbosity_string[] = {'v', '=', static_cast<char>(utest_verbosity_level + '0'),
                                     '\0'};
    const char* test_child_path = g_program_path;
    const char* const argv[] = {test_child_path, name, verbosity_string};
    zx_handle_t handles[1] = {channel2};
    uint32_t handle_ids[1] = {PA_USER0};

    launchpad_t* lp;
    unittest_printf("Creating process \"%s\"\n", name);
    status = create_inferior(name, fbl::count_of(argv), argv, NULL, fbl::count_of(handles),
                             handles, handle_ids, &lp);
    ASSERT_EQ(status, ZX_OK, "failed to create inferior");

    // Note: |inferior| is a borrowed handle here.
    zx_handle_t inferior = launchpad_get_process_handle(lp);
    ASSERT_NE(inferior, ZX_HANDLE_INVALID, "can't get launchpad process handle");

    zx_info_handle_basic_t process_info;
    tu_handle_get_basic_info(inferior, &process_info);
    unittest_printf("Inferior pid = %llu\n", (long long)process_info.koid);

    // |inferior| is given to the child by launchpad_go.
    // We need our own copy, and launchpad_go will give us one, but we need
    // it before we call launchpad_go in order to attach to the debugging
    // exception port. We could leave this to our caller to do, but since every
    // caller needs this for convenience sake we do this here.
    status = zx_handle_duplicate(inferior, ZX_RIGHT_SAME_RIGHTS, &inferior);
    ASSERT_EQ(status, ZX_OK, "zx_handle_duplicate failed");

    *out_lp = lp;
    *out_inferior = inferior;
    *out_channel = channel1;

    END_HELPER;
}

// While this should perhaps take a launchpad_t* argument instead of the
// inferior's handle, we later want to test attaching to an already running
// inferior.
// |max_threads| is the maximum number of threads the process is expected
// to have in its lifetime. A real debugger would be more flexible of course.
// N.B. |inferior| cannot be the result of launchpad_get_process_handle().
// That handle is passed to the inferior when started and thus is lost to us.
// Returns a boolean indicating success.

inferior_data_t* attach_inferior(zx_handle_t inferior, zx_handle_t eport, size_t max_threads) {
    // Fetch all current threads and attach async-waiters to them.
    // N.B. We assume threads aren't being created as we're running.
    // This is just a testcase so we can assume that. A real debugger
    // would not have this assumption.
    zx_koid_t* thread_koids = reinterpret_cast<zx_koid_t*>(tu_malloc(max_threads * sizeof(zx_koid_t)));
    size_t num_threads = tu_process_get_threads(inferior, thread_koids, max_threads);
    // For now require |max_threads| to be big enough.
    if (num_threads > max_threads)
        tu_fatal(__func__, ZX_ERR_BUFFER_TOO_SMALL);

    tu_set_exception_port(inferior, eport, kExceptionPortKey, ZX_EXCEPTION_PORT_DEBUGGER);
    tu_object_wait_async(inferior, eport, ZX_PROCESS_TERMINATED);

    inferior_data_t* data = reinterpret_cast<inferior_data_t*>(tu_malloc(sizeof(*data)));
    data->threads = reinterpret_cast<thread_data_t*>(tu_calloc(max_threads, sizeof(data->threads[0])));
    data->inferior = inferior;
    data->eport = eport;
    data->max_num_threads = max_threads;

    // Notification of thread termination and suspension is delivered by
    // signals. So that we can continue to only have to wait on |eport|
    // for inferior status change notification, install async-waiters
    // for each thread.
    size_t j = 0;
    zx_signals_t thread_signals = ZX_THREAD_TERMINATED | ZX_THREAD_RUNNING | ZX_THREAD_SUSPENDED;
    for (size_t i = 0; i < num_threads; ++i) {
        zx_handle_t thread = tu_process_get_thread(inferior, thread_koids[i]);
        if (thread != ZX_HANDLE_INVALID) {
            data->threads[j].tid = thread_koids[i];
            data->threads[j].handle = thread;
            tu_object_wait_async(thread, eport, thread_signals);
            ++j;
        }
    }
    free(thread_koids);

    unittest_printf("Attached to inferior\n");
    return data;
}

bool expect_debugger_attached_eq(zx_handle_t inferior, bool expected, const char* msg) {
    BEGIN_HELPER;

    zx_info_process_t info;
    // ZX_ASSERT returns false if the check fails.
    ASSERT_EQ(zx_object_get_info(inferior, ZX_INFO_PROCESS, &info, sizeof(info), NULL, NULL), ZX_OK);
    ASSERT_EQ(info.debugger_attached, expected, msg);

    END_HELPER;
}

void detach_inferior(inferior_data_t* data, bool unbind_eport) {
    if (unbind_eport) {
        unbind_inferior(data->inferior);
    }
    for (size_t i = 0; i < data->max_num_threads; ++i) {
        if (data->threads[i].handle != ZX_HANDLE_INVALID)
            tu_handle_close(data->threads[i].handle);
    }
    free(data->threads);
    free(data);
}

void unbind_inferior(zx_handle_t inferior) {
    tu_set_exception_port(inferior, ZX_HANDLE_INVALID, kExceptionPortKey,
                          ZX_EXCEPTION_PORT_DEBUGGER);
}

bool start_inferior(launchpad_t* lp) {
    zx_handle_t dup_inferior = tu_launch_fdio_fini(lp);
    unittest_printf("Inferior started\n");
    // launchpad_go returns a dup of |inferior|. The original inferior
    // handle is given to the child. However we don't need it, we already
    // created one so that we could attach to the inferior before starting it.
    tu_handle_close(dup_inferior);
    return true;
}

bool resume_inferior(zx_handle_t inferior, zx_handle_t port, zx_koid_t tid) {
    BEGIN_HELPER;

    zx_handle_t thread;
    zx_status_t status = zx_object_get_child(inferior, tid, ZX_RIGHT_SAME_RIGHTS, &thread);
    if (status == ZX_ERR_NOT_FOUND) {
        // If the process has exited then the kernel may have reaped the
        // thread already. Check.
        if (tu_process_has_exited(inferior))
            return true;
    }
    ASSERT_EQ(status, ZX_OK, "zx_object_get_child failed");

    unittest_printf("Resuming inferior ...\n");
    status = zx_task_resume_from_exception(thread, port, 0);
    if (status == ZX_ERR_BAD_STATE) {
        // If the process has exited then the thread may have exited
        // ExceptionHandlerExchange already. Check.
        if (tu_thread_is_dying_or_dead(thread)) {
            tu_handle_close(thread);
            return true;
        }
    }
    tu_handle_close(thread);
    ASSERT_EQ(status, ZX_OK, "zx_task_resume_from_exception failed");

    END_HELPER;
}

bool shutdown_inferior(zx_handle_t channel, zx_handle_t inferior) {
    BEGIN_HELPER;

    unittest_printf("Shutting down inferior\n");

    send_simple_request(channel, RQST_DONE);

    tu_process_wait_signaled(inferior);
    EXPECT_EQ(tu_process_get_return_code(inferior), kInferiorReturnCode, "");

    END_HELPER;
}

// Wait for and read an exception/signal on |eport|.

bool read_packet(zx_handle_t eport, zx_port_packet_t* packet) {
    BEGIN_HELPER;

    unittest_printf("Waiting for exception/signal on eport %d\n", eport);
    ASSERT_EQ(zx_port_wait(eport, ZX_TIME_INFINITE, packet), ZX_OK, "zx_port_wait failed");

    if (ZX_PKT_IS_EXCEPTION(packet->type))
        ASSERT_EQ(packet->key, kExceptionPortKey);

    unittest_printf("read_packet: got exception/signal %d\n", packet->type);

    END_HELPER;
}

// Wait for the thread to suspend
// We could get a thread exit report from a previous test, so
// we need to handle that, but no other exceptions are expected.
//
// The thread is assumed to be wait-async'd on |eport|. While we could just
// wait on the |thread| for the appropriate signal, the signal will also be
// sent to |eport| which our caller would then have to deal with. Keep things
// simpler by doing all waiting via |eport|. It also makes us exercise doing
// things this way, which is generally what debuggers will do.

bool wait_thread_suspended(zx_handle_t proc, zx_handle_t thread, zx_handle_t eport) {
    BEGIN_HELPER;

    zx_koid_t tid = tu_get_koid(thread);

    zx_signals_t signals = ZX_THREAD_TERMINATED | ZX_THREAD_RUNNING | ZX_THREAD_SUSPENDED;
    tu_object_wait_async(thread, eport, signals);

    while (true) {
        zx_port_packet_t packet;
        zx_status_t status = zx_port_wait(eport, zx_deadline_after(ZX_SEC(1)), &packet);
        if (status == ZX_ERR_TIMED_OUT) {
            // This shouldn't really happen unless the system is really loaded.
            // Just flag it and try again. The watchdog will catch failures.
            unittest_printf("%s: timed out???\n", __func__);
            tu_object_wait_async(thread, eport, signals);
            continue;
        }
        ASSERT_EQ(status, ZX_OK);
        if (ZX_PKT_IS_SIGNAL_ONE(packet.type)) {
            ASSERT_EQ(packet.key, tid);
            if (packet.signal.observed & ZX_THREAD_SUSPENDED)
                break;
            ASSERT_TRUE(packet.signal.observed & ZX_THREAD_RUNNING);
            tu_object_wait_async(thread, eport, signals);
        } else {
            ASSERT_TRUE(ZX_PKT_IS_EXCEPTION(packet.type));
            zx_koid_t report_tid = packet.exception.tid;
            ASSERT_NE(report_tid, tid);
            ASSERT_EQ(packet.type, (uint32_t)ZX_EXCP_THREAD_EXITING);
            // Note the thread may be completely gone by now.
            zx_handle_t other_thread;
            zx_status_t status =
                zx_object_get_child(proc, report_tid, ZX_RIGHT_SAME_RIGHTS, &other_thread);
            if (status == ZX_OK) {
                // And even if it's not gone it may be dead now.
                status = zx_task_resume_from_exception(other_thread, eport, 0);
                if (status == ZX_ERR_BAD_STATE)
                    ASSERT_TRUE(tu_thread_is_dying_or_dead(other_thread));
                else
                    ASSERT_EQ(status, ZX_OK);
                tu_handle_close(other_thread);
            }
        }
    }

    // Verify thread is suspended
    zx_info_thread_t info = tu_thread_get_info(thread);
    ASSERT_EQ(info.state, ZX_THREAD_STATE_SUSPENDED);
    ASSERT_EQ(info.wait_exception_port_type, ZX_EXCEPTION_PORT_TYPE_NONE);

    END_HELPER;
}

// This returns a bool as it's a unittest "helper" routine.
// N.B. This runs on the wait-inferior thread.

bool handle_thread_exiting(zx_handle_t inferior, zx_handle_t port, const zx_port_packet_t* packet) {
    BEGIN_HELPER;

    zx_koid_t tid = packet->exception.tid;
    zx_handle_t thread;
    zx_status_t status = zx_object_get_child(inferior, tid, ZX_RIGHT_SAME_RIGHTS, &thread);
    // If the process has exited then the kernel may have reaped the
    // thread already. Check.
    if (status == ZX_OK) {
        zx_info_thread_t info = tu_thread_get_info(thread);
        // The thread could still transition to DEAD here (if the
        // process exits), so check for either DYING or DEAD.
        EXPECT_TRUE(info.state == ZX_THREAD_STATE_DYING || info.state == ZX_THREAD_STATE_DEAD);
        // If the state is DYING it would be nice to check that the
        // value of |info.wait_exception_port_type| is DEBUGGER. Alas
        // if the process has exited then the thread will get
        // THREAD_SIGNAL_KILL which will cause
        // UserThread::ExceptionHandlerExchange to exit before we've
        // told the thread to "resume" from ZX_EXCP_THREAD_EXITING.
        // The thread is still in the DYING state but it is no longer
        // in an exception. Thus |info.wait_exception_port_type| can
        // either be DEBUGGER or NONE.
        EXPECT_TRUE(info.wait_exception_port_type == ZX_EXCEPTION_PORT_TYPE_NONE ||
                    info.wait_exception_port_type == ZX_EXCEPTION_PORT_TYPE_DEBUGGER);
        tu_handle_close(thread);
    } else {
        EXPECT_EQ(status, ZX_ERR_NOT_FOUND);
        EXPECT_TRUE(tu_process_has_exited(inferior));
    }
    unittest_printf("wait-inf: thread %" PRIu64 " exited\n", tid);
    // A thread is gone, but we only care about the process.
    if (!resume_inferior(inferior, port, tid))
        return false;

    END_HELPER;
}

// A simpler exception handler.
// All exceptions are passed on to |handler|.
// Returns false if a test fails.
// Otherwise waits for the inferior to exit and returns true.

static bool wait_inferior_thread_worker(
        inferior_data_t* inferior_data,
        wait_inferior_exception_handler_t* handler, void* handler_arg) {
    zx_handle_t inferior = inferior_data->inferior;
    zx_koid_t pid = tu_get_koid(inferior);
    zx_handle_t eport = inferior_data->eport;

    while (true) {
        zx_port_packet_t packet;
        if (!read_packet(eport, &packet))
            return false;

        // Is the inferior gone?
        if (ZX_PKT_IS_SIGNAL_ONE(packet.type)) {
            if (packet.key == pid) {
                if (packet.signal.observed & ZX_PROCESS_TERMINATED) {
                    return true;
                }
                tu_object_wait_async(inferior, eport, ZX_PROCESS_TERMINATED);
            } else {
                zx_signals_t thread_signals = ZX_THREAD_TERMINATED;
                if (packet.signal.observed & ZX_THREAD_RUNNING)
                    thread_signals |= ZX_THREAD_SUSPENDED;
                else if (packet.signal.observed & ZX_THREAD_SUSPENDED)
                    thread_signals |= ZX_THREAD_RUNNING;
                zx_handle_t thread = tu_process_get_thread(inferior, packet.key);
                if (thread == ZX_HANDLE_INVALID) {
                    continue;
                }
                tu_object_wait_async(thread, eport, thread_signals);
            }
        }

        if (!handler(inferior, eport, &packet, handler_arg))
            return false;
    }
}

struct wait_inferior_args_t {
    inferior_data_t* inferior_data;
    wait_inferior_exception_handler_t* handler;
    void* handler_arg;
};

static int wait_inferior_thread_func(void* arg) {
    wait_inferior_args_t* args = reinterpret_cast<wait_inferior_args_t*>(arg);
    inferior_data_t* inferior_data = args->inferior_data;
    wait_inferior_exception_handler_t* handler = args->handler;
    void* handler_arg = args->handler_arg;
    free(args);

    bool pass = wait_inferior_thread_worker(inferior_data, handler, handler_arg);

    return pass ? 0 : -1;
}

thrd_t start_wait_inf_thread(inferior_data_t* inferior_data,
                             wait_inferior_exception_handler_t* handler,
                             void* handler_arg) {
    wait_inferior_args_t* args =
        reinterpret_cast<wait_inferior_args_t*>(tu_calloc(1, sizeof(*args)));

    // The proc handle is loaned to the thread.
    // The caller of this function owns and must close it.
    args->inferior_data = inferior_data;
    args->handler = handler;
    args->handler_arg = handler_arg;

    thrd_t wait_inferior_thread;
    tu_thread_create_c11(&wait_inferior_thread, wait_inferior_thread_func, args, "wait-inf thread");
    return wait_inferior_thread;
}

bool join_wait_inf_thread(thrd_t wait_inf_thread) {
    BEGIN_HELPER;

    unittest_printf("Waiting for wait-inf thread\n");
    int thread_rc;
    int ret = thrd_join(wait_inf_thread, &thread_rc);
    EXPECT_EQ(ret, thrd_success, "thrd_join failed");
    EXPECT_EQ(thread_rc, 0, "unexpected wait-inf return");
    unittest_printf("wait-inf thread done\n");

    END_HELPER;
}
