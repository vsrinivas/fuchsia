// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <fidl/fuchsia.kernel/cpp/wire.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/sys/component/cpp/service_client.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/zx/channel.h>
#include <lib/zx/guest.h>
#include <lib/zx/port.h>
#include <lib/zx/resource.h>
#include <lib/zx/status.h>
#include <lib/zx/vcpu.h>
#include <lib/zx/vmar.h>
#include <threads.h>
#include <unistd.h>
#include <zircon/process.h>
#include <zircon/status.h>
#include <zircon/syscalls/hypervisor.h>
#include <zircon/syscalls/port.h>
#include <zircon/types.h>

#include <string>
#include <thread>

#include <fbl/auto_lock.h>
#include <fbl/mutex.h>
#include <fbl/unique_fd.h>
#include <gtest/gtest.h>

#include "arch.h"
#include "constants.h"
#include "hypervisor_tests.h"
#include "src/lib/fxl/test/test_settings.h"

namespace {

template <typename T>
zx::result<zx::resource> GetResource() {
  auto client_end = component::Connect<T>();
  if (client_end.is_error()) {
    return client_end.take_error();
  }
  auto result = fidl::WireCall(*client_end)->Get();
  if (!result.ok()) {
    return zx::error(result.status());
  }
  return zx::ok(std::move(result.value().resource));
}

// Return true if the platform we are running on supports running guests.
bool PlatformSupportsGuests() {
  // Get hypervisor permissions.
  auto hypervisor = GetResource<fuchsia_kernel::HypervisorResource>();
  FX_CHECK(hypervisor.is_ok()) << "Could not get hypervisor resource.";

  // Try create a guest.
  zx::guest guest;
  zx::vmar vmar;
  zx_status_t status = zx::guest::create(*hypervisor, 0, &guest, &vmar);
  if (status != ZX_OK) {
    FX_CHECK(status == ZX_ERR_NOT_SUPPORTED)
        << "Unexpected error attempting to create Zircon guest object: "
        << zx_status_get_string(status);
    return false;
  }

  // Create a single VCPU.
  zx::vcpu vcpu;
  status = zx::vcpu::create(guest, /*options=*/0, /*entry=*/0, &vcpu);
  if (status != ZX_OK) {
    FX_CHECK(status == ZX_ERR_NOT_SUPPORTED)
        << "Unexpected error attempting to create VCPU: " << zx_status_get_string(status);
    return false;
  }

  return true;
}

}  // namespace

// Setup a guest in fixture |test|.
//
// |start| and |end| point to the start and end of the code that will be copied into the guest for
// execution. If |start| and |end| are null, no code is copied.
void SetupGuest(TestCase* test, const char* start, const char* end) {
  ASSERT_GE(VMO_SIZE, end - start);

  ASSERT_EQ(zx::vmo::create(VMO_SIZE, 0, &test->vmo), ZX_OK);
  ASSERT_EQ(zx::vmar::root_self()->map(kHostMapFlags, 0, test->vmo, 0, VMO_SIZE, &test->host_addr),
            ZX_OK);
  cpp20::span<uint8_t> guest_memory(reinterpret_cast<uint8_t*>(test->host_addr), VMO_SIZE);

  // Add ZX_RIGHT_EXECUTABLE so we can map into guest address space.
  auto vmex = GetResource<fuchsia_kernel::VmexResource>();
  ASSERT_EQ(vmex.status_value(), ZX_OK);
  ASSERT_EQ(test->vmo.replace_as_executable(*vmex, &test->vmo), ZX_OK);

  auto hypervisor = GetResource<fuchsia_kernel::HypervisorResource>();
  ASSERT_EQ(hypervisor.status_value(), ZX_OK);
  zx_status_t status = zx::guest::create(*hypervisor, 0, &test->guest, &test->vmar);
  ASSERT_EQ(status, ZX_OK);

  zx_gpaddr_t guest_addr;
  ASSERT_EQ(test->vmar.map(kGuestMapFlags, 0, test->vmo, 0, VMO_SIZE, &guest_addr), ZX_OK);
  ASSERT_EQ(test->guest.set_trap(ZX_GUEST_TRAP_MEM, EXIT_TEST_ADDR, PAGE_SIZE, zx::port(), 0),
            ZX_OK);

  // Set up a simple page table structure for the guest.
  SetUpGuestPageTable(guest_memory);

  // Copy guest code into guest memory at address `GUEST_ENTRY`.
  if (start != nullptr && end != nullptr) {
    memcpy(guest_memory.data() + GUEST_ENTRY, start, end - start);
  }

  status = zx::vcpu::create(test->guest, 0, GUEST_ENTRY, &test->vcpu);
  ASSERT_EQ(status, ZX_OK);
}

namespace {

bool ExceptionThrown(const zx_packet_guest_mem_t& guest_mem, const zx::vcpu& vcpu) {
#if __x86_64__
  // The size of the instruction matches "mov imm, (EXIT_TEST_ADDR)", therefore
  // we assume that we exited the VM correctly.
  if (guest_mem.instruction_size == 12) {
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

}  // namespace

void EnterAndCleanExit(TestCase* test) {
  zx_port_packet_t packet = {};
  ASSERT_EQ(test->vcpu.enter(&packet), ZX_OK);
  EXPECT_EQ(packet.type, ZX_PKT_TYPE_GUEST_MEM);
  EXPECT_EQ(packet.guest_mem.addr, static_cast<zx_gpaddr_t>(EXIT_TEST_ADDR));
#if __x86_64__
  EXPECT_EQ(packet.guest_mem.default_operand_size, 4u);
#endif
  if (test->interrupts_enabled) {
    ASSERT_FALSE(ExceptionThrown(packet.guest_mem, test->vcpu));
  }
}

// Provide our own main so that we can abort testing if no guest support is detected.
int main(int argc, char** argv) {
  fxl::CommandLine cl = fxl::CommandLineFromArgcArgv(argc, argv);
  if (!fxl::SetTestSettings(cl)) {
    return EXIT_FAILURE;
  }

  // Ensure the platform supports running guests.
  if (!PlatformSupportsGuests()) {
    fprintf(stderr, "No support for running guests on current platform. Aborting tests.\n");
    return EXIT_FAILURE;
  }

  // Run tests.
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
