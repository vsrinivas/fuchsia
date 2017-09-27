// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <limits.h>
#include <threads.h>

#include <hypervisor/decode.h>
#include <hypervisor/guest.h>
#include <zircon/process.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/hypervisor.h>
#include <zircon/syscalls/port.h>
#include <zircon/types.h>
#include <unittest/unittest.h>

#include "constants_priv.h"

static const uint64_t kTrapKey = 0x1234;

extern const char vcpu_resume_start[];
extern const char vcpu_resume_end[];
extern const char vcpu_read_write_state_start[];
extern const char vcpu_read_write_state_end[];
extern const char guest_set_trap_start[];
extern const char guest_set_trap_end[];
extern const char guest_set_trap_with_port_start[];
extern const char guest_set_trap_with_port_end[];

typedef struct test {
    bool supported;

    zx_handle_t guest_physmem;
    uintptr_t guest_physaddr;

    zx_handle_t vcpu;
    Guest guest;
#if __x86_64__
    zx_handle_t vcpu_apicmem;
#endif // __x86_64__
} test_t;

static bool teardown(test_t* test) {
    ASSERT_EQ(zx_handle_close(test->vcpu), ZX_OK);
#if __x86_64__
    ASSERT_EQ(zx_handle_close(test->vcpu_apicmem), ZX_OK);
#endif // __x86_64__

    return true;
}

static bool setup(test_t* test, const char* start, const char* end) {
    memset(test, 0, sizeof(*test));

    zx_status_t status = test->guest.Init(VMO_SIZE);

    test->supported = status != ZX_ERR_NOT_SUPPORTED;
    if (!test->supported) {
        fprintf(stderr, "Guest creation not supported\n");
        return teardown(test);
    }
    ASSERT_EQ(status, ZX_OK);

    // Setup the guest.
    uintptr_t guest_ip = 0;

#if __x86_64__
    // TODO(abdulla): Convert test exits to ZX_GUEST_TRAP_BELL, so that they
    // work for both x86-64 and arm64.
    ASSERT_EQ(zx_guest_set_trap(test->guest.handle(), ZX_GUEST_TRAP_IO, EXIT_TEST_PORT, 1,
                                ZX_HANDLE_INVALID, 0),
              ZX_OK);
    ASSERT_EQ(test->guest.CreatePageTable(&guest_ip), ZX_OK);
    memcpy((void*)(test->guest.phys_mem().addr() + guest_ip), start, end - start);
    ASSERT_EQ(zx_vmo_create(PAGE_SIZE, 0, &test->vcpu_apicmem), ZX_OK);
#endif // __x86_64__

    zx_vcpu_create_args_t args = {
        guest_ip,
#if __x86_64__
        0 /* cr3 */,
        test->vcpu_apicmem,
#endif // __x86_64__
    };
    status = zx_vcpu_create(test->guest.handle(), 0, &args, &test->vcpu);
    test->supported = status != ZX_ERR_NOT_SUPPORTED;
    if (!test->supported) {
        fprintf(stderr, "VCPU creation not supported\n");
        return teardown(test);
    }
    ASSERT_EQ(status, ZX_OK);

    return true;
}

static bool vcpu_resume(void) {
    BEGIN_TEST;

    test_t test;
    ASSERT_TRUE(setup(&test, vcpu_resume_start, vcpu_resume_end));
    if (!test.supported) {
        // The hypervisor isn't supported, so don't run the test.
        return true;
    }

    // Trap on writes to UART_PORT.
    ASSERT_EQ(zx_guest_set_trap(test.guest.handle(), ZX_GUEST_TRAP_IO, UART_PORT, 1,
                                ZX_HANDLE_INVALID, 0),
              ZX_OK);

    zx_port_packet_t packet = {};
    ASSERT_EQ(zx_vcpu_resume(test.vcpu, &packet), ZX_OK);
    EXPECT_EQ(packet.key, 0);
    EXPECT_EQ(packet.type, ZX_PKT_TYPE_GUEST_IO);
    EXPECT_EQ(packet.guest_io.port, UART_PORT);
    EXPECT_EQ(packet.guest_io.access_size, 1u);
    EXPECT_EQ(packet.guest_io.data[0], 'm');

    ASSERT_EQ(zx_vcpu_resume(test.vcpu, &packet), ZX_OK);
    EXPECT_EQ(packet.key, 0);
    EXPECT_EQ(packet.type, ZX_PKT_TYPE_GUEST_IO);
    EXPECT_EQ(packet.guest_io.port, UART_PORT);
    EXPECT_EQ(packet.guest_io.access_size, 1u);
    EXPECT_EQ(packet.guest_io.data[0], 'x');

    ASSERT_EQ(zx_vcpu_resume(test.vcpu, &packet), ZX_OK);
    EXPECT_EQ(packet.guest_io.port, EXIT_TEST_PORT);

    ASSERT_TRUE(teardown(&test));

    END_TEST;
}

