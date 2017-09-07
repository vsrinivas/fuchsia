// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <inttypes.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include <launchpad/launchpad.h>
#include <launchpad/vmo.h>
#include <magenta/compiler.h>
#include <magenta/processargs.h>
#include <magenta/syscalls.h>
#include <magenta/syscalls/debug.h>
#include <magenta/syscalls/object.h>
#include <magenta/syscalls/port.h>
#include <test-utils/test-utils.h>
#include <unittest/unittest.h>

#include "utils.h"

// argv[0]
const char* program_path;

static const uint64_t exception_port_key = 0x6b6579; // "key"

uint32_t get_uint32(char* buf)
{
    uint32_t value = 0;
    memcpy(&value, buf, sizeof (value));
    return value;
}

uint64_t get_uint64(char* buf)
{
    uint64_t value = 0;
    memcpy(&value, buf, sizeof (value));
    return value;
}

void set_uint64(char* buf, uint64_t value)
{
    memcpy(buf, &value, sizeof (value));
}

uint32_t get_uint32_property(mx_handle_t handle, uint32_t prop)
{
    uint32_t value;
    mx_status_t status = mx_object_get_property(handle, prop, &value, sizeof(value));
    if (status != MX_OK)
        tu_fatal("mx_object_get_property failed", status);
    return value;
}

void send_msg(mx_handle_t handle, enum message msg)
{
    uint64_t data = msg;
    unittest_printf("sending message %d on handle %u\n", msg, handle);
    tu_channel_write(handle, 0, &data, sizeof(data), NULL, 0);
}

// This returns "bool" because it uses ASSERT_*.

bool recv_msg(mx_handle_t handle, enum message* msg)
{
    BEGIN_HELPER;

    uint64_t data;
    uint32_t num_bytes = sizeof(data);

    unittest_printf("waiting for message on handle %u\n", handle);

    ASSERT_TRUE(tu_channel_wait_readable(handle), "peer closed while trying to read message");

    tu_channel_read(handle, 0, &data, &num_bytes, NULL, 0);
    ASSERT_EQ(num_bytes, sizeof(data), "unexpected message size");

    *msg = data;
    unittest_printf("received message %d\n", *msg);

    END_HELPER;
}

typedef struct {
    const char* name;
    unsigned offset;
    unsigned count;
    unsigned size;
} regspec_t;

#ifdef __x86_64__

#define R(reg) { #reg, offsetof(mx_x86_64_general_regs_t, reg), 1, 64 }

static const regspec_t general_regs[] =
{
    R(rax),
    R(rbx),
    R(rcx),
    R(rdx),
    R(rsi),
    R(rdi),
    R(rbp),
    R(rsp),
    R(r8),
    R(r9),
    R(r10),
    R(r11),
    R(r12),
    R(r13),
    R(r14),
    R(r15),
    R(rip),
    R(rflags),
};

#undef R

#endif

#ifdef __aarch64__

#define R(reg) { #reg, offsetof(mx_arm64_general_regs_t, reg), 1, 64 }

static const regspec_t general_regs[] =
{
    { "r", offsetof(mx_arm64_general_regs_t, r), 30, 64 },
    R(lr),
    R(sp),
    R(pc),
    R(cpsr),
};

#undef R

#endif

void dump_gregs(mx_handle_t thread_handle, void* buf)
{
#if defined(__x86_64__) || defined(__aarch64__)
    unittest_printf("Registers for thread %d\n", thread_handle);
    for (unsigned i = 0; i < countof(general_regs); ++i) {
        const regspec_t* r = &general_regs[i];
        uint64_t val;
        for (unsigned j = 0; j < r->count; ++j)
        {
            if (r->size == 32)
            {
                void* value_ptr = (char*) buf + r->offset + j * sizeof(uint32_t);
                val = get_uint32(value_ptr);
            }
            else
            {
                void* value_ptr = (char*) buf + r->offset + j * sizeof(uint64_t);
                val = get_uint64(value_ptr);
            }
            if (r->count == 1)
                unittest_printf("  %8s      %24ld  0x%lx\n", r->name, (long) val, (long) val);
            else
                unittest_printf("  %8s[%2u]  %24ld  0x%lx\n", r->name, j, (long) val, (long) val);
        }
    }
#endif
}

void dump_arch_regs (mx_handle_t thread_handle, int regset, void* buf)
{
    switch (regset)
    {
    case 0:
        dump_gregs(thread_handle, buf);
        break;
    default:
        break;
    }
}

