// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.sysinfo/cpp/wire.h>
#include <lib/sys/component/cpp/service_client.h>
#include <lib/zx/vcpu.h>
#include <zircon/syscalls/hypervisor.h>
#include <zircon/syscalls/port.h>

#include <string>

#include <gtest/gtest.h>

#include "src/virtualization/tests/hypervisor/hypervisor_tests.h"

DECLARE_TEST_FUNCTION(vcpu_read_write_state)
DECLARE_TEST_FUNCTION(vcpu_interrupt)
DECLARE_TEST_FUNCTION(vcpu_wfi)
DECLARE_TEST_FUNCTION(vcpu_wfi_pending_interrupt_gicv2)
DECLARE_TEST_FUNCTION(vcpu_wfi_pending_interrupt_gicv3)
DECLARE_TEST_FUNCTION(vcpu_wfi_aarch32)
DECLARE_TEST_FUNCTION(vcpu_fp)
DECLARE_TEST_FUNCTION(vcpu_fp_aarch32)
DECLARE_TEST_FUNCTION(vcpu_psci_system_off)
DECLARE_TEST_FUNCTION(vcpu_dc_set_way_ops)
DECLARE_TEST_FUNCTION(vcpu_enable_mmu)
DECLARE_TEST_FUNCTION(vcpu_enable_disable_mmu)

