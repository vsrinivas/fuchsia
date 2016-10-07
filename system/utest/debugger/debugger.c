// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include <launchpad/launchpad.h>
#include <launchpad/vmo.h>
#include <magenta/processargs.h>
#include <magenta/syscalls.h>
#include <magenta/syscalls-debug.h>
#include <mxio/util.h>
#include <test-utils/test-utils.h>
#include <unittest/unittest.h>

#define ARRAY_SIZE(a) (sizeof(a) / sizeof(a[0]))

// 0.5 seconds
#define WATCHDOG_DURATION_TICK ((int64_t) 500 * 1000 * 1000)
// 5 seconds
#define WATCHDOG_DURATION_TICKS 10

#define TEST_MEMORY_SIZE 8
#define TEST_DATA_ADJUST 0x10

// Do the segv recovery test a number of times to stress test the API.
#define NUM_SEGV_TRIES 4

#define NUM_EXTRA_THREADS 4

// Produce a backtrace of sufficient size to be interesting but not excessive.
#define TEST_SEGFAULT_DEPTH 4

// argv[0]
static char* program_path;

static const char test_inferior_child_name[] = "inferior";
// The segfault child is not used by the test.
// It exists for debugging purposes.
static const char test_segfault_child_name[] = "segfault";

enum message {
    // Force the type to be signed, avoids mismatch clashes in unittest macros.
    MSG_FORCE_SIGNED = -1,
    MSG_DONE,
    MSG_PING,
    MSG_PONG,
    MSG_CRASH,
    MSG_RECOVERED_FROM_CRASH,
    MSG_START_EXTRA_THREADS,
    MSG_EXTRA_THREADS_STARTED,
};

static bool done_tests = false;

static uint32_t get_uint32(char* buf)
{
    uint32_t value = 0;
    memcpy(&value, buf, sizeof (value));
    return value;
}

static uint64_t get_uint64(char* buf)
{
    uint64_t value = 0;
    memcpy(&value, buf, sizeof (value));
    return value;
}

static void set_uint64(char* buf, uint64_t value)
{
    memcpy(buf, &value, sizeof (value));
}

static uint32_t get_uint32_property(mx_handle_t handle, uint32_t prop)
{
    uint32_t value;
    mx_status_t status = mx_object_get_property(handle, prop, &value, sizeof(value));
    if (status != NO_ERROR)
        tu_fatal("mx_object_get_property failed", status);
    return value;
}

static void send_msg(mx_handle_t handle, enum message msg)
{
    uint64_t data = msg;
    unittest_printf("sending message %d on handle %u\n", msg, handle);
    tu_message_write(handle, &data, sizeof(data), NULL, 0, 0);
}

// This returns "bool" because it uses ASSERT_*.

