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

static const uint8_t kExitTestPort = 0xff;
static const uint16_t kUartPort = 0x03f8;
static const uint32_t kMapFlags = MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE;
static const uint64_t kVmoSize = 2 << 20;

extern const char vcpu_resume_start[];
extern const char vcpu_resume_end[];
extern const char vcpu_read_write_state_start[];
extern const char vcpu_read_write_state_end[];
extern const char guest_set_trap_start[];
extern const char guest_set_trap_end[];

typedef struct test {
    bool supported;

    mx_handle_t guest;
    mx_handle_t guest_physmem;

    mx_handle_t vcpu;
#if __x86_64__
    mx_handle_t vcpu_apicmem;
#endif // __x86_64__
} test_t;


static bool setup(test_t *test, const char *start, const char *end) {
    ASSERT_EQ(mx_vmo_create(kVmoSize, 0, &test->guest_physmem), MX_OK, "");

    uintptr_t addr;
    ASSERT_EQ(mx_vmar_map(mx_vmar_root_self(), 0, test->guest_physmem, 0, kVmoSize, kMapFlags,
                          &addr),
              MX_OK, "");

    mx_handle_t resource;
    ASSERT_EQ(guest_get_resource(&resource), MX_OK, "");

    mx_status_t status = mx_guest_create(resource, 0, test->guest_physmem, &test->guest);
    test->supported = status != MX_ERR_NOT_SUPPORTED;
    if (!test->supported) {
        return true;
    }
    ASSERT_EQ(status, MX_OK, "");
    mx_handle_close(resource);

    // Setup the guest.
    uintptr_t guest_ip;
    ASSERT_EQ(guest_create_page_table(addr, kVmoSize, &guest_ip), MX_OK, "");

#if __x86_64__
    memcpy((void*)(addr + guest_ip), start, end - start);
    ASSERT_EQ(mx_vmo_create(PAGE_SIZE, 0, &test->vcpu_apicmem), MX_OK, "");
#endif // __x86_64__

    mx_vcpu_create_args_t args = {
        guest_ip,
#if __x86_64__
        0 /* cr3 */, test->vcpu_apicmem,
#endif // __x86_64__
    };
    status = mx_vcpu_create(test->guest, 0, &args, &test->vcpu);
    test->supported = status != MX_ERR_NOT_SUPPORTED;
    return true;
}

static bool teardown(test_t *test) {
    ASSERT_EQ(mx_handle_close(test->guest), MX_OK, "");
    ASSERT_EQ(mx_handle_close(test->guest_physmem), MX_OK, "");
    ASSERT_EQ(mx_handle_close(test->vcpu), MX_OK, "");

#if __x86_64__
    ASSERT_EQ(mx_handle_close(test->vcpu_apicmem), MX_OK, "");
#endif // __x86_64__

    return true;
}

static bool vcpu_resume(void) {
    BEGIN_TEST;

    test_t test;
    ASSERT_TRUE(setup(&test, vcpu_resume_start, vcpu_resume_end), "");
    if (!test.supported) {
        // The hypervisor isn't supported, so don't run the test.
        return true;
    }

    mx_guest_packet_t packet;
    ASSERT_EQ(mx_vcpu_resume(test.vcpu, &packet), MX_OK, "");
    EXPECT_EQ(packet.type, MX_GUEST_PKT_IO, "");
    EXPECT_EQ(packet.io.port, kUartPort, "");
    EXPECT_EQ(packet.io.access_size, 1u, "");
    EXPECT_EQ(packet.io.data[0], 'm', "");

    ASSERT_EQ(mx_vcpu_resume(test.vcpu, &packet), MX_OK, "");
    EXPECT_EQ(packet.io.port, kUartPort, "");
    EXPECT_EQ(packet.type, MX_GUEST_PKT_IO, "");
    EXPECT_EQ(packet.io.access_size, 1u, "");
    EXPECT_EQ(packet.io.data[0], 'x', "");

    ASSERT_EQ(mx_vcpu_resume(test.vcpu, &packet), MX_OK, "");
    EXPECT_EQ(packet.io.port, kExitTestPort, "");

    ASSERT_TRUE(teardown(&test), "");

    END_TEST;
}

