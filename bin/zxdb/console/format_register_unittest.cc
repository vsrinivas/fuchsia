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

void SetRegisterValue(Register* reg, uint64_t value) {
  std::vector<uint8_t> data;
  data.reserve(8);
  uint8_t* ptr = reinterpret_cast<uint8_t*>(&value);
  for (size_t i = 0; i < 8; i++, ptr++)
    data.emplace_back(*ptr);
  reg->data() = data;
}

Register CreateRegisterWithValue(RegisterID id, uint64_t value) {
  Register reg(CreateRegister(id, 8));
  SetRegisterValue(&reg, value);
  return reg;
}

void GetCategories(RegisterSet* registers) {
  std::vector<debug_ipc::RegisterCategory> categories;

  RegisterCategory cat1;
  cat1.type = RegisterCategory::Type::kGeneral;
  cat1.registers.push_back(CreateRegister(RegisterID::kX64_rax, 1));
  cat1.registers.push_back(CreateRegister(RegisterID::kX64_rbx, 2));
  cat1.registers.push_back(CreateRegister(RegisterID::kX64_rcx, 4));
  cat1.registers.push_back(CreateRegister(RegisterID::kX64_rdx, 8));
  categories.push_back(cat1);

  // Sanity check
  ASSERT_EQ(*(uint8_t*)&(cat1.registers[0].data[0]), 0x01u);
  ASSERT_EQ(*(uint16_t*)&(cat1.registers[1].data[0]), 0x0102u);
  ASSERT_EQ(*(uint32_t*)&(cat1.registers[2].data[0]), 0x01020304u);
  ASSERT_EQ(*(uint64_t*)&(cat1.registers[3].data[0]), 0x0102030405060708u);

  RegisterCategory cat2;
  cat2.type = RegisterCategory::Type::kVector;
  cat2.registers.push_back(CreateRegister(RegisterID::kX64_xmm0, 1));
  cat2.registers.push_back(CreateRegister(RegisterID::kX64_xmm1, 2));
  cat2.registers.push_back(CreateRegister(RegisterID::kX64_xmm2, 4));
  cat2.registers.push_back(CreateRegister(RegisterID::kX64_xmm3, 8));
  cat2.registers.push_back(CreateRegister(RegisterID::kX64_xmm4, 16));
  categories.push_back(cat2);

  RegisterCategory cat3;
  cat3.type = RegisterCategory::Type::kFloatingPoint;
  cat3.registers.push_back(CreateRegister(RegisterID::kX64_st0, 4));
  cat3.registers.push_back(CreateRegister(RegisterID::kX64_st1, 4));
  // Invalid
  cat3.registers.push_back(CreateRegister(RegisterID::kX64_st2, 16));
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

  std::vector<RegisterCategory::Type> cats_to_show = {
      RegisterCategory::Type::kGeneral};
  FilteredRegisterSet filtered_set;
  Err err = FilterRegisters(registers, &filtered_set, cats_to_show);
  ASSERT_FALSE(err.has_error()) << err.msg();

  OutputBuffer out;
  err = FormatRegisters(debug_ipc::Arch::kX64, filtered_set, &out);
  ASSERT_FALSE(err.has_error()) << err.msg();

  EXPECT_EQ(
      "General Purpose Registers\n"
      "Name             Value\n"
      "rax           00000001\n"
      "rbx           00000102\n"
      "rcx           01020304\n"
      "rdx  01020304 05060708\n"
      "\n",
      out.AsString());
}

TEST(FormatRegisters, VectorRegisters) {
  RegisterSet registers;
  GetCategories(&registers);

  std::vector<RegisterCategory::Type> cats_to_show = {
      RegisterCategory::Type::kVector};
  FilteredRegisterSet filtered_set;
  Err err = FilterRegisters(registers, &filtered_set, cats_to_show);
  ASSERT_FALSE(err.has_error()) << err.msg();

  OutputBuffer out;
  err = FormatRegisters(debug_ipc::Arch::kX64, filtered_set, &out);
  ASSERT_FALSE(err.has_error()) << err.msg();

  EXPECT_EQ(
      "Vector Registers\n"
      "Name                               Value\n"
      "xmm0                            00000001\n"
      "xmm1                            00000102\n"
      "xmm2                            01020304\n"
      "xmm3                   01020304 05060708\n"
      "xmm4 01020304 05060708 090a0b0c 0d0e0f10\n"
      "\n",
      out.AsString());
}

TEST(FormatRegisters, AllRegisters) {
  RegisterSet registers;
  GetCategories(&registers);

  std::vector<RegisterCategory::Type> cats_to_show = {
      {RegisterCategory::Type::kGeneral, RegisterCategory::Type::kFloatingPoint,
       RegisterCategory::Type::kVector, RegisterCategory::Type::kMisc}};
  FilteredRegisterSet filtered_set;
  Err err = FilterRegisters(registers, &filtered_set, cats_to_show);
  ASSERT_FALSE(err.has_error()) << err.msg();

  OutputBuffer out;
  err = FormatRegisters(debug_ipc::Arch::kX64, filtered_set, &out);
  ASSERT_FALSE(err.has_error()) << err.msg();

  // TODO(donosoc): Detect the maximum length and make the the tables coincide.
  EXPECT_EQ(
      "General Purpose Registers\n"
      "Name             Value\n"
      "rax           00000001\n"
      "rbx           00000102\n"
      "rcx           01020304\n"
      "rdx  01020304 05060708\n"
      "\n"
      "Floating Point Registers\n"
      "Name                                 Value                       FP\n"
      "st0                               01020304             2.387939e-38\n"
      "st1                               01020304             2.387939e-38\n"
      "st2    00000000 00000000 00000000 00000000 0.000000000000000000e+00\n"
      "\n"
      "Vector Registers\n"
      "Name                               Value\n"
      "xmm0                            00000001\n"
      "xmm1                            00000102\n"
      "xmm2                            01020304\n"
      "xmm3                   01020304 05060708\n"
      "xmm4 01020304 05060708 090a0b0c 0d0e0f10\n"
      "\n",
      out.AsString());
}

