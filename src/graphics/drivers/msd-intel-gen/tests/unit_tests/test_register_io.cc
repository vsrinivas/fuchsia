// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <map>

#include <gtest/gtest.h>

#include "hwreg/bitfields.h"
#include "mock/mock_mmio.h"
#include "msd_intel_register_io.h"
#include "registers.h"

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

namespace {
struct TestParam {
  ForceWakeDomain domain;
  uint32_t mmio_base;
};
}  // namespace

class TestMsdIntelRegisterIoForceWake : public testing::TestWithParam<TestParam>,
                                        public MsdIntelRegisterIo::Owner {
 public:
  bool IsForceWakeDomainActive(ForceWakeDomain domain) override {
    domain_check_counts_[domain]++;
    return true;
  }

  static constexpr uint32_t kGen12DeviceId = 0x9A49;
  static constexpr uint32_t kGen12MmioSize = 0x200000;  // big enough

  std::map<ForceWakeDomain, uint32_t> domain_check_counts_;
};

TEST_P(TestMsdIntelRegisterIoForceWake, ForceWakeDomainCheck) {
  auto param = GetParam();

  auto register_io =
      std::make_unique<MsdIntelRegisterIo>(this, MockMmio::Create(kGen12MmioSize), kGen12DeviceId);

  EXPECT_EQ(0u, domain_check_counts_[param.domain]);

  uint32_t expected_count = 0;

  // Not in gen12 forcewake ranges
  register_io->Read32(0x1000);
  EXPECT_EQ(expected_count, domain_check_counts_[param.domain]);
  register_io->Read32(0x10000);
  EXPECT_EQ(expected_count, domain_check_counts_[param.domain]);
  register_io->Read32(0x1CD000);
  EXPECT_EQ(expected_count, domain_check_counts_[param.domain]);

  // Domain specific lowest
  switch (param.domain) {
    case ForceWakeDomain::RENDER:
      register_io->Read32(0x2000);
      break;
    case ForceWakeDomain::GEN12_VDBOX0:
      register_io->Read32(0x20000);
      break;
    default:
      ASSERT_TRUE(false);
  }
  EXPECT_EQ(++expected_count, domain_check_counts_[param.domain]);

  // Domain specific highest
  switch (param.domain) {
    case ForceWakeDomain::RENDER:
      register_io->Read32(0x1BFFC);
      break;
    case ForceWakeDomain::GEN12_VDBOX0:
      register_io->Read32(0x1CCFFC);
      break;
    default:
      ASSERT_TRUE(false);
  }
  EXPECT_EQ(++expected_count, domain_check_counts_[param.domain]);

  // Common register read
  register_io->Read32(param.mmio_base + registers::Timestamp::kOffset);
  EXPECT_EQ(++expected_count, domain_check_counts_[param.domain]);

  // Common register write
  register_io->Write32(0, param.mmio_base + registers::Timestamp::kOffset);
  EXPECT_EQ(++expected_count, domain_check_counts_[param.domain]);

  // 64-bit register read (ExecListStatusGen12)
  register_io->Read64(param.mmio_base + 0x234);
  EXPECT_EQ(++expected_count, domain_check_counts_[param.domain]);
}

INSTANTIATE_TEST_SUITE_P(
    TestMsdIntelRegisterIoForceWake, TestMsdIntelRegisterIoForceWake,
    testing::Values(TestParam{.domain = ForceWakeDomain::RENDER, .mmio_base = 0x2000},
                    TestParam{.domain = ForceWakeDomain::GEN12_VDBOX0, .mmio_base = 0x1C0000}),
    [](testing::TestParamInfo<TestParam> info) {
      switch (info.param.domain) {
        case ForceWakeDomain::RENDER:
          return "RENDER";
        case ForceWakeDomain::GEN12_VDBOX0:
          return "GEN12_VDBOX0";
        default:
          return "Unknown";
      }
    });
