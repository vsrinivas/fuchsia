// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <inttypes.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include <launchpad/launchpad.h>
#include <launchpad/vmo.h>
#include <magenta/processargs.h>
#include <magenta/syscalls.h>
#include <magenta/syscalls/debug.h>
#include <magenta/syscalls/port.h>
#include <test-utils/test-utils.h>
#include <unittest/unittest.h>

#include "utils.h"

// argv[0]
const char* program_path;

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
    if (status != NO_ERROR)
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
    uint64_t data;
    uint32_t num_bytes = sizeof(data);

    unittest_printf("waiting for message on handle %u\n", handle);

    ASSERT_TRUE(tu_wait_readable(handle), "peer closed while trying to read message");

    tu_channel_read(handle, 0, &data, &num_bytes, NULL, 0);
    ASSERT_EQ(num_bytes, sizeof(data), "unexpected message size");

    *msg = data;
    unittest_printf("received message %d\n", *msg);
    return true;
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

#define R(reg) { #reg, offsetof(mx_aarch64_general_regs_t, reg), 1, 64 }

static const regspec_t general_regs[] =
{
    { "r", offsetof(mx_aarch64_general_regs_t, r), 30, 64 },
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
    for (unsigned i = 0; i < sizeof(general_regs) / sizeof(general_regs[0]); ++i) {
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
    mx_status_t status;

    uint32_t num_regsets = get_uint32_property(thread, MX_PROP_NUM_STATE_KINDS);

    for (unsigned i = 0; i < num_regsets; ++i) {
        uint32_t regset_size = 0;
        status = mx_thread_read_state(thread, MX_THREAD_STATE_REGSET0 + i, NULL, regset_size, &regset_size);
        ASSERT_EQ(status, ERR_BUFFER_TOO_SMALL, "getting regset size failed");
        void* buf = tu_malloc(regset_size);
        status = mx_thread_read_state(thread, MX_THREAD_STATE_REGSET0 + i, buf, regset_size, &regset_size);
        ASSERT_EQ(status, NO_ERROR, "getting regset failed");
        dump_arch_regs(thread, i, buf);
        free(buf);
    }

    return true;
}

uint32_t get_inferior_greg_buf_size(mx_handle_t thread)
{
    // The general regs are defined to be in regset zero.
    uint32_t regset_size = 0;
    mx_status_t status = mx_thread_read_state(thread, MX_THREAD_STATE_REGSET0, NULL, regset_size, &regset_size);
    // It's easier to just terminate if this fails.
    if (status != ERR_BUFFER_TOO_SMALL)
        tu_fatal("get_inferior_greg_buf_size: mx_thread_read_state", status);
    return regset_size;
}

// N.B. It is assumed |buf_size| is large enough.

void read_inferior_gregs(mx_handle_t thread, void* buf, unsigned buf_size)
{
    // By convention the general regs are in regset 0.
    mx_status_t status = mx_thread_read_state(thread, MX_THREAD_STATE_REGSET0, buf, buf_size, &buf_size);
    // It's easier to just terminate if this fails.
    if (status != NO_ERROR)
        tu_fatal("read_inferior_gregs: mx_thread_read_state", status);
}

void write_inferior_gregs(mx_handle_t thread, const void* buf, unsigned buf_size)
{
    // By convention the general regs are in regset 0.
    mx_status_t status = mx_thread_write_state(thread, MX_THREAD_STATE_REGSET0, buf, buf_size);
    // It's easier to just terminate if this fails.
    if (status != NO_ERROR)
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

mx_size_t read_inferior_memory(mx_handle_t proc, uintptr_t vaddr, void* buf, mx_size_t len)
{
    mx_status_t status = mx_process_read_memory(proc, vaddr, buf, len, &len);
    if (status < 0)
        tu_fatal("read_inferior_memory", status);
    return len;
}

mx_size_t write_inferior_memory(mx_handle_t proc, uintptr_t vaddr, const void* buf, mx_size_t len)
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

    mx_status_t status = launchpad_create(0u, name, &lp);
    if (status == NO_ERROR) {
        status = launchpad_elf_load(lp, launchpad_vmo_from_file(filename));
        if (status == NO_ERROR)
            status = launchpad_load_vdso(lp, MX_HANDLE_INVALID);
        if (status == NO_ERROR)
            status = launchpad_add_vdso_vmo(lp);
        if (status == NO_ERROR)
            status = launchpad_arguments(lp, argc, argv);
        if (status == NO_ERROR)
            status = launchpad_environ(lp, envp);
        if (status == NO_ERROR)
            status = launchpad_add_all_mxio(lp);
        if (status == NO_ERROR)
            status = launchpad_add_handles(lp, hnds_count, handles, ids);
    }

    *out_launchpad = lp;
    return status;
}

bool setup_inferior(const char* name, mx_handle_t* out_channel, mx_handle_t* out_inferior, mx_handle_t* out_eport)
{
    mx_status_t status;
    mx_handle_t channel1, channel2;
    tu_channel_create(&channel1, &channel2);

    const char verbosity_string[] = { 'v', '=', utest_verbosity_level + '0', '\0' };
    const char* test_child_path = program_path;
    const char* const argv[] = { test_child_path, name, verbosity_string };
    mx_handle_t handles[1] = { channel2 };
    uint32_t handle_ids[1] = { MX_HND_TYPE_USER0 };

    launchpad_t* lp;
    unittest_printf("Starting process \"%s\"\n", name);
    status = create_inferior(name, ARRAY_SIZE(argv), argv, NULL,
                             ARRAY_SIZE(handles), handles, handle_ids, &lp);
    ASSERT_EQ(status, NO_ERROR, "failed to create inferior");

    mx_handle_t inferior = launchpad_get_process_handle(lp);
    ASSERT_GT(inferior, 0, "can't get launchpad process handle");

    mx_info_handle_basic_t process_info;
    tu_handle_get_basic_info(inferior, &process_info);
    unittest_printf("Inferior pid = %llu\n", (long long) process_info.rec.koid);

    // |inferior| is given to the child by launchpad_start.
    // We need our own copy, but we need it before launchpad_start returns.
    // So create our own copy.
    status = mx_handle_duplicate(inferior, MX_RIGHT_SAME_RIGHTS, &inferior);
    ASSERT_EQ(status, 0, "mx_handle_duplicate failed");

    // TODO(dje): Set the debug exception port when available.
    mx_handle_t eport = tu_io_port_create(0);
    status = mx_object_bind_exception_port(inferior, eport, 0, 0);
    EXPECT_EQ(status, NO_ERROR, "error setting exception port");
    unittest_printf("Attached to inferior\n");

    // launchpad_start returns a dup of |inferior|. The original inferior
    // handle is given to the child.
    mx_handle_t dup_inferior = launchpad_start(lp);
    ASSERT_GT(dup_inferior, 0, "launchpad_start failed");
    unittest_printf("Inferior started\n");
    tu_handle_close(dup_inferior);

    launchpad_destroy(lp);

    enum message msg;
    send_msg(channel1, MSG_PING);
    if (!recv_msg(channel1, &msg))
        return false;
    EXPECT_EQ(msg, MSG_PONG, "unexpected response from ping");

    *out_channel = channel1;
    *out_inferior = inferior;
    *out_eport = eport;
    return true;
}

bool shutdown_inferior(mx_handle_t channel, mx_handle_t inferior, mx_handle_t eport)
{
    unittest_printf("Shutting down inferior\n");

    send_msg(channel, MSG_DONE);
    tu_handle_close(channel);

    mx_koid_t tid;
    if (!read_and_verify_exception(eport, inferior, MX_EXCP_GONE, &tid))
        return false;
    EXPECT_EQ(tid, 0u, "reading of process gone notification");

    tu_wait_signaled(inferior);
    EXPECT_EQ(tu_process_get_return_code(inferior), 1234,
              "unexpected inferior return code");

    // TODO(dje): detach-on-close
    mx_status_t status =
        mx_object_bind_exception_port(inferior, MX_HANDLE_INVALID, 0,
                                      MX_EXCEPTION_PORT_DEBUGGER);
    EXPECT_EQ(status, NO_ERROR, "error resetting exception port");
    tu_handle_close(eport);
    tu_handle_close(inferior);

    return true;
}

// Wait for and receive an exception on |eport|.

bool read_exception(mx_handle_t eport, mx_exception_packet_t* packet)
{
    unittest_printf("Waiting for exception on eport %d\n", eport);
    ASSERT_EQ(mx_port_wait(eport, MX_TIME_INFINITE, packet, sizeof(*packet)), NO_ERROR, "mx_port_wait failed");
    ASSERT_EQ(packet->hdr.key, 0u, "bad report key");
    return true;
}

bool verify_exception(const mx_exception_packet_t* packet,
                      mx_handle_t process,
                      mx_excp_type_t expected_type,
                      mx_koid_t* tid)
{
    const mx_exception_report_t* report = &packet->report;

#ifdef __x86_64__
    if (MX_EXCP_IS_ARCH (report->header.type))
        unittest_printf("Received exception, vector 0x%" PRIx64 ", err_code 0x%" PRIx64 ", pc %p\n",
                        report->context.arch.u.x86_64.vector,
                        report->context.arch.u.x86_64.err_code,
                        (void*) report->context.arch.pc);
#endif

    EXPECT_EQ(report->header.type, expected_type, "bad exception type");

    // Verify the exception was from |process|.
    if (process != MX_HANDLE_INVALID) {
        mx_info_handle_basic_t process_info;
        tu_handle_get_basic_info(process, &process_info);
        ASSERT_EQ(process_info.rec.koid, report->context.pid, "wrong process in exception report");
    }

    unittest_printf("exception received: pid %"
                    PRIu64 ", tid %" PRIu64 "\n",
                    report->context.pid, report->context.tid);
    *tid = report->context.tid;
    return true;
}

bool read_and_verify_exception(mx_handle_t eport,
                               mx_handle_t process,
                               mx_excp_type_t expected_type,
                               mx_koid_t* tid)
{
    mx_exception_packet_t packet;
    if (!read_exception(eport, &packet))
        return false;
    return verify_exception(&packet, process, expected_type, tid);
}