TEST(FormatRegisters, OneRegister) {
  RegisterSet registers;
  GetCategories(&registers);

  std::vector<RegisterCategory::Type> cats_to_show = {
      {RegisterCategory::Type::kGeneral, RegisterCategory::Type::kFloatingPoint,
       RegisterCategory::Type::kVector, RegisterCategory::Type::kMisc}};
  FilteredRegisterSet filtered_set;
  Err err = FilterRegisters(registers, &filtered_set, cats_to_show, "xmm3");
  ASSERT_FALSE(err.has_error()) << err.msg();

  OutputBuffer out;
  err = FormatRegisters(debug_ipc::Arch::kX64, filtered_set, &out);
  ASSERT_FALSE(err.has_error()) << err.msg();

  EXPECT_EQ(
      "Vector Registers\n"
      "Name             Value\n"
      "xmm3 01020304 05060708\n"
      "\n",
      out.AsString());
}

TEST(FormatRegister, RegexSearch) {
  RegisterSet registers;
  GetCategories(&registers);

  std::vector<RegisterCategory::Type> cats_to_show = {
      RegisterCategory::Type::kVector};
  FilteredRegisterSet filtered_set;
  Err err =
      FilterRegisters(registers, &filtered_set, cats_to_show, "XMm[2-4]$");
  ASSERT_FALSE(err.has_error()) << err.msg();

  OutputBuffer out;
  err = FormatRegisters(debug_ipc::Arch::kX64, filtered_set, &out);
  ASSERT_FALSE(err.has_error()) << err.msg();

  EXPECT_EQ(
      "Vector Registers\n"
      "Name                               Value\n"
      "xmm2                            01020304\n"
      "xmm3                   01020304 05060708\n"
      "xmm4 01020304 05060708 090a0b0c 0d0e0f10\n"
      "\n",
      out.AsString());
}

TEST(FormatRegisters, CannotFindRegister) {
  RegisterSet registers;
  GetCategories(&registers);

  std::vector<RegisterCategory::Type> cats_to_show = {
      {RegisterCategory::Type::kGeneral, RegisterCategory::Type::kFloatingPoint,
       RegisterCategory::Type::kVector, RegisterCategory::Type::kMisc}};
  FilteredRegisterSet filtered_set;
  Err err = FilterRegisters(registers, &filtered_set, cats_to_show, "W0");
  EXPECT_TRUE(err.has_error());
}

TEST(FormatRegisters, WithRflags) {
  RegisterSet register_set;
  GetCategories(&register_set);
  auto& cat = register_set.category_map()[RegisterCategory::Type::kGeneral];
  cat.push_back(CreateRegisterWithValue(RegisterID::kX64_rflags, 0));

  std::vector<RegisterCategory::Type> cats_to_show = {
      RegisterCategory::Type::kGeneral};
  FilteredRegisterSet filtered_set;
  Err err = FilterRegisters(register_set, &filtered_set, cats_to_show);
  ASSERT_FALSE(err.has_error()) << err.msg();

  OutputBuffer out;
  err = FormatRegisters(debug_ipc::Arch::kX64, filtered_set, &out);
  ASSERT_FALSE(err.has_error()) << err.msg();

  EXPECT_EQ(
      "General Purpose Registers\n"
      "Name             Value\n"
      "rax           00000001\n"
      "rbx           00000102\n"
      "rcx           01020304\n"
      "rdx  01020304 05060708\n"
      "\n"
      "rflags  00000000 (CF=0, PF=0, AF=0, ZF=0, SF=0, TF=0, IF=0, DF=0, OF=0)\n"
      "\n",
      out.AsString());
}

TEST(FormatRegisters, RFlagsValues) {
  RegisterSet register_set;
  auto& cat = register_set.category_map()[RegisterCategory::Type::kGeneral];
  cat.push_back(CreateRegisterWithValue(RegisterID::kX64_rflags, 0));

  std::vector<RegisterCategory::Type> cats_to_show = {
      RegisterCategory::Type::kGeneral};
  FilteredRegisterSet filtered_set;
  Err err =
      FilterRegisters(register_set, &filtered_set, cats_to_show, "rflags");
  ASSERT_FALSE(err.has_error()) << err.msg();

  auto& reg = filtered_set[RegisterCategory::Type::kGeneral].front();
  // filtered_set now holds a pointer to rflags that we can change.
  SetRegisterValue(&reg, 0b1110100110010101010101);

  OutputBuffer out;
  err = FormatRegisters(debug_ipc::Arch::kX64, filtered_set, &out);
  ASSERT_FALSE(err.has_error()) << err.msg();

  EXPECT_EQ(
      "General Purpose Registers\n"
      "rflags  003a6555 (CF=1, PF=1, AF=1, ZF=1, SF=0, TF=1, IF=0, DF=1, OF=0)\n"
      "\n",
      out.AsString());
}

}  // namespace zxdb
