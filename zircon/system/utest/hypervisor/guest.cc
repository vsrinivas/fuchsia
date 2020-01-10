// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <threads.h>
#include <unistd.h>

#include <fbl/unique_fd.h>
#include <fuchsia/sysinfo/c/fidl.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/zx/channel.h>
#include <lib/zx/guest.h>
#include <lib/zx/port.h>
#include <lib/zx/resource.h>
#include <lib/zx/vcpu.h>
#include <lib/zx/vmar.h>
#include <unittest/unittest.h>
#include <zircon/process.h>
#include <zircon/syscalls/hypervisor.h>
#include <zircon/syscalls/port.h>
#include <zircon/types.h>

#include <string>

#include "constants_priv.h"

static constexpr uint32_t kGuestMapFlags =
    ZX_VM_PERM_READ | ZX_VM_PERM_WRITE | ZX_VM_PERM_EXECUTE | ZX_VM_SPECIFIC;
static constexpr uint32_t kHostMapFlags = ZX_VM_PERM_READ | ZX_VM_PERM_WRITE;
// Inject an interrupt with vector 32, the first user defined interrupt vector.
static constexpr uint32_t kInterruptVector = 32u;
static constexpr uint64_t kTrapKey = 0x1234;
static const std::string kSysInfoPath = "/svc/" + std::string(fuchsia_sysinfo_SysInfo_Name);

#ifdef __x86_64__
static constexpr uint32_t kNmiVector = 2u;
static constexpr uint32_t kExceptionVector = 16u;
#endif

#define DECLARE_TEST_FUNCTION(name) \
  extern const char name##_start[]; \
  extern const char name##_end[];

DECLARE_TEST_FUNCTION(vcpu_resume)
DECLARE_TEST_FUNCTION(vcpu_read_write_state)
DECLARE_TEST_FUNCTION(vcpu_interrupt)
DECLARE_TEST_FUNCTION(guest_set_trap)
#ifdef __aarch64__
DECLARE_TEST_FUNCTION(vcpu_wfi)
DECLARE_TEST_FUNCTION(vcpu_wfi_pending_interrupt_gicv2)
DECLARE_TEST_FUNCTION(vcpu_wfi_pending_interrupt_gicv3)
DECLARE_TEST_FUNCTION(vcpu_aarch32_wfi)
DECLARE_TEST_FUNCTION(vcpu_fp)
DECLARE_TEST_FUNCTION(vcpu_aarch32_fp)
#elif __x86_64__
DECLARE_TEST_FUNCTION(vcpu_hlt)
DECLARE_TEST_FUNCTION(vcpu_pause)
DECLARE_TEST_FUNCTION(vcpu_write_cr0)
DECLARE_TEST_FUNCTION(vcpu_compat_mode)
DECLARE_TEST_FUNCTION(vcpu_syscall)
DECLARE_TEST_FUNCTION(vcpu_sysenter)
DECLARE_TEST_FUNCTION(vcpu_sysenter_compat)
DECLARE_TEST_FUNCTION(vcpu_vmcall)
DECLARE_TEST_FUNCTION(vcpu_extended_registers)
DECLARE_TEST_FUNCTION(guest_set_trap_with_io)
#endif
#undef DECLARE_TEST_FUNCTION

enum {
  X86_PTE_P = 0x01,   // P    Valid
  X86_PTE_RW = 0x02,  // R/W  Read/Write
  X86_PTE_U = 0x04,   // U    Page is user accessible
  X86_PTE_PS = 0x80,  // PS   Page size
};

typedef struct test {
  bool supported = false;
  bool interrupts_enabled = false;
  uintptr_t host_addr = 0;

  zx::vmo vmo;
  zx::guest guest;
  zx::vmar vmar;
  zx::vcpu vcpu;

  ~test() {
    if (host_addr != 0) {
      zx::vmar::root_self()->unmap(host_addr, VMO_SIZE);
    }
  }
} test_t;

// TODO(MAC-246): Convert to C++ FIDL interface.
static zx_status_t get_sysinfo(zx::channel* sysinfo) {
  fbl::unique_fd fd(open(kSysInfoPath.c_str(), O_RDWR));
  if (!fd) {
    return ZX_ERR_IO;
  }

  return fdio_get_service_handle(fd.release(), sysinfo->reset_and_get_address());
}

