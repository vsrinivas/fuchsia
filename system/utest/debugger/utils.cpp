// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <inttypes.h>
#include <link.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

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

#include "utils.h"

// argv[0]
const char* program_path;

static const uint64_t exception_port_key = 0x6b6579; // "key"

uint32_t get_uint32(char* buf) {
    uint32_t value = 0;
    memcpy(&value, buf, sizeof(value));
    return value;
}

uint64_t get_uint64(char* buf) {
    uint64_t value = 0;
    memcpy(&value, buf, sizeof(value));
    return value;
}

void set_uint64(char* buf, uint64_t value) {
    memcpy(buf, &value, sizeof(value));
}

uint32_t get_uint32_property(zx_handle_t handle, uint32_t prop) {
    uint32_t value;
    zx_status_t status = zx_object_get_property(handle, prop, &value, sizeof(value));
    if (status != ZX_OK)
        tu_fatal("zx_object_get_property failed", status);
    return value;
}

void send_msg(zx_handle_t handle, message msg) {
    uint64_t data = msg;
    unittest_printf("sending message %d on handle %u\n", msg, handle);
    tu_channel_write(handle, 0, &data, sizeof(data), NULL, 0);
}

// This returns "bool" because it uses ASSERT_*.

bool recv_msg(zx_handle_t handle, message* msg) {
    BEGIN_HELPER;

    uint64_t data;
    uint32_t num_bytes = sizeof(data);

    unittest_printf("waiting for message on handle %u\n", handle);

    ASSERT_TRUE(tu_channel_wait_readable(handle), "peer closed while trying to read message");

    tu_channel_read(handle, 0, &data, &num_bytes, NULL, 0);
    ASSERT_EQ(num_bytes, sizeof(data), "unexpected message size");

    *msg = static_cast<message>(data);
    unittest_printf("received message %d\n", *msg);

    END_HELPER;
}

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
    const char* test_child_path = program_path;
    const char* const argv[] = {test_child_path, name, verbosity_string};
    zx_handle_t handles[1] = {channel2};
    uint32_t handle_ids[1] = {PA_USER0};

    launchpad_t* lp;
    unittest_printf("Creating process \"%s\"\n", name);
    status = create_inferior(name, countof(argv), argv, NULL, countof(handles), handles, handle_ids,
                             &lp);
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
    zx_koid_t* thread_koids = static_cast<zx_koid_t*>(tu_malloc(max_threads * sizeof(zx_koid_t)));
    size_t num_threads = tu_process_get_threads(inferior, thread_koids, max_threads);
    // For now require |max_threads| to be big enough.
    if (num_threads > max_threads)
        tu_fatal(__func__, ZX_ERR_BUFFER_TOO_SMALL);

    tu_set_exception_port(inferior, eport, exception_port_key, ZX_EXCEPTION_PORT_DEBUGGER);
    tu_object_wait_async(inferior, eport, ZX_PROCESS_TERMINATED);

    inferior_data_t* data = static_cast<inferior_data_t*>(tu_malloc(sizeof(*data)));
    data->threads = static_cast<thread_data_t*>(tu_calloc(max_threads, sizeof(data->threads[0])));
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

