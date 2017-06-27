// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <limits.h>

#include <hypervisor/guest.h>
#include <magenta/syscalls.h>
#include <magenta/syscalls/hypervisor.h>
#include <unittest/unittest.h>

static const uint64_t kVmoSize = 2 << 20;

extern const char guest_start[];
extern const char guest_end[];

extern const char guest_set_gpr_start[];
extern const char guest_set_gpr_end[];

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

BEGIN_TEST_CASE(guest)
RUN_TEST(guest_enter)
RUN_TEST(guest_get_set_gpr)
END_TEST_CASE(guest)

#ifndef BUILD_COMBINED_TESTS
int main(int argc, char** argv) {
    return unittest_run_all_tests(argc, argv) ? 0 : -1;
}
#endif
