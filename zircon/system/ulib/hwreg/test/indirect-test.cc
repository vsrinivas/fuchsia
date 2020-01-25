// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <stdio.h>
#include <zircon/types.h>

#include <climits>
#include <limits>

#include <hwreg/bitfields.h>
#include <hwreg/indirect.h>
#include <hwreg/mmio.h>
#include <zxtest/zxtest.h>

// This function exists so that the resulting code can be inspected easily in the
// object file.
[[maybe_unused]] static void compilation_test() {
  typedef hwreg::IndirectIo<0x00, 0x04> Io;

  volatile struct {
    uint32_t index = 0xaa;
    uint32_t reserved = 0xff;
    uint32_t data = 0x11;
  } fake_regs;
  auto io = Io(static_cast<volatile void*>(&fake_regs));

  io.Write<uint32_t>(1, 0);
  io.Read<uint32_t>(0);
}

namespace {

template <typename IntType, typename IndexType>
void basic_access_test() {
  typedef struct {
    IndexType index;
    // Test an indexed register with a reserved field in the middle.
    IntType reserved;
    IntType data;
  } FakeRegs;
  volatile FakeRegs fake_regs;
  typedef hwreg::IndirectIo<
      static_cast<uint32_t>(offsetof(FakeRegs, index)),
      static_cast<uint32_t>(offsetof(FakeRegs, data)),
      IndexType> Io;
  fake_regs.index = 1;
  fake_regs.reserved = 3;
  fake_regs.data = 2;
  Io io(hwreg::RegisterIo(static_cast<volatile void*>(&fake_regs)));

  class Reg : public hwreg::RegisterBase<Reg, IntType> {
   public:
    static auto Get(uint32_t offset) { return hwreg::RegisterAddr<Reg>(offset); }
  };

  // Validate that reading from .data works
  EXPECT_EQ(2, Reg::Get(0).ReadFrom(&io).reg_value());
  EXPECT_EQ(0, fake_regs.index);
  EXPECT_EQ(3, fake_regs.reserved);

  // That reading from another register updates the index.
  fake_regs.data = 6;
  EXPECT_EQ(6, Reg::Get(2).ReadFrom(&io).reg_value());
  EXPECT_EQ(2, fake_regs.index);
  EXPECT_EQ(3, fake_regs.reserved);

  // And writing also updates the index.
  Reg::Get(0).ReadFrom(&io).set_reg_value(static_cast<IntType>(5)).WriteTo(&io);
  EXPECT_EQ(0, fake_regs.index);
  EXPECT_EQ(5, fake_regs.data);
  EXPECT_EQ(3, fake_regs.reserved);
}

template <typename IntType>
void single_thread_test() {
  basic_access_test<IntType, IntType>();
  basic_access_test<IntType, uint8_t>();
}

TEST(SingleThreadTestCase, Uint8) { ASSERT_NO_FAILURES(single_thread_test<uint8_t>()); }
TEST(SingleThreadTestCase, Uint16) { ASSERT_NO_FAILURES(single_thread_test<uint16_t>()); }
TEST(SingleThreadTestCase, Uint32) { ASSERT_NO_FAILURES(single_thread_test<uint32_t>()); }
TEST(SingleThreadTestCase, Uint64) { ASSERT_NO_FAILURES(single_thread_test<uint64_t>()); }

TEST(AlignedAccessTest, Uint32) {
  typedef struct {
    uint16_t index;
    uint16_t reserved;
    uint32_t data;
  } FakeRegs;
  volatile FakeRegs fake_regs;
  typedef hwreg::IndirectIo<offsetof(FakeRegs, index), offsetof(FakeRegs, data), uint16_t> Io;
  fake_regs.index = 0xaaaa;
  fake_regs.reserved = 0xffff;
  fake_regs.data = 0x12345678;
  Io io(hwreg::RegisterIo(static_cast<volatile void*>(&fake_regs)));

  class MatchingReg : public hwreg::RegisterBase<MatchingReg, uint32_t> {
   public:
    static auto Get(uint32_t offset) { return hwreg::RegisterAddr<MatchingReg>(offset); }
  };

  EXPECT_EQ(0x12345678, MatchingReg::Get(0).ReadFrom(&io).reg_value());

  class SmallReg : public hwreg::RegisterBase<SmallReg, uint16_t> {
   public:
    static auto Get(uint32_t offset) { return hwreg::RegisterAddr<SmallReg>(offset); }
  };

  EXPECT_EQ(fake_regs.data & 0xffff, SmallReg::Get(0).ReadFrom(&io).reg_value());
}

}  // namespace