// TODO(MAC-246): Convert to C++ FIDL interface.
static zx_status_t get_hypervisor_resource(zx::resource* resource) {
  zx::channel channel;
  zx_status_t status = get_sysinfo(&channel);
  if (status != ZX_OK) {
    return status;
  }

  zx_status_t fidl_status = fuchsia_sysinfo_SysInfoGetHypervisorResource(
      channel.get(), &status, resource->reset_and_get_address());
  if (fidl_status != ZX_OK) {
    return fidl_status;
  }
  return status;
}

#ifdef __aarch64__

// TODO(MAC-246): Convert to C++ FIDL interface.
static zx_status_t get_interrupt_controller_info(fuchsia_sysinfo_InterruptControllerInfo* info) {
  zx::channel channel;
  zx_status_t status = get_sysinfo(&channel);
  if (status != ZX_OK) {
    return status;
  }

  zx_status_t fidl_status =
      fuchsia_sysinfo_SysInfoGetInterruptControllerInfo(channel.get(), &status, info);
  if (fidl_status != ZX_OK) {
    return fidl_status;
  }
  return status;
}

#endif  // __aarch64__

// Setup a guest in fixture |test|.
//
// |start| and |end| point to the start and end of the code that will be copied into the guest for
// execution. If |start| and |end| are null, no code is copied.
//
// Returns true on success, false on failure.
static bool setup(test_t* test, const char* start, const char* end) {
  BEGIN_HELPER;

  ASSERT_EQ(zx::vmo::create(VMO_SIZE, 0, &test->vmo), ZX_OK);
  ASSERT_EQ(zx::vmar::root_self()->map(0, test->vmo, 0, VMO_SIZE, kHostMapFlags, &test->host_addr),
            ZX_OK);

  // Add ZX_RIGHT_EXECUTABLE so we can map into guest address space.
  ASSERT_EQ(test->vmo.replace_as_executable(zx::handle(), &test->vmo), ZX_OK);

  zx::resource resource;
  zx_status_t status = get_hypervisor_resource(&resource);
  ASSERT_EQ(status, ZX_OK);
  status = zx::guest::create(resource, 0, &test->guest, &test->vmar);
  test->supported = status != ZX_ERR_NOT_SUPPORTED;
  if (!test->supported) {
    fprintf(stderr, "Guest creation not supported\n");
    return true;
  }
  ASSERT_EQ(status, ZX_OK);

  zx_gpaddr_t guest_addr;
  ASSERT_EQ(test->vmar.map(0, test->vmo, 0, VMO_SIZE, kGuestMapFlags, &guest_addr), ZX_OK);
  ASSERT_EQ(test->guest.set_trap(ZX_GUEST_TRAP_MEM, EXIT_TEST_ADDR, PAGE_SIZE, zx::port(), 0),
            ZX_OK);

  // Setup the guest.
  uintptr_t entry = 0;
#if __x86_64__
  // PML4 entry pointing to (addr + 0x1000)
  uint64_t* pte_off = reinterpret_cast<uint64_t*>(test->host_addr);
  *pte_off = PAGE_SIZE | X86_PTE_P | X86_PTE_U | X86_PTE_RW;
  // PDP entry with 1GB page.
  pte_off = reinterpret_cast<uint64_t*>(test->host_addr + PAGE_SIZE);
  *pte_off = X86_PTE_PS | X86_PTE_P | X86_PTE_U | X86_PTE_RW;
  entry = GUEST_ENTRY;
#endif  // __x86_64__

  if (start != nullptr && end != nullptr) {
    memcpy((void*)(test->host_addr + entry), start, end - start);
  }

  status = zx::vcpu::create(test->guest, 0, entry, &test->vcpu);
  test->supported = status != ZX_ERR_NOT_SUPPORTED;
  if (!test->supported) {
    fprintf(stderr, "VCPU creation not supported\n");
    return true;
  }
  ASSERT_EQ(status, ZX_OK);

  END_HELPER;
}

