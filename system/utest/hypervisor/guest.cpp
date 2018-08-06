// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <threads.h>
#include <unistd.h>

#include <fbl/unique_fd.h>
#include <lib/zx/guest.h>
#include <lib/zx/port.h>
#include <lib/zx/resource.h>
#include <lib/zx/vcpu.h>
#include <lib/zx/vmar.h>
#include <unittest/unittest.h>
#include <zircon/device/sysinfo.h>
#include <zircon/process.h>
#include <zircon/syscalls/hypervisor.h>
#include <zircon/syscalls/port.h>
#include <zircon/types.h>

#include "constants_priv.h"

static constexpr uint32_t kMapFlags = ZX_VM_FLAG_PERM_READ | ZX_VM_FLAG_PERM_WRITE;
static constexpr uint64_t kTrapKey = 0x1234;
static constexpr char kResourcePath[] = "/dev/misc/sysinfo";

extern const char vcpu_resume_start[];
extern const char vcpu_resume_end[];
extern const char vcpu_interrupt_start[];
extern const char vcpu_interrupt_end[];
extern const char vcpu_hlt_start[];
extern const char vcpu_hlt_end[];
extern const char vcpu_pause_start[];
extern const char vcpu_pause_end[];
extern const char vcpu_write_cr0_start[];
extern const char vcpu_write_cr0_end[];
extern const char vcpu_wfi_start[];
extern const char vcpu_wfi_end[];
extern const char vcpu_aarch32_wfi_start[];
extern const char vcpu_aarch32_wfi_end[];
extern const char vcpu_fp_start[];
extern const char vcpu_fp_end[];
extern const char vcpu_aarch32_fp_start[];
extern const char vcpu_aarch32_fp_end[];
extern const char vcpu_read_write_state_start[];
extern const char vcpu_read_write_state_end[];
extern const char vcpu_compat_mode_start[];
extern const char vcpu_compat_mode_end[];
extern const char vcpu_syscall_start[];
extern const char vcpu_syscall_end[];
extern const char vcpu_sysenter_start[];
extern const char vcpu_sysenter_end[];
extern const char vcpu_sysenter_compat_start[];
extern const char vcpu_sysenter_compat_end[];
extern const char guest_set_trap_start[];
extern const char guest_set_trap_end[];
extern const char guest_set_trap_with_io_start[];
extern const char guest_set_trap_with_io_end[];

enum {
    X86_PTE_P = 0x01,  // P    Valid
    X86_PTE_RW = 0x02, // R/W  Read/Write
    X86_PTE_U = 0x04,  // U    Page is user accessible
    X86_PTE_PS = 0x80, // PS   Page size
};

typedef struct test {
    bool supported = false;
    bool interrupts_enabled = false;

    zx::vmo vmo;
    uintptr_t addr;
    zx::guest guest;
    zx::vcpu vcpu;
} test_t;

static bool teardown(test_t* test) {
    BEGIN_HELPER;
    ASSERT_EQ(zx::vmar::root_self()->unmap(test->addr, VMO_SIZE), ZX_OK);
    return true;
    END_HELPER;
}

static zx_status_t guest_get_resource(zx::resource* resource) {
    fbl::unique_fd fd(open(kResourcePath, O_RDWR));
    if (!fd) {
        return ZX_ERR_IO;
    }
    ssize_t n = ioctl_sysinfo_get_hypervisor_resource(fd.get(), resource->reset_and_get_address());
    return n < 0 ? ZX_ERR_IO : ZX_OK;
}

