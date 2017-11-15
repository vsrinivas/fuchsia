// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <threads.h>

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
extern const char vcpu_interrupt_start[];
extern const char vcpu_interrupt_end[];
extern const char vcpu_read_write_state_start[];
extern const char vcpu_read_write_state_end[];
extern const char guest_set_trap_start[];
extern const char guest_set_trap_end[];
extern const char guest_set_trap_with_io_start[];
extern const char guest_set_trap_with_io_end[];

typedef struct test {
    bool supported = false;

    zx_handle_t vcpu = ZX_HANDLE_INVALID;
    Guest guest;
#if __x86_64__
    zx_handle_t vcpu_apicmem = ZX_HANDLE_INVALID;
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
    zx_status_t status = test->guest.Init(VMO_SIZE);

    test->supported = status != ZX_ERR_NOT_SUPPORTED;
    if (!test->supported) {
        fprintf(stderr, "Guest creation not supported\n");
        return teardown(test);
    }
    ASSERT_EQ(status, ZX_OK);

    ASSERT_EQ(zx_guest_set_trap(test->guest.handle(), ZX_GUEST_TRAP_BELL, EXIT_TEST_ADDR, PAGE_SIZE,
                                ZX_HANDLE_INVALID, 0),
              ZX_OK);

    // Setup the guest.
    uintptr_t guest_ip = 0;
#if __x86_64__
    ASSERT_EQ(test->guest.CreatePageTable(&guest_ip), ZX_OK);
    ASSERT_EQ(guest_ip, GUEST_IP);
    ASSERT_EQ(zx_vmo_create(PAGE_SIZE, 0, &test->vcpu_apicmem), ZX_OK);
#endif // __x86_64__
    memcpy((void*)(test->guest.phys_mem().addr() + guest_ip), start, end - start);

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

    zx_port_packet_t packet = {};
    ASSERT_EQ(zx_vcpu_resume(test.vcpu, &packet), ZX_OK);
    EXPECT_EQ(packet.type, ZX_PKT_TYPE_GUEST_BELL);
    EXPECT_EQ(packet.guest_bell.addr, EXIT_TEST_ADDR);

    ASSERT_TRUE(teardown(&test));

    END_TEST;
}

static bool vcpu_interrupt(void) {
    BEGIN_TEST;

    test_t test;
    ASSERT_TRUE(setup(&test, vcpu_interrupt_start, vcpu_interrupt_end));
    if (!test.supported) {
        // The hypervisor isn't supported, so don't run the test.
        return true;
    }

    thrd_t thread;
    int ret = thrd_create(&thread, [](void* ctx) -> int {
        test_t* test = static_cast<test_t*>(ctx);
        zx_nanosleep(zx_deadline_after(ZX_MSEC(100)));
        return zx_vcpu_interrupt(test->vcpu, 0);
    }, &test);
    ASSERT_EQ(ret, thrd_success);

    zx_port_packet_t packet = {};
    ASSERT_EQ(zx_vcpu_resume(test.vcpu, &packet), ZX_OK);
    EXPECT_EQ(packet.type, ZX_PKT_TYPE_GUEST_BELL);
    EXPECT_EQ(packet.guest_bell.addr, EXIT_TEST_ADDR);

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
#if __aarch64__
        // clang-format off
        .x = {
             0u,  1u,  2u,  3u,  4u,  5u,  6u,  7u,  8u,  9u,
            10u, 11u, 12u, 13u, 14u, 15u, 16u, 17u, 18u, 19u,
            20u, 21u, 22u, 23u, 24u, 25u, 26u, 27u, 28u, 29u,
            30u,
        },
        // clang-format on
        .sp = 64u,
        .cpsr = 0,
#elif __x86_64__
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
        .rflags = 0,
#endif
    };

    ASSERT_EQ(zx_vcpu_write_state(test.vcpu, ZX_VCPU_STATE, &vcpu_state, sizeof(vcpu_state)),
              ZX_OK);

    zx_port_packet_t packet = {};
    ASSERT_EQ(zx_vcpu_resume(test.vcpu, &packet), ZX_OK);
    EXPECT_EQ(packet.type, ZX_PKT_TYPE_GUEST_BELL);
    EXPECT_EQ(packet.guest_bell.addr, EXIT_TEST_ADDR);

    ASSERT_EQ(zx_vcpu_read_state(test.vcpu, ZX_VCPU_STATE, &vcpu_state, sizeof(vcpu_state)), ZX_OK);

#if __aarch64__
    EXPECT_EQ(vcpu_state.x[0], EXIT_TEST_ADDR);
    EXPECT_EQ(vcpu_state.x[1], 2u);
    EXPECT_EQ(vcpu_state.x[2], 4u);
    EXPECT_EQ(vcpu_state.x[3], 6u);
    EXPECT_EQ(vcpu_state.x[4], 8u);
    EXPECT_EQ(vcpu_state.x[5], 10u);
    EXPECT_EQ(vcpu_state.x[6], 12u);
    EXPECT_EQ(vcpu_state.x[7], 14u);
    EXPECT_EQ(vcpu_state.x[8], 16u);
    EXPECT_EQ(vcpu_state.x[9], 18u);
    EXPECT_EQ(vcpu_state.x[10], 20u);
    EXPECT_EQ(vcpu_state.x[11], 22u);
    EXPECT_EQ(vcpu_state.x[12], 24u);
    EXPECT_EQ(vcpu_state.x[13], 26u);
    EXPECT_EQ(vcpu_state.x[14], 28u);
    EXPECT_EQ(vcpu_state.x[15], 30u);
    EXPECT_EQ(vcpu_state.x[16], 32u);
    EXPECT_EQ(vcpu_state.x[17], 34u);
    EXPECT_EQ(vcpu_state.x[18], 36u);
    EXPECT_EQ(vcpu_state.x[19], 38u);
    EXPECT_EQ(vcpu_state.x[20], 40u);
    EXPECT_EQ(vcpu_state.x[21], 42u);
    EXPECT_EQ(vcpu_state.x[22], 44u);
    EXPECT_EQ(vcpu_state.x[23], 46u);
    EXPECT_EQ(vcpu_state.x[24], 48u);
    EXPECT_EQ(vcpu_state.x[25], 50u);
    EXPECT_EQ(vcpu_state.x[26], 52u);
    EXPECT_EQ(vcpu_state.x[27], 54u);
    EXPECT_EQ(vcpu_state.x[28], 56u);
    EXPECT_EQ(vcpu_state.x[29], 58u);
    EXPECT_EQ(vcpu_state.x[30], 60u);
    EXPECT_EQ(vcpu_state.sp, 128u);
    EXPECT_EQ(vcpu_state.cpsr, 0b0110 << 28);
