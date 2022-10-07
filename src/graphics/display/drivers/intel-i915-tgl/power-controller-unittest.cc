// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/graphics/display/drivers/intel-i915-tgl/power-controller.h"

#include <lib/mmio/mmio-buffer.h>
#include <lib/zx/status.h>
#include <zircon/errors.h>

#include <atomic>
#include <cstdint>

#include <gtest/gtest.h>

#include "fake-mmio-reg/fake-mmio-reg.h"
#include "src/graphics/display/drivers/intel-i915-tgl/mock-mmio-range.h"

namespace i915_tgl {

namespace {

// MMIO addresses from IHD-OS-KBL-Vol 12-1.17 "Sequences for Changing CD Clock
// Frequency", pages 138-139

constexpr uint32_t kMailboxInterfaceOffset = 0x138124;
constexpr uint32_t kMailboxData0Offset = 0x138128;
constexpr uint32_t kMailboxData1Offset = 0x13812c;

class PowerControllerTest : public ::testing::Test {
 public:
  PowerControllerTest() = default;
  ~PowerControllerTest() override = default;

  void SetUp() override {}
  void TearDown() override { mmio_range_.CheckAllAccessesReplayed(); }

 protected:
  constexpr static int kMmioRangeSize = 0x140000;
  MockMmioRange mmio_range_{kMmioRangeSize, MockMmioRange::Size::k32};
  fdf::MmioBuffer mmio_buffer_{mmio_range_.GetMmioBuffer()};
};

TEST_F(PowerControllerTest, TransactImmediateSuccess) {
  mmio_range_.Expect(MockMmioRange::AccessList({
      {.address = kMailboxInterfaceOffset, .value = 0},
      {.address = kMailboxData0Offset, .value = 0x9abc'def0, .write = true},
      {.address = kMailboxData1Offset, .value = 0x1234'5678, .write = true},
      {.address = kMailboxInterfaceOffset, .value = 0x8042'4140, .write = true},
      {.address = kMailboxInterfaceOffset, .value = 0x0042'4140},
      {.address = kMailboxData0Offset, .value = 0x7654'3210},
      {.address = kMailboxData1Offset, .value = 0xfedc'ba98},
  }));
  PowerController power_controller(&mmio_buffer_);

  zx::status<uint64_t> result = power_controller.Transact({
      .command = 0x40,
      .param1 = 0x41,
      .param2 = 0x42,
      .data = 0x1234'5678'9abc'def0,
      .timeout_us = 3,
  });
  ASSERT_TRUE(result.is_ok()) << result.status_string();
  EXPECT_EQ(0xfedcba98'76543210u, result.value());
}

TEST_F(PowerControllerTest, TransactSuccessAfterDelay) {
  mmio_range_.Expect(MockMmioRange::AccessList({
      {.address = kMailboxInterfaceOffset, .value = 0},
      {.address = kMailboxData0Offset, .value = 0x9abc'def0, .write = true},
      {.address = kMailboxData1Offset, .value = 0x1234'5678, .write = true},
      {.address = kMailboxInterfaceOffset, .value = 0x8042'4140, .write = true},
      {.address = kMailboxInterfaceOffset, .value = 0x8042'4140},
      {.address = kMailboxInterfaceOffset, .value = 0x8042'4140},
      {.address = kMailboxInterfaceOffset, .value = 0x0042'4140},
      {.address = kMailboxData0Offset, .value = 0x7654'3210},
      {.address = kMailboxData1Offset, .value = 0xfedc'ba98},
  }));
  PowerController power_controller(&mmio_buffer_);

  zx::status<uint64_t> result = power_controller.Transact({
      .command = 0x40,
      .param1 = 0x41,
      .param2 = 0x42,
      .data = 0x1234'5678'9abc'def0,
      .timeout_us = 3,
  });
  ASSERT_TRUE(result.is_ok()) << result.status_string();
  EXPECT_EQ(0xfedc'ba98'7654'3210u, result.value());
}

TEST_F(PowerControllerTest, TransactCommandTimeout) {
  mmio_range_.Expect(MockMmioRange::AccessList({
      {.address = kMailboxInterfaceOffset, .value = 0},
      {.address = kMailboxData0Offset, .value = 0x9abc'def0, .write = true},
      {.address = kMailboxData1Offset, .value = 0x1234'5678, .write = true},
      {.address = kMailboxInterfaceOffset, .value = 0x8042'4140, .write = true},
      {.address = kMailboxInterfaceOffset, .value = 0x8042'4140},
      {.address = kMailboxInterfaceOffset, .value = 0x8042'4140},
      {.address = kMailboxInterfaceOffset, .value = 0x8042'4140},
      {.address = kMailboxInterfaceOffset, .value = 0x8042'4140},
  }));
  PowerController power_controller(&mmio_buffer_);

  const zx::status<uint64_t> result = power_controller.Transact({
      .command = 0x40,
      .param1 = 0x41,
      .param2 = 0x42,
      .data = 0x1234'5678'9abc'def0,
      .timeout_us = 3,
  });
  EXPECT_EQ(ZX_ERR_IO_MISSED_DEADLINE, result.status_value());
}

TEST_F(PowerControllerTest, TransactCommandZeroTimeout) {
  mmio_range_.Expect(MockMmioRange::AccessList({
      {.address = kMailboxInterfaceOffset, .value = 0},
      {.address = kMailboxData0Offset, .value = 0x9abc'def0, .write = true},
      {.address = kMailboxData1Offset, .value = 0x1234'5678, .write = true},
      {.address = kMailboxInterfaceOffset, .value = 0x8042'4140, .write = true},
  }));
  PowerController power_controller(&mmio_buffer_);

  zx::status<uint64_t> result = power_controller.Transact({
      .command = 0x40,
      .param1 = 0x41,
      .param2 = 0x42,
      .data = 0x1234'5678'9abc'def0,
      .timeout_us = 0,
  });
  ASSERT_TRUE(result.is_ok()) << result.status_string();
  EXPECT_EQ(0u, result.value());
}

TEST_F(PowerControllerTest, TransactSuccessAfterPriorCommandDelay) {
  mmio_range_.Expect(MockMmioRange::AccessList({
      {.address = kMailboxInterfaceOffset, .value = 0x8000'0007},
      {.address = kMailboxInterfaceOffset, .value = 0x8000'0007},
      {.address = kMailboxInterfaceOffset, .value = 0x0000'0007},
      {.address = kMailboxData0Offset, .value = 0x9abc'def0, .write = true},
      {.address = kMailboxData1Offset, .value = 0x1234'5678, .write = true},
      {.address = kMailboxInterfaceOffset, .value = 0x8042'4140, .write = true},
      {.address = kMailboxInterfaceOffset, .value = 0x0042'4140},
      {.address = kMailboxData0Offset, .value = 0x7654'3210},
      {.address = kMailboxData1Offset, .value = 0xfedc'ba98},
  }));
  PowerController power_controller(&mmio_buffer_);

  zx::status<uint64_t> result = power_controller.Transact({
      .command = 0x40,
      .param1 = 0x41,
      .param2 = 0x42,
      .data = 0x1234'5678'9abc'def0,
      .timeout_us = 3,
  });
  ASSERT_TRUE(result.is_ok()) << result.status_string();
  EXPECT_EQ(0xfedc'ba98'7654'3210u, result.value());
}

TEST_F(PowerControllerTest, TransactSuccessAfterPriorAndCurrentCommandDelay) {
  mmio_range_.Expect(MockMmioRange::AccessList({
      {.address = kMailboxInterfaceOffset, .value = 0x8000'0007},
      {.address = kMailboxInterfaceOffset, .value = 0x8000'0007},
      {.address = kMailboxInterfaceOffset, .value = 0x0000'0007},
      {.address = kMailboxData0Offset, .value = 0x9abc'def0, .write = true},
      {.address = kMailboxData1Offset, .value = 0x1234'5678, .write = true},
      {.address = kMailboxInterfaceOffset, .value = 0x8042'4140, .write = true},
      {.address = kMailboxInterfaceOffset, .value = 0x8042'4140},
      {.address = kMailboxInterfaceOffset, .value = 0x8042'4140},
      {.address = kMailboxInterfaceOffset, .value = 0x0042'4140},
      {.address = kMailboxData0Offset, .value = 0x7654'3210},
      {.address = kMailboxData1Offset, .value = 0xfedc'ba98},
  }));
  PowerController power_controller(&mmio_buffer_);

  zx::status<uint64_t> result = power_controller.Transact({
      .command = 0x40,
      .param1 = 0x41,
      .param2 = 0x42,
      .data = 0x1234'5678'9abc'def0,
      .timeout_us = 3,
  });
  ASSERT_TRUE(result.is_ok()) << result.status_string();
  EXPECT_EQ(0xfedc'ba98'7654'3210u, result.value());
}

TEST_F(PowerControllerTest, TransactPreviousCommandTimeout) {
  // Sadly, this test is tightly coupled with the Transact() implementation.
  //
  // In order to cover the previous command timeout path, we need to know the
  // number of polls that Transact() will perform before giving up.  Also, this
  // test will have to be rewritten once we start using interrupts.
  static constexpr int kMailboxPollsBeforeTimeout = 201;
  for (int i = 0; i < kMailboxPollsBeforeTimeout; ++i) {
    mmio_range_.Expect({.address = kMailboxInterfaceOffset, .value = 0x8042'4140});
  }
  PowerController power_controller(&mmio_buffer_);

  const zx::status<uint64_t> result = power_controller.Transact({
      .command = 0x40,
      .param1 = 0x41,
      .param2 = 0x42,
      .data = 0x1234'5678'9abc'def0,
      .timeout_us = 3,
  });
  EXPECT_EQ(ZX_ERR_IO_MISSED_DEADLINE, result.status_value());
}

TEST_F(PowerControllerTest, RequestDisplayVoltageLevelNoRetrySuccess) {
  mmio_range_.Expect(MockMmioRange::AccessList({
      {.address = kMailboxInterfaceOffset, .value = 0},
      {.address = kMailboxData0Offset, .value = 0x0000'0003, .write = true},
      {.address = kMailboxData1Offset, .value = 0x0000'0000, .write = true},
      {.address = kMailboxInterfaceOffset, .value = 0x8000'0007, .write = true},
      {.address = kMailboxInterfaceOffset, .value = 0x0000'0007},
      {.address = kMailboxData0Offset, .value = 0x0000'0001},
      {.address = kMailboxData1Offset, .value = 0x0000'0000},
  }));
  PowerController power_controller(&mmio_buffer_);

  const zx::status<> result =
      power_controller.RequestDisplayVoltageLevel(3, PowerController::RetryBehavior::kNoRetry);
  EXPECT_TRUE(result.is_ok()) << result.status_string();
}

TEST_F(PowerControllerTest, RequestDisplayVoltageLevelLowLevelNoRetrySuccess) {
  mmio_range_.Expect(MockMmioRange::AccessList({
      {.address = kMailboxInterfaceOffset, .value = 0},
      {.address = kMailboxData0Offset, .value = 0x0000'0001, .write = true},
      {.address = kMailboxData1Offset, .value = 0x0000'0000, .write = true},
      {.address = kMailboxInterfaceOffset, .value = 0x8000'0007, .write = true},
      {.address = kMailboxInterfaceOffset, .value = 0x0000'0007},
      {.address = kMailboxData0Offset, .value = 0x0000'0001},
      {.address = kMailboxData1Offset, .value = 0x0000'0000},
  }));
  PowerController power_controller(&mmio_buffer_);

  const zx::status<> result =
      power_controller.RequestDisplayVoltageLevel(1, PowerController::RetryBehavior::kNoRetry);
  EXPECT_TRUE(result.is_ok()) << result.status_string();
}

TEST_F(PowerControllerTest, RequestDisplayVoltageLevelLowLevelNoRetryRefused) {
  mmio_range_.Expect(MockMmioRange::AccessList({
      {.address = kMailboxInterfaceOffset, .value = 0},
      {.address = kMailboxData0Offset, .value = 0x0000'0001, .write = true},
      {.address = kMailboxData1Offset, .value = 0x0000'0000, .write = true},
      {.address = kMailboxInterfaceOffset, .value = 0x8000'0007, .write = true},
      {.address = kMailboxInterfaceOffset, .value = 0x0000'0007},
      {.address = kMailboxData0Offset, .value = 0x0000'0000},
      {.address = kMailboxData1Offset, .value = 0x0000'0000},
  }));
  PowerController power_controller(&mmio_buffer_);

  const zx::status<> result =
      power_controller.RequestDisplayVoltageLevel(1, PowerController::RetryBehavior::kNoRetry);
  EXPECT_EQ(ZX_ERR_IO_REFUSED, result.status_value());
}

TEST_F(PowerControllerTest, RequestDisplayVoltageLevelRetryImmediateSuccess) {
  mmio_range_.Expect(MockMmioRange::AccessList({
      {.address = kMailboxInterfaceOffset, .value = 0},
      {.address = kMailboxData0Offset, .value = 0x0000'0003, .write = true},
      {.address = kMailboxData1Offset, .value = 0x0000'0000, .write = true},
      {.address = kMailboxInterfaceOffset, .value = 0x8000'0007, .write = true},
      {.address = kMailboxInterfaceOffset, .value = 0x0000'0007},
      {.address = kMailboxData0Offset, .value = 0x0000'0001},
      {.address = kMailboxData1Offset, .value = 0x0000'0000},
  }));
  PowerController power_controller(&mmio_buffer_);

  const zx::status<> result = power_controller.RequestDisplayVoltageLevel(
      3, PowerController::RetryBehavior::kRetryUntilStateChanges);
  EXPECT_TRUE(result.is_ok()) << result.status_string();
}

TEST_F(PowerControllerTest, RequestDisplayVoltageLevelRetrySuccessAfterRetries) {
  mmio_range_.Expect(MockMmioRange::AccessList({
      {.address = kMailboxInterfaceOffset, .value = 0},
      {.address = kMailboxData0Offset, .value = 0x0000'0003, .write = true},
      {.address = kMailboxData1Offset, .value = 0x0000'0000, .write = true},
      {.address = kMailboxInterfaceOffset, .value = 0x8000'0007, .write = true},
      {.address = kMailboxInterfaceOffset, .value = 0x0000'0007},
      {.address = kMailboxData0Offset, .value = 0x0000'0000},
      {.address = kMailboxData1Offset, .value = 0x0000'0000},

      // First retry.
      {.address = kMailboxInterfaceOffset, .value = 0x0000'0007},
      {.address = kMailboxData0Offset, .value = 0x0000'0003, .write = true},
      {.address = kMailboxData1Offset, .value = 0x0000'0000, .write = true},
      {.address = kMailboxInterfaceOffset, .value = 0x8000'0007, .write = true},
      {.address = kMailboxInterfaceOffset, .value = 0x0000'0007},
      {.address = kMailboxData0Offset, .value = 0x0000'0000},
      {.address = kMailboxData1Offset, .value = 0x0000'0000},

      // Second retry.
      {.address = kMailboxInterfaceOffset, .value = 0x0000'0007},
      {.address = kMailboxData0Offset, .value = 0x0000'0003, .write = true},
      {.address = kMailboxData1Offset, .value = 0x0000'0000, .write = true},
      {.address = kMailboxInterfaceOffset, .value = 0x8000'0007, .write = true},
      {.address = kMailboxInterfaceOffset, .value = 0x0000'0007},
      {.address = kMailboxData0Offset, .value = 0x0000'0001},
      {.address = kMailboxData1Offset, .value = 0x0000'0000},
  }));
  PowerController power_controller(&mmio_buffer_);

  const zx::status<> result = power_controller.RequestDisplayVoltageLevel(
      3, PowerController::RetryBehavior::kRetryUntilStateChanges);
  EXPECT_TRUE(result.is_ok()) << result.status_string();
}

TEST_F(PowerControllerTest, RequestDisplayVoltageLevelRetryTimeout) {
  std::vector<ddk_fake::FakeMmioReg> fake_mmio_regs(0x140000 / sizeof(uint32_t));

  bool report_busy_mailbox = false;
  fake_mmio_regs[kMailboxInterfaceOffset / sizeof(uint32_t)].SetWriteCallback([&](uint64_t value) {
    EXPECT_EQ(0x8000'0007, value) << "Unexpected command";
    report_busy_mailbox = true;

    // Force the result to zero, so RequestedDisplayVoltageLevelRetryTimeout()
    // retries.
    fake_mmio_regs[kMailboxData0Offset / sizeof(uint32_t)].Write(0x0000'0000);
  });
  fake_mmio_regs[kMailboxInterfaceOffset / sizeof(uint32_t)].SetReadCallback([&]() -> uint64_t {
    if (report_busy_mailbox) {
      // Report busy once, so RequestDisplayVoltageLevelRetryTimeout() gets some
      // sleep between retries.
      report_busy_mailbox = false;
      return 0x8000'0007;
    }
    return 0x0000'0007;
  });
  fake_mmio_regs[kMailboxData0Offset / sizeof(uint32_t)].SetReadCallback([&]() -> uint64_t {
    // Always produce the result "voltage level not applied", so
    // RequestDisplayVoltageLevel() retries.
    return 0x0000'0000;
  });

  ddk_fake::FakeMmioRegRegion fake_mmio_region(fake_mmio_regs.data(), sizeof(uint32_t),
                                               fake_mmio_regs.size());
  fdf::MmioBuffer fake_mmio_buffer = fake_mmio_region.GetMmioBuffer();
  PowerController power_controller(&fake_mmio_buffer);

  const zx::status<> result = power_controller.RequestDisplayVoltageLevel(
      2, PowerController::RetryBehavior::kRetryUntilStateChanges);
  EXPECT_EQ(ZX_ERR_IO_REFUSED, result.status_value());
}

TEST_F(PowerControllerTest, SetDisplayTypeCColdBlockingTigerLakeOnNoRetrySuccess) {
  mmio_range_.Expect(MockMmioRange::AccessList({
      {.address = kMailboxInterfaceOffset, .value = 0},
      {.address = kMailboxData0Offset, .value = 0x0000'0000, .write = true},
      {.address = kMailboxData1Offset, .value = 0x0000'0000, .write = true},
      {.address = kMailboxInterfaceOffset, .value = 0x8000'0026, .write = true},
      {.address = kMailboxInterfaceOffset, .value = 0x0000'0026},
      {.address = kMailboxData0Offset, .value = 0x0000'0000},
      {.address = kMailboxData1Offset, .value = 0x0000'0000},
  }));
  PowerController power_controller(&mmio_buffer_);

  const zx::status<> result = power_controller.SetDisplayTypeCColdBlockingTigerLake(
      true, PowerController::RetryBehavior::kNoRetry);
  EXPECT_TRUE(result.is_ok()) << result.status_string();
}

TEST_F(PowerControllerTest, SetDisplayTypeCColdBlockingTigerLakeOffNoRetrySuccess) {
  mmio_range_.Expect(MockMmioRange::AccessList({
      {.address = kMailboxInterfaceOffset, .value = 0},
      {.address = kMailboxData0Offset, .value = 0x0000'0001, .write = true},
      {.address = kMailboxData1Offset, .value = 0x0000'0000, .write = true},
      {.address = kMailboxInterfaceOffset, .value = 0x8000'0026, .write = true},
      {.address = kMailboxInterfaceOffset, .value = 0x0000'0026},
      {.address = kMailboxData0Offset, .value = 0x0000'0001},
      {.address = kMailboxData1Offset, .value = 0x0000'0000},
  }));
  PowerController power_controller(&mmio_buffer_);

  const zx::status<> result = power_controller.SetDisplayTypeCColdBlockingTigerLake(
      false, PowerController::RetryBehavior::kNoRetry);
  EXPECT_TRUE(result.is_ok()) << result.status_string();
}

TEST_F(PowerControllerTest, SetDisplayTypeCColdBlockingTigerLakeOffNoRetryRefused) {
  mmio_range_.Expect(MockMmioRange::AccessList({
      {.address = kMailboxInterfaceOffset, .value = 0},
      {.address = kMailboxData0Offset, .value = 0x0000'0001, .write = true},
      {.address = kMailboxData1Offset, .value = 0x0000'0000, .write = true},
      {.address = kMailboxInterfaceOffset, .value = 0x8000'0026, .write = true},
      {.address = kMailboxInterfaceOffset, .value = 0x0000'0026},
      {.address = kMailboxData0Offset, .value = 0x0000'0000},
      {.address = kMailboxData1Offset, .value = 0x0000'0000},
  }));
  PowerController power_controller(&mmio_buffer_);

  const zx::status<> result = power_controller.SetDisplayTypeCColdBlockingTigerLake(
      false, PowerController::RetryBehavior::kNoRetry);
  EXPECT_EQ(ZX_ERR_IO_REFUSED, result.status_value());
}

TEST_F(PowerControllerTest, SetDisplayTypeCColdBlockingTigerLakeOnRetryImmediateSuccess) {
  mmio_range_.Expect(MockMmioRange::AccessList({
      {.address = kMailboxInterfaceOffset, .value = 0},
      {.address = kMailboxData0Offset, .value = 0x0000'0000, .write = true},
      {.address = kMailboxData1Offset, .value = 0x0000'0000, .write = true},
      {.address = kMailboxInterfaceOffset, .value = 0x8000'0026, .write = true},
      {.address = kMailboxInterfaceOffset, .value = 0x0000'0026},
      {.address = kMailboxData0Offset, .value = 0x0000'0000},
      {.address = kMailboxData1Offset, .value = 0x0000'0000},
  }));
  PowerController power_controller(&mmio_buffer_);

  const zx::status<> result = power_controller.SetDisplayTypeCColdBlockingTigerLake(
      true, PowerController::RetryBehavior::kRetryUntilStateChanges);
  EXPECT_TRUE(result.is_ok()) << result.status_string();
}

TEST_F(PowerControllerTest, SetDisplayTypeCColdBlockingTigerLakeOnRetrySuccessAfterRetries) {
  mmio_range_.Expect(MockMmioRange::AccessList({
      {.address = kMailboxInterfaceOffset, .value = 0},
      {.address = kMailboxData0Offset, .value = 0x0000'0000, .write = true},
      {.address = kMailboxData1Offset, .value = 0x0000'0000, .write = true},
      {.address = kMailboxInterfaceOffset, .value = 0x8000'0026, .write = true},
      {.address = kMailboxInterfaceOffset, .value = 0x0000'0026},
      {.address = kMailboxData0Offset, .value = 0x0000'0001},
      {.address = kMailboxData1Offset, .value = 0x0000'0000},

      // First retry.
      {.address = kMailboxInterfaceOffset, .value = 0x0000'0026},
      {.address = kMailboxData0Offset, .value = 0x0000'0000, .write = true},
      {.address = kMailboxData1Offset, .value = 0x0000'0000, .write = true},
      {.address = kMailboxInterfaceOffset, .value = 0x8000'0026, .write = true},
      {.address = kMailboxInterfaceOffset, .value = 0x0000'0026},
      {.address = kMailboxData0Offset, .value = 0x0000'0001},
      {.address = kMailboxData1Offset, .value = 0x0000'0000},

      // Second retry.
      {.address = kMailboxInterfaceOffset, .value = 0x0000'0026},
      {.address = kMailboxData0Offset, .value = 0x0000'0000, .write = true},
      {.address = kMailboxData1Offset, .value = 0x0000'0000, .write = true},
      {.address = kMailboxInterfaceOffset, .value = 0x8000'0026, .write = true},
      {.address = kMailboxInterfaceOffset, .value = 0x0000'0026},
      {.address = kMailboxData0Offset, .value = 0x0000'0000},
      {.address = kMailboxData1Offset, .value = 0x0000'0000},
  }));
  PowerController power_controller(&mmio_buffer_);

  const zx::status<> result = power_controller.SetDisplayTypeCColdBlockingTigerLake(
      true, PowerController::RetryBehavior::kRetryUntilStateChanges);
  EXPECT_TRUE(result.is_ok()) << result.status_string();
}

TEST_F(PowerControllerTest, SetDisplayTypeCColdBlockingTigerLakeOnRetryTimeout) {
  std::vector<ddk_fake::FakeMmioReg> fake_mmio_regs(0x140000 / sizeof(uint32_t));

  bool report_busy_mailbox = false;
  fake_mmio_regs[kMailboxInterfaceOffset / sizeof(uint32_t)].SetWriteCallback([&](uint64_t value) {
    EXPECT_EQ(0x8000'0026, value) << "Unexpected command";
    report_busy_mailbox = true;
  });
  fake_mmio_regs[kMailboxInterfaceOffset / sizeof(uint32_t)].SetReadCallback([&]() -> uint64_t {
    if (report_busy_mailbox) {
      // Report busy once, so SetDisplayTypeCColdBlockingTigerLake() gets some
      // sleep between retries.
      report_busy_mailbox = false;
      return 0x8000'0026;
    }
    return 0x0000'0026;
  });
  fake_mmio_regs[kMailboxData0Offset / sizeof(uint32_t)].SetReadCallback([&]() -> uint64_t {
    // Always produce the result "in TCCOLD", so
    // SetDisplayTypeCColdBlockingTigerLake() retries.
    return 0x0000'0001;
  });

  ddk_fake::FakeMmioRegRegion fake_mmio_region(fake_mmio_regs.data(), sizeof(uint32_t),
                                               fake_mmio_regs.size());
  fdf::MmioBuffer fake_mmio_buffer = fake_mmio_region.GetMmioBuffer();
  PowerController power_controller(&fake_mmio_buffer);

  const zx::status<> result = power_controller.SetDisplayTypeCColdBlockingTigerLake(
      true, PowerController::RetryBehavior::kRetryUntilStateChanges);
  EXPECT_EQ(ZX_ERR_IO_REFUSED, result.status_value());
}

}  // namespace

}  // namespace i915_tgl