namespace {

zx::status<fuchsia_sysinfo::InterruptControllerType> GetInterruptControllerType() {
  auto client_end = component::Connect<fuchsia_sysinfo::SysInfo>();
  if (client_end.is_error()) {
    return client_end.take_error();
  }
  auto result = fidl::WireCall(*client_end)->GetInterruptControllerInfo();
  if (!result.ok()) {
    return zx::error(result.status());
  }
  if (result->status != ZX_OK) {
    return zx::error(result->status);
  }
  return zx::ok(result->info->type);
}

TEST(Guest, VcpuReadWriteState) {
  TestCase test;
  ASSERT_NO_FATAL_FAILURE(
      SetupGuest(&test, vcpu_read_write_state_start, vcpu_read_write_state_end));

  zx_vcpu_state_t vcpu_state = {
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
  };

  ASSERT_EQ(test.vcpu.write_state(ZX_VCPU_STATE, &vcpu_state, sizeof(vcpu_state)), ZX_OK);

  ASSERT_NO_FATAL_FAILURE(EnterAndCleanExit(&test));

  ASSERT_EQ(test.vcpu.read_state(ZX_VCPU_STATE, &vcpu_state, sizeof(vcpu_state)), ZX_OK);

  EXPECT_EQ(vcpu_state.x[0], static_cast<uint64_t>(EXIT_TEST_ADDR));
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
  EXPECT_EQ(vcpu_state.cpsr, 0b0110u << 28);
}

TEST(Guest, VcpuInterrupt) {
  TestCase test;
  ASSERT_NO_FATAL_FAILURE(SetupGuest(&test, vcpu_interrupt_start, vcpu_interrupt_end));
  test.interrupts_enabled = true;

  ASSERT_EQ(test.vcpu.interrupt(kInterruptVector), ZX_OK);
  ASSERT_NO_FATAL_FAILURE(EnterAndCleanExit(&test));
}

TEST(Guest, VcpuWfi) {
  TestCase test;
  ASSERT_NO_FATAL_FAILURE(SetupGuest(&test, vcpu_wfi_start, vcpu_wfi_end));

  ASSERT_NO_FATAL_FAILURE(EnterAndCleanExit(&test));
}

TEST(Guest, VcpuWfiPendingInterrupt) {
  auto type = GetInterruptControllerType();
  ASSERT_EQ(ZX_OK, type.status_value());

  TestCase test;
  switch (*type) {
    case fuchsia_sysinfo::InterruptControllerType::kGicV2:
      ASSERT_NO_FATAL_FAILURE(SetupGuest(&test, vcpu_wfi_pending_interrupt_gicv2_start,
                                         vcpu_wfi_pending_interrupt_gicv2_end));
      break;
    case fuchsia_sysinfo::InterruptControllerType::kGicV3:
      ASSERT_NO_FATAL_FAILURE(SetupGuest(&test, vcpu_wfi_pending_interrupt_gicv3_start,
                                         vcpu_wfi_pending_interrupt_gicv3_end));
      break;
    default:
      ASSERT_TRUE(false) << "Unsupported GIC version";
  }

  // Inject two interrupts so that there will be one pending when the guest exits on wfi.
  ASSERT_EQ(test.vcpu.interrupt(kInterruptVector), ZX_OK);
  ASSERT_EQ(test.vcpu.interrupt(kInterruptVector + 1), ZX_OK);

  ASSERT_NO_FATAL_FAILURE(EnterAndCleanExit(&test));
}

TEST(Guest, VcpuWfiAarch32) {
  TestCase test;
  ASSERT_NO_FATAL_FAILURE(SetupGuest(&test, vcpu_wfi_aarch32_start, vcpu_wfi_aarch32_end));

  zx_port_packet_t packet = {};
  ASSERT_EQ(test.vcpu.enter(&packet), ZX_OK);
  EXPECT_EQ(packet.type, ZX_PKT_TYPE_GUEST_MEM);
  EXPECT_EQ(packet.guest_mem.addr, static_cast<zx_gpaddr_t>(EXIT_TEST_ADDR));
  EXPECT_EQ(packet.guest_mem.read, false);
  EXPECT_EQ(packet.guest_mem.data, 0u);
}

TEST(Guest, VcpuFp) {
  TestCase test;
  ASSERT_NO_FATAL_FAILURE(SetupGuest(&test, vcpu_fp_start, vcpu_fp_end));

  ASSERT_NO_FATAL_FAILURE(EnterAndCleanExit(&test));
}

TEST(Guest, VcpuFpAarch32) {
  TestCase test;
  ASSERT_NO_FATAL_FAILURE(SetupGuest(&test, vcpu_fp_aarch32_start, vcpu_fp_aarch32_end));

  zx_port_packet_t packet = {};
  ASSERT_EQ(test.vcpu.enter(&packet), ZX_OK);
  EXPECT_EQ(packet.type, ZX_PKT_TYPE_GUEST_MEM);
  EXPECT_EQ(packet.guest_mem.addr, static_cast<zx_gpaddr_t>(EXIT_TEST_ADDR));
  EXPECT_EQ(packet.guest_mem.read, false);
  EXPECT_EQ(packet.guest_mem.data, 0u);
}

TEST(Guest, VcpuPsciSystemOff) {
  TestCase test;
  ASSERT_NO_FATAL_FAILURE(SetupGuest(&test, vcpu_psci_system_off_start, vcpu_psci_system_off_end));

  zx_port_packet_t packet = {};
  ASSERT_EQ(test.vcpu.enter(&packet), ZX_ERR_UNAVAILABLE);
}

TEST(Guest, VcpuWriteStateIoAarch32) {
  TestCase test;
  ASSERT_NO_FATAL_FAILURE(SetupGuest(&test, nullptr, nullptr));

  // ZX_VCPU_IO is not supported on arm64.
  zx_vcpu_io_t io{};
  io.access_size = 1;
  ASSERT_EQ(test.vcpu.write_state(ZX_VCPU_IO, &io, sizeof(io)), ZX_ERR_INVALID_ARGS);
}

TEST(Guest, DataCacheSetWayOperations) {
  TestCase test;
  ASSERT_NO_FATAL_FAILURE(SetupGuest(&test, vcpu_dc_set_way_ops_start, vcpu_dc_set_way_ops_end));

  ASSERT_NO_FATAL_FAILURE(EnterAndCleanExit(&test));
}

TEST(Guest, EnableMmu) {
  TestCase test;
  ASSERT_NO_FATAL_FAILURE(SetupGuest(&test, vcpu_enable_mmu_start, vcpu_enable_mmu_end));

  ASSERT_NO_FATAL_FAILURE(EnterAndCleanExit(&test));
}

TEST(Guest, DisableMmu) {
  TestCase test;
  ASSERT_NO_FATAL_FAILURE(
      SetupGuest(&test, vcpu_enable_disable_mmu_start, vcpu_enable_disable_mmu_end));

  ASSERT_NO_FATAL_FAILURE(EnterAndCleanExit(&test));
}

}  // namespace