bool dump_inferior_regs(mx_handle_t thread)
{
    BEGIN_HELPER;

    mx_status_t status;
    uint32_t num_regsets = get_uint32_property(thread, MX_PROP_NUM_STATE_KINDS);
    for (unsigned i = 0; i < num_regsets; ++i) {
        uint32_t regset_size = 0;
        status = mx_thread_read_state(thread, MX_THREAD_STATE_REGSET0 + i, NULL, regset_size, &regset_size);
        ASSERT_EQ(status, MX_ERR_BUFFER_TOO_SMALL, "getting regset size failed");
        void* buf = tu_malloc(regset_size);
        status = mx_thread_read_state(thread, MX_THREAD_STATE_REGSET0 + i, buf, regset_size, &regset_size);
        // Regset reads can fail for legitimate reasons:
        // MX_ERR_NOT_SUPPORTED - the regset is not supported on this chip
        // MX_ERR_UNAVAILABLE - the regset may be currently unavailable
        switch (status) {
        case MX_OK:
            dump_arch_regs(thread, i, buf);
            break;
        case MX_ERR_NOT_SUPPORTED:
            unittest_printf("Regset %u not supported\n", i);
            break;
        case MX_ERR_UNAVAILABLE:
            unittest_printf("Regset %u unavailable\n", i);
            break;
        default:
            ASSERT_EQ(status, MX_OK, "getting regset failed");
        }
        free(buf);
    }

    END_HELPER;
}

uint32_t get_inferior_greg_buf_size(mx_handle_t thread)
{
    // The general regs are defined to be in regset zero.
    uint32_t regset_size = 0;
    mx_status_t status = mx_thread_read_state(thread, MX_THREAD_STATE_REGSET0, NULL, regset_size, &regset_size);
    // It's easier to just terminate if this fails.
    if (status != MX_ERR_BUFFER_TOO_SMALL)
        tu_fatal("get_inferior_greg_buf_size: mx_thread_read_state", status);
    return regset_size;
}

// N.B. It is assumed |buf_size| is large enough.

void read_inferior_gregs(mx_handle_t thread, void* buf, unsigned buf_size)
{
    // By convention the general regs are in regset 0.
    mx_status_t status = mx_thread_read_state(thread, MX_THREAD_STATE_REGSET0, buf, buf_size, &buf_size);
    // It's easier to just terminate if this fails.
    if (status != MX_OK)
        tu_fatal("read_inferior_gregs: mx_thread_read_state", status);
}

void write_inferior_gregs(mx_handle_t thread, const void* buf, unsigned buf_size)
{
    // By convention the general regs are in regset 0.
    mx_status_t status = mx_thread_write_state(thread, MX_THREAD_STATE_REGSET0, buf, buf_size);
    // It's easier to just terminate if this fails.
    if (status != MX_OK)
        tu_fatal("write_inferior_gregs: mx_thread_write_state", status);
}

// This assumes |regno| is in an array of uint64_t values.

uint64_t get_uint64_register(mx_handle_t thread, size_t offset) {
    unsigned greg_buf_size = get_inferior_greg_buf_size(thread);
    char* buf = tu_malloc(greg_buf_size);
    read_inferior_gregs(thread, buf, greg_buf_size);
    uint64_t value = get_uint64(buf + offset);
    free(buf);
    return value;
}

// This assumes |regno| is in an array of uint64_t values.

void set_uint64_register(mx_handle_t thread, size_t offset, uint64_t value) {
    unsigned greg_buf_size = get_inferior_greg_buf_size(thread);
    char* buf = tu_malloc(greg_buf_size);
    read_inferior_gregs(thread, buf, greg_buf_size);
    set_uint64(buf + offset, value);
    write_inferior_gregs(thread, buf, greg_buf_size);
    free(buf);
}

size_t read_inferior_memory(mx_handle_t proc, uintptr_t vaddr, void* buf, size_t len)
{
    mx_status_t status = mx_process_read_memory(proc, vaddr, buf, len, &len);
    if (status < 0)
        tu_fatal("read_inferior_memory", status);
    return len;
}

size_t write_inferior_memory(mx_handle_t proc, uintptr_t vaddr, const void* buf, size_t len)
{
    mx_status_t status = mx_process_write_memory(proc, vaddr, buf, len, &len);
    if (status < 0)
        tu_fatal("write_inferior_memory", status);
    return len;
}

// This does everything that launchpad_launch_mxio_etc does except
// start the inferior. We want to attach to it first.
// TODO(dje): Are there other uses of such a wrapper? Move to launchpad?
// Plus there's a fair bit of code here. IWBN to not have to update it as
// launchpad_launch_mxio_etc changes.

mx_status_t create_inferior(const char* name,
                            int argc, const char* const* argv,
                            const char* const* envp,
                            size_t hnds_count, mx_handle_t* handles,
                            uint32_t* ids, launchpad_t** out_launchpad)
{
    launchpad_t* lp = NULL;

    const char* filename = argv[0];
    if (name == NULL)
        name = filename;

    mx_status_t status;
    launchpad_create(0u, name, &lp);
    launchpad_load_from_file(lp, filename);
    launchpad_set_args(lp, argc, argv);
    launchpad_set_environ(lp, envp);
    launchpad_clone(lp, LP_CLONE_MXIO_ALL);
    status = launchpad_add_handles(lp, hnds_count, handles, ids);

    if (status < 0) {
        launchpad_destroy(lp);
    } else {
        *out_launchpad = lp;
    }
    return status;
}

