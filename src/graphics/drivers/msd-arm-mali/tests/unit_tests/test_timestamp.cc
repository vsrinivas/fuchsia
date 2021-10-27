// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>
#include <mock/mock_mmio.h>

#include "magma_util/register_io.h"
#include "src/graphics/drivers/msd-arm-mali/src/registers.h"

class Hook : public mali::RegisterIo::Hook {
 public:
  Hook(mali::RegisterIo* register_io) : register_io_(register_io) {}

  void Write32(uint32_t val, uint32_t offset) override {}
  void Read64(uint64_t val, uint32_t offset) override {}

  void Read32(uint32_t val, uint32_t offset) override {
    // Increment the bottom 8 bits - this may rollover the upper timestamp bits
    uint8_t bits = val & 0xff;
    bits += 1;
    register_io_->Write32((val & 0xFFFFFF00) | bits, offset);
  }

  // Raw pointer to avoid circular reference. This class is owned by this
  // register_io_ and will be destroyed when it is.
  mali::RegisterIo* register_io_;
};

class TestTimestamp : public testing::Test {
 public:
  void SetUp() override {
    register_io_ = std::make_shared<mali::RegisterIo>(MockMmio::Create(8 * 1024 * 1024));
  }

  std::shared_ptr<mali::RegisterIo> register_io_;
};

constexpr uint64_t kTimestampBits = 0xff1234abcd;

TEST_F(TestTimestamp, Rollover) {
  uint32_t offset = registers::Timestamp::Get().addr();
  register_io_->Write32(kTimestampBits >> 32, offset + 4);
  register_io_->Write32(static_cast<uint32_t>(kTimestampBits), offset);

  // Hook will increment the timestamp register
  register_io_->InstallHook(std::make_unique<Hook>(register_io_.get()));

  EXPECT_EQ(
      0x001234abceul,
      registers::Timestamp::Get().FromValue(0).ReadConsistentFrom(register_io_.get()).reg_value());
}

TEST_F(TestTimestamp, NoRollover) {
  uint32_t offset = registers::Timestamp::Get().addr();
  constexpr uint64_t kTimestampBits = 0xff1234abcd;
  register_io_->Write32(kTimestampBits >> 32, offset + 4);
  register_io_->Write32(static_cast<uint32_t>(kTimestampBits), offset);

  EXPECT_EQ(
      0xff1234abcdul,
      registers::Timestamp::Get().FromValue(0).ReadConsistentFrom(register_io_.get()).reg_value());
}
