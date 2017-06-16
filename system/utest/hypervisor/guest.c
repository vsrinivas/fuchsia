// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <limits.h>
#include <threads.h>

#include <hypervisor/decode.h>
#include <hypervisor/guest.h>
#include <magenta/process.h>
#include <magenta/syscalls.h>
#include <magenta/syscalls/hypervisor.h>
#include <magenta/types.h>
#include <unittest/unittest.h>

static const uint64_t kVmoSize = 2 << 20;

extern const char guest_start[];
extern const char guest_end[];

extern const char guest_set_gpr_start[];
extern const char guest_set_gpr_end[];

extern const char guest_mem_trap_start[];
extern const char guest_mem_trap_end[];

typedef struct test {
    bool supported;

    mx_handle_t hypervisor;
    mx_handle_t guest_phys_mem;
    mx_handle_t guest_ctl_fifo;
    mx_handle_t guest;

#if __x86_64__
    mx_handle_t guest_apic_mem;
#endif // __x86_64__
} test_t;


static bool setup(test_t *test, const char *start, const char *end) {
    mx_status_t status = mx_hypervisor_create(MX_HANDLE_INVALID, 0,
                                              &test->hypervisor);
    test->supported = status != MX_ERR_NOT_SUPPORTED;
    if (!test->supported) {
        return true;
    }
    ASSERT_EQ(status, MX_OK, "");

    uintptr_t addr;
    ASSERT_EQ(guest_create_phys_mem(&addr, kVmoSize, &test->guest_phys_mem),
              MX_OK, "");

    ASSERT_EQ(guest_create(test->hypervisor, test->guest_phys_mem,
                           &test->guest_ctl_fifo, &test->guest), MX_OK, "");

    // Setup the guest.
    uintptr_t guest_ip = 0;
    ASSERT_EQ(guest_create_page_table(addr, kVmoSize, &guest_ip),
              MX_OK, "");

#if __x86_64__
    memcpy((void*)(addr + guest_ip), start, end - start);
    uintptr_t guest_cr3 = 0;
    ASSERT_EQ(mx_hypervisor_op(test->guest, MX_HYPERVISOR_OP_GUEST_SET_ENTRY_CR3,
                               &guest_cr3, sizeof(guest_cr3), NULL, 0),
              MX_OK, "");

    ASSERT_EQ(mx_vmo_create(PAGE_SIZE, 0, &test->guest_apic_mem), MX_OK, "");
    ASSERT_EQ(mx_hypervisor_op(test->guest, MX_HYPERVISOR_OP_GUEST_SET_APIC_MEM,
                               &test->guest_apic_mem,
                               sizeof(test->guest_apic_mem), NULL, 0),
              MX_OK, "");
#endif // __x86_64__

    ASSERT_EQ(mx_hypervisor_op(test->guest, MX_HYPERVISOR_OP_GUEST_SET_ENTRY_IP,
                               &guest_ip, sizeof(guest_ip), NULL, 0),
              MX_OK, "");

    return true;
}

static bool teardown(test_t *test) {
    ASSERT_EQ(mx_handle_close(test->guest), MX_OK, "");
    ASSERT_EQ(mx_handle_close(test->guest_ctl_fifo), MX_OK, "");
    ASSERT_EQ(mx_handle_close(test->guest_phys_mem), MX_OK, "");
    ASSERT_EQ(mx_handle_close(test->hypervisor), MX_OK, "");

#if __x86_64__
    ASSERT_EQ(mx_handle_close(test->guest_apic_mem), MX_OK, "");
#endif // __x86_64__

    return true;
}

static bool guest_enter(void) {
    BEGIN_TEST;

    test_t test;
    ASSERT_TRUE(setup(&test, guest_start, guest_end), "");
    if (!test.supported) {
        // The hypervisor isn't supported, so don't run the test.
        return true;
    }

    // Enter the guest.
    ASSERT_EQ(mx_hypervisor_op(test.guest, MX_HYPERVISOR_OP_GUEST_ENTER,
                               NULL, 0, NULL, 0),
              MX_ERR_STOP, "");

    mx_guest_packet_t packet[2];
    uint32_t num_packets;
    ASSERT_EQ(mx_fifo_read(test.guest_ctl_fifo, packet, sizeof(packet), &num_packets),
              MX_OK, "");
    EXPECT_EQ(num_packets, 2u, "");
    EXPECT_EQ(packet[0].type, MX_GUEST_PKT_TYPE_PORT_OUT, "");
    EXPECT_EQ(packet[0].port_out.access_size, 1u, "");
    EXPECT_EQ(packet[0].port_out.data[0], 'm', "");
    EXPECT_EQ(packet[1].type, MX_GUEST_PKT_TYPE_PORT_OUT, "");
    EXPECT_EQ(packet[1].port_out.access_size, 1u, "");
    EXPECT_EQ(packet[1].port_out.data[0], 'x', "");

    ASSERT_TRUE(teardown(&test), "");

    END_TEST;
}