bool setup_inferior(const char* name, launchpad_t** out_lp, mx_handle_t* out_inferior, mx_handle_t* out_channel)
{
    BEGIN_HELPER;

    mx_status_t status;
    mx_handle_t channel1, channel2;
    tu_channel_create(&channel1, &channel2);

    const char verbosity_string[] = { 'v', '=', utest_verbosity_level + '0', '\0' };
    const char* test_child_path = program_path;
    const char* const argv[] = { test_child_path, name, verbosity_string };
    mx_handle_t handles[1] = { channel2 };
    uint32_t handle_ids[1] = { PA_USER0 };

    launchpad_t* lp;
    unittest_printf("Creating process \"%s\"\n", name);
    status = create_inferior(name, countof(argv), argv, NULL,
                             countof(handles), handles, handle_ids, &lp);
    ASSERT_EQ(status, MX_OK, "failed to create inferior");

    // Note: |inferior| is a borrowed handle here.
    mx_handle_t inferior = launchpad_get_process_handle(lp);
    ASSERT_NE(inferior, MX_HANDLE_INVALID, "can't get launchpad process handle");

    mx_info_handle_basic_t process_info;
    tu_handle_get_basic_info(inferior, &process_info);
    unittest_printf("Inferior pid = %llu\n", (long long) process_info.koid);

    // |inferior| is given to the child by launchpad_start.
    // We need our own copy, and launchpad_start will give us one, but we need
    // it before we call launchpad_start in order to attach to the debugging
    // exception port. We could leave this to our caller to do, but since every
    // caller needs this for convenience sake we do this here.
    status = mx_handle_duplicate(inferior, MX_RIGHT_SAME_RIGHTS, &inferior);
    ASSERT_EQ(status, MX_OK, "mx_handle_duplicate failed");

    *out_lp = lp;
    *out_inferior = inferior;
    *out_channel = channel1;

    END_HELPER;
}

// While this should perhaps take a launchpad_t* argument instead of the
// inferior's handle, we later want to test attaching to an already running
// inferior.

mx_handle_t attach_inferior(mx_handle_t inferior)
{
    mx_handle_t eport = tu_io_port_create();
    tu_set_exception_port(inferior, eport, exception_port_key, MX_EXCEPTION_PORT_DEBUGGER);
    unittest_printf("Attached to inferior\n");
    return eport;
}

bool start_inferior(launchpad_t* lp)
{
    mx_handle_t dup_inferior = tu_launch_mxio_fini(lp);
    unittest_printf("Inferior started\n");
    // launchpad_start returns a dup of |inferior|. The original inferior
    // handle is given to the child. However we don't need it, we already
    // created one so that we could attach to the inferior before starting it.
    tu_handle_close(dup_inferior);
    return true;
}

bool verify_inferior_running(mx_handle_t channel)
{
    BEGIN_HELPER;

    enum message msg;
    send_msg(channel, MSG_PING);
    if (!recv_msg(channel, &msg))
        return false;
    EXPECT_EQ(msg, MSG_PONG, "unexpected response from ping");

    END_HELPER;
}

bool resume_inferior(mx_handle_t inferior, mx_koid_t tid)
{
    BEGIN_HELPER;

    mx_handle_t thread;
    mx_status_t status = mx_object_get_child(inferior, tid, MX_RIGHT_SAME_RIGHTS, &thread);
    if (status == MX_ERR_NOT_FOUND) {
        // If the process has exited then the kernel may have reaped the
        // thread already. Check.
        if (tu_process_has_exited(inferior))
            return true;
    }
    ASSERT_EQ(status, MX_OK, "mx_object_get_child failed");

    unittest_printf("Resuming inferior ...\n");
    status = mx_task_resume(thread, MX_RESUME_EXCEPTION);
    tu_handle_close(thread);
    if (status == MX_ERR_BAD_STATE) {
        // If the process has exited then the thread may have exited
        // ExceptionHandlerExchange already. Check.
        if (tu_process_has_exited(inferior))
            return true;
    }
    ASSERT_EQ(status, MX_OK, "mx_task_resume failed");

    END_HELPER;
}

bool shutdown_inferior(mx_handle_t channel, mx_handle_t inferior)
{
    BEGIN_HELPER;

    unittest_printf("Shutting down inferior\n");

    send_msg(channel, MSG_DONE);

    tu_process_wait_signaled(inferior);
    EXPECT_EQ(tu_process_get_return_code(inferior), 1234,
              "unexpected inferior return code");

    END_HELPER;
}

// Wait for and receive an exception on |eport|.

bool read_exception(mx_handle_t eport, mx_handle_t inferior,
                    mx_port_packet_t* packet)
{
    BEGIN_HELPER;

    unittest_printf("Waiting for exception on eport %d\n", eport);
    ASSERT_EQ(mx_port_wait(eport, MX_TIME_INFINITE, packet, 0), MX_OK, "mx_port_wait failed");
    ASSERT_EQ(packet->key, exception_port_key, "bad report key");

    unittest_printf("read_exception: got exception %d\n", packet->type);

    END_HELPER;
}
