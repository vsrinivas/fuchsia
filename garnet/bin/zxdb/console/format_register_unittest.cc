// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>

#include "garnet/bin/zxdb/console/format_register.cc"
#include "lib/fxl/logging.h"
#include "src/developer/debug/shared/arch_arm64.h"
#include "src/developer/debug/shared/arch_x86.h"

namespace zxdb {

using namespace debug_ipc;

namespace {

// Creates fake data for a register.
// |length| is how long the register data (and thus the register) is.
// |val_loop| is to determine a loop that will fill the register with a
// particular pattern (010203....).
std::vector<uint8_t> CreateData(size_t length, size_t val_loop) {
  FXL_DCHECK(length >= val_loop);

  std::vector<uint8_t> data(length);
  // So that we get the number backwards (0x0102...).
  uint8_t base = val_loop;
  for (size_t i = 0; i < val_loop; i++) {
    data[i] = base - i;
  }
  return data;
}

debug_ipc::Register CreateRegister(RegisterID id, size_t length,
                                   size_t val_loop) {
  debug_ipc::Register reg;
  reg.id = id;
  reg.data = CreateData(length, val_loop);
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
  Register reg(CreateRegister(id, 8, 8));
  SetRegisterValue(&reg, value);
  return reg;
}

void GetCategories(RegisterSet* registers) {
  std::vector<debug_ipc::RegisterCategory> categories;

  RegisterCategory cat1;
  cat1.type = RegisterCategory::Type::kGeneral;
  cat1.registers.push_back(CreateRegister(RegisterID::kX64_rax, 8, 1));
  cat1.registers.push_back(CreateRegister(RegisterID::kX64_rbx, 8, 2));
  cat1.registers.push_back(CreateRegister(RegisterID::kX64_rcx, 8, 4));
  cat1.registers.push_back(CreateRegister(RegisterID::kX64_rdx, 8, 8));
  categories.push_back(cat1);

  // Sanity check
  ASSERT_EQ(*(uint8_t*)&(cat1.registers[0].data[0]), 0x01u);
  ASSERT_EQ(*(uint16_t*)&(cat1.registers[1].data[0]), 0x0102u);
  ASSERT_EQ(*(uint32_t*)&(cat1.registers[2].data[0]), 0x01020304u);
  ASSERT_EQ(*(uint64_t*)&(cat1.registers[3].data[0]), 0x0102030405060708u);

  RegisterCategory cat2;
  cat2.type = RegisterCategory::Type::kVector;
  cat2.registers.push_back(CreateRegister(RegisterID::kX64_xmm0, 16, 1));
  cat2.registers.push_back(CreateRegister(RegisterID::kX64_xmm1, 16, 2));
  cat2.registers.push_back(CreateRegister(RegisterID::kX64_xmm2, 16, 4));
  cat2.registers.push_back(CreateRegister(RegisterID::kX64_xmm3, 16, 8));
  cat2.registers.push_back(CreateRegister(RegisterID::kX64_xmm4, 16, 16));
  categories.push_back(cat2);

  RegisterCategory cat3;
  cat3.type = RegisterCategory::Type::kFP;
  cat3.registers.push_back(CreateRegister(RegisterID::kX64_st0, 16, 4));
  cat3.registers.push_back(CreateRegister(RegisterID::kX64_st1, 16, 4));
  // Invalid
  cat3.registers.push_back(CreateRegister(RegisterID::kX64_st2, 16, 16));
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

  FormatRegisterOptions options;
  options.arch = debug_ipc::Arch::kX64;
  options.categories = {RegisterCategory::Type::kGeneral};

  FilteredRegisterSet filtered_set;
  Err err = FilterRegisters(options, registers, &filtered_set);
  ASSERT_FALSE(err.has_error()) << err.msg();

  // Force rcx to -2 to test negative integer formatting.
  auto& reg = filtered_set[RegisterCategory::Type::kGeneral];
  Register& rcx = reg[2];
  EXPECT_EQ(RegisterID::kX64_rcx, rcx.id());
  SetRegisterValue(&rcx, static_cast<uint64_t>(-2));

  OutputBuffer out;
  err = FormatRegisters(options, filtered_set, &out);
  ASSERT_FALSE(err.has_error()) << err.msg();

  EXPECT_EQ(
      "General Purpose Registers\n"
      "rax                 0x1 = 1\n"
      "rbx               0x102 = 258\n"
      "rcx  0xfffffffffffffffe = -2\n"
      "rdx   0x102030405060708 \n"
      "\n",
      out.AsString());
}

TEST(FormatRegisters, VectorRegisters) {
  RegisterSet registers;
  GetCategories(&registers);

  FormatRegisterOptions options;
  options.arch = debug_ipc::Arch::kX64;
  options.categories = {RegisterCategory::Type::kVector};

  FilteredRegisterSet filtered_set;
  Err err = FilterRegisters(options, registers, &filtered_set);
  ASSERT_FALSE(err.has_error()) << err.msg();

  OutputBuffer out;
  err = FormatRegisters(options, filtered_set, &out);
  ASSERT_FALSE(err.has_error()) << err.msg();

  EXPECT_EQ(
      "Vector Registers\n"
      "xmm0 00000000 00000000 00000000 00000001\n"
      "xmm1 00000000 00000000 00000000 00000102\n"
      "xmm2 00000000 00000000 00000000 01020304\n"
      "xmm3 00000000 00000000 01020304 05060708\n"
      "xmm4 01020304 05060708 090a0b0c 0d0e0f10\n"
      "\n",
      out.AsString());
}

TEST(FormatRegisters, AllRegisters) {
  RegisterSet registers;
  GetCategories(&registers);

  FormatRegisterOptions options;
  options.arch = debug_ipc::Arch::kX64;
  options.categories = {RegisterCategory::Type::kGeneral,
                        RegisterCategory::Type::kFP,
                        RegisterCategory::Type::kVector};
  FilteredRegisterSet filtered_set;
  Err err = FilterRegisters(options, registers, &filtered_set);
  ASSERT_FALSE(err.has_error()) << err.msg();

  OutputBuffer out;
  err = FormatRegisters(options, filtered_set, &out);
  ASSERT_FALSE(err.has_error()) << err.msg();

  // TODO(donosoc): Detect the maximum length and make the tables coincide.
  EXPECT_EQ(
      "General Purpose Registers\n"
      "rax                0x1 = 1\n"
      "rbx              0x102 = 258\n"
      "rcx          0x1020304 \n"
      "rdx  0x102030405060708 \n"
      "\n"
      "Floating Point Registers\n"
      "st0  6.163689759657267600e-4944  00000000 00000000 00000000 01020304\n"
      "st1  6.163689759657267600e-4944  00000000 00000000 00000000 01020304\n"
      "st2  0.000000000000000000e+00    00000000 00000000 00000000 00000000\n"
      "\n"
      "Vector Registers\n"
      "xmm0 00000000 00000000 00000000 00000001\n"
      "xmm1 00000000 00000000 00000000 00000102\n"
      "xmm2 00000000 00000000 00000000 01020304\n"
      "xmm3 00000000 00000000 01020304 05060708\n"
      "xmm4 01020304 05060708 090a0b0c 0d0e0f10\n"
      "\n",
      out.AsString());
}

TEST(FormatRegisters, OneRegister) {
  RegisterSet registers;
  GetCategories(&registers);

  FormatRegisterOptions options;
  options.arch = debug_ipc::Arch::kX64;
  options.filter_regexp = "xmm3";
  options.categories = {RegisterCategory::Type::kGeneral,
                        RegisterCategory::Type::kFP,
                        RegisterCategory::Type::kVector};

  FilteredRegisterSet filtered_set;
  Err err = FilterRegisters(options, registers, &filtered_set);
  ASSERT_FALSE(err.has_error()) << err.msg();

  OutputBuffer out;
  err = FormatRegisters(options, filtered_set, &out);
  ASSERT_FALSE(err.has_error()) << err.msg();

  EXPECT_EQ(
      "Vector Registers\n"
      "xmm3 00000000 00000000 01020304 05060708\n"
      "\n",
      out.AsString());
}

TEST(FormatRegister, RegexSearch) {
  RegisterSet registers;
  GetCategories(&registers);

  FormatRegisterOptions options;
  options.arch = debug_ipc::Arch::kX64;
  options.filter_regexp = "XMm[2-4]$";
  options.categories = {RegisterCategory::Type::kVector};

  FilteredRegisterSet filtered_set;
  Err err = FilterRegisters(options, registers, &filtered_set);
  ASSERT_FALSE(err.has_error()) << err.msg();

  OutputBuffer out;
  err = FormatRegisters(options, filtered_set, &out);
  ASSERT_FALSE(err.has_error()) << err.msg();

  EXPECT_EQ(
      "Vector Registers\n"
      "xmm2 00000000 00000000 00000000 01020304\n"
      "xmm3 00000000 00000000 01020304 05060708\n"
      "xmm4 01020304 05060708 090a0b0c 0d0e0f10\n"
      "\n",
      out.AsString());
}

TEST(FormatRegisters, CannotFindRegister) {
  RegisterSet registers;
  GetCategories(&registers);

  FormatRegisterOptions options;
  options.arch = debug_ipc::Arch::kX64;
  options.filter_regexp = "W0";
  options.categories = {RegisterCategory::Type::kGeneral,
                        RegisterCategory::Type::kFP,
                        RegisterCategory::Type::kVector};
  FilteredRegisterSet filtered_set;
  Err err = FilterRegisters(options, registers, &filtered_set);
  EXPECT_TRUE(err.has_error());
}

TEST(FormatRegisters, WithRflags) {
  RegisterSet register_set;
  GetCategories(&register_set);
  auto& cat = register_set.category_map()[RegisterCategory::Type::kGeneral];
  cat.push_back(CreateRegisterWithValue(RegisterID::kX64_rflags, 0));

  FormatRegisterOptions options;
  options.arch = debug_ipc::Arch::kX64;
  options.categories = {RegisterCategory::Type::kGeneral};

  FilteredRegisterSet filtered_set;
  Err err = FilterRegisters(options, register_set, &filtered_set);
  ASSERT_FALSE(err.has_error()) << err.msg();

  OutputBuffer out;
  err = FormatRegisters(options, filtered_set, &out);
  ASSERT_FALSE(err.has_error()) << err.msg();

  EXPECT_EQ(
      "General Purpose Registers\n"
      "   rax                0x1 = 1\n"
      "   rbx              0x102 = 258\n"
      "   rcx          0x1020304 \n"
      "   rdx  0x102030405060708 \n"
      "rflags         0x00000000 CF=0, PF=0, AF=0, ZF=0, SF=0, TF=0, IF=0, "
      "DF=0, OF=0\n"
      "\n",
      out.AsString());
}

TEST(FormatRegisters, RFlagsValues) {
  RegisterSet register_set;
  auto& cat = register_set.category_map()[RegisterCategory::Type::kGeneral];
  cat.push_back(CreateRegisterWithValue(RegisterID::kX64_rflags, 0));

  FormatRegisterOptions options;
  options.arch = debug_ipc::Arch::kX64;
  options.filter_regexp = "rflags";
  options.categories = {RegisterCategory::Type::kGeneral};

  FilteredRegisterSet filtered_set;
  Err err = FilterRegisters(options, register_set, &filtered_set);
  ASSERT_FALSE(err.has_error()) << err.msg();

  // filtered_set now holds a pointer to rflags that we can change.
  auto& reg = filtered_set[RegisterCategory::Type::kGeneral].front();
  SetRegisterValue(&reg, X86_FLAG_MASK(RflagsCF) | X86_FLAG_MASK(RflagsPF) |
                             X86_FLAG_MASK(RflagsAF) | X86_FLAG_MASK(RflagsZF) |
                             X86_FLAG_MASK(RflagsTF) | X86_FLAG_MASK(RflagsDF));

  OutputBuffer out;
  err = FormatRegisters(options, filtered_set, &out);
  ASSERT_FALSE(err.has_error()) << err.msg();

  EXPECT_EQ(
      "General Purpose Registers\n"
      "rflags  0x00000555 CF=1, PF=1, AF=1, ZF=1, SF=0, TF=1, IF=0, DF=1, "
      "OF=0\n"
      "\n",
      out.AsString());
}

TEST(FormatRegisters, RFlagsValuesExtended) {
  RegisterSet register_set;
  auto& cat = register_set.category_map()[RegisterCategory::Type::kGeneral];
  cat.push_back(CreateRegisterWithValue(RegisterID::kX64_rflags, 0));

  FormatRegisterOptions options;
  options.arch = debug_ipc::Arch::kX64;
  options.filter_regexp = "rflags";
  options.extended = true;
  options.categories = {RegisterCategory::Type::kGeneral};

  FilteredRegisterSet filtered_set;
  Err err = FilterRegisters(options, register_set, &filtered_set);
  ASSERT_FALSE(err.has_error()) << err.msg();

  // filtered_set now holds a pointer to rflags that we can change.
  auto& reg = filtered_set[RegisterCategory::Type::kGeneral].front();

  SetRegisterValue(&reg, X86_FLAG_MASK(RflagsCF) | X86_FLAG_MASK(RflagsPF) |
                             X86_FLAG_MASK(RflagsAF) | X86_FLAG_MASK(RflagsZF) |
                             X86_FLAG_MASK(RflagsTF) | X86_FLAG_MASK(RflagsDF) |
                             // Extended flags
                             (0b10 << kRflagsIOPLShift) |
                             X86_FLAG_MASK(RflagsNT) | X86_FLAG_MASK(RflagsVM) |
                             X86_FLAG_MASK(RflagsVIF) |
                             X86_FLAG_MASK(RflagsID));

  OutputBuffer out;
  err = FormatRegisters(options, filtered_set, &out);
  ASSERT_FALSE(err.has_error()) << err.msg();

  EXPECT_EQ(
      "General Purpose Registers\n"
      "rflags  0x002a6555 CF=1, PF=1, AF=1, ZF=1, SF=0, TF=1, IF=0, DF=1, "
      "OF=0\n"
      "                   IOPL=2, NT=1, RF=0, VM=1, AC=0, VIF=1, VIP=0, ID=1\n"
      "\n",
      out.AsString());
}

TEST(FormatRegisters, CPSRValues) {
  RegisterSet register_set;
  auto& cat = register_set.category_map()[RegisterCategory::Type::kGeneral];
  cat.push_back(CreateRegisterWithValue(RegisterID::kARMv8_cpsr, 0));

  FormatRegisterOptions options;
  options.arch = debug_ipc::Arch::kArm64;
  options.filter_regexp = "cpsr";
  options.categories = {RegisterCategory::Type::kGeneral};

  FilteredRegisterSet filtered_set;
  Err err = FilterRegisters(options, register_set, &filtered_set);
  ASSERT_FALSE(err.has_error()) << err.msg();

  // filtered_set now holds a pointer to rflags that we can change.
  auto& reg = filtered_set[RegisterCategory::Type::kGeneral].front();
  SetRegisterValue(&reg, ARM64_FLAG_MASK(Cpsr, C) | ARM64_FLAG_MASK(Cpsr, N));

  OutputBuffer out;
  err = FormatRegisters(options, filtered_set, &out);
  ASSERT_FALSE(err.has_error()) << err.msg();

  EXPECT_EQ(
      "General Purpose Registers\n"
      "cpsr  0xa0000000 V=0, C=1, Z=0, N=1\n"
      "\n",
      out.AsString());

  // Check out extended
  SetRegisterValue(&reg,
                   ARM64_FLAG_MASK(Cpsr, C) | ARM64_FLAG_MASK(Cpsr, N) |
                       // Extended flags.
                       ARM64_FLAG_MASK(Cpsr, EL) | ARM64_FLAG_MASK(Cpsr, I) |
                       ARM64_FLAG_MASK(Cpsr, A) | ARM64_FLAG_MASK(Cpsr, IL) |
                       ARM64_FLAG_MASK(Cpsr, PAN) | ARM64_FLAG_MASK(Cpsr, UAO));

  options.extended = true;
  err = FormatRegisters(options, filtered_set, &out);
  ASSERT_FALSE(err.has_error()) << err.msg();

  EXPECT_EQ(
      "General Purpose Registers\n"
      "cpsr  0xa0d00181 V=0, C=1, Z=0, N=1\n"
      "                 EL=1, F=0, I=1, A=1, D=0, IL=1, SS=0, PAN=1, UAO=1\n"
      "\n",
      out.AsString());
}

TEST(FormatRegisters, DebugRegisters_x86) {
  RegisterSet register_set;
  auto& cat = register_set.category_map()[RegisterCategory::Type::kDebug];
  cat.push_back(CreateRegisterWithValue(RegisterID::kX64_dr0, 0x1234));
  cat.push_back(CreateRegisterWithValue(RegisterID::kX64_dr1, 0x1234567));
  cat.push_back(CreateRegisterWithValue(RegisterID::kX64_dr2, 0x123456789ab));
  cat.push_back(
      CreateRegisterWithValue(RegisterID::kX64_dr3, 0x123456789abcdef));

  cat.push_back(CreateRegisterWithValue(RegisterID::kX64_dr6, 0xaffa));
  cat.push_back(CreateRegisterWithValue(RegisterID::kX64_dr7, 0xaaaa26aa));

  FormatRegisterOptions options;
  options.arch = debug_ipc::Arch::kX64;
  options.categories = {RegisterCategory::Type::kDebug};

  FilteredRegisterSet filtered_set;
  Err err = FilterRegisters(options, register_set, &filtered_set);
  ASSERT_FALSE(err.has_error()) << err.msg();

  OutputBuffer out;
  err = FormatRegisters(options, filtered_set, &out);
  ASSERT_FALSE(err.has_error()) << err.msg();

  EXPECT_EQ(
      "Debug Registers\n"
      "dr0             0x1234 = 4660\n"
      "dr1          0x1234567 \n"
      "dr2      0x123456789ab \n"
      "dr3  0x123456789abcdef \n"
      "dr6         0x0000affa B0=0, B1=1, B2=0, B3=1, BD=1, BS=0, BT=1\n"
      "dr7         0xaaaa26aa L0=0, G0=1, L1=0, G1=1, L2=0, G2=1, L3=0, G4=1, "
      "LE=0, GE=1, GD=1\n"
      "                       R/W0=2, LEN0=2, R/W1=2, LEN1=2, R/W2=2, LEN2=2, "
      "R/W3=2, LEN3=2\n"
      "\n",
      out.AsString());
}

TEST(FormatRegisters, DebugRegisters_arm64) {
  RegisterSet register_set;
  auto& cat = register_set.category_map()[RegisterCategory::Type::kDebug];
  cat.push_back(CreateRegisterWithValue(RegisterID::kARMv8_dbgbcr0_el1,
                                        ARM64_FLAG_MASK(DBGBCR, PMC) |
                                            ARM64_FLAG_MASK(DBGBCR, HMC) |
                                            ARM64_FLAG_MASK(DBGBCR, LBN)));
  cat.push_back(CreateRegisterWithValue(RegisterID::kARMv8_dbgbvr0_el1,
                                        0xdeadbeefaabbccdd));
  cat.push_back(CreateRegisterWithValue(
      RegisterID::kARMv8_dbgbcr15_el1,
      ARM64_FLAG_MASK(DBGBCR, E) | ARM64_FLAG_MASK(DBGBCR, BAS) |
          ARM64_FLAG_MASK(DBGBCR, SSC) | ARM64_FLAG_MASK(DBGBCR, BT)));
  cat.push_back(CreateRegisterWithValue(RegisterID::kARMv8_dbgbvr0_el1,
                                        0xaabbccdd11223344));
  cat.push_back(
      CreateRegisterWithValue(RegisterID::kARMv8_id_aa64dfr0_el1,
                              ARM64_FLAG_MASK(ID_AA64DFR0_EL1, DV) |
                                  ARM64_FLAG_MASK(ID_AA64DFR0_EL1, PMUV) |
                                  ARM64_FLAG_MASK(ID_AA64DFR0_EL1, BRP) |
                                  ARM64_FLAG_MASK(ID_AA64DFR0_EL1, WRP) |
                                  ARM64_FLAG_MASK(ID_AA64DFR0_EL1, PMSV)));
  cat.push_back(CreateRegisterWithValue(
      RegisterID::kARMv8_mdscr_el1,
      ARM64_FLAG_MASK(MDSCR_EL1, SS) | ARM64_FLAG_MASK(MDSCR_EL1, TDCC) |
          ARM64_FLAG_MASK(MDSCR_EL1, MDE) | ARM64_FLAG_MASK(MDSCR_EL1, TXU) |
          ARM64_FLAG_MASK(MDSCR_EL1, RXfull)));

  FormatRegisterOptions options;
  options.arch = debug_ipc::Arch::kArm64;
  options.categories = {RegisterCategory::Type::kDebug};

  FilteredRegisterSet filtered_set;
  Err err = FilterRegisters(options, register_set, &filtered_set);
  ASSERT_FALSE(err.has_error()) << err.msg();

  OutputBuffer out;
  err = FormatRegisters(options, filtered_set, &out);
  ASSERT_FALSE(err.has_error()) << err.msg();

  EXPECT_EQ(
      "Debug Registers\n"
      " kARMv8_dbgbcr0_el1          0x000f2006 E=0, PMC=3, BAS=0, HMC=1, "
      "SSC=0, LBN=15, BT=0\n"
      " kARMv8_dbgbvr0_el1  0xdeadbeefaabbccdd \n"
      "kARMv8_dbgbcr15_el1          0x00f0c1e1 E=1, PMC=0, BAS=15, HMC=0, "
      "SSC=3, LBN=0, BT=15\n"
      " kARMv8_dbgbvr0_el1  0xaabbccdd11223344 \n"
      "    id_aa64dfr0_el1         0xf00f0ff0f DV=15, TV=0, PMUV=15, BRP=16, "
      "WRP=16, CTX_CMP=1, PMSV=15\n"
      "          mdscr_el1          0x44009001 SS=1, TDCC=1, KDE=0, HDE=0, "
      "MDE=1, RAZ/WI=0, TDA=0, INTdis=0, TXU=1, RXO=0, TXfull=0, RXfull=1\n"
      "\n",
      out.AsString());
}

}  // namespace zxdb