static bool setup(test_t* test, const char* start, const char* end) {
    ASSERT_EQ(zx::vmo::create(VMO_SIZE, 0, &test->vmo), ZX_OK);
    ASSERT_EQ(zx::vmar::root_self()->map(0, test->vmo, 0, VMO_SIZE, kMapFlags, &test->addr), ZX_OK);

    zx::resource resource;
    ASSERT_EQ(guest_get_resource(&resource), ZX_OK);
    zx_status_t status = zx::guest::create(resource, 0, test->vmo, &test->guest);
    if (status != ZX_OK) {
        fprintf(stderr, "Failed to create guest\n");
        return status;
    }

    test->supported = status != ZX_ERR_NOT_SUPPORTED;
    if (!test->supported) {
        fprintf(stderr, "Guest creation not supported\n");
        return teardown(test);
    }
    ASSERT_EQ(status, ZX_OK);

    ASSERT_EQ(test->guest.set_trap(ZX_GUEST_TRAP_MEM, EXIT_TEST_ADDR, PAGE_SIZE,
                                   zx::port(), 0),
              ZX_OK);

    // Setup the guest.
    uintptr_t entry = 0;
#if __x86_64__
    // PML4 entry pointing to (addr + 0x1000)
    uint64_t* pte_off = reinterpret_cast<uint64_t*>(test->addr);
    *pte_off = PAGE_SIZE | X86_PTE_P | X86_PTE_U | X86_PTE_RW;
    // PDP entry with 1GB page.
    pte_off = reinterpret_cast<uint64_t*>(test->addr + PAGE_SIZE);
    *pte_off = X86_PTE_PS | X86_PTE_P | X86_PTE_U | X86_PTE_RW;
    entry = GUEST_ENTRY;
#endif // __x86_64__
    memcpy((void*)(test->addr + entry), start, end - start);

    status = zx::vcpu::create(test->guest, 0, entry, &test->vcpu);
    test->supported = status != ZX_ERR_NOT_SUPPORTED;
    if (!test->supported) {
        fprintf(stderr, "VCPU creation not supported\n");
        return teardown(test);
    }
    ASSERT_EQ(status, ZX_OK);

    return true;
}

static bool setup_and_interrupt(test_t* test, const char* start, const char* end) {
    ASSERT_TRUE(setup(test, start, end));
    if (!test->supported) {
        // The hypervisor isn't supported, so don't run the test.
        return true;
    }
    test->interrupts_enabled = true;

    thrd_t thread;
    int ret = thrd_create(&thread, [](void* ctx) -> int {
        test_t* test = static_cast<test_t*>(ctx);
        // Inject an interrupt with vector 32, the first user defined interrupt vector.
        return test->vcpu.interrupt(32) == ZX_OK ? thrd_success : thrd_error;
    },
                          test);
    ASSERT_EQ(ret, thrd_success);

    return true;
}

static inline bool exception_thrown(const zx_packet_guest_mem_t& guest_mem,
                                    const zx::vcpu& vcpu) {
#if __x86_64__
    if (guest_mem.inst_len != 12) {
        // Not the expected mov imm, (EXIT_TEST_ADDR) size.
        return true;
    }
    if (guest_mem.inst_buf[8] == 0 &&
        guest_mem.inst_buf[9] == 0 &&
        guest_mem.inst_buf[10] == 0 &&
        guest_mem.inst_buf[11] == 0) {
        return false;
    }
    zx_vcpu_state_t vcpu_state;
    if (vcpu.read_state(ZX_VCPU_STATE, &vcpu_state, sizeof(vcpu_state)) != ZX_OK) {
        return true;
    }
    // Print out debug values from the exception handler.
    fprintf(stderr, "Unexpected exception in guest\n");
    fprintf(stderr, "vector = %lu\n", vcpu_state.rax);
    fprintf(stderr, "error code = %lu\n", vcpu_state.rbx);
    fprintf(stderr, "rip = 0x%lx\n", vcpu_state.rcx);
    return true;
#else
    return false;
#endif
}

static inline bool resume_and_clean_exit(test_t* test) {
    BEGIN_HELPER;
    zx_port_packet_t packet = {};
    ASSERT_EQ(test->vcpu.resume(&packet), ZX_OK);
    EXPECT_EQ(packet.type, ZX_PKT_TYPE_GUEST_MEM);
    EXPECT_EQ(packet.guest_mem.addr, EXIT_TEST_ADDR);
#if __x86_64__
    EXPECT_EQ(packet.guest_mem.default_operand_size, 4u);
#endif
    if (test->interrupts_enabled) {
        ASSERT_FALSE(exception_thrown(packet.guest_mem, test->vcpu));
    }
    return true;
    END_HELPER;
}