void detach_inferior(inferior_data_t* data, bool unbind_eport) {
    if (unbind_eport) {
        tu_set_exception_port(data->inferior, ZX_HANDLE_INVALID, exception_port_key,
                              ZX_EXCEPTION_PORT_DEBUGGER);
    }
    for (size_t i = 0; i < data->max_num_threads; ++i) {
        if (data->threads[i].handle != ZX_HANDLE_INVALID)
            tu_handle_close(data->threads[i].handle);
    }
    free(data->threads);
    free(data);
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

bool verify_inferior_running(zx_handle_t channel) {
    BEGIN_HELPER;

    enum message msg;
    send_msg(channel, MSG_PING);
    if (!recv_msg(channel, &msg))
        return false;
    EXPECT_EQ(msg, MSG_PONG, "unexpected response from ping");

    END_HELPER;
}

static bool recv_msg_handle(zx_handle_t channel, message expected_msg, zx_handle_t* handle) {
    BEGIN_HELPER;

    unittest_printf("waiting for message on channel %u\n", channel);
    ASSERT_TRUE(tu_channel_wait_readable(channel), "peer closed while trying to read message");

    uint64_t data;
    uint32_t num_bytes = sizeof(data);
    uint32_t num_handles = 1;
    tu_channel_read(channel, 0, &data, &num_bytes, handle, &num_handles);
    ASSERT_EQ(num_bytes, sizeof(data));
    ASSERT_EQ(num_handles, 1u);

    message msg = static_cast<message>(data);
    ASSERT_EQ(msg, expected_msg);

    unittest_printf("received handle %d\n", *handle);

    END_HELPER;
}

bool get_inferior_thread_handle(zx_handle_t channel, zx_handle_t* thread) {
    BEGIN_HELPER;

    send_msg(channel, MSG_GET_THREAD_HANDLE);
    ASSERT_TRUE(recv_msg_handle(channel, MSG_THREAD_HANDLE, thread));

    END_HELPER;
}

bool resume_inferior(zx_handle_t inferior, zx_koid_t tid) {
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
    status = zx_task_resume(thread, ZX_RESUME_EXCEPTION);
    if (status == ZX_ERR_BAD_STATE) {
        // If the process has exited then the thread may have exited
        // ExceptionHandlerExchange already. Check.
        if (tu_thread_is_dying_or_dead(thread)) {
            tu_handle_close(thread);
            return true;
        }
    }
    tu_handle_close(thread);
    ASSERT_EQ(status, ZX_OK, "zx_task_resume failed");

    END_HELPER;
}

bool shutdown_inferior(zx_handle_t channel, zx_handle_t inferior) {
    BEGIN_HELPER;

    unittest_printf("Shutting down inferior\n");

    send_msg(channel, MSG_DONE);

    tu_process_wait_signaled(inferior);
    EXPECT_EQ(tu_process_get_return_code(inferior), 1234, "unexpected inferior return code");

    END_HELPER;
}

// Wait for and read an exception/signal on |eport|.

bool read_exception(zx_handle_t eport, zx_port_packet_t* packet) {
    BEGIN_HELPER;

    unittest_printf("Waiting for exception/signal on eport %d\n", eport);
    ASSERT_EQ(zx_port_wait(eport, ZX_TIME_INFINITE, packet, 1), ZX_OK, "zx_port_wait failed");

    if (ZX_PKT_IS_EXCEPTION(packet->type))
        ASSERT_EQ(packet->key, exception_port_key);

    unittest_printf("read_exception: got exception/signal %d\n", packet->type);

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

    while (true) {
        zx_port_packet_t packet;
        zx_status_t status = zx_port_wait(eport, zx_deadline_after(ZX_SEC(1)), &packet, 1);
        if (status == ZX_ERR_TIMED_OUT) {
            // This shouldn't really happen unless the system is really loaded.
            // Just flag it and try again. The watchdog will catch failures.
            unittest_printf("%s: timed out???\n", __func__);
            continue;
        }
        ASSERT_EQ(status, ZX_OK);
        if (ZX_PKT_IS_SIGNAL_REP(packet.type)) {
            ASSERT_EQ(packet.key, tid);
            if (packet.signal.observed & ZX_THREAD_SUSPENDED)
                break;
            ASSERT_TRUE(packet.signal.observed & ZX_THREAD_RUNNING);
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
                status = zx_task_resume(other_thread, ZX_RESUME_EXCEPTION);
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

static int phdr_info_callback(dl_phdr_info* info, size_t size, void* data) {
    dl_phdr_info* key = static_cast<dl_phdr_info*>(data);
    if (info->dlpi_addr == key->dlpi_addr) {
        *key = *info;
        return 1;
    }
    return 0;
}

// Fetch the [inclusive] range of the executable segment of the vdso.

bool get_vdso_exec_range(uintptr_t* start, uintptr_t* end) {
    BEGIN_HELPER;

    char msg[128];

    uintptr_t prop_vdso_base;
    zx_status_t status =
        zx_object_get_property(zx_process_self(), ZX_PROP_PROCESS_VDSO_BASE_ADDRESS,
                               &prop_vdso_base, sizeof(prop_vdso_base));
    snprintf(msg, sizeof(msg), "zx_object_get_property failed: %d", status);
    ASSERT_EQ(status, 0, msg);

    dl_phdr_info info;
    info.dlpi_addr = prop_vdso_base;
    int ret = dl_iterate_phdr(&phdr_info_callback, &info);
    ASSERT_EQ(ret, 1, "dl_iterate_phdr didn't see vDSO?");

    uintptr_t vdso_code_start = 0;
    size_t vdso_code_len = 0;
    for (unsigned i = 0; i < info.dlpi_phnum; ++i) {
        if (info.dlpi_phdr[i].p_type == PT_LOAD && (info.dlpi_phdr[i].p_flags & PF_X)) {
            vdso_code_start = info.dlpi_addr + info.dlpi_phdr[i].p_vaddr;
            vdso_code_len = info.dlpi_phdr[i].p_memsz;
            break;
        }
    }
    ASSERT_NE(vdso_code_start, 0u, "vDSO has no code segment?");
    ASSERT_NE(vdso_code_len, 0u, "vDSO has no code segment?");

    *start = vdso_code_start;
    *end = vdso_code_start + vdso_code_len - 1;

    END_HELPER;
}
