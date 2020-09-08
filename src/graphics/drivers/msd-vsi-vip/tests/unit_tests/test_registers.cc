// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>

#include "mock/mock_mmio.h"
#include "src/graphics/drivers/msd-vsi-vip/src/registers.h"

class TestRegisters : public ::testing::Test {
 public:
  void SetUp() override {
    register_io_ = std::make_unique<magma::RegisterIo>(MockMmio::Create(1024 * 1024));
    ASSERT_NE(nullptr, register_io_);
  }

  void SetIdleState(uint32_t value) {
    registers::IdleState::Get().FromValue(value).WriteTo(register_io_.get());
  }

  uint32_t IsIdle() { return registers::IdleState::Get().ReadFrom(register_io_.get()).IsIdle(); }

 protected:
  std::unique_ptr<magma::RegisterIo> register_io_;
};

TEST_F(TestRegisters, IsIdle) {
  constexpr uint32_t kIdle = 0x7fffffff;
  SetIdleState(kIdle);
  ASSERT_TRUE(IsIdle());

  constexpr uint32_t kIdleAdditionalBitSet = 0xffffffff;
  SetIdleState(kIdleAdditionalBitSet);
  ASSERT_TRUE(IsIdle());

  constexpr uint32_t kNotIdle = 0x7ffffffe;
  SetIdleState(kNotIdle);
  ASSERT_FALSE(IsIdle());
}