static bool guest_get_set_gpr(void) {
    BEGIN_TEST;

    test_t test;
    ASSERT_TRUE(setup(&test, guest_set_gpr_start, guest_set_gpr_end), "");
    if (!test.supported) {
        // The hypervisor isn't supported, so don't run the test.
        return true;
    }

    mx_guest_gpr_t guest_gpr = {
#if __x86_64__
        .rax = 1u,
        .rcx = 2u,
        .rdx = 3u,
        .rbx = 4u,
        .rsp = 5u,
        .rbp = 6u,
        .rsi = 7u,
        .rdi = 8u,
        .r8 = 9u,
        .r9 = 10u,
        .r10 = 11u,
        .r11 = 12u,
        .r12 = 13u,
        .r13 = 14u,
        .r14 = 15u,
        .r15 = 16u,
        .flags = 0
#endif // __x86_64__
    };

    ASSERT_EQ(mx_hypervisor_op(test.guest, MX_HYPERVISOR_OP_GUEST_SET_GPR,
                               &guest_gpr, sizeof(guest_gpr), NULL, 0),
              MX_OK, "");

    // Enter the guest.
    ASSERT_EQ(mx_hypervisor_op(test.guest, MX_HYPERVISOR_OP_GUEST_ENTER,
                               NULL, 0, NULL, 0),
              MX_ERR_STOP, "");

    ASSERT_EQ(mx_hypervisor_op(test.guest, MX_HYPERVISOR_OP_GUEST_GET_GPR,
                               NULL, 0, &guest_gpr, sizeof(guest_gpr)),
              MX_OK, "");

#if __x86_64__
    EXPECT_EQ(guest_gpr.rax, 2u, "");
    EXPECT_EQ(guest_gpr.rcx, 4u, "");
    EXPECT_EQ(guest_gpr.rdx, 6u, "");
    EXPECT_EQ(guest_gpr.rbx, 8u, "");
    EXPECT_EQ(guest_gpr.rsp, 10u, "");
    EXPECT_EQ(guest_gpr.rbp, 12u, "");
    EXPECT_EQ(guest_gpr.rsi, 14u, "");
    EXPECT_EQ(guest_gpr.rdi, 16u, "");
    EXPECT_EQ(guest_gpr.r8, 18u, "");
    EXPECT_EQ(guest_gpr.r9, 20u, "");
    EXPECT_EQ(guest_gpr.r10, 22u, "");
    EXPECT_EQ(guest_gpr.r11, 24u, "");
    EXPECT_EQ(guest_gpr.r12, 26u, "");
    EXPECT_EQ(guest_gpr.r13, 28u, "");
    EXPECT_EQ(guest_gpr.r14, 30u, "");
    EXPECT_EQ(guest_gpr.r15, 32u, "");
    EXPECT_EQ(guest_gpr.flags, 1u, "");
#endif // __x86_64__

    ASSERT_TRUE(teardown(&test), "");

    END_TEST;
}

// Returns whether the mem trap object contains the expected values.
static bool verify_mem_trap_packet(test_t *test,
                                   const mx_guest_mem_trap_t* mem_trap) {
#if __x86_64__
    mx_guest_gpr_t guest_gpr;

    instruction_t inst;
    mx_status_t status = inst_decode(mem_trap->instruction_buffer,
                                     mem_trap->instruction_length,
                                     &guest_gpr, &inst);
    ASSERT_EQ(status, MX_OK, "");
    ASSERT_EQ(mem_trap->guest_paddr, kVmoSize - PAGE_SIZE, "");
    ASSERT_EQ(inst.type, INST_MOV_READ, "");
    ASSERT_EQ(inst.mem, 8u, "");
    ASSERT_EQ(inst.imm, 0u, "");
    ASSERT_EQ(inst.reg, &guest_gpr.rax, "");
    ASSERT_EQ(inst.flags, NULL, "");
#endif

    return true;
}

