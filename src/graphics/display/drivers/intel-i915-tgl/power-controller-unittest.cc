// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/graphics/display/drivers/intel-i915-tgl/power-controller.h"

#include <lib/mmio/mmio-buffer.h>
#include <lib/zx/result.h>
#include <zircon/errors.h>

#include <atomic>
#include <cstdint>

#include <fake-mmio-reg/fake-mmio-reg.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/graphics/display/drivers/intel-i915-tgl/mock-mmio-range.h"
#include "src/graphics/display/drivers/intel-i915-tgl/scoped-value-change.h"

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
  // Ensures that the tests don't time out due to external factors (such as
  // being pre-empted by the scheduler) while replaying MMIO access lists.
  constexpr static int kLargeTimeout = 1'000'000'000;

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

  zx::result<uint64_t> result = power_controller.Transact({
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

  zx::result<uint64_t> result = power_controller.Transact({
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

  const zx::result<uint64_t> result = power_controller.Transact({
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

  zx::result<uint64_t> result = power_controller.Transact({
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

  zx::result<uint64_t> result = power_controller.Transact({
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

  zx::result<uint64_t> result = power_controller.Transact({
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

  const zx::result<uint64_t> result = power_controller.Transact({
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

  const zx::result<> result =
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

  const zx::result<> result =
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

  const zx::result<> result =
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

  const zx::result<> result = power_controller.RequestDisplayVoltageLevel(
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

  const zx::result<> result = power_controller.RequestDisplayVoltageLevel(
      3, PowerController::RetryBehavior::kRetryUntilStateChanges);
  EXPECT_TRUE(result.is_ok()) << result.status_string();
}

TEST_F(PowerControllerTest, RequestDisplayVoltageLevelRetryTimeout) {
  std::vector<ddk_fake::FakeMmioReg> fake_mmio_regs(0x140000 / sizeof(uint32_t));

  bool report_busy_mailbox = false;
  fake_mmio_regs[kMailboxInterfaceOffset / sizeof(uint32_t)].SetWriteCallback([&](uint64_t value) {
    EXPECT_EQ(0x8000'0007, value) << "Unexpected command";
    report_busy_mailbox = true;
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

  const zx::result<> result = power_controller.RequestDisplayVoltageLevel(
      2, PowerController::RetryBehavior::kRetryUntilStateChanges);
  EXPECT_EQ(ZX_ERR_IO_REFUSED, result.status_value());
}

TEST_F(PowerControllerTest, SetDisplayTypeCColdBlockingTigerLakeOnNoRetrySuccess) {
  ScopedValueChange<int> reply_timeout_change =
      PowerController::OverrideTypeCColdBlockingChangeReplyTimeoutUsForTesting(kLargeTimeout);
  ScopedValueChange<int> timeout_change =
      PowerController::OverrideTypeCColdBlockingChangeTotalTimeoutUsForTesting(kLargeTimeout);
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

  const zx::result<> result = power_controller.SetDisplayTypeCColdBlockingTigerLake(
      true, PowerController::RetryBehavior::kNoRetry);
  EXPECT_TRUE(result.is_ok()) << result.status_string();
}

TEST_F(PowerControllerTest, SetDisplayTypeCColdBlockingTigerLakeOffNoRetrySuccess) {
  ScopedValueChange<int> reply_timeout_change =
      PowerController::OverrideTypeCColdBlockingChangeReplyTimeoutUsForTesting(kLargeTimeout);
  ScopedValueChange<int> timeout_change =
      PowerController::OverrideTypeCColdBlockingChangeTotalTimeoutUsForTesting(kLargeTimeout);
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

  const zx::result<> result = power_controller.SetDisplayTypeCColdBlockingTigerLake(
      false, PowerController::RetryBehavior::kNoRetry);
  EXPECT_TRUE(result.is_ok()) << result.status_string();
}

TEST_F(PowerControllerTest, SetDisplayTypeCColdBlockingTigerLakeOffNoRetryRefused) {
  ScopedValueChange<int> reply_timeout_change =
      PowerController::OverrideTypeCColdBlockingChangeReplyTimeoutUsForTesting(kLargeTimeout);
  ScopedValueChange<int> timeout_change =
      PowerController::OverrideTypeCColdBlockingChangeTotalTimeoutUsForTesting(kLargeTimeout);
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

  const zx::result<> result = power_controller.SetDisplayTypeCColdBlockingTigerLake(
      false, PowerController::RetryBehavior::kNoRetry);
  EXPECT_EQ(ZX_ERR_IO_REFUSED, result.status_value());
}

TEST_F(PowerControllerTest, SetDisplayTypeCColdBlockingTigerLakeOffNoRetryReplyTimeout) {
  ScopedValueChange<int> reply_timeout_change =
      PowerController::OverrideTypeCColdBlockingChangeReplyTimeoutUsForTesting(1);
  ScopedValueChange<int> timeout_change =
      PowerController::OverrideTypeCColdBlockingChangeTotalTimeoutUsForTesting(kLargeTimeout);
  mmio_range_.Expect(MockMmioRange::AccessList({
      {.address = kMailboxInterfaceOffset, .value = 0},
      {.address = kMailboxData0Offset, .value = 0x0000'0001, .write = true},
      {.address = kMailboxData1Offset, .value = 0x0000'0000, .write = true},
      {.address = kMailboxInterfaceOffset, .value = 0x8000'0026, .write = true},
      {.address = kMailboxInterfaceOffset, .value = 0x8000'0026},
      {.address = kMailboxInterfaceOffset, .value = 0x8000'0026},
  }));
  PowerController power_controller(&mmio_buffer_);

  const zx::result<> result = power_controller.SetDisplayTypeCColdBlockingTigerLake(
      false, PowerController::RetryBehavior::kNoRetry);
  EXPECT_EQ(ZX_ERR_IO_MISSED_DEADLINE, result.status_value());
}

TEST_F(PowerControllerTest, SetDisplayTypeCColdBlockingTigerLakeOnRetryImmediateSuccess) {
  ScopedValueChange<int> reply_timeout_change =
      PowerController::OverrideTypeCColdBlockingChangeReplyTimeoutUsForTesting(kLargeTimeout);
  ScopedValueChange<int> timeout_change =
      PowerController::OverrideTypeCColdBlockingChangeTotalTimeoutUsForTesting(kLargeTimeout);
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

  const zx::result<> result = power_controller.SetDisplayTypeCColdBlockingTigerLake(
      true, PowerController::RetryBehavior::kRetryUntilStateChanges);
  EXPECT_TRUE(result.is_ok()) << result.status_string();
}

TEST_F(PowerControllerTest, SetDisplayTypeCColdBlockingTigerLakeOnRetrySuccessAfterRetries) {
  ScopedValueChange<int> reply_timeout_change =
      PowerController::OverrideTypeCColdBlockingChangeReplyTimeoutUsForTesting(kLargeTimeout);
  ScopedValueChange<int> timeout_change =
      PowerController::OverrideTypeCColdBlockingChangeTotalTimeoutUsForTesting(kLargeTimeout);
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

  const zx::result<> result = power_controller.SetDisplayTypeCColdBlockingTigerLake(
      true, PowerController::RetryBehavior::kRetryUntilStateChanges);
  EXPECT_TRUE(result.is_ok()) << result.status_string();
}

TEST_F(PowerControllerTest, SetDisplayTypeCColdBlockingTigerLakeOnRetryTimeout) {
  ScopedValueChange<int> reply_timeout_change =
      PowerController::OverrideTypeCColdBlockingChangeReplyTimeoutUsForTesting(kLargeTimeout);
  ScopedValueChange<int> timeout_change =
      PowerController::OverrideTypeCColdBlockingChangeTotalTimeoutUsForTesting(0);
  mmio_range_.Expect(MockMmioRange::AccessList({
      {.address = kMailboxInterfaceOffset, .value = 0},
      {.address = kMailboxData0Offset, .value = 0x0000'0000, .write = true},
      {.address = kMailboxData1Offset, .value = 0x0000'0000, .write = true},
      {.address = kMailboxInterfaceOffset, .value = 0x8000'0026, .write = true},
      {.address = kMailboxInterfaceOffset, .value = 0x0000'0026},
      {.address = kMailboxData0Offset, .value = 0x0000'0001},
      {.address = kMailboxData1Offset, .value = 0x0000'0000},
  }));
  PowerController power_controller(&mmio_buffer_);

  const zx::result<> result = power_controller.SetDisplayTypeCColdBlockingTigerLake(
      true, PowerController::RetryBehavior::kRetryUntilStateChanges);
  EXPECT_EQ(ZX_ERR_IO_REFUSED, result.status_value());
}

TEST_F(PowerControllerTest, SetDisplayTypeCColdBlockingTigerLakeOffOnRetryReplyTimeout) {
  ScopedValueChange<int> reply_timeout_change =
      PowerController::OverrideTypeCColdBlockingChangeReplyTimeoutUsForTesting(1);
  ScopedValueChange<int> timeout_change =
      PowerController::OverrideTypeCColdBlockingChangeTotalTimeoutUsForTesting(kLargeTimeout);
  mmio_range_.Expect(MockMmioRange::AccessList({
      {.address = kMailboxInterfaceOffset, .value = 0},
      {.address = kMailboxData0Offset, .value = 0x0000'0001, .write = true},
      {.address = kMailboxData1Offset, .value = 0x0000'0000, .write = true},
      {.address = kMailboxInterfaceOffset, .value = 0x8000'0026, .write = true},
      {.address = kMailboxInterfaceOffset, .value = 0x8000'0026},
      {.address = kMailboxInterfaceOffset, .value = 0x8000'0026},
  }));
  PowerController power_controller(&mmio_buffer_);

  const zx::result<> result = power_controller.SetDisplayTypeCColdBlockingTigerLake(
      false, PowerController::RetryBehavior::kRetryUntilStateChanges);
  EXPECT_EQ(ZX_ERR_IO_MISSED_DEADLINE, result.status_value());
}

TEST_F(PowerControllerTest, SetSystemAgentGeyservilleEnabledFalseNoRetrySuccess) {
  mmio_range_.Expect(MockMmioRange::AccessList({
      {.address = kMailboxInterfaceOffset, .value = 0},
      {.address = kMailboxData0Offset, .value = 0x0000'0000, .write = true},
      {.address = kMailboxData1Offset, .value = 0x0000'0000, .write = true},
      {.address = kMailboxInterfaceOffset, .value = 0x8000'0021, .write = true},
      {.address = kMailboxInterfaceOffset, .value = 0x0000'0021},
      {.address = kMailboxData0Offset, .value = 0x0000'0001},
      {.address = kMailboxData1Offset, .value = 0x0000'0000},
  }));
  PowerController power_controller(&mmio_buffer_);

  const zx::result<> result = power_controller.SetSystemAgentGeyservilleEnabled(
      false, PowerController::RetryBehavior::kNoRetry);
  EXPECT_TRUE(result.is_ok()) << result.status_string();
}

TEST_F(PowerControllerTest, SetSystemAgentGeyservilleEnabledTrueNoRetrySuccess) {
  mmio_range_.Expect(MockMmioRange::AccessList({
      {.address = kMailboxInterfaceOffset, .value = 0},
      {.address = kMailboxData0Offset, .value = 0x0000'0003, .write = true},
      {.address = kMailboxData1Offset, .value = 0x0000'0000, .write = true},
      {.address = kMailboxInterfaceOffset, .value = 0x8000'0021, .write = true},
      {.address = kMailboxInterfaceOffset, .value = 0x0000'0021},
      {.address = kMailboxData0Offset, .value = 0x0000'0001},
      {.address = kMailboxData1Offset, .value = 0x0000'0000},
  }));
  PowerController power_controller(&mmio_buffer_);

  const zx::result<> result = power_controller.SetSystemAgentGeyservilleEnabled(
      true, PowerController::RetryBehavior::kNoRetry);
  EXPECT_TRUE(result.is_ok()) << result.status_string();
}

TEST_F(PowerControllerTest, SetSystemAgentGeyservilleEnabledTrueNoRetryRefused) {
  mmio_range_.Expect(MockMmioRange::AccessList({
      {.address = kMailboxInterfaceOffset, .value = 0},
      {.address = kMailboxData0Offset, .value = 0x0000'0003, .write = true},
      {.address = kMailboxData1Offset, .value = 0x0000'0000, .write = true},
      {.address = kMailboxInterfaceOffset, .value = 0x8000'0021, .write = true},
      {.address = kMailboxInterfaceOffset, .value = 0x0000'0021},
      {.address = kMailboxData0Offset, .value = 0x0000'0000},
      {.address = kMailboxData1Offset, .value = 0x0000'0000},
  }));
  PowerController power_controller(&mmio_buffer_);

  const zx::result<> result = power_controller.SetSystemAgentGeyservilleEnabled(
      true, PowerController::RetryBehavior::kNoRetry);
  EXPECT_EQ(ZX_ERR_IO_REFUSED, result.status_value());
}

TEST_F(PowerControllerTest, SetSystemAgentGeyservilleEnabledFalseRetryImmediateSuccess) {
  mmio_range_.Expect(MockMmioRange::AccessList({
      {.address = kMailboxInterfaceOffset, .value = 0},
      {.address = kMailboxData0Offset, .value = 0x0000'0000, .write = true},
      {.address = kMailboxData1Offset, .value = 0x0000'0000, .write = true},
      {.address = kMailboxInterfaceOffset, .value = 0x8000'0021, .write = true},
      {.address = kMailboxInterfaceOffset, .value = 0x0000'0021},
      {.address = kMailboxData0Offset, .value = 0x0000'0001},
      {.address = kMailboxData1Offset, .value = 0x0000'0000},
  }));
  PowerController power_controller(&mmio_buffer_);

  const zx::result<> result = power_controller.SetSystemAgentGeyservilleEnabled(
      false, PowerController::RetryBehavior::kRetryUntilStateChanges);
  EXPECT_TRUE(result.is_ok()) << result.status_string();
}

TEST_F(PowerControllerTest, SetSystemAgentGeyservilleEnabledFalseRetrySuccessAfterRetries) {
  mmio_range_.Expect(MockMmioRange::AccessList({
      {.address = kMailboxInterfaceOffset, .value = 0},
      {.address = kMailboxData0Offset, .value = 0x0000'0000, .write = true},
      {.address = kMailboxData1Offset, .value = 0x0000'0000, .write = true},
      {.address = kMailboxInterfaceOffset, .value = 0x8000'0021, .write = true},
      {.address = kMailboxInterfaceOffset, .value = 0x0000'0021},
      {.address = kMailboxData0Offset, .value = 0x0000'0000},
      {.address = kMailboxData1Offset, .value = 0x0000'0000},

      // First retry.
      {.address = kMailboxInterfaceOffset, .value = 0x0000'0021},
      {.address = kMailboxData0Offset, .value = 0x0000'0000, .write = true},
      {.address = kMailboxData1Offset, .value = 0x0000'0000, .write = true},
      {.address = kMailboxInterfaceOffset, .value = 0x8000'0021, .write = true},
      {.address = kMailboxInterfaceOffset, .value = 0x0000'0021},
      {.address = kMailboxData0Offset, .value = 0x0000'0000},
      {.address = kMailboxData1Offset, .value = 0x0000'0000},

      // Second retry.
      {.address = kMailboxInterfaceOffset, .value = 0x0000'0021},
      {.address = kMailboxData0Offset, .value = 0x0000'0000, .write = true},
      {.address = kMailboxData1Offset, .value = 0x0000'0000, .write = true},
      {.address = kMailboxInterfaceOffset, .value = 0x8000'0021, .write = true},
      {.address = kMailboxInterfaceOffset, .value = 0x0000'0021},
      {.address = kMailboxData0Offset, .value = 0x0000'0001},
      {.address = kMailboxData1Offset, .value = 0x0000'0000},
  }));
  PowerController power_controller(&mmio_buffer_);

  const zx::result<> result = power_controller.SetSystemAgentGeyservilleEnabled(
      false, PowerController::RetryBehavior::kRetryUntilStateChanges);
  EXPECT_TRUE(result.is_ok()) << result.status_string();
}

TEST_F(PowerControllerTest, SetSystemAgentGeyservilleEnabledFalseRetryTimeout) {
  std::vector<ddk_fake::FakeMmioReg> fake_mmio_regs(0x140000 / sizeof(uint32_t));

  bool report_busy_mailbox = false;
  fake_mmio_regs[kMailboxInterfaceOffset / sizeof(uint32_t)].SetWriteCallback([&](uint64_t value) {
    EXPECT_EQ(0x8000'0021, value) << "Unexpected command";
    report_busy_mailbox = true;
  });
  fake_mmio_regs[kMailboxInterfaceOffset / sizeof(uint32_t)].SetReadCallback([&]() -> uint64_t {
    if (report_busy_mailbox) {
      // Report busy once, so SetSystemAgentGeyservilleEnabledRetryTimeout() gets some
      // sleep between retries.
      report_busy_mailbox = false;
      return 0x8000'0021;
    }
    return 0x0000'0021;
  });
  fake_mmio_regs[kMailboxData0Offset / sizeof(uint32_t)].SetReadCallback([&]() -> uint64_t {
    // Always produce the result "voltage level not applied", so
    // SetSystemAgentGeyservilleEnabled() retries.
    return 0x0000'0000;
  });

  ddk_fake::FakeMmioRegRegion fake_mmio_region(fake_mmio_regs.data(), sizeof(uint32_t),
                                               fake_mmio_regs.size());
  fdf::MmioBuffer fake_mmio_buffer = fake_mmio_region.GetMmioBuffer();
  PowerController power_controller(&fake_mmio_buffer);

  const zx::result<> result = power_controller.SetSystemAgentGeyservilleEnabled(
      2, PowerController::RetryBehavior::kRetryUntilStateChanges);
  EXPECT_EQ(ZX_ERR_IO_REFUSED, result.status_value());
}

TEST_F(PowerControllerTest, GetSystemAgentBlockTimeUsTigerLakeSuccess) {
  mmio_range_.Expect(MockMmioRange::AccessList({
      {.address = kMailboxInterfaceOffset, .value = 0},
      {.address = kMailboxData0Offset, .value = 0x0000'0000, .write = true},
      {.address = kMailboxData1Offset, .value = 0x0000'0000, .write = true},
      {.address = kMailboxInterfaceOffset, .value = 0x8000'0023, .write = true},
      {.address = kMailboxInterfaceOffset, .value = 0x0000'0000},
      {.address = kMailboxData0Offset, .value = 0x0000'0042},
      {.address = kMailboxData1Offset, .value = 0xdead'beef},
      {.address = kMailboxInterfaceOffset, .value = 0x0000'0000},
  }));
  PowerController power_controller(&mmio_buffer_);

  const zx::result<uint32_t> result = power_controller.GetSystemAgentBlockTimeUsTigerLake();
  ASSERT_TRUE(result.is_ok()) << result.status_string();
  EXPECT_EQ(0x42u, result.value());
}

TEST_F(PowerControllerTest, GetSystemAgentBlockTimeUsTigerLakeError) {
  mmio_range_.Expect(MockMmioRange::AccessList({
      {.address = kMailboxInterfaceOffset, .value = 0},
      {.address = kMailboxData0Offset, .value = 0x0000'0000, .write = true},
      {.address = kMailboxData1Offset, .value = 0x0000'0000, .write = true},
      {.address = kMailboxInterfaceOffset, .value = 0x8000'0023, .write = true},
      {.address = kMailboxInterfaceOffset, .value = 0x0000'0001},
      {.address = kMailboxData0Offset, .value = 0x0000'0042},
      {.address = kMailboxData1Offset, .value = 0xdead'beef},
      {.address = kMailboxInterfaceOffset, .value = 0x0000'0001},
  }));
  PowerController power_controller(&mmio_buffer_);

  const zx::result<uint32_t> result = power_controller.GetSystemAgentBlockTimeUsTigerLake();
  EXPECT_EQ(ZX_ERR_IO_REFUSED, result.status_value());
}

TEST_F(PowerControllerTest, GetSystemAgentBlockTimeUsTigerLakeTimeout) {
  mmio_range_.Expect(MockMmioRange::AccessList({
      {.address = kMailboxInterfaceOffset, .value = 0},
      {.address = kMailboxData0Offset, .value = 0x0000'0000, .write = true},
      {.address = kMailboxData1Offset, .value = 0x0000'0000, .write = true},
      {.address = kMailboxInterfaceOffset, .value = 0x8000'0023, .write = true},
  }));

  // Sadly, this test is tightly coupled with the GetSystemAgentBlockTimeUsTigerLake()
  // and Transact() implementations.
  //
  // In order to cover the mailbox timeout path, we need to know the number of
  // polls that the underlying Transact() call will perform before giving up.
  // Also, this test will have to be rewritten once we start using interrupts.
  static constexpr int kMailboxPollsBeforeTimeout = 101;
  for (int i = 0; i < kMailboxPollsBeforeTimeout; ++i) {
    mmio_range_.Expect({.address = kMailboxInterfaceOffset, .value = 0x8000'0023});
  }
  PowerController power_controller(&mmio_buffer_);

  const zx::result<uint32_t> result = power_controller.GetSystemAgentBlockTimeUsTigerLake();
  EXPECT_EQ(ZX_ERR_IO_MISSED_DEADLINE, result.status_value());
}

TEST_F(PowerControllerTest, GetSystemAgentBlockTimeUsKabyLake) {
  PowerController power_controller(&mmio_buffer_);
  const zx::result<uint32_t> result = power_controller.GetSystemAgentBlockTimeUsKabyLake();
  ASSERT_TRUE(result.is_ok()) << result.status_string();
  EXPECT_GT(result.value(), 0u);
}

TEST_F(PowerControllerTest, GetRawMemoryLatencyDataUsSuccess) {
  mmio_range_.Expect(MockMmioRange::AccessList({
      {.address = kMailboxInterfaceOffset, .value = 0},
      {.address = kMailboxData0Offset, .value = 0x0000'0000, .write = true},
      {.address = kMailboxData1Offset, .value = 0x0000'0000, .write = true},
      {.address = kMailboxInterfaceOffset, .value = 0x8000'0006, .write = true},
      {.address = kMailboxInterfaceOffset, .value = 0x0000'0000},
      {.address = kMailboxData0Offset, .value = 0x4443'4241},
      {.address = kMailboxData1Offset, .value = 0xdead'beef},
      {.address = kMailboxInterfaceOffset, .value = 0x0000'0000},

      {.address = kMailboxInterfaceOffset, .value = 0},
      {.address = kMailboxData0Offset, .value = 0x0000'0001, .write = true},
      {.address = kMailboxData1Offset, .value = 0x0000'0000, .write = true},
      {.address = kMailboxInterfaceOffset, .value = 0x8000'0006, .write = true},
      {.address = kMailboxInterfaceOffset, .value = 0x0000'0000},
      {.address = kMailboxData0Offset, .value = 0x4847'4645},
      {.address = kMailboxData1Offset, .value = 0xdead'beef},
      {.address = kMailboxInterfaceOffset, .value = 0x0000'0000},
  }));
  PowerController power_controller(&mmio_buffer_);

  const zx::result<std::array<uint8_t, 8>> result = power_controller.GetRawMemoryLatencyDataUs();
  ASSERT_TRUE(result.is_ok()) << result.status_string();
  EXPECT_THAT(result.value(), testing::ElementsAre(0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48));
}

TEST_F(PowerControllerTest, GetRawMemoryLatencyDataUsGroupOneFailure) {
  mmio_range_.Expect(MockMmioRange::AccessList({
      {.address = kMailboxInterfaceOffset, .value = 0},
      {.address = kMailboxData0Offset, .value = 0x0000'0000, .write = true},
      {.address = kMailboxData1Offset, .value = 0x0000'0000, .write = true},
      {.address = kMailboxInterfaceOffset, .value = 0x8000'0006, .write = true},
      {.address = kMailboxInterfaceOffset, .value = 0x0000'0001},
      {.address = kMailboxData0Offset, .value = 0x4443'4241},
      {.address = kMailboxData1Offset, .value = 0xdead'beef},
      {.address = kMailboxInterfaceOffset, .value = 0x0000'0001},
  }));
  PowerController power_controller(&mmio_buffer_);

  const zx::result<std::array<uint8_t, 8>> result = power_controller.GetRawMemoryLatencyDataUs();
  EXPECT_EQ(ZX_ERR_IO_REFUSED, result.status_value());
}

TEST_F(PowerControllerTest, GetRawMemoryLatencyDataUsGroupTwoFailure) {
  mmio_range_.Expect(MockMmioRange::AccessList({
      {.address = kMailboxInterfaceOffset, .value = 0},
      {.address = kMailboxData0Offset, .value = 0x0000'0000, .write = true},
      {.address = kMailboxData1Offset, .value = 0x0000'0000, .write = true},
      {.address = kMailboxInterfaceOffset, .value = 0x8000'0006, .write = true},
      {.address = kMailboxInterfaceOffset, .value = 0x0000'0000},
      {.address = kMailboxData0Offset, .value = 0x4443'4241},
      {.address = kMailboxData1Offset, .value = 0xdead'beef},
      {.address = kMailboxInterfaceOffset, .value = 0x0000'0000},

      {.address = kMailboxInterfaceOffset, .value = 0},
      {.address = kMailboxData0Offset, .value = 0x0000'0001, .write = true},
      {.address = kMailboxData1Offset, .value = 0x0000'0000, .write = true},
      {.address = kMailboxInterfaceOffset, .value = 0x8000'0006, .write = true},
      {.address = kMailboxInterfaceOffset, .value = 0x0000'0001},
      {.address = kMailboxData0Offset, .value = 0x4847'4645},
      {.address = kMailboxData1Offset, .value = 0xdead'beef},
      {.address = kMailboxInterfaceOffset, .value = 0x0000'0001},
  }));
  PowerController power_controller(&mmio_buffer_);

  const zx::result<std::array<uint8_t, 8>> result = power_controller.GetRawMemoryLatencyDataUs();
  EXPECT_EQ(ZX_ERR_IO_REFUSED, result.status_value());
}

TEST_F(PowerControllerTest, GetRawMemoryLatencyDataUsGroupOneTimeout) {
  mmio_range_.Expect(MockMmioRange::AccessList({
      {.address = kMailboxInterfaceOffset, .value = 0},
      {.address = kMailboxData0Offset, .value = 0x0000'0000, .write = true},
      {.address = kMailboxData1Offset, .value = 0x0000'0000, .write = true},
      {.address = kMailboxInterfaceOffset, .value = 0x8000'0006, .write = true},
  }));

  // Sadly, this test is tightly coupled with the GetRawMemoryLatencyDataUs()
  // and Transact() implementations.
  //
  // In order to cover the mailbox timeout path, we need to know the number of
  // polls that the underlying Transact() call will perform before giving up.
  // Also, this test will have to be rewritten once we start using interrupts.
  static constexpr int kMailboxPollsBeforeTimeout = 101;
  for (int i = 0; i < kMailboxPollsBeforeTimeout; ++i) {
    mmio_range_.Expect({.address = kMailboxInterfaceOffset, .value = 0x8000'0006});
  }
  PowerController power_controller(&mmio_buffer_);

  const zx::result<std::array<uint8_t, 8>> result = power_controller.GetRawMemoryLatencyDataUs();
  EXPECT_EQ(ZX_ERR_IO_MISSED_DEADLINE, result.status_value());
}

TEST_F(PowerControllerTest, GetRawMemoryLatencyDataUsGroupTwoTimeout) {
  mmio_range_.Expect(MockMmioRange::AccessList({
      {.address = kMailboxInterfaceOffset, .value = 0},
      {.address = kMailboxData0Offset, .value = 0x0000'0000, .write = true},
      {.address = kMailboxData1Offset, .value = 0x0000'0000, .write = true},
      {.address = kMailboxInterfaceOffset, .value = 0x8000'0006, .write = true},
      {.address = kMailboxInterfaceOffset, .value = 0x0000'0000},
      {.address = kMailboxData0Offset, .value = 0x4443'4241},
      {.address = kMailboxData1Offset, .value = 0xdead'beef},
      {.address = kMailboxInterfaceOffset, .value = 0x0000'0000},

      {.address = kMailboxInterfaceOffset, .value = 0},
      {.address = kMailboxData0Offset, .value = 0x0000'0001, .write = true},
      {.address = kMailboxData1Offset, .value = 0x0000'0000, .write = true},
      {.address = kMailboxInterfaceOffset, .value = 0x8000'0006, .write = true},
  }));

  // Sadly, this test is tightly coupled with the GetRawMemoryLatencyDataUs()
  // and Transact() implementations.
  //
  // In order to cover the mailbox timeout path, we need to know the number of
  // polls that the underlying Transact() call will perform before giving up.
  // Also, this test will have to be rewritten once we start using interrupts.
  static constexpr int kMailboxPollsBeforeTimeout = 101;
  for (int i = 0; i < kMailboxPollsBeforeTimeout; ++i) {
    mmio_range_.Expect({.address = kMailboxInterfaceOffset, .value = 0x8000'0006});
  }
  PowerController power_controller(&mmio_buffer_);

  const zx::result<std::array<uint8_t, 8>> result = power_controller.GetRawMemoryLatencyDataUs();
  EXPECT_EQ(ZX_ERR_IO_MISSED_DEADLINE, result.status_value());
}

TEST(MemorySubsystemInfoGlobalInfoTest, CreateFromMailboxDataTigerLake) {
  auto dell_5420_info = MemorySubsystemInfo::GlobalInfo::CreateFromMailboxDataTigerLake(0x410);
  EXPECT_EQ(MemorySubsystemInfo::RamType::kDoubleDataRam4, dell_5420_info.ram_type);
  EXPECT_EQ(1, dell_5420_info.memory_channel_count);
  EXPECT_EQ(4, dell_5420_info.agent_point_count);

  auto nuc_11_info = MemorySubsystemInfo::GlobalInfo::CreateFromMailboxDataTigerLake(0x120);
  EXPECT_EQ(MemorySubsystemInfo::RamType::kDoubleDataRam4, dell_5420_info.ram_type);
  EXPECT_EQ(2, nuc_11_info.memory_channel_count);
  EXPECT_EQ(1, nuc_11_info.agent_point_count);
}

TEST(MemorySubsystemInfoAgentPointTest, CreateFromMailboxDataTigerLake) {
  auto dell_5420_point1 =
      MemorySubsystemInfo::AgentPoint::CreateFromMailboxDataTigerLake(0x2308'0f0f'0080);
  EXPECT_EQ(2'133'248, dell_5420_point1.dram_clock_khz);
  EXPECT_EQ(15, dell_5420_point1.row_precharge_to_open_cycles);
  EXPECT_EQ(15, dell_5420_point1.row_access_to_column_access_delay_cycles);
  EXPECT_EQ(8, dell_5420_point1.read_to_precharge_cycles);
  EXPECT_EQ(35, dell_5420_point1.row_activate_to_precharge_cycles);

  // NUC 11 has a single point with this configuration.
  auto dell_5420_point3 =
      MemorySubsystemInfo::AgentPoint::CreateFromMailboxDataTigerLake(0x340c'1616'00c0);
  EXPECT_EQ(3'199'872, dell_5420_point3.dram_clock_khz);
  EXPECT_EQ(22, dell_5420_point3.row_precharge_to_open_cycles);
  EXPECT_EQ(22, dell_5420_point3.row_access_to_column_access_delay_cycles);
  EXPECT_EQ(12, dell_5420_point3.read_to_precharge_cycles);
  EXPECT_EQ(52, dell_5420_point3.row_activate_to_precharge_cycles);

  auto dell_5420_point4 =
      MemorySubsystemInfo::AgentPoint::CreateFromMailboxDataTigerLake(0x2b0a'1313'00a0);
  EXPECT_EQ(2'666'560, dell_5420_point4.dram_clock_khz);
  EXPECT_EQ(19, dell_5420_point4.row_precharge_to_open_cycles);
  EXPECT_EQ(19, dell_5420_point4.row_access_to_column_access_delay_cycles);
  EXPECT_EQ(10, dell_5420_point4.read_to_precharge_cycles);
  EXPECT_EQ(43, dell_5420_point4.row_activate_to_precharge_cycles);
}

TEST_F(PowerControllerTest, GetMemorySubsystemInfoTigerLakeSuccessNoPoints) {
  mmio_range_.Expect(MockMmioRange::AccessList({
      {.address = kMailboxInterfaceOffset, .value = 0},
      {.address = kMailboxData0Offset, .value = 0x0000'0000, .write = true},
      {.address = kMailboxData1Offset, .value = 0x0000'0000, .write = true},
      {.address = kMailboxInterfaceOffset, .value = 0x8000'000d, .write = true},
      {.address = kMailboxInterfaceOffset, .value = 0x0000'0000},
      {.address = kMailboxData0Offset, .value = 0x0000'0025},
      {.address = kMailboxData1Offset, .value = 0x0000'0000},
  }));
  PowerController power_controller(&mmio_buffer_);

  const zx::result<MemorySubsystemInfo> result = power_controller.GetMemorySubsystemInfoTigerLake();
  ASSERT_TRUE(result.is_ok()) << result.status_string();
  EXPECT_EQ(MemorySubsystemInfo::RamType::kLowPowerDoubleDataRam3, result->global_info.ram_type);
  EXPECT_EQ(2, result->global_info.memory_channel_count);
  EXPECT_EQ(0, result->global_info.agent_point_count);
}

TEST_F(PowerControllerTest, GetMemorySubsystemInfoTigerLakeSuccessOnePoint) {
  mmio_range_.Expect(MockMmioRange::AccessList({
      {.address = kMailboxInterfaceOffset, .value = 0},
      {.address = kMailboxData0Offset, .value = 0x0000'0000, .write = true},
      {.address = kMailboxData1Offset, .value = 0x0000'0000, .write = true},
      {.address = kMailboxInterfaceOffset, .value = 0x8000'000d, .write = true},
      {.address = kMailboxInterfaceOffset, .value = 0x0000'0000},
      {.address = kMailboxData0Offset, .value = 0x0000'0125},
      {.address = kMailboxData1Offset, .value = 0x0000'0000},

      {.address = kMailboxInterfaceOffset, .value = 0},
      {.address = kMailboxData0Offset, .value = 0x0000'0000, .write = true},
      {.address = kMailboxData1Offset, .value = 0x0000'0000, .write = true},
      {.address = kMailboxInterfaceOffset, .value = 0x8000'010d, .write = true},
      {.address = kMailboxInterfaceOffset, .value = 0x0000'0000},
      {.address = kMailboxData0Offset, .value = 0x1616'00c0},
      {.address = kMailboxData1Offset, .value = 0x0000'340c},
      {.address = kMailboxInterfaceOffset, .value = 0x0000'0000},
  }));
  PowerController power_controller(&mmio_buffer_);

  const zx::result<MemorySubsystemInfo> result = power_controller.GetMemorySubsystemInfoTigerLake();
  ASSERT_TRUE(result.is_ok()) << result.status_string();

  EXPECT_EQ(MemorySubsystemInfo::RamType::kLowPowerDoubleDataRam3, result->global_info.ram_type);
  EXPECT_EQ(2, result->global_info.memory_channel_count);
  EXPECT_EQ(1, result->global_info.agent_point_count);

  EXPECT_EQ(3'199'872, result->points[0].dram_clock_khz);
  EXPECT_EQ(22, result->points[0].row_precharge_to_open_cycles);
  EXPECT_EQ(22, result->points[0].row_access_to_column_access_delay_cycles);
  EXPECT_EQ(12, result->points[0].read_to_precharge_cycles);
  EXPECT_EQ(52, result->points[0].row_activate_to_precharge_cycles);
}

TEST_F(PowerControllerTest, GetMemorySubsystemInfoTigerLakeSuccessThreePoints) {
  mmio_range_.Expect(MockMmioRange::AccessList({
      {.address = kMailboxInterfaceOffset, .value = 0},
      {.address = kMailboxData0Offset, .value = 0x0000'0000, .write = true},
      {.address = kMailboxData1Offset, .value = 0x0000'0000, .write = true},
      {.address = kMailboxInterfaceOffset, .value = 0x8000'000d, .write = true},
      {.address = kMailboxInterfaceOffset, .value = 0x0000'0000},
      {.address = kMailboxData0Offset, .value = 0x0000'0310},
      {.address = kMailboxData1Offset, .value = 0x0000'0000},

      {.address = kMailboxInterfaceOffset, .value = 0},
      {.address = kMailboxData0Offset, .value = 0x0000'0000, .write = true},
      {.address = kMailboxData1Offset, .value = 0x0000'0000, .write = true},
      {.address = kMailboxInterfaceOffset, .value = 0x8000'010d, .write = true},
      {.address = kMailboxInterfaceOffset, .value = 0x0000'0000},
      {.address = kMailboxData0Offset, .value = 0x0f0f'0080},
      {.address = kMailboxData1Offset, .value = 0x0000'2308},
      {.address = kMailboxInterfaceOffset, .value = 0x0000'0000},

      {.address = kMailboxInterfaceOffset, .value = 0},
      {.address = kMailboxData0Offset, .value = 0x0000'0000, .write = true},
      {.address = kMailboxData1Offset, .value = 0x0000'0000, .write = true},
      {.address = kMailboxInterfaceOffset, .value = 0x8001'010d, .write = true},
      {.address = kMailboxInterfaceOffset, .value = 0x0000'0000},
      {.address = kMailboxData0Offset, .value = 0x1616'00c0},
      {.address = kMailboxData1Offset, .value = 0x0000'340c},
      {.address = kMailboxInterfaceOffset, .value = 0x0000'0000},

      {.address = kMailboxInterfaceOffset, .value = 0},
      {.address = kMailboxData0Offset, .value = 0x0000'0000, .write = true},
      {.address = kMailboxData1Offset, .value = 0x0000'0000, .write = true},
      {.address = kMailboxInterfaceOffset, .value = 0x8002'010d, .write = true},
      {.address = kMailboxInterfaceOffset, .value = 0x0000'0000},
      {.address = kMailboxData0Offset, .value = 0x1313'00a0},
      {.address = kMailboxData1Offset, .value = 0x0000'2b0a},
      {.address = kMailboxInterfaceOffset, .value = 0x0000'0000},
  }));
  PowerController power_controller(&mmio_buffer_);

  const zx::result<MemorySubsystemInfo> result = power_controller.GetMemorySubsystemInfoTigerLake();
  ASSERT_TRUE(result.is_ok()) << result.status_string();

  EXPECT_EQ(MemorySubsystemInfo::RamType::kDoubleDataRam4, result->global_info.ram_type);
  EXPECT_EQ(1, result->global_info.memory_channel_count);
  EXPECT_EQ(3, result->global_info.agent_point_count);

  EXPECT_EQ(2'133'248, result->points[0].dram_clock_khz);
  EXPECT_EQ(15, result->points[0].row_precharge_to_open_cycles);
  EXPECT_EQ(15, result->points[0].row_access_to_column_access_delay_cycles);
  EXPECT_EQ(8, result->points[0].read_to_precharge_cycles);
  EXPECT_EQ(35, result->points[0].row_activate_to_precharge_cycles);

  EXPECT_EQ(3'199'872, result->points[1].dram_clock_khz);
  EXPECT_EQ(22, result->points[1].row_precharge_to_open_cycles);
  EXPECT_EQ(22, result->points[1].row_access_to_column_access_delay_cycles);
  EXPECT_EQ(12, result->points[1].read_to_precharge_cycles);
  EXPECT_EQ(52, result->points[1].row_activate_to_precharge_cycles);

  EXPECT_EQ(2'666'560, result->points[2].dram_clock_khz);
  EXPECT_EQ(19, result->points[2].row_precharge_to_open_cycles);
  EXPECT_EQ(19, result->points[2].row_access_to_column_access_delay_cycles);
  EXPECT_EQ(10, result->points[2].read_to_precharge_cycles);
  EXPECT_EQ(43, result->points[2].row_activate_to_precharge_cycles);
}

TEST_F(PowerControllerTest, GetMemorySubsystemInfoTigerLakePointOneFailure) {
  mmio_range_.Expect(MockMmioRange::AccessList({
      // Based on the MMIO list in the SuccessOnePoint test.
      {.address = kMailboxInterfaceOffset, .value = 0},
      {.address = kMailboxData0Offset, .value = 0x0000'0000, .write = true},
      {.address = kMailboxData1Offset, .value = 0x0000'0000, .write = true},
      {.address = kMailboxInterfaceOffset, .value = 0x8000'000d, .write = true},
      {.address = kMailboxInterfaceOffset, .value = 0x0000'0000},
      {.address = kMailboxData0Offset, .value = 0x0000'0125},
      {.address = kMailboxData1Offset, .value = 0x0000'0000},

      {.address = kMailboxInterfaceOffset, .value = 0},
      {.address = kMailboxData0Offset, .value = 0x0000'0000, .write = true},
      {.address = kMailboxData1Offset, .value = 0x0000'0000, .write = true},
      {.address = kMailboxInterfaceOffset, .value = 0x8000'010d, .write = true},
      {.address = kMailboxInterfaceOffset, .value = 0x0000'0001},
      {.address = kMailboxData0Offset, .value = 0x1616'00c0},
      {.address = kMailboxData1Offset, .value = 0x0000'340c},
      {.address = kMailboxInterfaceOffset, .value = 0x0000'0001},
  }));
  PowerController power_controller(&mmio_buffer_);

  const zx::result<MemorySubsystemInfo> result = power_controller.GetMemorySubsystemInfoTigerLake();
  EXPECT_EQ(ZX_ERR_IO_REFUSED, result.status_value());
}

TEST_F(PowerControllerTest, GetMemorySubsystemInfoTigerLakePointTwoFailure) {
  mmio_range_.Expect(MockMmioRange::AccessList({
      // Based on the MMIO list in the SuccessThreePoints test.
      {.address = kMailboxInterfaceOffset, .value = 0},
      {.address = kMailboxData0Offset, .value = 0x0000'0000, .write = true},
      {.address = kMailboxData1Offset, .value = 0x0000'0000, .write = true},
      {.address = kMailboxInterfaceOffset, .value = 0x8000'000d, .write = true},
      {.address = kMailboxInterfaceOffset, .value = 0x0000'0000},
      {.address = kMailboxData0Offset, .value = 0x0000'0310},
      {.address = kMailboxData1Offset, .value = 0x0000'0000},

      {.address = kMailboxInterfaceOffset, .value = 0},
      {.address = kMailboxData0Offset, .value = 0x0000'0000, .write = true},
      {.address = kMailboxData1Offset, .value = 0x0000'0000, .write = true},
      {.address = kMailboxInterfaceOffset, .value = 0x8000'010d, .write = true},
      {.address = kMailboxInterfaceOffset, .value = 0x0000'0000},
      {.address = kMailboxData0Offset, .value = 0x0f0f'0080},
      {.address = kMailboxData1Offset, .value = 0x0000'2308},
      {.address = kMailboxInterfaceOffset, .value = 0x0000'0000},

      {.address = kMailboxInterfaceOffset, .value = 0},
      {.address = kMailboxData0Offset, .value = 0x0000'0000, .write = true},
      {.address = kMailboxData1Offset, .value = 0x0000'0000, .write = true},
      {.address = kMailboxInterfaceOffset, .value = 0x8001'010d, .write = true},
      {.address = kMailboxInterfaceOffset, .value = 0x0000'0001},
      {.address = kMailboxData0Offset, .value = 0x1616'00c0},
      {.address = kMailboxData1Offset, .value = 0x0000'340c},
      {.address = kMailboxInterfaceOffset, .value = 0x0000'0001},
  }));
  PowerController power_controller(&mmio_buffer_);

  const zx::result<MemorySubsystemInfo> result = power_controller.GetMemorySubsystemInfoTigerLake();
  EXPECT_EQ(ZX_ERR_IO_REFUSED, result.status_value());
}

TEST_F(PowerControllerTest, GetMemorySubsystemInfoTigerLakeGlobalInfoTimeout) {
  mmio_range_.Expect(MockMmioRange::AccessList({
      {.address = kMailboxInterfaceOffset, .value = 0},
      {.address = kMailboxData0Offset, .value = 0x0000'0000, .write = true},
      {.address = kMailboxData1Offset, .value = 0x0000'0000, .write = true},
      {.address = kMailboxInterfaceOffset, .value = 0x8000'000d, .write = true},
  }));

  // Sadly, this test is tightly coupled with the
  // GetMemorySubsystemInfoTigerLake() and Transact() implementations.
  //
  // In order to cover the mailbox timeout path, we need to know the number of
  // polls that the underlying Transact() call will perform before giving up.
  // Also, this test will have to be rewritten once we start using interrupts.
  static constexpr int kMailboxPollsBeforeTimeout = 151;
  for (int i = 0; i < kMailboxPollsBeforeTimeout; ++i) {
    mmio_range_.Expect({.address = kMailboxInterfaceOffset, .value = 0x8000'000d});
  }
  PowerController power_controller(&mmio_buffer_);

  const zx::result<MemorySubsystemInfo> result = power_controller.GetMemorySubsystemInfoTigerLake();
  EXPECT_EQ(ZX_ERR_IO_MISSED_DEADLINE, result.status_value());
}

TEST_F(PowerControllerTest, GetMemorySubsystemInfoTigerLakePointOneTimeout) {
  mmio_range_.Expect(MockMmioRange::AccessList({
      // Based on the MMIO list in the PointOneFailure test.
      {.address = kMailboxInterfaceOffset, .value = 0},
      {.address = kMailboxData0Offset, .value = 0x0000'0000, .write = true},
      {.address = kMailboxData1Offset, .value = 0x0000'0000, .write = true},
      {.address = kMailboxInterfaceOffset, .value = 0x8000'000d, .write = true},
      {.address = kMailboxInterfaceOffset, .value = 0x0000'0000},
      {.address = kMailboxData0Offset, .value = 0x0000'0125},
      {.address = kMailboxData1Offset, .value = 0x0000'0000},

      {.address = kMailboxInterfaceOffset, .value = 0},
      {.address = kMailboxData0Offset, .value = 0x0000'0000, .write = true},
      {.address = kMailboxData1Offset, .value = 0x0000'0000, .write = true},
      {.address = kMailboxInterfaceOffset, .value = 0x8000'010d, .write = true},
  }));

  // Sadly, this test is tightly coupled with the
  // GetMemorySubsystemInfoTigerLake() and Transact() implementations.
  //
  // In order to cover the mailbox timeout path, we need to know the number of
  // polls that the underlying Transact() call will perform before giving up.
  // Also, this test will have to be rewritten once we start using interrupts.
  static constexpr int kMailboxPollsBeforeTimeout = 151;
  for (int i = 0; i < kMailboxPollsBeforeTimeout; ++i) {
    mmio_range_.Expect({.address = kMailboxInterfaceOffset, .value = 0x8000'010d});
  }
  PowerController power_controller(&mmio_buffer_);

  const zx::result<MemorySubsystemInfo> result = power_controller.GetMemorySubsystemInfoTigerLake();
  EXPECT_EQ(ZX_ERR_IO_MISSED_DEADLINE, result.status_value());
}

TEST_F(PowerControllerTest, GetMemorySubsystemInfoTigerLakePointTwoTimeout) {
  mmio_range_.Expect(MockMmioRange::AccessList({
      // Based on the MMIO list in the PointTwoFailure test.
      {.address = kMailboxInterfaceOffset, .value = 0},
      {.address = kMailboxData0Offset, .value = 0x0000'0000, .write = true},
      {.address = kMailboxData1Offset, .value = 0x0000'0000, .write = true},
      {.address = kMailboxInterfaceOffset, .value = 0x8000'000d, .write = true},
      {.address = kMailboxInterfaceOffset, .value = 0x0000'0000},
      {.address = kMailboxData0Offset, .value = 0x0000'0310},
      {.address = kMailboxData1Offset, .value = 0x0000'0000},

      {.address = kMailboxInterfaceOffset, .value = 0},
      {.address = kMailboxData0Offset, .value = 0x0000'0000, .write = true},
      {.address = kMailboxData1Offset, .value = 0x0000'0000, .write = true},
      {.address = kMailboxInterfaceOffset, .value = 0x8000'010d, .write = true},
      {.address = kMailboxInterfaceOffset, .value = 0x0000'0000},
      {.address = kMailboxData0Offset, .value = 0x0f0f'0080},
      {.address = kMailboxData1Offset, .value = 0x0000'2308},
      {.address = kMailboxInterfaceOffset, .value = 0x0000'0000},

      {.address = kMailboxInterfaceOffset, .value = 0},
      {.address = kMailboxData0Offset, .value = 0x0000'0000, .write = true},
      {.address = kMailboxData1Offset, .value = 0x0000'0000, .write = true},
      {.address = kMailboxInterfaceOffset, .value = 0x8001'010d, .write = true},
  }));

  // Sadly, this test is tightly coupled with the
  // GetMemorySubsystemInfoTigerLake() and Transact() implementations.
  //
  // In order to cover the mailbox timeout path, we need to know the number of
  // polls that the underlying Transact() call will perform before giving up.
  // Also, this test will have to be rewritten once we start using interrupts.
  static constexpr int kMailboxPollsBeforeTimeout = 151;
  for (int i = 0; i < kMailboxPollsBeforeTimeout; ++i) {
    mmio_range_.Expect({.address = kMailboxInterfaceOffset, .value = 0x8001'010d});
  }
  PowerController power_controller(&mmio_buffer_);

  const zx::result<MemorySubsystemInfo> result = power_controller.GetMemorySubsystemInfoTigerLake();
  EXPECT_EQ(ZX_ERR_IO_MISSED_DEADLINE, result.status_value());
}

}  // namespace

}  // namespace i915_tgl