#elif __x86_64__
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

static bool guest_set_trap_with_mem(void) {
    BEGIN_TEST;

    test_t test;
    ASSERT_TRUE(setup(&test, guest_set_trap_start, guest_set_trap_end));
    if (!test.supported) {
        // The hypervisor isn't supported, so don't run the test.
        return true;
    }

    // Trap on access of TRAP_ADDR.
    ASSERT_EQ(zx_guest_set_trap(test.guest.handle(), ZX_GUEST_TRAP_MEM, TRAP_ADDR, PAGE_SIZE,
                                ZX_HANDLE_INVALID, kTrapKey),
              ZX_OK);

    zx_port_packet_t packet = {};
    ASSERT_EQ(zx_vcpu_resume(test.vcpu, &packet), ZX_OK);
    EXPECT_EQ(packet.key, kTrapKey);
    EXPECT_EQ(packet.type, ZX_PKT_TYPE_GUEST_MEM);

    ASSERT_EQ(zx_vcpu_resume(test.vcpu, &packet), ZX_OK);
    EXPECT_EQ(packet.type, ZX_PKT_TYPE_GUEST_BELL);
    EXPECT_EQ(packet.guest_bell.addr, EXIT_TEST_ADDR);

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

    // Trap on access of TRAP_ADDR.
    ASSERT_EQ(zx_guest_set_trap(test.guest.handle(), ZX_GUEST_TRAP_BELL, TRAP_ADDR, PAGE_SIZE, port,
                                kTrapKey),
              ZX_OK);

    zx_port_packet_t packet = {};
    ASSERT_EQ(zx_vcpu_resume(test.vcpu, &packet), ZX_OK);
    EXPECT_EQ(packet.type, ZX_PKT_TYPE_GUEST_BELL);
    EXPECT_EQ(packet.guest_bell.addr, EXIT_TEST_ADDR);

    ASSERT_EQ(zx_port_wait(port, ZX_TIME_INFINITE, &packet, 0), ZX_OK);
    EXPECT_EQ(packet.key, kTrapKey);
    EXPECT_EQ(packet.type, ZX_PKT_TYPE_GUEST_BELL);
    EXPECT_EQ(packet.guest_bell.addr, TRAP_ADDR);

    EXPECT_EQ(zx_handle_close(port), ZX_OK);
    ASSERT_TRUE(teardown(&test));

    END_TEST;
}

static bool guest_set_trap_with_io(void) {
    BEGIN_TEST;

    test_t test;
    ASSERT_TRUE(setup(&test, guest_set_trap_with_io_start, guest_set_trap_with_io_end));
    if (!test.supported) {
        // The hypervisor isn't supported, so don't run the test.
        return true;
    }

    zx_handle_t port;
    ASSERT_EQ(zx_port_create(0, &port), ZX_OK);

    // Trap on writes to TRAP_PORT.
    ASSERT_EQ(zx_guest_set_trap(test.guest.handle(), ZX_GUEST_TRAP_IO, TRAP_PORT, 1, port,
                                kTrapKey),
              ZX_OK);

    zx_port_packet_t packet = {};
    ASSERT_EQ(zx_vcpu_resume(test.vcpu, &packet), ZX_OK);
    EXPECT_EQ(packet.type, ZX_PKT_TYPE_GUEST_BELL);
    EXPECT_EQ(packet.guest_bell.addr, EXIT_TEST_ADDR);

    ASSERT_EQ(zx_port_wait(port, ZX_TIME_INFINITE, &packet, 0), ZX_OK);
    ASSERT_EQ(packet.key, kTrapKey);
    EXPECT_EQ(packet.type, ZX_PKT_TYPE_GUEST_IO);
    EXPECT_EQ(packet.guest_io.port, TRAP_PORT);

    EXPECT_EQ(zx_handle_close(port), ZX_OK);
    ASSERT_TRUE(teardown(&test));

    END_TEST;
}

BEGIN_TEST_CASE(guest)
RUN_TEST(vcpu_resume)
RUN_TEST(vcpu_read_write_state)
RUN_TEST(vcpu_interrupt)
RUN_TEST(guest_set_trap_with_mem)
RUN_TEST(guest_set_trap_with_bell)
#if __x86_64__
RUN_TEST(guest_set_trap_with_io)
#endif
END_TEST_CASE(guest)

#ifndef BUILD_COMBINED_TESTS
int main(int argc, char** argv) {
    return unittest_run_all_tests(argc, argv) ? 0 : -1;
}
#endif