static bool recv_msg(mx_handle_t handle, enum message* msg)
{
    uint64_t data;
    uint32_t num_bytes = sizeof(data);

    unittest_printf("waiting for message on handle %u\n", handle);

    ASSERT_TRUE(tu_wait_readable(handle), "peer closed while trying to read message");

    tu_message_read(handle, &data, &num_bytes, NULL, 0, 0);
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

static void dump_gregs(mx_handle_t thread_handle, void* buf)
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

static void dump_arch_regs (mx_handle_t thread_handle, int regset, void* buf)
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

static bool dump_inferior_regs(mx_handle_t thread)
{
    mx_status_t status;

    uint32_t num_regsets = get_uint32_property(thread, MX_PROP_NUM_STATE_KINDS);

    for (unsigned i = 0; i < num_regsets; ++i) {
        uint32_t regset_size = 0;
        status = mx_thread_read_state(thread, MX_THREAD_STATE_REGSET0 + i, NULL, &regset_size);
        ASSERT_EQ(status, ERR_BUFFER_TOO_SMALL, "getting regset size failed");
        void* buf = tu_malloc(regset_size);
        status = mx_thread_read_state(thread, MX_THREAD_STATE_REGSET0 + i, buf, &regset_size);
        ASSERT_EQ(status, NO_ERROR, "getting regset failed");
        dump_arch_regs(thread, i, buf);
        free(buf);
    }

    return true;
}

static uint32_t get_inferior_greg_buf_size(mx_handle_t thread)
{
    // The general regs are defined to be in regset zero.
    uint32_t regset_size = 0;
    mx_status_t status = mx_thread_read_state(thread, MX_THREAD_STATE_REGSET0, NULL, &regset_size);
    // It's easier to just terminate if this fails.
    if (status != ERR_BUFFER_TOO_SMALL)
        tu_fatal("get_inferior_greg_buf_size: mx_thread_read_state", status);
    return regset_size;
}

// N.B. It is assumed |buf_size| is large enough.

static void read_inferior_gregs(mx_handle_t thread, void* buf, unsigned buf_size)
{
    // By convention the general regs are in regset 0.
    mx_status_t status = mx_thread_read_state(thread, MX_THREAD_STATE_REGSET0, buf, &buf_size);
    // It's easier to just terminate if this fails.
    if (status != NO_ERROR)
        tu_fatal("read_inferior_gregs: mx_thread_read_state", status);
}

static void write_inferior_gregs(mx_handle_t thread, const void* buf, unsigned buf_size)
{
    // By convention the general regs are in regset 0.
    mx_status_t status = mx_thread_write_state(thread, MX_THREAD_STATE_REGSET0, buf, buf_size);
    // It's easier to just terminate if this fails.
    if (status != NO_ERROR)
        tu_fatal("write_inferior_gregs: mx_thread_write_state", status);
}

// This assumes |regno| is in an array of uint64_t values.

static uint64_t get_uint64_register(mx_handle_t thread, size_t offset) {
    unsigned greg_buf_size = get_inferior_greg_buf_size(thread);
    char* buf = tu_malloc(greg_buf_size);
    read_inferior_gregs(thread, buf, greg_buf_size);
    uint64_t value = get_uint64(buf + offset);
    free(buf);
    return value;
}

// This assumes |regno| is in an array of uint64_t values.

static void set_uint64_register(mx_handle_t thread, size_t offset, uint64_t value) {
    unsigned greg_buf_size = get_inferior_greg_buf_size(thread);
    char* buf = tu_malloc(greg_buf_size);
    read_inferior_gregs(thread, buf, greg_buf_size);
    set_uint64(buf + offset, value);
    write_inferior_gregs(thread, buf, greg_buf_size);
    free(buf);
}

static mx_ssize_t read_inferior_memory(mx_handle_t proc, uintptr_t vaddr, void* buf, mx_size_t buf_size)
{
    mx_ssize_t bytes_read = mx_debug_read_memory(proc, vaddr, buf_size, buf);
    if (bytes_read < 0)
        tu_fatal("read_inferior_memory", bytes_read);
    return bytes_read;
}

static mx_ssize_t write_inferior_memory(mx_handle_t proc, uintptr_t vaddr, const void* buf, mx_size_t buf_size)
{
    mx_ssize_t bytes_written = mx_debug_write_memory(proc, vaddr, buf_size, buf);
    if (bytes_written < 0)
        tu_fatal("write_inferior_memory", bytes_written);
    return bytes_written;
}

static void test_memory_ops(mx_handle_t inferior, mx_handle_t thread)
{
    uint64_t test_data_addr = 0;
    mx_ssize_t ssize;
    uint8_t test_data[TEST_MEMORY_SIZE];

#ifdef __x86_64__
    test_data_addr = get_uint64_register(thread, offsetof(mx_x86_64_general_regs_t, r9));
#endif
#ifdef __aarch64__
    test_data_addr = get_uint64_register(thread, offsetof(mx_aarch64_general_regs_t, r[9]));
#endif

    ssize = read_inferior_memory(inferior, test_data_addr, test_data, sizeof(test_data));
    EXPECT_EQ(ssize, (mx_ssize_t) sizeof(test_data), "read_inferior_memory: short read");

    for (unsigned i = 0; i < sizeof(test_data); ++i) {
        EXPECT_EQ(test_data[i], i, "test_memory_ops");
    }

    for (unsigned i = 0; i < sizeof(test_data); ++i)
        test_data[i] += TEST_DATA_ADJUST;

    ssize = write_inferior_memory(inferior, test_data_addr, test_data, sizeof(test_data));
    EXPECT_EQ(ssize, (mx_ssize_t) sizeof(test_data), "write_inferior_memory: short write");

    // Note: Verification of the write is done in the inferior.
}

static void fix_inferior_segv(mx_handle_t thread)
{
    unittest_printf("Fixing inferior segv\n");

#ifdef __x86_64__
    // The segv was because r8 == 0, change it to a usable value.
    // See test_prep_and_segv.
    uint64_t rsp = get_uint64_register(thread, offsetof(mx_x86_64_general_regs_t, rsp));
    set_uint64_register(thread, offsetof(mx_x86_64_general_regs_t, r8), rsp);
#endif

#ifdef __aarch64__
    // The segv was because r8 == 0, change it to a usable value.
    // See test_prep_and_segv.
    uint64_t sp = get_uint64_register(thread, offsetof(mx_aarch64_general_regs_t, sp));
    set_uint64_register(thread, offsetof(mx_aarch64_general_regs_t, r[8]), sp);
#endif
}

// This exists so that we can use ASSERT_EQ which does a return on failure.

static bool wait_inferior_thread_worker(void* arg)
{
    mx_handle_t* args = arg;
    mx_handle_t inferior = args[0];
    mx_handle_t eport = args[1];
    int i;

    for (i = 0; i < NUM_SEGV_TRIES; ++i) {
        unittest_printf("wait-inf: waiting on inferior\n");

        mx_exception_packet_t packet;
        ASSERT_EQ(mx_port_wait(eport, &packet, sizeof(packet)), NO_ERROR, "mx_io_port_wait failed");
        unittest_printf("wait-inf: finished waiting, got exception 0x%x\n", packet.report.header.type);
        if (packet.report.header.type == MX_EXCP_GONE) {
            unittest_printf("wait-inf: inferior gone\n");
            break;
        } else if (MX_EXCP_IS_ARCH(packet.report.header.type)) {
            unittest_printf("wait-inf: got exception\n");
        } else {
            ASSERT_EQ(false, true, "wait-inf: unexpected exception type");
        }

        mx_koid_t tid = packet.report.context.tid;
        mx_handle_t thread = mx_debug_task_get_child(inferior, tid);
        ASSERT_GT(thread, 0, "mx_debug_task_get_child failed");

        dump_inferior_regs(thread);

        // Do some tests that require a suspended inferior.
        test_memory_ops(inferior, thread);

        // Now correct the issue and resume the inferior.

        fix_inferior_segv(thread);
        // Useful for debugging, otherwise a bit too verbose.
        //dump_inferior_regs(thread);

        mx_status_t status = mx_task_resume(thread, MX_RESUME_EXCEPTION);
        tu_handle_close(thread);
        ASSERT_EQ(status, NO_ERROR, "mx_task_resume failed");
    }

    ASSERT_EQ(i, NUM_SEGV_TRIES, "segv tests terminated prematurely");

    return true;
}

static int wait_inferior_thread_func(void* arg)
{
    wait_inferior_thread_worker(arg);

    // We have to call thread_exit ourselves.
    mx_thread_exit();
}

static int watchdog_thread_func(void* arg)
{
    for (int i = 0; i < WATCHDOG_DURATION_TICKS; ++i) {
        mx_nanosleep(WATCHDOG_DURATION_TICK);
        if (done_tests)
            mx_thread_exit();
    }
    unittest_printf("WATCHDOG TIMER FIRED\n");
    // This should kill the entire process, not just this thread.
    exit(5);
}

// This does everything that launchpad_launch_mxio_etc does except
// start the inferior. We want to attach to it first.
// TODO(dje): Are there other uses of such a wrapper? Move to launchpad?
// Plus there's a fair bit of code here. IWBN to not have to update it as
// launchpad_launch_mxio_etc changes.

static mx_status_t create_inferior(const char* name,
                                   int argc, const char* const* argv,
                                   const char* const* envp,
                                   size_t hnds_count, mx_handle_t* handles,
                                   uint32_t* ids, launchpad_t** out_launchpad)
{
    launchpad_t* lp = NULL;

    const char* filename = argv[0];
    if (name == NULL)
        name = filename;

    mx_status_t status = launchpad_create(name, &lp);
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

static bool setup_inferior(mx_handle_t* out_pipe, mx_handle_t* out_inferior, mx_handle_t* out_eport)
{
    mx_status_t status;
    mx_handle_t pipe1, pipe2;
    tu_message_pipe_create(&pipe1, &pipe2);

    const char verbosity_string[] = { 'v', '=', utest_verbosity_level + '0', '\0' };
    const char* test_child_path = program_path;
    const char* const argv[] = { test_child_path, test_inferior_child_name, verbosity_string };
    mx_handle_t handles[1] = { pipe2 };
    uint32_t handle_ids[1] = { MX_HND_TYPE_USER0 };

    launchpad_t* lp;
    unittest_printf("Starting process \"%s\"\n", test_inferior_child_name);
    status = create_inferior(test_inferior_child_name, ARRAY_SIZE(argv), argv, NULL,
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
    inferior = mx_handle_duplicate(inferior, MX_RIGHT_SAME_RIGHTS);
    ASSERT_GT(inferior, 0, "mx_handle_duplicate failed");

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
    send_msg(pipe1, MSG_PING);
    if (!recv_msg(pipe1, &msg))
        return false;
    EXPECT_EQ(msg, MSG_PONG, "unexpected response from ping");

    *out_pipe = pipe1;
    *out_inferior = inferior;
    *out_eport = eport;
    return true;
}

static bool shutdown_inferior(mx_handle_t pipe, mx_handle_t inferior, mx_handle_t eport)
{
    unittest_printf("Shutting down inferior\n");

    send_msg(pipe, MSG_DONE);
    tu_handle_close(pipe);

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

static bool debugger_test(void)
{
    BEGIN_TEST;

    mx_handle_t pipe, inferior, eport;

    if (!setup_inferior(&pipe, &inferior, &eport))
        return false;

    mx_handle_t wait_inf_args[2] = { inferior, eport };
    thrd_t wait_inferior_thread;
    tu_thread_create_c11(&wait_inferior_thread, wait_inferior_thread_func, (void*) &wait_inf_args[0], "wait-inf thread");

    enum message msg;
    send_msg(pipe, MSG_CRASH);
    if (!recv_msg(pipe, &msg))
        return false;
    EXPECT_EQ(msg, MSG_RECOVERED_FROM_CRASH, "unexpected response from crash");

    if (!shutdown_inferior(pipe, inferior, eport))
        return false;

    unittest_printf("Waiting for wait-inf thread\n");
    int ret = thrd_join(wait_inferior_thread, NULL);
    EXPECT_EQ(ret, thrd_success, "thrd_join failed");
    unittest_printf("wait-inf thread done\n");

    END_TEST;
}

static bool debugger_thread_list_test(void)
{
    BEGIN_TEST;

    mx_handle_t pipe, inferior, eport;

    if (!setup_inferior(&pipe, &inferior, &eport))
        return false;

    enum message msg;
    send_msg(pipe, MSG_START_EXTRA_THREADS);
    if (!recv_msg(pipe, &msg))
        return false;
    EXPECT_EQ(msg, MSG_EXTRA_THREADS_STARTED, "unexpected response when starting extra threads");

    uint32_t buf_size = sizeof(mx_info_process_threads_t) + 100 * sizeof(mx_record_process_thread_t);
    mx_info_process_threads_t* threads = tu_malloc(buf_size);
    mx_ssize_t size = mx_object_get_info(inferior, MX_INFO_PROCESS_THREADS, sizeof(mx_record_process_thread_t), threads, buf_size);

    // There should be at least 1+NUM_EXTRA_THREADS threads in the result.
    ASSERT_GE((size_t) size, sizeof(mx_info_header_t) + (1+NUM_EXTRA_THREADS) * sizeof(mx_record_process_thread_t), "mx_object_get_info failed");

    uint32_t num_threads = threads->hdr.count;

    // Verify each entry is valid.
    for (uint32_t i = 0; i < num_threads; ++i) {
        mx_koid_t koid = threads->rec[i].koid;
        unittest_printf("Looking up thread %llu\n", (long long) koid);
        mx_handle_t thread = mx_debug_task_get_child(inferior, koid);
        EXPECT_GT(thread, 0, "mx_debug_task_get_child failed");
        mx_info_handle_basic_t info;
        size = mx_object_get_info(thread, MX_INFO_HANDLE_BASIC, sizeof(mx_record_handle_basic_t), &info, sizeof(info));
        EXPECT_EQ((size_t) size, sizeof(info), "mx_object_get_info failed");
        EXPECT_EQ(info.rec.type, (uint32_t) MX_OBJ_TYPE_THREAD, "not a thread");
    }

    if (!shutdown_inferior(pipe, inferior, eport))
        return false;

    END_TEST;
}

// This function is marked as no-inline to avoid duplicate label in case the
// function call is being inlined.
__NO_INLINE static bool test_prep_and_segv(void)
{
    uint8_t test_data[TEST_MEMORY_SIZE];
    for (unsigned i = 0; i < sizeof(test_data); ++i)
        test_data[i] = i;

#ifdef __x86_64__
    void* segv_pc;
    // Note: Fuchsia is always pic.
    __asm__ ("movq .Lsegv_here@GOTPCREL(%%rip),%0" : "=r" (segv_pc));
    unittest_printf("About to segv, pc 0x%lx\n", (long) segv_pc);

    // Set r9 to point to test_data so we can easily access it
    // from the parent process.
    __asm__ ("\
	movq %0,%%r9\n\
	movq $0,%%r8\n\
.Lsegv_here:\n\
	movq (%%r8),%%rax\
"
        : : "r" (&test_data[0]) : "rax", "r8", "r9");
#endif

#ifdef __aarch64__
    void* segv_pc;
    // Note: Fuchsia is always pic.
    __asm__ ("mov %0,.Lsegv_here" : "=r" (segv_pc));
    unittest_printf("About to segv, pc 0x%lx\n", (long) segv_pc);

    // Set r9 to point to test_data so we can easily access it
    // from the parent process.
    __asm__ ("\
	mov x9,%0\n\
	mov x8,0\n\
.Lsegv_here:\n\
	ldr x0,[x8]\
"
        : : "r" (&test_data[0]) : "x0", "x8", "x9");
#endif

    // On resumption test_data should have had TEST_DATA_ADJUST added to each element.
    // Note: This is the inferior process, it's not running under the test harness.
    for (unsigned i = 0; i < sizeof(test_data); ++i) {
        if (test_data[i] != i + TEST_DATA_ADJUST) {
            unittest_printf("test_prep_and_segv: bad data on resumption, test_data[%u] = 0x%x\n",
                            i, test_data[i]);
            return false;
        }
    }

    unittest_printf("Inferior successfully resumed!\n");

    return true;
}

static int extra_thread_func(void* arg)
{
    unittest_printf("Extra thread started.\n");
    while (true)
        mx_nanosleep(1000 * 1000 * 1000);
    return 0;
}

// This returns "bool" because it uses ASSERT_*.

static bool msg_loop(mx_handle_t pipe)
{
    BEGIN_HELPER;

    bool my_done_tests = false;

    while (!done_tests && !my_done_tests)
    {
        enum message msg;
        ASSERT_TRUE(recv_msg(pipe, &msg), "Error while receiving msg");
        switch (msg)
        {
        case MSG_DONE:
            my_done_tests = true;
            break;
        case MSG_PING:
            send_msg(pipe, MSG_PONG);
            break;
        case MSG_CRASH:
            for (int i = 0; i < NUM_SEGV_TRIES; ++i) {
                if (!test_prep_and_segv())
                    exit(21);
            }
            send_msg(pipe, MSG_RECOVERED_FROM_CRASH);
            break;
        case MSG_START_EXTRA_THREADS:
            for (int i = 0; i < NUM_EXTRA_THREADS; ++i) {
                // For our purposes, we don't need to track the threads.
                // They'll be terminated when the process exits.
                thrd_t thread;
                tu_thread_create_c11(&thread, extra_thread_func, NULL, "extra-thread");
            }
            send_msg(pipe, MSG_EXTRA_THREADS_STARTED);
            break;
        default:
            unittest_printf("unknown message received: %d\n", msg);
            break;
        }
    }

    END_HELPER;
}

void test_inferior(void)
{
    mx_handle_t pipe = mxio_get_startup_handle(MX_HND_TYPE_USER0);
    unittest_printf("test_inferior: got handle %d\n", pipe);

    if (!msg_loop(pipe))
        exit(20);

    done_tests = true;
    unittest_printf("Inferior done\n");
    exit(1234);
}

// Compilers are getting too smart.
// These maintain the semantics we want even under optimization.

volatile int* crashing_ptr = (int*) 42;
volatile int crash_depth;

// This is used to cause fp != sp when the crash happens on arm64.
int leaf_stack_size = 10;

static int __NO_INLINE test_segfault_doit2(int*);

static int __NO_INLINE test_segfault_leaf(int n, int* p)
{
    volatile int x[n];
    x[0] = *p;
    *crashing_ptr = x[0];
    return 0;
}

static int __NO_INLINE test_segfault_doit1(int* p)
{
    if (crash_depth > 0)
    {
        int n = crash_depth;
        int use_stack[n];
        memset(use_stack, 0x99, n * sizeof(int));
        --crash_depth;
        return test_segfault_doit2(use_stack) + 99;
    }
    return test_segfault_leaf(leaf_stack_size, p) + 99;
}

static int __NO_INLINE test_segfault_doit2(int* p)
{
    return test_segfault_doit1(p) + *p;
}

// Produce a crash with a moderately interesting backtrace.

static int __NO_INLINE test_segfault(void)
{
    crash_depth = TEST_SEGFAULT_DEPTH;
    int i = 0;
    return test_segfault_doit1(&i);
}

BEGIN_TEST_CASE(debugger_tests)
RUN_TEST(debugger_test);
RUN_TEST(debugger_thread_list_test);
END_TEST_CASE(debugger_tests)

static void check_verbosity(int argc, char** argv)
{
    for (int i = 1; i < argc; ++i) {
        if (strncmp(argv[i], "v=", 2) == 0) {
            int verbosity = atoi(argv[i] + 2);
            unittest_set_verbosity_level(verbosity);
            break;
        }
    }
}

int main(int argc, char **argv)
{
    program_path = argv[0];

    if (argc >= 2 && strcmp(argv[1], test_inferior_child_name) == 0) {
        check_verbosity(argc, argv);
        test_inferior();
        return 0;
    }
    if (argc >= 2 && strcmp(argv[1], test_segfault_child_name) == 0) {
        check_verbosity(argc, argv);
        return test_segfault();
    }

    thrd_t watchdog_thread;
    tu_thread_create_c11(&watchdog_thread, watchdog_thread_func, NULL, "watchdog-thread");

    bool success = unittest_run_all_tests(argc, argv);

    done_tests = true;
    thrd_join(watchdog_thread, NULL);
    return success ? 0 : -1;
}