static bool vcpu_resume() {
    BEGIN_TEST;

    test_t test;
    ASSERT_TRUE(setup(&test, vcpu_resume_start, vcpu_resume_end));
    if (!test.supported) {
        // The hypervisor isn't supported, so don't run the test.
        return true;
    }

    ASSERT_TRUE(resume_and_clean_exit(&test));
    ASSERT_TRUE(teardown(&test));

    END_TEST;
}

static bool vcpu_interrupt() {
    BEGIN_TEST;

    test_t test;
    ASSERT_TRUE(setup_and_interrupt(&test, vcpu_interrupt_start, vcpu_interrupt_end));
    if (!test.supported) {
        // The hypervisor isn't supported, so don't run the test.
        return true;
    }

    ASSERT_TRUE(resume_and_clean_exit(&test));
    ASSERT_TRUE(teardown(&test));

    END_TEST;
}

static bool vcpu_hlt() {
    BEGIN_TEST;

    test_t test;
    ASSERT_TRUE(setup_and_interrupt(&test, vcpu_hlt_start, vcpu_hlt_end));
    if (!test.supported) {
        // The hypervisor isn't supported, so don't run the test.
        return true;
    }

    ASSERT_TRUE(resume_and_clean_exit(&test));
    ASSERT_TRUE(teardown(&test));

    END_TEST;
}

static bool vcpu_pause() {
    BEGIN_TEST;

    test_t test;
    ASSERT_TRUE(setup(&test, vcpu_pause_start, vcpu_pause_end));
    if (!test.supported) {
        // The hypervisor isn't supported, so don't run the test.
        return true;
    }

    ASSERT_TRUE(resume_and_clean_exit(&test));
    ASSERT_TRUE(teardown(&test));

    END_TEST;
}

static bool vcpu_write_cr0() {
    BEGIN_TEST;

    test_t test;
    ASSERT_TRUE(setup(&test, vcpu_write_cr0_start, vcpu_write_cr0_end));
    if (!test.supported) {
        // The hypervisor isn't supported, so don't run the test.
        return true;
    }

    ASSERT_TRUE(resume_and_clean_exit(&test));

#if __x86_64__
    zx_vcpu_state_t vcpu_state;
    ASSERT_EQ(test.vcpu.read_state(ZX_VCPU_STATE, &vcpu_state, sizeof(vcpu_state)), ZX_OK);
    // Check that cr0 has the NE bit set when read.
    EXPECT_TRUE(vcpu_state.rax & X86_CR0_NE);
#endif

    ASSERT_TRUE(teardown(&test));

    END_TEST;
}

static bool vcpu_wfi() {
    BEGIN_TEST;

    test_t test;
    ASSERT_TRUE(setup(&test, vcpu_wfi_start, vcpu_wfi_end));
    if (!test.supported) {
        // The hypervisor isn't supported, so don't run the test.
        return true;
    }

    ASSERT_TRUE(resume_and_clean_exit(&test));
    ASSERT_TRUE(teardown(&test));

    END_TEST;
}

static bool vcpu_wfi_aarch32() {
    BEGIN_TEST;

    test_t test;
    ASSERT_TRUE(setup(&test, vcpu_aarch32_wfi_start, vcpu_aarch32_wfi_end));
    if (!test.supported) {
        // The hypervisor isn't supported, so don't run the test.
        return true;
    }

    zx_port_packet_t packet = {};
    ASSERT_EQ(test.vcpu.resume(&packet), ZX_OK);
    EXPECT_EQ(packet.type, ZX_PKT_TYPE_GUEST_MEM);
    EXPECT_EQ(packet.guest_mem.addr, EXIT_TEST_ADDR);
#if __aarch64__
    EXPECT_EQ(packet.guest_mem.read, false);
    EXPECT_EQ(packet.guest_mem.data, 0);
#endif // __aarch64__

    ASSERT_TRUE(teardown(&test));

    END_TEST;
}

