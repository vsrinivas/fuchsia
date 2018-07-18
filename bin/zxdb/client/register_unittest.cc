// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/client/register.h"
#include "gtest/gtest.h"

namespace zxdb {

using debug_ipc::RegisterID;

namespace {

std::vector<uint8_t> CreateData(size_t length) {
  std::vector<uint8_t> data;
  data.reserve(length);
  // So that we get the number backwards (0x0102...).
  uint8_t base = length;
  for (size_t i = 0; i < length; i++) {
    data.emplace_back(base - i);
  }
  return data;
}

debug_ipc::Register CreateRegister(RegisterID id, size_t length) {
  debug_ipc::Register reg;
  reg.id = id;
  reg.data = CreateData(length);
  return reg;
}

}  // namespace

TEST(Register, CorrectlyCreatesBoundaries) {
  debug_ipc::Register ipc_reg = CreateRegister(RegisterID::ARMv8_x0, 1);
  Register reg(ipc_reg);
  ASSERT_EQ(reg.size(), 1u);
  ASSERT_EQ((size_t)(reg.end() - reg.begin()), ipc_reg.data.size());
  EXPECT_EQ(reg.GetValue(), 0x01u);
  auto it = reg.begin();
  EXPECT_EQ(*it++, ipc_reg.data[0]);
  EXPECT_EQ(it, reg.end());

  ipc_reg = CreateRegister(RegisterID::ARMv8_x1, 2);
  reg = Register(ipc_reg);
  ASSERT_EQ(reg.size(), 2u);
  ASSERT_EQ((size_t)(reg.end() - reg.begin()), ipc_reg.data.size());
  EXPECT_EQ(reg.GetValue(), 0x0102u);
  it = reg.begin();
  EXPECT_EQ(*it++, ipc_reg.data[0]);
  EXPECT_EQ(*it++, ipc_reg.data[1]);
  EXPECT_EQ(it, reg.end());

  ipc_reg = CreateRegister(RegisterID::ARMv8_lr, 4);
  reg = Register(ipc_reg);
  ASSERT_EQ(reg.size(), 4u);
  ASSERT_EQ((size_t)(reg.end() - reg.begin()), ipc_reg.data.size());
  EXPECT_EQ(reg.GetValue(), 0x01020304u);
  it = reg.begin();
  EXPECT_EQ(*it++, ipc_reg.data[0]);
  EXPECT_EQ(*it++, ipc_reg.data[1]);
  EXPECT_EQ(*it++, ipc_reg.data[2]);
  EXPECT_EQ(*it++, ipc_reg.data[3]);
  EXPECT_EQ(it, reg.end());

  ipc_reg = CreateRegister(RegisterID::ARMv8_cpsr, 8);
  reg = Register(ipc_reg);
  ASSERT_EQ(reg.size(), 8u);
  ASSERT_EQ((size_t)(reg.end() - reg.begin()), ipc_reg.data.size());
  EXPECT_EQ(reg.GetValue(), 0x0102030405060708u);
  it = reg.begin();
  EXPECT_EQ(*it++, ipc_reg.data[0]);
  EXPECT_EQ(*it++, ipc_reg.data[1]);
  EXPECT_EQ(*it++, ipc_reg.data[2]);
  EXPECT_EQ(*it++, ipc_reg.data[3]);
  EXPECT_EQ(*it++, ipc_reg.data[4]);
  EXPECT_EQ(*it++, ipc_reg.data[5]);
  EXPECT_EQ(*it++, ipc_reg.data[6]);
  EXPECT_EQ(*it++, ipc_reg.data[7]);
  EXPECT_EQ(it, reg.end());

  ipc_reg = CreateRegister(RegisterID::ARMv8_x11, 16);
  reg = Register(ipc_reg);
  ASSERT_EQ(reg.size(), 16u);
  ASSERT_EQ((size_t)(reg.end() - reg.begin()), ipc_reg.data.size());
  it = reg.begin();
  EXPECT_EQ(*it++, ipc_reg.data[0]);
  EXPECT_EQ(*it++, ipc_reg.data[1]);
  EXPECT_EQ(*it++, ipc_reg.data[2]);
  EXPECT_EQ(*it++, ipc_reg.data[3]);
  EXPECT_EQ(*it++, ipc_reg.data[4]);
  EXPECT_EQ(*it++, ipc_reg.data[5]);
  EXPECT_EQ(*it++, ipc_reg.data[6]);
  EXPECT_EQ(*it++, ipc_reg.data[7]);
  EXPECT_EQ(*it++, ipc_reg.data[8]);
  EXPECT_EQ(*it++, ipc_reg.data[9]);
  EXPECT_EQ(*it++, ipc_reg.data[10]);
  EXPECT_EQ(*it++, ipc_reg.data[11]);
  EXPECT_EQ(*it++, ipc_reg.data[12]);
  EXPECT_EQ(*it++, ipc_reg.data[13]);
  EXPECT_EQ(*it++, ipc_reg.data[14]);
  EXPECT_EQ(*it++, ipc_reg.data[15]);
  EXPECT_EQ(it, reg.end());
}

}   // namespace zxdb