// Handles the expected mem trap guest packet.
static int mem_trap_handler_thread(void *arg) {
    test_t *test = (test_t *)arg;

    mx_signals_t observed;
    mx_status_t status = mx_object_wait_one(test->guest_ctl_fifo,
                                            MX_FIFO_READABLE | MX_FIFO_PEER_CLOSED,
                                            mx_deadline_after(MX_SEC(5)),
                                            &observed);
    if (status != MX_OK) {
        UNITTEST_TRACEF("failed waiting on fifo : error %d\n", status);
        exit(MX_ERR_INTERNAL);
    } else if (observed & MX_FIFO_PEER_CLOSED) {
        UNITTEST_TRACEF("fifo peer closed\n");
        exit(MX_ERR_INTERNAL);
    }
    mx_guest_packet_t packet;
    uint32_t num_entries;
    status = mx_fifo_read(test->guest_ctl_fifo, &packet, sizeof(packet),
                          &num_entries);
    if (status != MX_OK) {
        UNITTEST_TRACEF("failed reading from fifo : error %d\n", status);
        exit(MX_ERR_INTERNAL);
    } else if (num_entries != 1) {
        UNITTEST_TRACEF("invalid number of entries read : %d\n", num_entries);
        exit(MX_ERR_INTERNAL);
    } else if (packet.type != MX_GUEST_PKT_TYPE_MEM_TRAP) {
        UNITTEST_TRACEF("invalid packet type : %d\n", packet.type);
        exit(MX_ERR_INTERNAL);
    }

    bool is_valid_packet = verify_mem_trap_packet(test, &packet.mem_trap);

    // Resume the VM.
    mx_guest_packet_t resp_packet;
    resp_packet.type = MX_GUEST_PKT_TYPE_MEM_TRAP;
    resp_packet.mem_trap_ret.fault = false;
    uint32_t num_written;
    status = mx_fifo_write(test->guest_ctl_fifo, &resp_packet,
                           sizeof(resp_packet), &num_written);
    // Could not resume VM.
    if (status != MX_OK) {
        UNITTEST_TRACEF("failed writing to fifo : error %d\n", status);
        exit(MX_ERR_INTERNAL);
    } else if (num_written != 1) {
        UNITTEST_TRACEF("invalid number of entries written : %d\n", num_written);
        exit(MX_ERR_INTERNAL);
    }
    return is_valid_packet ? MX_OK : MX_ERR_INTERNAL;
}


static bool guest_mem_trap(void) {
    BEGIN_TEST;

    test_t test;
    ASSERT_TRUE(setup(&test, guest_mem_trap_start, guest_mem_trap_end), "");
    if (!test.supported) {
        // The hypervisor isn't supported, so don't run the test.
        return true;
    }
    // Unmap the last page from the EPT.
    uint64_t mem_trap_args[2] = { kVmoSize - PAGE_SIZE, PAGE_SIZE };
    ASSERT_EQ(mx_hypervisor_op(test.guest, MX_HYPERVISOR_OP_GUEST_MEM_TRAP,
                               mem_trap_args, sizeof(mem_trap_args), NULL, 0),
              MX_OK, "");

    thrd_t handler;
    ASSERT_EQ(thrd_create(&handler, mem_trap_handler_thread, &test),
              thrd_success, "");

    // Enter the guest.
    ASSERT_EQ(mx_hypervisor_op(test.guest, MX_HYPERVISOR_OP_GUEST_ENTER,
                               NULL, 0, NULL, 0),
              MX_ERR_STOP, "");

    int handler_ret;
    ASSERT_EQ(thrd_join(handler, &handler_ret), thrd_success, "");
    ASSERT_EQ(handler_ret, MX_OK, "");

    ASSERT_TRUE(teardown(&test), "");

    END_TEST;
}

BEGIN_TEST_CASE(guest)
RUN_TEST(guest_enter)
RUN_TEST(guest_get_set_gpr)
RUN_TEST(guest_mem_trap)
END_TEST_CASE(guest)

#ifndef BUILD_COMBINED_TESTS
int main(int argc, char** argv) {
    return unittest_run_all_tests(argc, argv) ? 0 : -1;
}
#endif
