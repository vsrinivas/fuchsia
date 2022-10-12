// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>

#include "hwreg/bitfields.h"
#include "mock/mock_mmio.h"
#include "msd_intel_register_io.h"

class TestMsdIntelRegisterIo : public testing::Test {
 public:
  class TestRegister32 : public hwreg::RegisterBase<TestRegister32, uint32_t> {};
  class TestRegister64 : public hwreg::RegisterBase<TestRegister64, uint64_t> {};
};

TEST_F(TestMsdIntelRegisterIo, ReadWrite32) {
  auto register_io = std::make_unique<MsdIntelRegisterIo>(MockMmio::Create(0x10));

  constexpr uint32_t kExpected = 0xdeadbeef;

  {
    constexpr uint32_t kAddr = 0x4;  // 32bit aligned address
    EXPECT_NE(kExpected, register_io->Read32(kAddr));
    register_io->Write32(kExpected, kAddr);
    EXPECT_EQ(kExpected, register_io->Read32(kAddr));
  }
  {
    constexpr uint32_t kAddr = 0x8;  // 64bit aligned address
    EXPECT_NE(kExpected, register_io->Read32(kAddr));
    register_io->Write32(kExpected, kAddr);
    EXPECT_EQ(kExpected, register_io->Read32(kAddr));
  }
}

TEST_F(TestMsdIntelRegisterIo, Read64Write32) {
  auto register_io = std::make_unique<MsdIntelRegisterIo>(MockMmio::Create(0x20));

  constexpr uint64_t kExpected = 0xdeadbeefabcd1234;

  {
    constexpr uint32_t kAddr = 0x4;  // 32bit aligned address
    EXPECT_NE(kExpected, register_io->Read64(kAddr));
    register_io->Write32(kExpected & 0xFFFF'FFFF, kAddr);
    register_io->Write32(kExpected >> 32, kAddr + sizeof(uint32_t));
    EXPECT_EQ(kExpected, register_io->Read64(kAddr));
  }
  {
    constexpr uint32_t kAddr = 0x10;  // 64bit aligned address
    EXPECT_NE(kExpected, register_io->Read64(kAddr));
    register_io->Write32(kExpected & 0xFFFF'FFFF, kAddr);
    register_io->Write32(kExpected >> 32, kAddr + sizeof(uint32_t));
    EXPECT_EQ(kExpected, register_io->Read64(kAddr));
  }
}

TEST_F(TestMsdIntelRegisterIo, RegisterReadWrite32) {
  auto register_io = std::make_unique<MsdIntelRegisterIo>(MockMmio::Create(0x10));

  constexpr uint32_t kExpected = 0xdeadbeef;

  {
    constexpr uint32_t kAddr = 0x4;  // 32bit aligned address

    auto reg_a = hwreg::RegisterAddr<TestRegister32>(kAddr).ReadFrom(register_io.get());
    EXPECT_NE(kExpected, reg_a.reg_value());
    reg_a.set_reg_value(kExpected).WriteTo(register_io.get());

    auto reg_b = hwreg::RegisterAddr<TestRegister32>(kAddr).ReadFrom(register_io.get());
    EXPECT_EQ(kExpected, reg_b.reg_value());
  }
  {
    constexpr uint32_t kAddr = 0x8;  // 64bit aligned address

    auto reg_a = hwreg::RegisterAddr<TestRegister32>(kAddr).ReadFrom(register_io.get());
    EXPECT_NE(kExpected, reg_a.reg_value());
    reg_a.set_reg_value(kExpected).WriteTo(register_io.get());

    auto reg_b = hwreg::RegisterAddr<TestRegister32>(kAddr).ReadFrom(register_io.get());
    EXPECT_EQ(kExpected, reg_b.reg_value());
  }
}

TEST_F(TestMsdIntelRegisterIo, RegisterRead64) {
  auto register_io = std::make_unique<MsdIntelRegisterIo>(MockMmio::Create(0x20));

  constexpr uint64_t kExpected = 0xdeadbeefabcd1234;

  {
    constexpr uint32_t kAddr = 0x4;  // 32bit aligned address

    auto reg_a = hwreg::RegisterAddr<TestRegister64>(kAddr).ReadFrom(register_io.get());
    EXPECT_NE(kExpected, reg_a.reg_value());

    register_io->Write32(kExpected & 0xFFFF'FFFF, kAddr);
    register_io->Write32(kExpected >> 32, kAddr + sizeof(uint32_t));

    auto reg_b = hwreg::RegisterAddr<TestRegister64>(kAddr).ReadFrom(register_io.get());
    EXPECT_EQ(kExpected, reg_b.reg_value());
  }
  {
    constexpr uint32_t kAddr = 0x10;  // 64bit aligned address

    auto reg_a = hwreg::RegisterAddr<TestRegister64>(kAddr).ReadFrom(register_io.get());
    EXPECT_NE(kExpected, reg_a.reg_value());

    register_io->Write32(kExpected & 0xFFFF'FFFF, kAddr);
    register_io->Write32(kExpected >> 32, kAddr + sizeof(uint32_t));

    auto reg_b = hwreg::RegisterAddr<TestRegister64>(kAddr).ReadFrom(register_io.get());
    EXPECT_EQ(kExpected, reg_b.reg_value());
  }
}