static bool vcpu_read_write_state(void) {
    BEGIN_TEST;

    test_t test;
    ASSERT_TRUE(setup(&test, vcpu_read_write_state_start, vcpu_read_write_state_end));
    if (!test.supported) {
        // The hypervisor isn't supported, so don't run the test.
        return true;
    }

    zx_vcpu_state_t vcpu_state = {
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
        .rflags = 0
#endif // __x86_64__
    };

    ASSERT_EQ(zx_vcpu_write_state(test.vcpu, ZX_VCPU_STATE, &vcpu_state, sizeof(vcpu_state)),
              ZX_OK);

    zx_port_packet_t packet = {};
    ASSERT_EQ(zx_vcpu_resume(test.vcpu, &packet), ZX_OK);
    EXPECT_EQ(packet.guest_io.port, EXIT_TEST_PORT);

    ASSERT_EQ(zx_vcpu_read_state(test.vcpu, ZX_VCPU_STATE, &vcpu_state, sizeof(vcpu_state)), ZX_OK);

#if __x86_64__
    EXPECT_EQ(vcpu_state.rax, 2u);
    EXPECT_EQ(vcpu_state.rcx, 4u);
    EXPECT_EQ(vcpu_state.rdx, 6u);
    EXPECT_EQ(vcpu_state.rbx, 8u);
    EXPECT_EQ(vcpu_state.rsp, 10u);
    EXPECT_EQ(vcpu_state.rbp, 12u);
    EXPECT_EQ(vcpu_state.rsi, 14u);
    EXPECT_EQ(vcpu_state.rdi, 16u);
    EXPECT_EQ(vcpu_state.r8, 18u);
    EXPECT_EQ(vcpu_state.r9, 20u);
    EXPECT_EQ(vcpu_state.r10, 22u);
    EXPECT_EQ(vcpu_state.r11, 24u);
    EXPECT_EQ(vcpu_state.r12, 26u);
    EXPECT_EQ(vcpu_state.r13, 28u);
    EXPECT_EQ(vcpu_state.r14, 30u);
    EXPECT_EQ(vcpu_state.r15, 32u);
    EXPECT_EQ(vcpu_state.rflags, (1u << 0) | (1u << 18));
#endif // __x86_64__

    ASSERT_TRUE(teardown(&test));

    END_TEST;
}

static bool guest_set_trap(void) {
    BEGIN_TEST;

    test_t test;
    ASSERT_TRUE(setup(&test, guest_set_trap_start, guest_set_trap_end));
    if (!test.supported) {
        // The hypervisor isn't supported, so don't run the test.
        return true;
    }

    // Trap on access to the last page.
    ASSERT_EQ(zx_guest_set_trap(test.guest.handle(), ZX_GUEST_TRAP_MEM, VMO_SIZE - PAGE_SIZE,
                                PAGE_SIZE, ZX_HANDLE_INVALID, kTrapKey),
              ZX_OK);

    zx_port_packet_t packet = {};
    ASSERT_EQ(zx_vcpu_resume(test.vcpu, &packet), ZX_OK);
    ASSERT_EQ(packet.key, kTrapKey);
    ASSERT_EQ(packet.type, ZX_PKT_TYPE_GUEST_MEM);

#if __x86_64__
    zx_vcpu_state_t vcpu_state;
    instruction_t inst;
    EXPECT_EQ(inst_decode(packet.guest_mem.inst_buf, packet.guest_mem.inst_len, &vcpu_state, &inst),
              ZX_OK);
    EXPECT_EQ(packet.guest_mem.addr, VMO_SIZE - PAGE_SIZE);
    EXPECT_EQ(inst.type, INST_MOV_READ);
    EXPECT_EQ(inst.mem, 8u);
    EXPECT_EQ(inst.imm, 0u);
    EXPECT_EQ(inst.reg, &vcpu_state.rax);
    EXPECT_NULL(inst.flags);
#endif

    ASSERT_TRUE(teardown(&test));

    END_TEST;
}