static bool vcpu_read_write_state(void) {
    BEGIN_TEST;

    test_t test;
    ASSERT_TRUE(setup(&test, vcpu_read_write_state_start, vcpu_read_write_state_end), "");
    if (!test.supported) {
        // The hypervisor isn't supported, so don't run the test.
        return true;
    }

    mx_vcpu_state_t vcpu_state = {
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

    ASSERT_EQ(mx_vcpu_write_state(test.vcpu, MX_VCPU_STATE, &vcpu_state, sizeof(vcpu_state)),
              MX_OK, "");

    mx_guest_packet_t packet;
    ASSERT_EQ(mx_vcpu_resume(test.vcpu, &packet), MX_OK, "");
    EXPECT_EQ(packet.io.port, kExitTestPort, "");

    ASSERT_EQ(mx_vcpu_read_state(test.vcpu, MX_VCPU_STATE, &vcpu_state, sizeof(vcpu_state)),
              MX_OK, "");

#if __x86_64__
    EXPECT_EQ(vcpu_state.rax, 2u, "");
    EXPECT_EQ(vcpu_state.rcx, 4u, "");
    EXPECT_EQ(vcpu_state.rdx, 6u, "");
    EXPECT_EQ(vcpu_state.rbx, 8u, "");
    EXPECT_EQ(vcpu_state.rsp, 10u, "");
    EXPECT_EQ(vcpu_state.rbp, 12u, "");
    EXPECT_EQ(vcpu_state.rsi, 14u, "");
    EXPECT_EQ(vcpu_state.rdi, 16u, "");
    EXPECT_EQ(vcpu_state.r8, 18u, "");
    EXPECT_EQ(vcpu_state.r9, 20u, "");
    EXPECT_EQ(vcpu_state.r10, 22u, "");
    EXPECT_EQ(vcpu_state.r11, 24u, "");
    EXPECT_EQ(vcpu_state.r12, 26u, "");
    EXPECT_EQ(vcpu_state.r13, 28u, "");
    EXPECT_EQ(vcpu_state.r14, 30u, "");
    EXPECT_EQ(vcpu_state.r15, 32u, "");
    EXPECT_EQ(vcpu_state.flags, 1u, "");
#endif // __x86_64__

    ASSERT_TRUE(teardown(&test), "");

    END_TEST;
}

static bool guest_set_trap(void) {
    BEGIN_TEST;

    test_t test;
    ASSERT_TRUE(setup(&test, guest_set_trap_start, guest_set_trap_end), "");
    if (!test.supported) {
        // The hypervisor isn't supported, so don't run the test.
        return true;
    }

    // Unmap the last page from the EPT.
    ASSERT_EQ(mx_guest_set_trap(test.guest, MX_GUEST_TRAP_MEMORY, kVmoSize - PAGE_SIZE, PAGE_SIZE,
                                MX_HANDLE_INVALID),
              MX_OK, "");

    mx_guest_packet_t packet;
    ASSERT_EQ(mx_vcpu_resume(test.vcpu, &packet), MX_OK, "");

#if __x86_64__
    mx_vcpu_state_t vcpu_state;
    instruction_t inst;
    ASSERT_EQ(inst_decode(packet.memory.inst_buf, packet.memory.inst_len, &vcpu_state, &inst),
              MX_OK, "");
    ASSERT_EQ(packet.memory.addr, kVmoSize - PAGE_SIZE, "");
    ASSERT_EQ(inst.type, INST_MOV_READ, "");
    ASSERT_EQ(inst.mem, 8u, "");
    ASSERT_EQ(inst.imm, 0u, "");
    ASSERT_EQ(inst.reg, &vcpu_state.rax, "");
    ASSERT_EQ(inst.flags, NULL, "");
#endif

    ASSERT_TRUE(teardown(&test), "");

    END_TEST;
}

BEGIN_TEST_CASE(guest)
RUN_TEST(vcpu_resume)
RUN_TEST(vcpu_read_write_state)
RUN_TEST(guest_set_trap)
END_TEST_CASE(guest)

#ifndef BUILD_COMBINED_TESTS
int main(int argc, char** argv) {
    return unittest_run_all_tests(argc, argv) ? 0 : -1;
}
#endif
