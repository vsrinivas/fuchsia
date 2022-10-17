// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/graphics/display/drivers/intel-i915-tgl/ddi-aux-channel.h"

#include <lib/mmio/mmio-buffer.h>
#include <lib/stdcompat/span.h>
#include <lib/zx/status.h>
#include <zircon/errors.h>

#include <cstdint>
#include <optional>

#include <gtest/gtest.h>

#include "src/graphics/display/drivers/intel-i915-tgl/mock-mmio-range.h"
#include "src/graphics/display/drivers/intel-i915-tgl/registers-ddi.h"

namespace i915_tgl {

namespace {

// Register addresses from IHD-OS-KBL-Vol 2c-1.17 Part 1 pages 436-441.
constexpr int kDdiAuxCtlAOffset = 0x64010;
constexpr int kDdiAuxDataA0Offset = 0x64014;
constexpr int kDdiAuxDataA1Offset = 0x64018;
constexpr int kDdiAuxDataA2Offset = 0x6401c;
constexpr int kDdiAuxDataA3Offset = 0x64020;
constexpr int kDdiAuxDataA4Offset = 0x64024;

// Register addresses from IHD-OS-TGL-Vol 2c-1.22.Rev 2.0 Part 1 pages 342-351.
constexpr int kDdiAuxCtlCOffset = 0x64210;
constexpr int kDdiAuxCtlUsbBOffset = 0x64410;

// Defaults from IHD-OS-KBL-Vol 2c-1.17 Part 1 page 436. The other PRMs list the
// same defaults.
constexpr uint32_t kDdiAuxCtlDefault = 0b0000'0000'0000'0000'0000'0010'0011'1111;

// Configuration that doesn't require any fixes. Minimizes logging in tests.
//
// Based on IHD-OS-KBL-Vol 2c-1.17 Part 1 page 436, but the Fast Wake Pulse
// Count is set to the recommended value (7) instead of the default value (17),
// and the timeout is set to the maximum possible.
constexpr uint32_t kDdiAuxCtlQuietStart = 0b0000'1100'0000'0000'0000'0000'1111'1111;

constexpr int kAtlasGpuDeviceId = 0x591c;
constexpr int kDell5420GpuDeviceId = 0x9a49;

class DdiAuxChannelTest : public ::testing::Test {
 public:
  DdiAuxChannelTest() = default;
  ~DdiAuxChannelTest() override = default;

  void SetUp() override {}
  void TearDown() override { mmio_range_.CheckAllAccessesReplayed(); }