static bool vcpu_fp() {
    BEGIN_TEST;

    test_t test;
    ASSERT_TRUE(setup(&test, vcpu_fp_start, vcpu_fp_end));
    if (!test.supported) {
        // The hypervisor isn't supported, so don't run the test.
        return true;
    }

    ASSERT_TRUE(resume_and_clean_exit(&test));
    ASSERT_TRUE(teardown(&test));

    END_TEST;
}

static bool vcpu_fp_aarch32() {
    BEGIN_TEST;

    test_t test;
    ASSERT_TRUE(setup(&test, vcpu_aarch32_fp_start, vcpu_aarch32_fp_end));
    if (!test.supported) {
        // The hypervisor isn't supported, so don't run the test.
        return true;
    }

    zx_port_packet_t packet = {};
    ASSERT_EQ(test.vcpu.resume(&packet), ZX_OK);
    EXPECT_EQ(packet.type, ZX_PKT_TYPE_GUEST_MEM);
    EXPECT_EQ(packet.guest_mem.addr, EXIT_TEST_ADDR);
#if __aarch64__
    EXPECT_EQ(packet.guest_mem.read, false);
    EXPECT_EQ(packet.guest_mem.data, 0);
#endif // __aarch64__

    ASSERT_TRUE(teardown(&test));

    END_TEST;
}