static bool setup_and_interrupt(test_t* test, const char* start, const char* end) {
  ASSERT_TRUE(setup(test, start, end));
  if (!test->supported) {
    // The hypervisor isn't supported, so don't run the test.
    return true;
  }
  test->interrupts_enabled = true;

  thrd_t thread;
  int ret = thrd_create(
      &thread,
      [](void* ctx) -> int {
        test_t* test = static_cast<test_t*>(ctx);
        return test->vcpu.interrupt(kInterruptVector) == ZX_OK ? thrd_success : thrd_error;
      },
      test);
  ASSERT_EQ(ret, thrd_success);

  return true;
}

static inline bool exception_thrown(const zx_packet_guest_mem_t& guest_mem, const zx::vcpu& vcpu) {
#if __x86_64__
  if (guest_mem.inst_len != 12) {
    // Not the expected mov imm, (EXIT_TEST_ADDR) size.
    return true;
  }
  if (guest_mem.inst_buf[8] == 0 && guest_mem.inst_buf[9] == 0 && guest_mem.inst_buf[10] == 0 &&
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
    .padding1 = {},
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

  ASSERT_EQ(test.vcpu.write_state(ZX_VCPU_STATE, &vcpu_state, sizeof(vcpu_state)), ZX_OK);

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
#endif  // __x86_64__

  END_TEST;
}

static bool vcpu_interrupt() {
  BEGIN_TEST;

  test_t test;
  ASSERT_TRUE(setup(&test, vcpu_interrupt_start, vcpu_interrupt_end));
  if (!test.supported) {
    // The hypervisor isn't supported, so don't run the test.
    return true;
  }
  test.interrupts_enabled = true;

#if __x86_64__
  // Resume once and wait for the guest to set up an IDT.
  ASSERT_TRUE(resume_and_clean_exit(&test));
#endif

  ASSERT_EQ(test.vcpu.interrupt(kInterruptVector), ZX_OK);
  ASSERT_TRUE(resume_and_clean_exit(&test));

#if __x86_64__
  zx_vcpu_state_t vcpu_state;
  ASSERT_EQ(test.vcpu.read_state(ZX_VCPU_STATE, &vcpu_state, sizeof(vcpu_state)), ZX_OK);
  EXPECT_EQ(vcpu_state.rax, kInterruptVector);
#endif

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
  ASSERT_EQ(test.guest.set_trap(ZX_GUEST_TRAP_BELL, TRAP_ADDR, PAGE_SIZE, port, kTrapKey), ZX_OK);

  ASSERT_TRUE(resume_and_clean_exit(&test));

  zx_port_packet_t packet = {};
  ASSERT_EQ(port.wait(zx::time::infinite(), &packet), ZX_OK);
  EXPECT_EQ(packet.key, kTrapKey);
  EXPECT_EQ(packet.type, ZX_PKT_TYPE_GUEST_BELL);
  EXPECT_EQ(packet.guest_bell.addr, TRAP_ADDR);

  END_TEST;
}

// Test for ZX-4206.
static bool guest_set_trap_with_bell_drop() {
  BEGIN_TEST;

  // Build the port before test so test is destructed first.
  zx::port port;
  ASSERT_EQ(zx::port::create(0, &port), ZX_OK);

  test_t test;
  ASSERT_TRUE(setup(&test, guest_set_trap_start, guest_set_trap_end));
  if (!test.supported) {
    // The hypervisor isn't supported, so don't run the test.
    return true;
  }

  // Trap on access of TRAP_ADDR.
  ASSERT_EQ(test.guest.set_trap(ZX_GUEST_TRAP_BELL, TRAP_ADDR, PAGE_SIZE, port, kTrapKey), ZX_OK);

  ASSERT_TRUE(resume_and_clean_exit(&test));

  // The guest in test is destructed with one packet still queued on the
  // port. This should work correctly.

  END_TEST;
}

// Test for ZX-4221.
static bool guest_set_trap_with_bell_and_user() {
  BEGIN_TEST;

  zx::port port;
  ASSERT_EQ(zx::port::create(0, &port), ZX_OK);

  // Queue a packet with the same key as the trap.
  zx_port_packet packet = {};
  packet.key = kTrapKey;
  packet.type = ZX_PKT_TYPE_USER;
  ASSERT_EQ(port.queue(&packet), ZX_OK);

  // Force guest to be released and cancel all packets associated with traps.
  {
    test_t test;
    ASSERT_TRUE(setup(&test, guest_set_trap_start, guest_set_trap_end));
    if (!test.supported) {
      // The hypervisor isn't supported, so don't run the test.
      return true;
    }

    // Trap on access of TRAP_ADDR.
    ASSERT_EQ(test.guest.set_trap(ZX_GUEST_TRAP_BELL, TRAP_ADDR, PAGE_SIZE, port, kTrapKey), ZX_OK);

    ASSERT_TRUE(resume_and_clean_exit(&test));
  }

  ASSERT_EQ(port.wait(zx::time::infinite(), &packet), ZX_OK);
  EXPECT_EQ(packet.key, kTrapKey);
  EXPECT_EQ(packet.type, ZX_PKT_TYPE_USER);

  END_TEST;
}

// Test for ZX-4220.
static bool guest_set_trap_with_bell_and_max_user() {
  BEGIN_TEST;

  zx::port port;
  ASSERT_EQ(zx::port::create(0, &port), ZX_OK);

  // Keep queueing packets until we receive ZX_ERR_SHOULD_WAIT.
  zx_port_packet packet = {};
  packet.type = ZX_PKT_TYPE_USER;
  zx_status_t status = ZX_OK;
  while ((status = port.queue(&packet)) == ZX_OK) {
  }
  ASSERT_EQ(status, ZX_ERR_SHOULD_WAIT);

  test_t test;
  ASSERT_TRUE(setup(&test, guest_set_trap_start, guest_set_trap_end));
  if (!test.supported) {
    // The hypervisor isn't supported, so don't run the test.
    return true;
  }

  // Trap on access of TRAP_ADDR.
  ASSERT_EQ(test.guest.set_trap(ZX_GUEST_TRAP_BELL, TRAP_ADDR, PAGE_SIZE, port, kTrapKey), ZX_OK);

  ASSERT_TRUE(resume_and_clean_exit(&test));

  END_TEST;
}

// See that zx::vcpu::resume returns ZX_ERR_BAD_STATE if the port has been closed.
static bool guest_set_trap_close_port() {
  BEGIN_TEST;

  zx::port port;
  ASSERT_EQ(zx::port::create(0, &port), ZX_OK);

  test_t test;
  ASSERT_TRUE(setup(&test, guest_set_trap_start, guest_set_trap_end));
  if (!test.supported) {
    // The hypervisor isn't supported, so don't run the test.
    return true;
  }

  ASSERT_EQ(test.guest.set_trap(ZX_GUEST_TRAP_BELL, TRAP_ADDR, PAGE_SIZE, port, kTrapKey), ZX_OK);

  port.reset();

  zx_port_packet_t packet = {};
  ASSERT_EQ(test.vcpu.resume(&packet), ZX_ERR_BAD_STATE);

  ASSERT_TRUE(resume_and_clean_exit(&test));

  END_TEST;
}

#ifdef __aarch64__

static bool vcpu_wfi() {
  BEGIN_TEST;

  test_t test;
  ASSERT_TRUE(setup(&test, vcpu_wfi_start, vcpu_wfi_end));
  if (!test.supported) {
    // The hypervisor isn't supported, so don't run the test.
    return true;
  }

  ASSERT_TRUE(resume_and_clean_exit(&test));

  END_TEST;
}

static bool vcpu_wfi_pending_interrupt() {
  BEGIN_TEST;

  fuchsia_sysinfo_InterruptControllerInfo info;
  ASSERT_EQ(ZX_OK, get_interrupt_controller_info(&info));

  test_t test;
  switch (info.type) {
    case fuchsia_sysinfo_InterruptControllerType_GIC_V2:
      ASSERT_TRUE(setup(&test, vcpu_wfi_pending_interrupt_gicv2_start,
                        vcpu_wfi_pending_interrupt_gicv2_end));
      break;
    case fuchsia_sysinfo_InterruptControllerType_GIC_V3:
      ASSERT_TRUE(setup(&test, vcpu_wfi_pending_interrupt_gicv3_start,
                        vcpu_wfi_pending_interrupt_gicv3_end));
      break;
    default:
      ASSERT_TRUE(false, "Unsupported GIC version");
  }
  if (!test.supported) {
    // The hypervisor isn't supported, so don't run the test.
    return true;
  }

  // Inject two interrupts so that there will be one pending when the guest exits on wfi.
  ASSERT_EQ(test.vcpu.interrupt(kInterruptVector), ZX_OK);
  ASSERT_EQ(test.vcpu.interrupt(kInterruptVector + 1), ZX_OK);

  ASSERT_TRUE(resume_and_clean_exit(&test));

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
  EXPECT_EQ(packet.guest_mem.read, false);
  EXPECT_EQ(packet.guest_mem.data, 0);

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
  EXPECT_EQ(packet.guest_mem.read, false);
  EXPECT_EQ(packet.guest_mem.data, 0);

  END_TEST;
}

static bool vcpu_write_state_io_aarch32() {
  BEGIN_TEST;

  test_t test;
  ASSERT_TRUE(setup(&test, nullptr, nullptr));
  if (!test.supported) {
    // The hypervisor isn't supported, so don't run the test.
    return true;
  }

  // ZX_VCPU_IO is not supported on arm64.
  zx_vcpu_io_t io{};
  io.access_size = 1;
  ASSERT_EQ(test.vcpu.write_state(ZX_VCPU_IO, &io, sizeof(io)), ZX_ERR_INVALID_ARGS);

  END_TEST;
}

#elif __x86_64__

static bool vcpu_interrupt_priority() {
  BEGIN_TEST;

  test_t test;
  ASSERT_TRUE(setup(&test, vcpu_interrupt_start, vcpu_interrupt_end));
  if (!test.supported) {
    // The hypervisor isn't supported, so don't run the test.
    return true;
  }
  test.interrupts_enabled = true;

  // Resume once and wait for the guest to set up an IDT.
  ASSERT_TRUE(resume_and_clean_exit(&test));

  // Check that interrupts have higher priority than exceptions.
  ASSERT_EQ(test.vcpu.interrupt(kExceptionVector), ZX_OK);
  ASSERT_EQ(test.vcpu.interrupt(kInterruptVector), ZX_OK);

  ASSERT_TRUE(resume_and_clean_exit(&test));

  zx_vcpu_state_t vcpu_state;
  ASSERT_EQ(test.vcpu.read_state(ZX_VCPU_STATE, &vcpu_state, sizeof(vcpu_state)), ZX_OK);
  EXPECT_EQ(vcpu_state.rax, kInterruptVector);

  // TODO(MAC-225): Check that the exception is cleared.

  END_TEST;
}

static bool vcpu_nmi() {
  BEGIN_TEST;

  test_t test;
  ASSERT_TRUE(setup(&test, vcpu_interrupt_start, vcpu_interrupt_end));
  if (!test.supported) {
    // The hypervisor isn't supported, so don't run the test.
    return true;
  }
  test.interrupts_enabled = true;

  // Resume once and wait for the guest to set up an IDT.
  ASSERT_TRUE(resume_and_clean_exit(&test));

  // Check that NMIs are handled.
  ASSERT_EQ(test.vcpu.interrupt(kNmiVector), ZX_OK);

  ASSERT_TRUE(resume_and_clean_exit(&test));

  zx_vcpu_state_t vcpu_state;
  ASSERT_EQ(test.vcpu.read_state(ZX_VCPU_STATE, &vcpu_state, sizeof(vcpu_state)), ZX_OK);
  EXPECT_EQ(vcpu_state.rax, kNmiVector);

  END_TEST;
}

static bool vcpu_nmi_priority() {
  BEGIN_TEST;

  test_t test;
  ASSERT_TRUE(setup(&test, vcpu_interrupt_start, vcpu_interrupt_end));
  if (!test.supported) {
    // The hypervisor isn't supported, so don't run the test.
    return true;
  }
  test.interrupts_enabled = true;

  // Resume once and wait for the guest to set up an IDT.
  ASSERT_TRUE(resume_and_clean_exit(&test));

  // Check that NMIs have higher priority than interrupts.
  ASSERT_EQ(test.vcpu.interrupt(kInterruptVector), ZX_OK);
  ASSERT_EQ(test.vcpu.interrupt(kNmiVector), ZX_OK);

  ASSERT_TRUE(resume_and_clean_exit(&test));

  zx_vcpu_state_t vcpu_state;
  ASSERT_EQ(test.vcpu.read_state(ZX_VCPU_STATE, &vcpu_state, sizeof(vcpu_state)), ZX_OK);
  EXPECT_EQ(vcpu_state.rax, kNmiVector);

  // TODO(MAC-225): Check that the interrupt is queued.

  END_TEST;
}

static bool vcpu_exception() {
  BEGIN_TEST;

  test_t test;
  ASSERT_TRUE(setup(&test, vcpu_interrupt_start, vcpu_interrupt_end));
  if (!test.supported) {
    // The hypervisor isn't supported, so don't run the test.
    return true;
  }
  test.interrupts_enabled = true;

  // Resume once and wait for the guest to set up an IDT.
  ASSERT_TRUE(resume_and_clean_exit(&test));

  // Check that exceptions are handled.
  ASSERT_EQ(test.vcpu.interrupt(kExceptionVector), ZX_OK);

  ASSERT_TRUE(resume_and_clean_exit(&test));

  zx_vcpu_state_t vcpu_state;
  ASSERT_EQ(test.vcpu.read_state(ZX_VCPU_STATE, &vcpu_state, sizeof(vcpu_state)), ZX_OK);
  EXPECT_EQ(vcpu_state.rax, kExceptionVector);

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

  zx_vcpu_state_t vcpu_state;
  ASSERT_EQ(test.vcpu.read_state(ZX_VCPU_STATE, &vcpu_state, sizeof(vcpu_state)), ZX_OK);
  // Check that cr0 has the NE bit set when read.
  EXPECT_TRUE(vcpu_state.rax & X86_CR0_NE);

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

  END_TEST;
}

static bool vcpu_vmcall() {
  BEGIN_TEST;

  test_t test;
  ASSERT_TRUE(setup(&test, vcpu_vmcall_start, vcpu_vmcall_end));
  if (!test.supported) {
    // The hypervisor isn't supported, so don't run the test.
    return true;
  }

  ASSERT_TRUE(resume_and_clean_exit(&test));

  zx_vcpu_state_t vcpu_state;
  ASSERT_EQ(test.vcpu.read_state(ZX_VCPU_STATE, &vcpu_state, sizeof(vcpu_state)), ZX_OK);

  const uint64_t kVmCallNoSys = -1000;
  EXPECT_EQ(vcpu_state.rax, kVmCallNoSys);

  END_TEST;
}

static bool vcpu_extended_registers() {
  BEGIN_TEST;

  test_t test;
  ASSERT_TRUE(setup(&test, vcpu_extended_registers_start, vcpu_extended_registers_end));
  if (!test.supported) {
    // The hypervisor isn't supported, so don't run the test.
    return true;
  }

  // Guest sets xmm0.
  ASSERT_TRUE(resume_and_clean_exit(&test));

  // Clear host xmm0.
  __asm__("xorps %%xmm0, %%xmm0" ::: "xmm0");

  // Guest reads xmm0 into rax:rbx.
  ASSERT_TRUE(resume_and_clean_exit(&test));

  // Check that the host xmm0 is restored to zero.
  bool xmm0_is_zero;
  __asm__(
      "ptest %%xmm0, %%xmm0\n"
      "sete %0"
      : "=q"(xmm0_is_zero));
  EXPECT_TRUE(xmm0_is_zero);

  zx_vcpu_state_t vcpu_state;
  ASSERT_EQ(test.vcpu.read_state(ZX_VCPU_STATE, &vcpu_state, sizeof(vcpu_state)), ZX_OK);
  EXPECT_EQ(vcpu_state.rax, 0x89abcdef01234567);
  EXPECT_EQ(vcpu_state.rbx, 0x76543210fedcba98);

  // Guest disables SSE
  ASSERT_TRUE(resume_and_clean_exit(&test));
  // Guest successfully runs again
  ASSERT_TRUE(resume_and_clean_exit(&test));

  END_TEST;
}

// Verify that write_state with ZX_VCPU_IO only accepts valid access sizes.
static bool vcpu_write_state_io_invalid_size() {
  BEGIN_TEST;

  test_t test;
  // Passing nullptr for start and end since we don't need to actually run the guest for this test.
  ASSERT_TRUE(setup(&test, nullptr, nullptr));
  if (!test.supported) {
    // The hypervisor isn't supported, so don't run the test.
    return true;
  }

  // valid access sizes
  zx_vcpu_io_t io{};
  io.access_size = 1;
  ASSERT_EQ(test.vcpu.write_state(ZX_VCPU_IO, &io, sizeof(io)), ZX_OK);
  io.access_size = 2;
  ASSERT_EQ(test.vcpu.write_state(ZX_VCPU_IO, &io, sizeof(io)), ZX_OK);
  io.access_size = 4;
  ASSERT_EQ(test.vcpu.write_state(ZX_VCPU_IO, &io, sizeof(io)), ZX_OK);

  // invalid access sizes
  io.access_size = 0;
  ASSERT_EQ(test.vcpu.write_state(ZX_VCPU_IO, &io, sizeof(io)), ZX_ERR_INVALID_ARGS);
  io.access_size = 3;
  ASSERT_EQ(test.vcpu.write_state(ZX_VCPU_IO, &io, sizeof(io)), ZX_ERR_INVALID_ARGS);
  io.access_size = 5;
  ASSERT_EQ(test.vcpu.write_state(ZX_VCPU_IO, &io, sizeof(io)), ZX_ERR_INVALID_ARGS);
  io.access_size = 255;
  ASSERT_EQ(test.vcpu.write_state(ZX_VCPU_IO, &io, sizeof(io)), ZX_ERR_INVALID_ARGS);

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
  ASSERT_EQ(test.guest.set_trap(ZX_GUEST_TRAP_IO, TRAP_PORT, 1, zx::port(), kTrapKey), ZX_OK);

  zx_port_packet_t packet = {};
  ASSERT_EQ(test.vcpu.resume(&packet), ZX_OK);
  EXPECT_EQ(packet.key, kTrapKey);
  EXPECT_EQ(packet.type, ZX_PKT_TYPE_GUEST_IO);
  EXPECT_EQ(packet.guest_io.port, TRAP_PORT);

  ASSERT_TRUE(resume_and_clean_exit(&test));

  END_TEST;
}

#endif  // __x86_64__

BEGIN_TEST_CASE(guest)
RUN_TEST(vcpu_resume)
RUN_TEST(vcpu_read_write_state)
RUN_TEST(vcpu_interrupt)
RUN_TEST(guest_set_trap_with_mem)
RUN_TEST(guest_set_trap_with_bell)
RUN_TEST(guest_set_trap_with_bell_drop)
RUN_TEST(guest_set_trap_with_bell_and_user)
RUN_TEST(guest_set_trap_with_bell_and_max_user)
RUN_TEST(guest_set_trap_close_port)
#if __aarch64__
RUN_TEST(vcpu_wfi)
RUN_TEST(vcpu_wfi_pending_interrupt)
RUN_TEST(vcpu_wfi_aarch32)
RUN_TEST(vcpu_fp)
RUN_TEST(vcpu_fp_aarch32)
RUN_TEST(vcpu_write_state_io_aarch32)
#elif __x86_64__
RUN_TEST(vcpu_interrupt_priority)
RUN_TEST(vcpu_nmi)
RUN_TEST(vcpu_nmi_priority)
RUN_TEST(vcpu_exception)
RUN_TEST(vcpu_hlt)
RUN_TEST(vcpu_pause)
RUN_TEST(vcpu_write_cr0)
RUN_TEST(vcpu_compat_mode)
RUN_TEST(vcpu_syscall)
RUN_TEST(vcpu_sysenter)
RUN_TEST(vcpu_sysenter_compat)
RUN_TEST(vcpu_vmcall)
RUN_TEST(vcpu_extended_registers)
RUN_TEST(vcpu_write_state_io_invalid_size)
RUN_TEST(guest_set_trap_with_io)
#endif
END_TEST_CASE(guest)