 protected:
  constexpr static int kMmioRangeSize = 0x100000;
  MockMmioRange mmio_range_{kMmioRangeSize, MockMmioRange::Size::k32};
  fdf::MmioBuffer mmio_buffer_{mmio_range_.GetMmioBuffer()};
};

// Verify that the constructor grabs the correct control register.
class DdiAuxChannelConstructorTest : public DdiAuxChannelTest {};

TEST_F(DdiAuxChannelConstructorTest, KabyLakeDdiA) {
  mmio_range_.Expect(MockMmioRange::AccessList({
      {.address = kDdiAuxCtlAOffset, .value = kDdiAuxCtlQuietStart},
  }));

  DdiAuxChannel aux_channel(&mmio_buffer_, tgl_registers::DDI_A, kAtlasGpuDeviceId);
}
TEST_F(DdiAuxChannelConstructorTest, KabyLakeDdiC) {
  mmio_range_.Expect(MockMmioRange::AccessList({
      {.address = kDdiAuxCtlCOffset, .value = kDdiAuxCtlQuietStart},
  }));

  DdiAuxChannel aux_channel(&mmio_buffer_, tgl_registers::DDI_C, kAtlasGpuDeviceId);
}
TEST_F(DdiAuxChannelConstructorTest, TigerLakeDdiA) {
  mmio_range_.Expect(MockMmioRange::AccessList({
      {.address = kDdiAuxCtlAOffset, .value = kDdiAuxCtlQuietStart},
  }));

  DdiAuxChannel aux_channel(&mmio_buffer_, tgl_registers::DDI_A, kDell5420GpuDeviceId);
}
TEST_F(DdiAuxChannelConstructorTest, TigerLakeDdiC) {
  mmio_range_.Expect(MockMmioRange::AccessList({
      {.address = kDdiAuxCtlCOffset, .value = kDdiAuxCtlQuietStart},
  }));

  DdiAuxChannel aux_channel(&mmio_buffer_, tgl_registers::DDI_C, kDell5420GpuDeviceId);
}
TEST_F(DdiAuxChannelConstructorTest, TigerLakeDdiTC2) {
  mmio_range_.Expect(MockMmioRange::AccessList({
      {.address = kDdiAuxCtlUsbBOffset, .value = kDdiAuxCtlQuietStart},
  }));

  DdiAuxChannel aux_channel(&mmio_buffer_, tgl_registers::DDI_TC_2, kDell5420GpuDeviceId);
}

TEST_F(DdiAuxChannelConstructorTest, WaitsForPendingTransaction) {
  mmio_range_.Expect(MockMmioRange::AccessList({
      {.address = kDdiAuxCtlAOffset, .value = 0x8000'00ff},
      {.address = kDdiAuxCtlAOffset, .value = 0x4000'00ff},
  }));

  DdiAuxChannel aux_channel(&mmio_buffer_, tgl_registers::DDI_A, kAtlasGpuDeviceId);
}

TEST_F(DdiAuxChannelConstructorTest, ControlNotChangedDuringPendingTransaction) {
  mmio_range_.Expect(MockMmioRange::AccessList({
      // The SYNC pulse count fields are both incorrect. This verifies that they
      // don't get overwritten (with good values) while a transaction is pending.
      {.address = kDdiAuxCtlAOffset, .value = 0x8000'0000},
      {.address = kDdiAuxCtlAOffset, .value = 0x4000'0000},
  }));

  DdiAuxChannel aux_channel(&mmio_buffer_, tgl_registers::DDI_A, kAtlasGpuDeviceId);
}

class DdiAuxChannelConfigTest : public DdiAuxChannelTest {};

TEST_F(DdiAuxChannelConfigTest, KabyLakeDefault) {
  mmio_range_.Expect(MockMmioRange::AccessList({
      {.address = kDdiAuxCtlAOffset, .value = kDdiAuxCtlDefault},
  }));
  DdiAuxChannel aux_channel(&mmio_buffer_, tgl_registers::DDI_A, kAtlasGpuDeviceId);

  const DdiAuxChannelConfig config = aux_channel.Config();
  EXPECT_EQ(400, config.timeout_us);
  EXPECT_EQ(32, config.sync_pulse_count);
  EXPECT_EQ(18, config.fast_wake_sync_pulse_count);
  EXPECT_EQ(false, config.use_thunderbolt);
}
TEST_F(DdiAuxChannelConfigTest, KabyLakeQuietStart) {
  mmio_range_.Expect(MockMmioRange::AccessList({
      {.address = kDdiAuxCtlAOffset, .value = kDdiAuxCtlQuietStart},
  }));
  DdiAuxChannel aux_channel(&mmio_buffer_, tgl_registers::DDI_A, kAtlasGpuDeviceId);

  const DdiAuxChannelConfig config = aux_channel.Config();
  EXPECT_EQ(1'600, config.timeout_us);
  EXPECT_EQ(32, config.sync_pulse_count);
  EXPECT_EQ(8, config.fast_wake_sync_pulse_count);
  EXPECT_EQ(false, config.use_thunderbolt);
}
TEST_F(DdiAuxChannelConfigTest, KabyLakeZeros) {
  mmio_range_.Expect(MockMmioRange::AccessList({
      {.address = kDdiAuxCtlAOffset, .value = 0},
  }));
  DdiAuxChannel aux_channel(&mmio_buffer_, tgl_registers::DDI_A, kAtlasGpuDeviceId);

  const DdiAuxChannelConfig config = aux_channel.Config();
  EXPECT_EQ(400, config.timeout_us);
  EXPECT_EQ(1, config.sync_pulse_count);
  EXPECT_EQ(1, config.fast_wake_sync_pulse_count);
  EXPECT_EQ(false, config.use_thunderbolt);
}
TEST_F(DdiAuxChannelConfigTest, KabyLakeOnes) {
  mmio_range_.Expect(MockMmioRange::AccessList({
      {.address = kDdiAuxCtlAOffset, .value = 0x7ef0'03ff},
  }));
  DdiAuxChannel aux_channel(&mmio_buffer_, tgl_registers::DDI_A, kAtlasGpuDeviceId);

  const DdiAuxChannelConfig config = aux_channel.Config();
  EXPECT_EQ(1'600, config.timeout_us);
  EXPECT_EQ(32, config.sync_pulse_count);
  EXPECT_EQ(32, config.fast_wake_sync_pulse_count);
  EXPECT_EQ(false, config.use_thunderbolt);
}

TEST_F(DdiAuxChannelConfigTest, TigerLakeDefault) {
  mmio_range_.Expect(MockMmioRange::AccessList({
      {.address = kDdiAuxCtlAOffset, .value = kDdiAuxCtlDefault},
  }));
  DdiAuxChannel aux_channel(&mmio_buffer_, tgl_registers::DDI_A, kDell5420GpuDeviceId);

  const DdiAuxChannelConfig config = aux_channel.Config();
  EXPECT_EQ(400, config.timeout_us);
  EXPECT_EQ(32, config.sync_pulse_count);
  EXPECT_EQ(18, config.fast_wake_sync_pulse_count);
  EXPECT_EQ(false, config.use_thunderbolt);
}
TEST_F(DdiAuxChannelConfigTest, TigerLakeQuietStart) {
  mmio_range_.Expect(MockMmioRange::AccessList({
      {.address = kDdiAuxCtlAOffset, .value = kDdiAuxCtlQuietStart},
  }));
  DdiAuxChannel aux_channel(&mmio_buffer_, tgl_registers::DDI_A, kDell5420GpuDeviceId);

  const DdiAuxChannelConfig config = aux_channel.Config();
  EXPECT_EQ(4'000, config.timeout_us);
  EXPECT_EQ(32, config.sync_pulse_count);
  EXPECT_EQ(8, config.fast_wake_sync_pulse_count);
  EXPECT_EQ(false, config.use_thunderbolt);
}
TEST_F(DdiAuxChannelConfigTest, TigerLakeZeros) {
  mmio_range_.Expect(MockMmioRange::AccessList({
      {.address = kDdiAuxCtlAOffset, .value = 0},
  }));
  DdiAuxChannel aux_channel(&mmio_buffer_, tgl_registers::DDI_A, kDell5420GpuDeviceId);

  const DdiAuxChannelConfig config = aux_channel.Config();
  EXPECT_EQ(400, config.timeout_us);
  EXPECT_EQ(1, config.sync_pulse_count);
  EXPECT_EQ(1, config.fast_wake_sync_pulse_count);
  EXPECT_EQ(false, config.use_thunderbolt);
}
TEST_F(DdiAuxChannelConfigTest, TigerLakeOnes) {
  mmio_range_.Expect(MockMmioRange::AccessList({
      {.address = kDdiAuxCtlAOffset, .value = 0x7ef0'0bff},
  }));
  DdiAuxChannel aux_channel(&mmio_buffer_, tgl_registers::DDI_A, kDell5420GpuDeviceId);

  const DdiAuxChannelConfig config = aux_channel.Config();
  EXPECT_EQ(4'000, config.timeout_us);
  EXPECT_EQ(32, config.sync_pulse_count);
  EXPECT_EQ(32, config.fast_wake_sync_pulse_count);
  EXPECT_EQ(true, config.use_thunderbolt);
}
TEST_F(DdiAuxChannelConfigTest, SetUseThunderbolt) {
  mmio_range_.Expect(MockMmioRange::AccessList({
      // Start from an all-zeros configuration to verify that the right bit it set.
      {.address = kDdiAuxCtlUsbBOffset, .value = 0},
  }));
  DdiAuxChannel aux_channel(&mmio_buffer_, tgl_registers::DDI_TC_2, kDell5420GpuDeviceId);
  aux_channel.SetUseThunderbolt(true);

  const DdiAuxChannelConfig config = aux_channel.Config();
  EXPECT_EQ(400, config.timeout_us);
  EXPECT_EQ(1, config.sync_pulse_count);
  EXPECT_EQ(1, config.fast_wake_sync_pulse_count);
  EXPECT_EQ(true, config.use_thunderbolt);
}
TEST_F(DdiAuxChannelConfigTest, SetUseThunderboltClear) {
  mmio_range_.Expect(MockMmioRange::AccessList({
      // Start from an allmost-all-ones configuration to verify that the right bit is cleared.
      {.address = kDdiAuxCtlAOffset, .value = 0x7ef0'0bff},
  }));
  DdiAuxChannel aux_channel(&mmio_buffer_, tgl_registers::DDI_A, kDell5420GpuDeviceId);
  aux_channel.SetUseThunderbolt(false);

  const DdiAuxChannelConfig config = aux_channel.Config();
  EXPECT_EQ(4'000, config.timeout_us);
  EXPECT_EQ(32, config.sync_pulse_count);
  EXPECT_EQ(32, config.fast_wake_sync_pulse_count);
  EXPECT_EQ(false, config.use_thunderbolt);
}

class DdiAuxChannelWriteRequestTest : public DdiAuxChannelTest {
 public:
  void SetUp() override {
    DdiAuxChannelTest::SetUp();

    // The DdiAuxChannel constructor's MMIO activity.
    mmio_range_.Expect(MockMmioRange::AccessList({
        {.address = kDdiAuxCtlAOffset, .value = kDdiAuxCtlQuietStart},
    }));
  }
};

TEST_F(DdiAuxChannelWriteRequestTest, Read1Byte) {
  mmio_range_.Expect(MockMmioRange::AccessList({
      {.address = kDdiAuxDataA0Offset, .value = 0x9abc'de00, .write = true},
  }));
  DdiAuxChannel aux_channel(&mmio_buffer_, tgl_registers::DDI_A, kAtlasGpuDeviceId);
  aux_channel.WriteRequestForTesting(
      {.address = 0xabcde, .command = 9, .op_size = 1, .data = cpp20::span<uint8_t>()});
}
TEST_F(DdiAuxChannelWriteRequestTest, Read16Bytes) {
  mmio_range_.Expect(MockMmioRange::AccessList({
      {.address = kDdiAuxDataA0Offset, .value = 0x9abc'de0f, .write = true},
  }));
  DdiAuxChannel aux_channel(&mmio_buffer_, tgl_registers::DDI_A, kAtlasGpuDeviceId);
  aux_channel.WriteRequestForTesting(
      {.address = 0xabcde, .command = 9, .op_size = 16, .data = cpp20::span<uint8_t>()});
}

TEST_F(DdiAuxChannelWriteRequestTest, Write1Byte) {
  mmio_range_.Expect(MockMmioRange::AccessList({
      {.address = kDdiAuxDataA0Offset, .value = 0x8abc'de00, .write = true},
      {.address = kDdiAuxDataA1Offset, .value = 0x4100'0000, .write = true},
  }));
  static constexpr uint8_t kData[] = {0x41};
  DdiAuxChannel aux_channel(&mmio_buffer_, tgl_registers::DDI_A, kAtlasGpuDeviceId);
  aux_channel.WriteRequestForTesting(
      {.address = 0xabcde, .command = 8, .op_size = 1, .data = kData});
}
TEST_F(DdiAuxChannelWriteRequestTest, Write2Bytes) {
  mmio_range_.Expect(MockMmioRange::AccessList({
      {.address = kDdiAuxDataA0Offset, .value = 0x8abc'de01, .write = true},
      {.address = kDdiAuxDataA1Offset, .value = 0x4142'0000, .write = true},
  }));
  static constexpr uint8_t kData[] = {0x41, 0x42};
  DdiAuxChannel aux_channel(&mmio_buffer_, tgl_registers::DDI_A, kAtlasGpuDeviceId);
  aux_channel.WriteRequestForTesting(
      {.address = 0xabcde, .command = 8, .op_size = 2, .data = kData});
}
TEST_F(DdiAuxChannelWriteRequestTest, Write3Bytes) {
  mmio_range_.Expect(MockMmioRange::AccessList({
      {.address = kDdiAuxDataA0Offset, .value = 0x8abc'de02, .write = true},
      {.address = kDdiAuxDataA1Offset, .value = 0x4142'4300, .write = true},
  }));
  static constexpr uint8_t kData[] = {0x41, 0x42, 0x43};
  DdiAuxChannel aux_channel(&mmio_buffer_, tgl_registers::DDI_A, kAtlasGpuDeviceId);
  aux_channel.WriteRequestForTesting(
      {.address = 0xabcde, .command = 8, .op_size = 3, .data = kData});
}
TEST_F(DdiAuxChannelWriteRequestTest, Write4Bytes) {
  mmio_range_.Expect(MockMmioRange::AccessList({
      {.address = kDdiAuxDataA0Offset, .value = 0x8abc'de03, .write = true},
      {.address = kDdiAuxDataA1Offset, .value = 0x4142'4344, .write = true},
  }));
  static constexpr uint8_t kData[] = {0x41, 0x42, 0x43, 0x44};
  DdiAuxChannel aux_channel(&mmio_buffer_, tgl_registers::DDI_A, kAtlasGpuDeviceId);
  aux_channel.WriteRequestForTesting(
      {.address = 0xabcde, .command = 8, .op_size = 4, .data = kData});
}
TEST_F(DdiAuxChannelWriteRequestTest, Write5Bytes) {
  mmio_range_.Expect(MockMmioRange::AccessList({
      {.address = kDdiAuxDataA0Offset, .value = 0x8abc'de04, .write = true},
      {.address = kDdiAuxDataA1Offset, .value = 0x4142'4344, .write = true},
      {.address = kDdiAuxDataA2Offset, .value = 0x4500'0000, .write = true},
  }));
  static constexpr uint8_t kData[] = {0x41, 0x42, 0x43, 0x44, 0x45};
  DdiAuxChannel aux_channel(&mmio_buffer_, tgl_registers::DDI_A, kAtlasGpuDeviceId);
  aux_channel.WriteRequestForTesting(
      {.address = 0xabcde, .command = 8, .op_size = 5, .data = kData});
}
TEST_F(DdiAuxChannelWriteRequestTest, Write6Bytes) {
  mmio_range_.Expect(MockMmioRange::AccessList({
      {.address = kDdiAuxDataA0Offset, .value = 0x8abc'de05, .write = true},
      {.address = kDdiAuxDataA1Offset, .value = 0x4142'4344, .write = true},
      {.address = kDdiAuxDataA2Offset, .value = 0x4546'0000, .write = true},
  }));
  static constexpr uint8_t kData[] = {0x41, 0x42, 0x43, 0x44, 0x45, 0x46};
  DdiAuxChannel aux_channel(&mmio_buffer_, tgl_registers::DDI_A, kAtlasGpuDeviceId);
  aux_channel.WriteRequestForTesting(
      {.address = 0xabcde, .command = 8, .op_size = 6, .data = kData});
}
TEST_F(DdiAuxChannelWriteRequestTest, Write7Bytes) {
  mmio_range_.Expect(MockMmioRange::AccessList({
      {.address = kDdiAuxDataA0Offset, .value = 0x8abc'de06, .write = true},
      {.address = kDdiAuxDataA1Offset, .value = 0x4142'4344, .write = true},
      {.address = kDdiAuxDataA2Offset, .value = 0x4546'4700, .write = true},
  }));
  static constexpr uint8_t kData[] = {0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47};
  DdiAuxChannel aux_channel(&mmio_buffer_, tgl_registers::DDI_A, kAtlasGpuDeviceId);
  aux_channel.WriteRequestForTesting(
      {.address = 0xabcde, .command = 8, .op_size = 7, .data = kData});
}
TEST_F(DdiAuxChannelWriteRequestTest, Write8Bytes) {
  mmio_range_.Expect(MockMmioRange::AccessList({
      {.address = kDdiAuxDataA0Offset, .value = 0x8abc'de07, .write = true},
      {.address = kDdiAuxDataA1Offset, .value = 0x4142'4344, .write = true},
      {.address = kDdiAuxDataA2Offset, .value = 0x4546'4748, .write = true},
  }));
  static constexpr uint8_t kData[] = {0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48};
  DdiAuxChannel aux_channel(&mmio_buffer_, tgl_registers::DDI_A, kAtlasGpuDeviceId);
  aux_channel.WriteRequestForTesting(
      {.address = 0xabcde, .command = 8, .op_size = 8, .data = kData});
}

TEST_F(DdiAuxChannelWriteRequestTest, Write15Bytes) {
  mmio_range_.Expect(MockMmioRange::AccessList({
      {.address = kDdiAuxDataA0Offset, .value = 0x8abc'de0e, .write = true},
      {.address = kDdiAuxDataA1Offset, .value = 0x4142'4344, .write = true},
      {.address = kDdiAuxDataA2Offset, .value = 0x4546'4748, .write = true},
      {.address = kDdiAuxDataA3Offset, .value = 0x494a'4b4c, .write = true},
      {.address = kDdiAuxDataA4Offset, .value = 0x4d4e'4f00, .write = true},
  }));
  static constexpr uint8_t kData[] = {0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48,
                                      0x49, 0x4a, 0x4b, 0x4c, 0x4d, 0x4e, 0x4f};
  DdiAuxChannel aux_channel(&mmio_buffer_, tgl_registers::DDI_A, kAtlasGpuDeviceId);
  aux_channel.WriteRequestForTesting(
      {.address = 0xabcde, .command = 8, .op_size = 15, .data = kData});
}
TEST_F(DdiAuxChannelWriteRequestTest, Write16Bytes) {
  mmio_range_.Expect(MockMmioRange::AccessList({
      {.address = kDdiAuxDataA0Offset, .value = 0x8abc'de0f, .write = true},
      {.address = kDdiAuxDataA1Offset, .value = 0x4142'4344, .write = true},
      {.address = kDdiAuxDataA2Offset, .value = 0x4546'4748, .write = true},
      {.address = kDdiAuxDataA3Offset, .value = 0x494a'4b4c, .write = true},
      {.address = kDdiAuxDataA4Offset, .value = 0x4d4e'4f50, .write = true},
  }));
  static constexpr uint8_t kData[] = {0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48,
                                      0x49, 0x4a, 0x4b, 0x4c, 0x4d, 0x4e, 0x4f, 0x50};
  DdiAuxChannel aux_channel(&mmio_buffer_, tgl_registers::DDI_A, kAtlasGpuDeviceId);
  aux_channel.WriteRequestForTesting(
      {.address = 0xabcde, .command = 8, .op_size = 16, .data = kData});
}

class DdiAuxChannelTransactTest : public DdiAuxChannelTest {
 public:
  // MMIO activity for DdiAuxChannel setup and a 16-byte read request.
  void SetUpMmioExpectations() {
    mmio_range_.Expect(MockMmioRange::AccessList({
        {.address = kDdiAuxCtlAOffset, .value = kDdiAuxCtlQuietStart},
        {.address = kDdiAuxDataA0Offset, .value = 0x9abc'de0f, .write = true},
    }));
  }

  // Sets up the AUX channel for a 16-byte read request.
  void SetUpTransaction() {
    aux_channel_.emplace(&mmio_buffer_, tgl_registers::DDI_A, kAtlasGpuDeviceId);
    aux_channel_->WriteRequestForTesting(
        {.address = 0xabcde, .command = 9, .op_size = 16, .data = cpp20::span<uint8_t>()});
  }

 protected:
  // Populated in SetUpTransaction().
  std::optional<DdiAuxChannel> aux_channel_;
};

TEST_F(DdiAuxChannelTransactTest, TransactAdjustsZeroControl) {
  mmio_range_.Expect(MockMmioRange::AccessList({
      {.address = kDdiAuxCtlAOffset, .value = 0},
      {.address = kDdiAuxDataA0Offset, .value = 0x9abc'de0f, .write = true},
      {.address = kDdiAuxCtlAOffset, .value = 0xfe40'00f9, .write = true},
      {.address = kDdiAuxCtlAOffset, .value = 0x6c20'00f9},
  }));

  DdiAuxChannel aux_channel(&mmio_buffer_, tgl_registers::DDI_A, kAtlasGpuDeviceId);
  aux_channel.WriteRequestForTesting(
      {.address = 0xabcde, .command = 9, .op_size = 16, .data = cpp20::span<uint8_t>()});
  const zx::result transact_status = aux_channel.TransactForTesting();
  EXPECT_TRUE(transact_status.is_ok()) << transact_status.status_string();
}

TEST_F(DdiAuxChannelTest, TransactAdjustsDefaultControl) {
  mmio_range_.Expect(MockMmioRange::AccessList({
      {.address = kDdiAuxCtlAOffset, .value = kDdiAuxCtlDefault},
      {.address = kDdiAuxDataA0Offset, .value = 0x9abc'de0f, .write = true},
      {.address = kDdiAuxCtlAOffset, .value = 0xfe40'00ff, .write = true},
      {.address = kDdiAuxCtlAOffset, .value = 0x6c20'00ff},
  }));

  DdiAuxChannel aux_channel(&mmio_buffer_, tgl_registers::DDI_A, kAtlasGpuDeviceId);
  aux_channel.WriteRequestForTesting(
      {.address = 0xabcde, .command = 9, .op_size = 16, .data = cpp20::span<uint8_t>()});
  const zx::result transact_status = aux_channel.TransactForTesting();
  EXPECT_TRUE(transact_status.is_ok()) << transact_status.status_string();
}

TEST_F(DdiAuxChannelTransactTest, InstantSuccess) {
  SetUpMmioExpectations();
  mmio_range_.Expect(MockMmioRange::AccessList({
      {.address = kDdiAuxCtlAOffset, .value = 0xfe40'00ff, .write = true},
      {.address = kDdiAuxCtlAOffset, .value = 0x6c20'00ff},
  }));

  SetUpTransaction();
  const zx::result transact_status = aux_channel_->TransactForTesting();
  EXPECT_TRUE(transact_status.is_ok()) << transact_status.status_string();
}

TEST_F(DdiAuxChannelTransactTest, PendingThenSuccess) {
  SetUpMmioExpectations();
  mmio_range_.Expect(MockMmioRange::AccessList({
      {.address = kDdiAuxCtlAOffset, .value = 0xfe40'00ff, .write = true},
      {.address = kDdiAuxCtlAOffset, .value = 0xac00'00ff},
      {.address = kDdiAuxCtlAOffset, .value = 0xac00'00ff},
      {.address = kDdiAuxCtlAOffset, .value = 0xac00'00ff},
      {.address = kDdiAuxCtlAOffset, .value = 0x6c20'00ff},
  }));

  SetUpTransaction();
  const zx::result transact_status = aux_channel_->TransactForTesting();
  EXPECT_TRUE(transact_status.is_ok()) << transact_status.status_string();
}

TEST_F(DdiAuxChannelTransactTest, PendingThenNotCompleteThenSuccess) {
  SetUpMmioExpectations();
  mmio_range_.Expect(MockMmioRange::AccessList({
      {.address = kDdiAuxCtlAOffset, .value = 0xfe40'00ff, .write = true},
      {.address = kDdiAuxCtlAOffset, .value = 0xac00'00ff},
      {.address = kDdiAuxCtlAOffset, .value = 0x2c00'00ff},
      {.address = kDdiAuxCtlAOffset, .value = 0x2c00'00ff},
      {.address = kDdiAuxCtlAOffset, .value = 0x6c20'00ff},
  }));

  SetUpTransaction();
  const zx::result transact_status = aux_channel_->TransactForTesting();
  EXPECT_TRUE(transact_status.is_ok()) << transact_status.status_string();
}

TEST_F(DdiAuxChannelTransactTest, DdiReportsReceiveError) {
  SetUpMmioExpectations();
  mmio_range_.Expect(MockMmioRange::AccessList({
      {.address = kDdiAuxCtlAOffset, .value = 0xfe40'00ff, .write = true},
      {.address = kDdiAuxCtlAOffset, .value = 0x6d20'00ff},
  }));

  SetUpTransaction();
  const zx::result transact_status = aux_channel_->TransactForTesting();
  EXPECT_EQ(ZX_ERR_IO_DATA_INTEGRITY, transact_status.error_value())
      << transact_status.status_string();
}

TEST_F(DdiAuxChannelTransactTest, DdiReportsTimeout) {
  SetUpMmioExpectations();
  mmio_range_.Expect(MockMmioRange::AccessList({
      {.address = kDdiAuxCtlAOffset, .value = 0xfe40'00ff, .write = true},
      {.address = kDdiAuxCtlAOffset, .value = 0x7c20'00ff},
  }));

  SetUpTransaction();
  const zx::result transact_status = aux_channel_->TransactForTesting();
  EXPECT_EQ(ZX_ERR_IO_MISSED_DEADLINE, transact_status.error_value())
      << transact_status.status_string();
}

TEST_F(DdiAuxChannelTransactTest, EmptyReply) {
  SetUpMmioExpectations();
  mmio_range_.Expect(MockMmioRange::AccessList({
      {.address = kDdiAuxCtlAOffset, .value = 0xfe40'00ff, .write = true},
      {.address = kDdiAuxCtlAOffset, .value = 0x6c00'00ff},
  }));

  SetUpTransaction();
  const zx::result transact_status = aux_channel_->TransactForTesting();
  EXPECT_EQ(ZX_ERR_IO_DATA_INTEGRITY, transact_status.error_value())
      << transact_status.status_string();
}

TEST_F(DdiAuxChannelTransactTest, LongReply) {
  SetUpMmioExpectations();
  mmio_range_.Expect(MockMmioRange::AccessList({
      {.address = kDdiAuxCtlAOffset, .value = 0xfe40'00ff, .write = true},
      {.address = kDdiAuxCtlAOffset, .value = 0x6d20'00ff},
  }));

  SetUpTransaction();
  const zx::result transact_status = aux_channel_->TransactForTesting();
  EXPECT_EQ(ZX_ERR_IO_DATA_INTEGRITY, transact_status.error_value())
      << transact_status.status_string();
}

TEST_F(DdiAuxChannelTransactTest, DdiTimesOut) {
  SetUpMmioExpectations();
  mmio_range_.Expect(MockMmioRange::AccessList({
      {.address = kDdiAuxCtlAOffset, .value = 0xfe40'00ff, .write = true},
  }));

  // Sadly, this test is tightly coupled with the TransactForTesting() implementation.
  //
  // In order to cover the DDI timeout path, we need to know the number of polls
  // that TransactForTesting() will perform before giving up. Also, this test will have to
  // be rewritten once we start using interrupts.
  static constexpr int kDdiPollsBeforeTimeout = 10'001;
  for (int i = 0; i < kDdiPollsBeforeTimeout; ++i) {
    mmio_range_.Expect({.address = kDdiAuxCtlAOffset, .value = 0xac00'00ff});
  }

  SetUpTransaction();
  const zx::result transact_status = aux_channel_->TransactForTesting();
  EXPECT_EQ(ZX_ERR_IO_MISSED_DEADLINE, transact_status.error_value())
      << transact_status.status_string();
}

class DdiAuxChannelReadReplyTest : public DdiAuxChannelTest {
 public:
  // MMIO activity for DdiAuxChannel setup and a 16-byte read transaction.
  //
  // The test case is responsible for adding an expectation for a MMIO read of
  // the AUX control register that conveys "transaction succeeded", so it can
  // set the register's data size field.
  //
  // We use a 16-byte read request because that minimizes the constraints on
  // the rest of the test. The maximum data payload size for AUX transactions
  // is 16 bytes, and short reads are allowed. So, the read reply could be of
  // any (valid) size.
  void SetUpMmioExpectations() {
    mmio_range_.Expect(MockMmioRange::AccessList({
        {.address = kDdiAuxCtlAOffset, .value = kDdiAuxCtlQuietStart},
        {.address = kDdiAuxDataA0Offset, .value = 0x9abc'de0f, .write = true},
        {.address = kDdiAuxCtlAOffset, .value = 0xfe40'00ff, .write = true},
    }));
  }

  // Sets up the AUX channel for reading the reply to a 16-byte read request.
  bool SetUpReadReplyForTesting() {
    aux_channel_.emplace(&mmio_buffer_, tgl_registers::DDI_A, kAtlasGpuDeviceId);
    aux_channel_->WriteRequestForTesting(
        {.address = 0xabcde, .command = 9, .op_size = 16, .data = cpp20::span<uint8_t>()});
    const zx::result transact_status = aux_channel_->TransactForTesting();

    // ASSERT_TRUE() doesn't stop the test when called from a nested function.
    EXPECT_TRUE(transact_status.is_ok()) << transact_status.status_string();
    return transact_status.is_ok();
  }

 protected:
  // Populated in SetUpTransaction().
  std::optional<DdiAuxChannel> aux_channel_;
};

// The DisplayPort standard mandates that reading unavailable DPCD registers
// results in ACK with no data. So, this is a valid reply to a read request.
TEST_F(DdiAuxChannelReadReplyTest, EmptyAckRead) {
  SetUpMmioExpectations();
  mmio_range_.Expect(MockMmioRange::AccessList({
      {.address = kDdiAuxCtlAOffset, .value = 0x6c10'00ff},
      {.address = kDdiAuxDataA0Offset, .value = 0x00de'adbe},
  }));
  ASSERT_TRUE(SetUpReadReplyForTesting());

  const DdiAuxChannel::ReplyInfo reply_info =
      aux_channel_->ReadReplyForTesting(cpp20::span<uint8_t>());
  EXPECT_EQ(0, reply_info.reply_header);
  EXPECT_EQ(0, reply_info.reply_data_size);
}

TEST_F(DdiAuxChannelReadReplyTest, EmptyNackRead) {
  SetUpMmioExpectations();
  mmio_range_.Expect(MockMmioRange::AccessList({
      {.address = kDdiAuxCtlAOffset, .value = 0x6c10'00ff},
      {.address = kDdiAuxDataA0Offset, .value = 0x10de'adbe},
  }));
  ASSERT_TRUE(SetUpReadReplyForTesting());

  const DdiAuxChannel::ReplyInfo reply_info =
      aux_channel_->ReadReplyForTesting(cpp20::span<uint8_t>());
  EXPECT_EQ(0x10, reply_info.reply_header);
  EXPECT_EQ(0, reply_info.reply_data_size);
}

TEST_F(DdiAuxChannelReadReplyTest, ReadAck1Byte) {
  SetUpMmioExpectations();
  mmio_range_.Expect(MockMmioRange::AccessList({
      {.address = kDdiAuxCtlAOffset, .value = 0x6c20'00ff},
      {.address = kDdiAuxDataA0Offset, .value = 0x0041'dead},
  }));
  ASSERT_TRUE(SetUpReadReplyForTesting());

  std::array<uint8_t, 1> data_buffer;
  const DdiAuxChannel::ReplyInfo reply_info = aux_channel_->ReadReplyForTesting(data_buffer);
  EXPECT_EQ(1, reply_info.reply_data_size);

  const std::array<uint8_t, 1> expected_data = {0x41};
  EXPECT_EQ(expected_data, data_buffer);
}
TEST_F(DdiAuxChannelReadReplyTest, ReadAck2Bytes) {
  SetUpMmioExpectations();
  mmio_range_.Expect(MockMmioRange::AccessList({
      {.address = kDdiAuxCtlAOffset, .value = 0x6c30'00ff},
      {.address = kDdiAuxDataA0Offset, .value = 0x0041'42de},
  }));
  ASSERT_TRUE(SetUpReadReplyForTesting());

  std::array<uint8_t, 2> data_buffer;
  const DdiAuxChannel::ReplyInfo reply_info = aux_channel_->ReadReplyForTesting(data_buffer);
  EXPECT_EQ(2, reply_info.reply_data_size);

  const std::array<uint8_t, 2> expected_data = {0x41, 0x42};
  EXPECT_EQ(expected_data, data_buffer);
}
TEST_F(DdiAuxChannelReadReplyTest, ReadAck3Bytes) {
  SetUpMmioExpectations();
  mmio_range_.Expect(MockMmioRange::AccessList({
      {.address = kDdiAuxCtlAOffset, .value = 0x6c40'00ff},
      {.address = kDdiAuxDataA0Offset, .value = 0x0041'4243},
  }));
  ASSERT_TRUE(SetUpReadReplyForTesting());

  std::array<uint8_t, 3> data_buffer;
  const DdiAuxChannel::ReplyInfo reply_info = aux_channel_->ReadReplyForTesting(data_buffer);
  EXPECT_EQ(3, reply_info.reply_data_size);

  const std::array<uint8_t, 3> expected_data = {0x41, 0x42, 0x43};
  EXPECT_EQ(expected_data, data_buffer);
}
TEST_F(DdiAuxChannelReadReplyTest, ReadAck4Bytes) {
  SetUpMmioExpectations();
  mmio_range_.Expect(MockMmioRange::AccessList({
      {.address = kDdiAuxCtlAOffset, .value = 0x6c50'00ff},
      {.address = kDdiAuxDataA0Offset, .value = 0x0041'4243},
      {.address = kDdiAuxDataA1Offset, .value = 0x44de'adbe},
  }));
  ASSERT_TRUE(SetUpReadReplyForTesting());

  std::array<uint8_t, 4> data_buffer;
  const DdiAuxChannel::ReplyInfo reply_info = aux_channel_->ReadReplyForTesting(data_buffer);
  EXPECT_EQ(4, reply_info.reply_data_size);

  const std::array<uint8_t, 4> expected_data = {0x41, 0x42, 0x43, 0x44};
  EXPECT_EQ(expected_data, data_buffer);
}
TEST_F(DdiAuxChannelReadReplyTest, ReadAck5Bytes) {
  SetUpMmioExpectations();
  mmio_range_.Expect(MockMmioRange::AccessList({
      {.address = kDdiAuxCtlAOffset, .value = 0x6c60'00ff},
      {.address = kDdiAuxDataA0Offset, .value = 0x0041'4243},
      {.address = kDdiAuxDataA1Offset, .value = 0x4445'dead},
  }));
  ASSERT_TRUE(SetUpReadReplyForTesting());

  std::array<uint8_t, 5> data_buffer;
  const DdiAuxChannel::ReplyInfo reply_info = aux_channel_->ReadReplyForTesting(data_buffer);
  EXPECT_EQ(5, reply_info.reply_data_size);

  const std::array<uint8_t, 5> expected_data = {0x41, 0x42, 0x43, 0x44, 0x45};
  EXPECT_EQ(expected_data, data_buffer);
}
TEST_F(DdiAuxChannelReadReplyTest, ReadAck6Bytes) {
  SetUpMmioExpectations();
  mmio_range_.Expect(MockMmioRange::AccessList({
      {.address = kDdiAuxCtlAOffset, .value = 0x6c70'00ff},
      {.address = kDdiAuxDataA0Offset, .value = 0x0041'4243},
      {.address = kDdiAuxDataA1Offset, .value = 0x4445'46de},
  }));
  ASSERT_TRUE(SetUpReadReplyForTesting());

  std::array<uint8_t, 6> data_buffer;
  const DdiAuxChannel::ReplyInfo reply_info = aux_channel_->ReadReplyForTesting(data_buffer);
  EXPECT_EQ(6, reply_info.reply_data_size);

  const std::array<uint8_t, 6> expected_data = {0x41, 0x42, 0x43, 0x44, 0x45, 0x46};
  EXPECT_EQ(expected_data, data_buffer);
}
TEST_F(DdiAuxChannelReadReplyTest, ReadAck7Bytes) {
  SetUpMmioExpectations();
  mmio_range_.Expect(MockMmioRange::AccessList({
      {.address = kDdiAuxCtlAOffset, .value = 0x6c80'00ff},
      {.address = kDdiAuxDataA0Offset, .value = 0x00414243},
      {.address = kDdiAuxDataA1Offset, .value = 0x44454647},
  }));
  ASSERT_TRUE(SetUpReadReplyForTesting());

  std::array<uint8_t, 7> data_buffer;
  const DdiAuxChannel::ReplyInfo reply_info = aux_channel_->ReadReplyForTesting(data_buffer);
  EXPECT_EQ(7, reply_info.reply_data_size);

  const std::array<uint8_t, 7> expected_data = {0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47};
  EXPECT_EQ(expected_data, data_buffer);
}
TEST_F(DdiAuxChannelReadReplyTest, ReadAck8Bytes) {
  SetUpMmioExpectations();
  mmio_range_.Expect(MockMmioRange::AccessList({
      {.address = kDdiAuxCtlAOffset, .value = 0x6c90'00ff},
      {.address = kDdiAuxDataA0Offset, .value = 0x0041'4243},
      {.address = kDdiAuxDataA1Offset, .value = 0x4445'4647},
      {.address = kDdiAuxDataA2Offset, .value = 0x48de'adbe},
  }));
  ASSERT_TRUE(SetUpReadReplyForTesting());

  std::array<uint8_t, 8> data_buffer;
  const DdiAuxChannel::ReplyInfo reply_info = aux_channel_->ReadReplyForTesting(data_buffer);
  EXPECT_EQ(8, reply_info.reply_data_size);

  const std::array<uint8_t, 8> expected_data = {0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48};
  EXPECT_EQ(expected_data, data_buffer);
}

TEST_F(DdiAuxChannelReadReplyTest, ReadAck15Bytes) {
  SetUpMmioExpectations();
  mmio_range_.Expect(MockMmioRange::AccessList({
      {.address = kDdiAuxCtlAOffset, .value = 0x6d00'00ff},
      {.address = kDdiAuxDataA0Offset, .value = 0x0041'4243},
      {.address = kDdiAuxDataA1Offset, .value = 0x4445'4647},
      {.address = kDdiAuxDataA2Offset, .value = 0x4849'4a4b},
      {.address = kDdiAuxDataA3Offset, .value = 0x4c4d'4e4f},
  }));
  ASSERT_TRUE(SetUpReadReplyForTesting());

  std::array<uint8_t, 15> data_buffer;
  const DdiAuxChannel::ReplyInfo reply_info = aux_channel_->ReadReplyForTesting(data_buffer);
  EXPECT_EQ(15, reply_info.reply_data_size);

  const std::array<uint8_t, 15> expected_data = {0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48,
                                                 0x49, 0x4a, 0x4b, 0x4c, 0x4d, 0x4e, 0x4f};
  EXPECT_EQ(expected_data, data_buffer);
}
TEST_F(DdiAuxChannelReadReplyTest, ReadAck16Bytes) {
  SetUpMmioExpectations();
  mmio_range_.Expect(MockMmioRange::AccessList({
      {.address = kDdiAuxCtlAOffset, .value = 0x6d10'00ff},
      {.address = kDdiAuxDataA0Offset, .value = 0x0041'4243},
      {.address = kDdiAuxDataA1Offset, .value = 0x4445'4647},
      {.address = kDdiAuxDataA2Offset, .value = 0x4849'4a4b},
      {.address = kDdiAuxDataA3Offset, .value = 0x4c4d'4e4f},
      {.address = kDdiAuxDataA4Offset, .value = 0x50de'adbe},
  }));
  ASSERT_TRUE(SetUpReadReplyForTesting());

  std::array<uint8_t, 16> data_buffer;
  const DdiAuxChannel::ReplyInfo reply_info = aux_channel_->ReadReplyForTesting(data_buffer);
  EXPECT_EQ(16, reply_info.reply_data_size);

  const std::array<uint8_t, 16> expected_data = {0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48,
                                                 0x49, 0x4a, 0x4b, 0x4c, 0x4d, 0x4e, 0x4f, 0x50};
  EXPECT_EQ(expected_data, data_buffer);
}

TEST_F(DdiAuxChannelTest, DoTransactReadAck1Byte) {
  mmio_range_.Expect(MockMmioRange::AccessList({
      {.address = kDdiAuxCtlAOffset, .value = kDdiAuxCtlQuietStart},
      {.address = kDdiAuxDataA0Offset, .value = 0x9abc'de00, .write = true},
      {.address = kDdiAuxCtlAOffset, .value = 0xfe40'00ff, .write = true},
      {.address = kDdiAuxCtlAOffset, .value = 0x6c20'00ff},
      {.address = kDdiAuxDataA0Offset, .value = 0x0041'dead},
  }));
  DdiAuxChannel aux_channel(&mmio_buffer_, tgl_registers::DDI_A, kAtlasGpuDeviceId);

  const DdiAuxChannel::Request request = {
      .address = 0xabcde, .command = 9, .op_size = 1, .data = cpp20::span<uint8_t>()};
  std::array<uint8_t, 1> reply_data_buffer;
  const zx::result<DdiAuxChannel::ReplyInfo> result =
      aux_channel.DoTransaction(request, reply_data_buffer);
  ASSERT_TRUE(result.is_ok()) << result.status_string();

  EXPECT_EQ(0, result->reply_header);
  EXPECT_EQ(1, result->reply_data_size);
  const std::array<uint8_t, 1> expected_data = {0x41};
  EXPECT_EQ(expected_data, reply_data_buffer);
}

TEST_F(DdiAuxChannelTest, DoTransactWrite7BytesNack) {
  mmio_range_.Expect(MockMmioRange::AccessList({
      {.address = kDdiAuxCtlAOffset, .value = kDdiAuxCtlQuietStart},
      {.address = kDdiAuxDataA0Offset, .value = 0x8abc'de06, .write = true},
      {.address = kDdiAuxDataA1Offset, .value = 0x4142'4344, .write = true},
      {.address = kDdiAuxDataA2Offset, .value = 0x4546'4700, .write = true},
      {.address = kDdiAuxCtlAOffset, .value = 0xfeb0'00ff, .write = true},
      {.address = kDdiAuxCtlAOffset, .value = 0x6c20'00ff},
      {.address = kDdiAuxDataA0Offset, .value = 0x1004'dead},
  }));
  DdiAuxChannel aux_channel(&mmio_buffer_, tgl_registers::DDI_A, kAtlasGpuDeviceId);

  const uint8_t request_data[] = {0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47};
  const DdiAuxChannel::Request request = {
      .address = 0xabcde, .command = 8, .op_size = 7, .data = request_data};

  std::array<uint8_t, 1> reply_data_buffer;
  const zx::result<DdiAuxChannel::ReplyInfo> result =
      aux_channel.DoTransaction(request, reply_data_buffer);
  ASSERT_TRUE(result.is_ok()) << result.status_string();

  EXPECT_EQ(0x10, result->reply_header);
  EXPECT_EQ(1, result->reply_data_size);
  const std::array<uint8_t, 1> expected_data = {0x04};
  EXPECT_EQ(expected_data, reply_data_buffer);
}

}  // namespace

}  // namespace i915_tgl