static bool vcpu_read_write_state() {
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

    ASSERT_EQ(test.vcpu.write_state(ZX_VCPU_STATE, &vcpu_state, sizeof(vcpu_state)),
              ZX_OK);

    ASSERT_TRUE(resume_and_clean_exit(&test));

    ASSERT_EQ(test.vcpu.read_state(ZX_VCPU_STATE, &vcpu_state, sizeof(vcpu_state)), ZX_OK);

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

static bool vcpu_compat_mode() {
    BEGIN_TEST;

    test_t test;
    ASSERT_TRUE(setup(&test, vcpu_compat_mode_start, vcpu_compat_mode_end));
    if (!test.supported) {
        // The hypervisor isn't supported, so don't run the test.
        return true;
    }

    ASSERT_TRUE(resume_and_clean_exit(&test));

    zx_vcpu_state_t vcpu_state;
    ASSERT_EQ(test.vcpu.read_state(ZX_VCPU_STATE, &vcpu_state, sizeof(vcpu_state)), ZX_OK);
#if __x86_64__
    EXPECT_EQ(vcpu_state.rbx, 1u);
    EXPECT_EQ(vcpu_state.rcx, 2u);
#endif

    ASSERT_TRUE(teardown(&test));

    END_TEST;
}

static bool vcpu_syscall() {
    BEGIN_TEST;

    test_t test;
    ASSERT_TRUE(setup(&test, vcpu_syscall_start, vcpu_syscall_end));
    if (!test.supported) {
        // The hypervisor isn't supported, so don't run the test.
        return true;
    }

    ASSERT_TRUE(resume_and_clean_exit(&test));
    ASSERT_TRUE(teardown(&test));

    END_TEST;
}

static bool vcpu_sysenter() {
    BEGIN_TEST;

    test_t test;
    ASSERT_TRUE(setup(&test, vcpu_sysenter_start, vcpu_sysenter_end));
    if (!test.supported) {
        // The hypervisor isn't supported, so don't run the test.
        return true;
    }

    ASSERT_TRUE(resume_and_clean_exit(&test));
    ASSERT_TRUE(teardown(&test));

    END_TEST;
}

static bool vcpu_sysenter_compat() {
    BEGIN_TEST;

    test_t test;
    ASSERT_TRUE(setup(&test, vcpu_sysenter_compat_start, vcpu_sysenter_compat_end));
    if (!test.supported) {
        // The hypervisor isn't supported, so don't run the test.
        return true;
    }

    ASSERT_TRUE(resume_and_clean_exit(&test));
    ASSERT_TRUE(teardown(&test));

    END_TEST;
}

static bool guest_set_trap_with_mem() {
    BEGIN_TEST;

    test_t test;
    ASSERT_TRUE(setup(&test, guest_set_trap_start, guest_set_trap_end));
    if (!test.supported) {
        // The hypervisor isn't supported, so don't run the test.
        return true;
    }

    // Trap on access of TRAP_ADDR.
    ASSERT_EQ(test.guest.set_trap(ZX_GUEST_TRAP_MEM, TRAP_ADDR, PAGE_SIZE, zx::port(), kTrapKey),
              ZX_OK);

    zx_port_packet_t packet = {};
    ASSERT_EQ(test.vcpu.resume(&packet), ZX_OK);
    EXPECT_EQ(packet.key, kTrapKey);
    EXPECT_EQ(packet.type, ZX_PKT_TYPE_GUEST_MEM);

    ASSERT_TRUE(resume_and_clean_exit(&test));
    ASSERT_TRUE(teardown(&test));

    END_TEST;
}

static bool guest_set_trap_with_bell() {
    BEGIN_TEST;

    test_t test;
    ASSERT_TRUE(setup(&test, guest_set_trap_start, guest_set_trap_end));
    if (!test.supported) {
        // The hypervisor isn't supported, so don't run the test.
        return true;
    }

    zx::port port;
    ASSERT_EQ(zx::port::create(0, &port), ZX_OK);

    // Trap on access of TRAP_ADDR.
    ASSERT_EQ(test.guest.set_trap(ZX_GUEST_TRAP_BELL, TRAP_ADDR, PAGE_SIZE, port, kTrapKey),
              ZX_OK);

    zx_port_packet_t packet = {};
    ASSERT_EQ(test.vcpu.resume(&packet), ZX_OK);
    EXPECT_EQ(packet.type, ZX_PKT_TYPE_GUEST_MEM);
    EXPECT_EQ(packet.guest_mem.addr, EXIT_TEST_ADDR);

    ASSERT_EQ(port.wait(zx::time::infinite(), &packet), ZX_OK);
    EXPECT_EQ(packet.key, kTrapKey);
    EXPECT_EQ(packet.type, ZX_PKT_TYPE_GUEST_BELL);
    EXPECT_EQ(packet.guest_bell.addr, TRAP_ADDR);

    ASSERT_TRUE(teardown(&test));

    END_TEST;
}

static bool guest_set_trap_with_io() {
    BEGIN_TEST;

    test_t test;
    ASSERT_TRUE(setup(&test, guest_set_trap_with_io_start, guest_set_trap_with_io_end));
    if (!test.supported) {
        // The hypervisor isn't supported, so don't run the test.
        return true;
    }

    // Trap on writes to TRAP_PORT.
    ASSERT_EQ(test.guest.set_trap(ZX_GUEST_TRAP_IO, TRAP_PORT, 1, zx::port(), kTrapKey),
              ZX_OK);

    zx_port_packet_t packet = {};
    ASSERT_EQ(test.vcpu.resume(&packet), ZX_OK);
    EXPECT_EQ(packet.key, kTrapKey);
    EXPECT_EQ(packet.type, ZX_PKT_TYPE_GUEST_IO);
    EXPECT_EQ(packet.guest_io.port, TRAP_PORT);

    ASSERT_TRUE(resume_and_clean_exit(&test));
    ASSERT_TRUE(teardown(&test));

    END_TEST;
}

BEGIN_TEST_CASE(guest)
RUN_TEST(vcpu_resume)
RUN_TEST(vcpu_read_write_state)
RUN_TEST(vcpu_interrupt)
RUN_TEST(guest_set_trap_with_mem)
RUN_TEST(guest_set_trap_with_bell)
#if __aarch64__
RUN_TEST(vcpu_wfi)
RUN_TEST(vcpu_wfi_aarch32)
RUN_TEST(vcpu_fp)
RUN_TEST(vcpu_fp_aarch32)
#elif __x86_64__
RUN_TEST(guest_set_trap_with_io)
RUN_TEST(vcpu_hlt)
RUN_TEST(vcpu_pause)
RUN_TEST(vcpu_write_cr0)
RUN_TEST(vcpu_compat_mode)
RUN_TEST(vcpu_syscall)
RUN_TEST(vcpu_sysenter)
RUN_TEST(vcpu_sysenter_compat)
#endif
END_TEST_CASE(guest)