static bool guest_set_trap_with_port(void) {
    BEGIN_TEST;

    test_t test;
    ASSERT_TRUE(setup(&test, guest_set_trap_with_port_start, guest_set_trap_with_port_end));
    if (!test.supported) {
        // The hypervisor isn't supported, so don't run the test.
        return true;
    }

    zx_handle_t port;
    ASSERT_EQ(zx_port_create(0, &port), ZX_OK);

    // Trap on writes to TRAP_PORT.
    ASSERT_EQ(
        zx_guest_set_trap(test.guest.handle(), ZX_GUEST_TRAP_IO, TRAP_PORT, 1, port, kTrapKey),
        ZX_OK);

    zx_port_packet_t packet = {};
    ASSERT_EQ(zx_vcpu_resume(test.vcpu, &packet), ZX_OK);
    EXPECT_EQ(packet.type, ZX_PKT_TYPE_GUEST_IO);
    EXPECT_EQ(packet.guest_io.port, EXIT_TEST_PORT);

    ASSERT_EQ(zx_port_wait(port, ZX_TIME_INFINITE, &packet, 0), ZX_OK);
    ASSERT_EQ(packet.key, kTrapKey);
    EXPECT_EQ(packet.type, ZX_PKT_TYPE_GUEST_IO);
    EXPECT_EQ(packet.guest_io.port, TRAP_PORT);

    EXPECT_EQ(zx_handle_close(port), ZX_OK);
    ASSERT_TRUE(teardown(&test));

    END_TEST;
}

static bool guest_set_trap_with_bell(void) {
    BEGIN_TEST;

    test_t test;
    ASSERT_TRUE(setup(&test, guest_set_trap_start, guest_set_trap_end));
    if (!test.supported) {
        // The hypervisor isn't supported, so don't run the test.
        return true;
    }

    zx_handle_t port;
    ASSERT_EQ(zx_port_create(0, &port), ZX_OK);

    // Trap on access to the last page.
    ASSERT_EQ(zx_guest_set_trap(test.guest.handle(), ZX_GUEST_TRAP_BELL, VMO_SIZE - PAGE_SIZE,
                                PAGE_SIZE, port, kTrapKey),
              ZX_OK);

    zx_port_packet_t packet = {};
    ASSERT_EQ(zx_vcpu_resume(test.vcpu, &packet), ZX_OK);
    EXPECT_EQ(packet.type, ZX_PKT_TYPE_GUEST_IO);
    EXPECT_EQ(packet.guest_io.port, EXIT_TEST_PORT);

    ASSERT_EQ(zx_port_wait(port, ZX_TIME_INFINITE, &packet, 0), ZX_OK);
    ASSERT_EQ(packet.key, kTrapKey);
    EXPECT_EQ(packet.type, ZX_PKT_TYPE_GUEST_BELL);
    EXPECT_EQ(packet.guest_bell.addr, VMO_SIZE - PAGE_SIZE);

    EXPECT_EQ(zx_handle_close(port), ZX_OK);
    ASSERT_TRUE(teardown(&test));

    END_TEST;
}

BEGIN_TEST_CASE(guest)
RUN_TEST(vcpu_resume)
RUN_TEST(vcpu_read_write_state)
RUN_TEST(guest_set_trap)
RUN_TEST(guest_set_trap_with_port)
RUN_TEST(guest_set_trap_with_bell)
END_TEST_CASE(guest)

#ifndef BUILD_COMBINED_TESTS
int main(int argc, char** argv) {
    return unittest_run_all_tests(argc, argv) ? 0 : -1;
}
#endif
