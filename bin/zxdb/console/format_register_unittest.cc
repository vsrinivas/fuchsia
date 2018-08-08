// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/console/format_register.cc"
#include "gtest/gtest.h"

namespace zxdb {

using namespace debug_ipc;

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

void GetCategories(RegisterSet* registers) {
  std::vector<debug_ipc::RegisterCategory> categories;

  RegisterCategory cat1;
  cat1.type = RegisterCategory::Type::kGeneral;
  cat1.registers.push_back(CreateRegister(RegisterID::kARMv8_lr, 1));
  cat1.registers.push_back(CreateRegister(RegisterID::kARMv8_pc, 2));
  cat1.registers.push_back(CreateRegister(RegisterID::kARMv8_sp, 4));
  cat1.registers.push_back(CreateRegister(RegisterID::kARMv8_cpsr, 8));
  categories.push_back(cat1);

  // Sanity check
  ASSERT_EQ(*(uint8_t*)&(cat1.registers[0].data[0]), 0x01u);
  ASSERT_EQ(*(uint16_t*)&(cat1.registers[1].data[0]), 0x0102u);
  ASSERT_EQ(*(uint32_t*)&(cat1.registers[2].data[0]), 0x01020304u);
  ASSERT_EQ(*(uint64_t*)&(cat1.registers[3].data[0]), 0x0102030405060708u);

  RegisterCategory cat2;
  cat2.type = RegisterCategory::Type::kVector;
  cat2.registers.push_back(CreateRegister(RegisterID::kARMv8_x0, 1));
  cat2.registers.push_back(CreateRegister(RegisterID::kARMv8_x1, 2));
  cat2.registers.push_back(CreateRegister(RegisterID::kARMv8_x2, 4));
  cat2.registers.push_back(CreateRegister(RegisterID::kARMv8_x3, 8));
  cat2.registers.push_back(CreateRegister(RegisterID::kARMv8_x4, 16));
  categories.push_back(cat2);

  RegisterCategory cat3;
  cat3.type = RegisterCategory::Type::kFloatingPoint;
  cat3.registers.push_back(CreateRegister(RegisterID::kARMv8_x20, 4));
  cat3.registers.push_back(CreateRegister(RegisterID::kARMv8_x21, 4));
  // Invalid
  cat3.registers.push_back(CreateRegister(RegisterID::kARMv8_x24, 16));
  // Push a valid 16-byte long double value.
  auto& reg = cat3.registers.back();
  // Clear all (create a 0 value).
  for (size_t i = 0; i < reg.data.size(); i++)
    reg.data[i] = 0;

  categories.push_back(cat3);

  RegisterSet regs(debug_ipc::Arch::kArm64, std::move(categories));
  *registers = std::move(regs);
}

}  // namespace

TEST(FormatRegisters, GeneralRegisters) {
  RegisterSet registers;
  GetCategories(&registers);
  OutputBuffer out;
  Err err = FormatRegisters(registers, "", &out);

  ASSERT_FALSE(err.has_error()) << err.msg();

  EXPECT_EQ(
      "General Purpose Registers\n"
      "Name Size               Value\n"
      "lr      1                  01\n"
      "pc      2                0102\n"
      "sp      4            01020304\n"
      "cpsr    8   01020304 05060708\n"
      "\n",
      out.AsString());
}

TEST(FormatRegisters, VectorRegisters) {
  RegisterSet registers;
  GetCategories(&registers);
  OutputBuffer out;
  Err err =
      FormatRegisters(registers, "", &out, {RegisterCategory::Type::kVector});

  ASSERT_FALSE(err.has_error()) << err.msg();

  EXPECT_EQ(
      "Vector Registers\n"
      "Name Size                                 Value\n"
      "x0      1                                    01\n"
      "x1      2                                  0102\n"
      "x2      4                              01020304\n"
      "x3      8                     01020304 05060708\n"
      "x4     16   01020304 05060708 090a0b0c 0d0e0f10\n"
      "\n",
      out.AsString());
}

TEST(FormatRegisters, AllRegisters) {
  RegisterSet registers;
  GetCategories(&registers);
  OutputBuffer out;
  Err err = FormatRegisters(
      registers, "", &out,
      {RegisterCategory::Type::kGeneral, RegisterCategory::Type::kFloatingPoint,
       RegisterCategory::Type::kVector, RegisterCategory::Type::kMisc});

  ASSERT_FALSE(err.has_error()) << err.msg();

  // TODO(donosoc): Detect the maximum length and make the the tables coincide.
  EXPECT_EQ(
      "General Purpose Registers\n"
      "Name Size               Value\n"
      "lr      1                  01\n"
      "pc      2                0102\n"
      "sp      4            01020304\n"
      "cpsr    8   01020304 05060708\n"
      "\n"
      "Floating Point Registers\n"
      "Name Size                                 Value                       "
      "FP\n"
      "x20     4                              01020304             "
      "2.387939e-38\n"
      "x21     4                              01020304             "
      "2.387939e-38\n"
      "x24    16   00000000 00000000 00000000 00000000 "
      "0.000000000000000000e+00\n"
      "\n"
      "Vector Registers\n"
      "Name Size                                 Value\n"
      "x0      1                                    01\n"
      "x1      2                                  0102\n"
      "x2      4                              01020304\n"
      "x3      8                     01020304 05060708\n"
      "x4     16   01020304 05060708 090a0b0c 0d0e0f10\n"
      "\n",
      out.AsString());
}

TEST(FormatRegisters, OneRegister) {
  RegisterSet registers;
  GetCategories(&registers);
  OutputBuffer out;
  Err err =
      FormatRegisters(registers, "x3", &out, {RegisterCategory::Type::kVector});

  ASSERT_FALSE(err.has_error()) << err.msg();

  EXPECT_EQ(
      "Vector Registers\n"
      "Name Size               Value\n"
      "x3      8   01020304 05060708\n"
      "\n",
      out.AsString());
}

TEST(FormatRegister, RegexSearch) {
  RegisterSet registers;
  GetCategories(&registers);
  OutputBuffer out;

  // Case insensitive search.
  Err err = FormatRegisters(registers, "X[3-5]$", &out,
                            {RegisterCategory::Type::kVector});

  ASSERT_FALSE(err.has_error()) << err.msg();

  EXPECT_EQ(
      "Vector Registers\n"
      "Name Size                                 Value\n"
      "x3      8                     01020304 05060708\n"
      "x4     16   01020304 05060708 090a0b0c 0d0e0f10\n"
      "\n",
      out.AsString());
}

TEST(FormatRegisters, CannotFindRegister) {
  RegisterSet registers;
  GetCategories(&registers);
  OutputBuffer out;
  Err err = FormatRegisters(registers, "W0", &out);

  ASSERT_TRUE(err.has_error());
}

}  // namespace zxdb
