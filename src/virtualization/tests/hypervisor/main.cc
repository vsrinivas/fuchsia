// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <fuchsia/kernel/cpp/fidl.h>
#include <fuchsia/sysinfo/cpp/fidl.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
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

#include "constants.h"
#include "hypervisor_tests.h"
#include "src/lib/fxl/test/test_settings.h"

namespace {

#if __x86_64__
enum {
  X86_PTE_P = 0x01,   // P    Valid
  X86_PTE_RW = 0x02,  // R/W  Read/Write
  X86_PTE_U = 0x04,   // U    Page is user accessible
  X86_PTE_PS = 0x80,  // PS   Page size
};
#endif

zx_status_t GetVmexResource(zx::resource* resource) {
  fuchsia::kernel::VmexResourceSyncPtr vmex_resource;
  auto path = std::string("/svc/") + fuchsia::kernel::VmexResource::Name_;
  zx_status_t status =
      fdio_service_connect(path.data(), vmex_resource.NewRequest().TakeChannel().release());
  if (status != ZX_OK) {
    return status;
  }
  return vmex_resource->Get(resource);
}

zx_status_t GetHypervisorResource(zx::resource* resource) {
  fuchsia::kernel::HypervisorResourceSyncPtr hypervisor_rsrc;
  auto path = std::string("/svc/") + fuchsia::kernel::HypervisorResource::Name_;
  zx_status_t status =
      fdio_service_connect(path.data(), hypervisor_rsrc.NewRequest().TakeChannel().release());
  if (status != ZX_OK) {
    return status;
  }

  return hypervisor_rsrc->Get(resource);
}

// Return true if the platform we are running on supports running guests.
bool PlatformSupportsGuests() {
  // Get hypervisor permissions.
  zx::resource hypervisor_resource;
  zx_status_t status = GetHypervisorResource(&hypervisor_resource);
  FX_CHECK(status == ZX_OK) << "Could not get hypervisor resource.";

  // Try create a guest.
  zx::guest guest;
  zx::vmar vmar;
  status = zx::guest::create(hypervisor_resource, 0, &guest, &vmar);
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
  ASSERT_EQ(zx::vmo::create(VMO_SIZE, 0, &test->vmo), ZX_OK);
  ASSERT_EQ(zx::vmar::root_self()->map(kHostMapFlags, 0, test->vmo, 0, VMO_SIZE, &test->host_addr),
            ZX_OK);

  // Add ZX_RIGHT_EXECUTABLE so we can map into guest address space.
  zx::resource vmex_resource;
  ASSERT_EQ(GetVmexResource(&vmex_resource), ZX_OK);
  ASSERT_EQ(test->vmo.replace_as_executable(vmex_resource, &test->vmo), ZX_OK);

  zx::resource hypervisor_resource;
  ASSERT_EQ(GetHypervisorResource(&hypervisor_resource), ZX_OK);
  zx_status_t status = zx::guest::create(hypervisor_resource, 0, &test->guest, &test->vmar);
  ASSERT_EQ(status, ZX_OK);

  zx_gpaddr_t guest_addr;
  ASSERT_EQ(test->vmar.map(kGuestMapFlags, 0, test->vmo, 0, VMO_SIZE, &guest_addr), ZX_OK);
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
    memcpy(reinterpret_cast<void*>(test->host_addr + entry), start, end - start);
  }

  status = zx::vcpu::create(test->guest, 0, entry, &test->vcpu);
  ASSERT_EQ(status, ZX_OK);
}

namespace {

bool ExceptionThrown(const zx_packet_guest_mem_t& guest_mem, const zx::vcpu& vcpu) {
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

}  // namespace

void ResumeAndCleanExit(TestCase* test) {
  zx_port_packet_t packet = {};
  ASSERT_EQ(test->vcpu.resume(&packet), ZX_OK);
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
