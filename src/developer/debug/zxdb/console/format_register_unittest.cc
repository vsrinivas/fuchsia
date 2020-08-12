// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/console/format_register.cc"

#include <lib/syslog/cpp/macros.h>
#include <zircon/hw/debug/arm64.h>

#include <gtest/gtest.h>

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
  FX_DCHECK(length >= val_loop);

  std::vector<uint8_t> data(length);
  // So that we get the number backwards (0x0102...).
  uint8_t base = val_loop;
  for (size_t i = 0; i < val_loop; i++) {
    data[i] = base - i;
  }
  return data;
}

debug_ipc::Register CreateRegister(RegisterID id, size_t length, size_t val_loop) {
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
  reg->data = data;
}

Register CreateRegisterWithValue(RegisterID id, uint64_t value) {
  Register reg(CreateRegister(id, 8, 8));
  SetRegisterValue(&reg, value);
  return reg;
}

void FillGeneralRegisters(std::vector<Register>* out) {
  out->push_back(CreateRegister(RegisterID::kX64_rax, 8, 1));
  out->push_back(CreateRegister(RegisterID::kX64_rcx, 8, 4));
  out->push_back(CreateRegister(RegisterID::kX64_rdx, 8, 8));
  // This one is out-of-order to force testing the sorting.
  out->push_back(CreateRegister(RegisterID::kX64_rbx, 8, 2));
}

void FillFloatingPointRegisters(std::vector<Register>* out) {
  out->push_back(CreateRegister(RegisterID::kX64_st0, 16, 4));
  out->push_back(CreateRegister(RegisterID::kX64_st1, 16, 4));
  // Invalid
  out->push_back(CreateRegister(RegisterID::kX64_st2, 16, 16));
  // Push a valid 16-byte long double value.
  auto& reg = out->back();
  // Clear all (create a 0 value).
  for (size_t i = 0; i < reg.data.size(); i++)
    reg.data[i] = 0;
}

void FillVectorRegisters(std::vector<Register>* out) {
  out->push_back(CreateRegister(RegisterID::kX64_xmm1, 16, 2));
  out->push_back(CreateRegister(RegisterID::kX64_xmm2, 16, 4));
  out->push_back(CreateRegister(RegisterID::kX64_xmm3, 16, 8));
  out->push_back(CreateRegister(RegisterID::kX64_xmm4, 16, 16));
  // This one is out-of-order to force testing the sorting.
  out->push_back(CreateRegister(RegisterID::kX64_xmm0, 16, 1));
}

}  // namespace

TEST(FormatRegisters, GeneralRegisters) {
  std::vector<Register> registers;
  FillGeneralRegisters(&registers);

  FormatRegisterOptions options;
  options.arch = debug_ipc::Arch::kX64;

  // Force rcx to -2 to test negative integer formatting.
  Register& rcx = registers[1];
  EXPECT_EQ(RegisterID::kX64_rcx, rcx.id);
  SetRegisterValue(&rcx, static_cast<uint64_t>(-2));

  EXPECT_EQ(
      "General Purpose Registers\n"
      "  rax                 0x1 = 1\n"
      "  rbx               0x102 = 258\n"
      "  rcx  0xfffffffffffffffe = -2\n"
      "  rdx   0x102030405060708 \n"
      "\n",
      FormatRegisters(options, registers).AsString());
}

TEST(FormatRegisters, VectorRegisters) {
  // Add a zmm register. This should be converted to a ymm register.
  std::vector<Register> registers;

  registers.emplace_back(RegisterID::kX64_zmm0,
                         std::vector<uint8_t>{0xd0, 0x0f, 0x49, 0x40});  // = 3.14159 in float.
  // Pad out to 64 bytes;
  while (registers[0].data.size() < 64)
    registers[0].data.push_back(0);

  FormatRegisterOptions options;
  options.arch = debug_ipc::Arch::kX64;
  options.vector_format = VectorRegisterFormat::kFloat;

  EXPECT_EQ(
      "Vector Registers\n"
      "  Name [7] [6] [5] [4] [3] [2] [1]     [0]\n"
      "  ymm0   0   0   0   0   0   0   0 3.14159\n"
      "    (Use \"get/set vector-format\" to control vector register intepretation.\n"
      "     Currently showing vectors of \"float\".)\n"
      "\n",
      FormatRegisters(options, registers).AsString());

  // Format as 128-bit.
  options.vector_format = VectorRegisterFormat::kUnsigned128;
  EXPECT_EQ(
      "Vector Registers\n"
      "  Name                                [1]                                [0]\n"
      "  ymm0 0x00000000000000000000000000000000 0x00000000000000000000000040490fd0\n"
      "    (Use \"get/set vector-format\" to control vector register intepretation.\n"
      "     Currently showing vectors of \"u128\".)\n"
      "\n",
      FormatRegisters(options, registers).AsString());
}

TEST(FormatRegisters, AllRegisters) {
  std::vector<Register> registers;
  FillGeneralRegisters(&registers);
  FillFloatingPointRegisters(&registers);
  FillVectorRegisters(&registers);

  // Add mxcsr since that appears in a separate category.
  registers.emplace_back(RegisterID::kX64_mxcsr, static_cast<uint64_t>(0x1f80));

  FormatRegisterOptions options;
  options.arch = debug_ipc::Arch::kX64;
  options.vector_format = VectorRegisterFormat::kUnsigned32;

  EXPECT_EQ(
      "General Purpose Registers\n"
      "  rax                0x1 = 1\n"
      "  rbx              0x102 = 258\n"
      "  rcx          0x1020304 \n"
      "  rdx  0x102030405060708 \n"
      "\n"
      "Floating Point Registers\n"
      "  st0  6.163689759657267600e-4944  00000000 00000000 00000000 01020304\n"
      "  st1  6.163689759657267600e-4944  00000000 00000000 00000000 01020304\n"
      "  st2  0.000000000000000000e+00    00000000 00000000 00000000 00000000\n"
      "\n"
      "Vector Registers\n"
      "  mxcsr 0x1f80 = 8064\n"
      "\n"
      "  Name        [3]        [2]        [1]        [0]\n"
      "  xmm0 0x00000000 0x00000000 0x00000000 0x00000001\n"
      "  xmm1 0x00000000 0x00000000 0x00000000 0x00000102\n"
      "  xmm2 0x00000000 0x00000000 0x00000000 0x01020304\n"
      "  xmm3 0x00000000 0x00000000 0x01020304 0x05060708\n"
      "  xmm4 0x01020304 0x05060708 0x090a0b0c 0x0d0e0f10\n"
      "    (Use \"get/set vector-format\" to control vector register intepretation.\n"
      "     Currently showing vectors of \"u32\".)\n"
      "\n",
      FormatRegisters(options, registers).AsString());
}

TEST(FormatRegisters, WithRflags) {
  std::vector<Register> registers;
  FillGeneralRegisters(&registers);
  registers.push_back(CreateRegisterWithValue(RegisterID::kX64_rflags, 0));

  FormatRegisterOptions options;
  options.arch = debug_ipc::Arch::kX64;

  EXPECT_EQ(
      "General Purpose Registers\n"
      "     rax                0x1 = 1\n"
      "     rbx              0x102 = 258\n"
      "     rcx          0x1020304 \n"
      "     rdx  0x102030405060708 \n"
      "  rflags         0x00000000 CF=0, PF=0, AF=0, ZF=0, SF=0, TF=0, IF=0, "
      "DF=0, OF=0\n"
      "\n",
      FormatRegisters(options, registers).AsString());
}

TEST(FormatRegisters, RFlagsValues) {
  std::vector<Register> registers;
  registers.push_back(CreateRegisterWithValue(RegisterID::kX64_rflags, 0));
  SetRegisterValue(&registers.back(), X86_FLAG_MASK(RflagsCF) | X86_FLAG_MASK(RflagsPF) |
                                          X86_FLAG_MASK(RflagsAF) | X86_FLAG_MASK(RflagsZF) |
                                          X86_FLAG_MASK(RflagsTF) | X86_FLAG_MASK(RflagsDF));

  FormatRegisterOptions options;
  options.arch = debug_ipc::Arch::kX64;

  EXPECT_EQ(
      "General Purpose Registers\n"
      "  rflags  0x00000555 CF=1, PF=1, AF=1, ZF=1, SF=0, TF=1, IF=0, DF=1, "
      "OF=0\n"
      "\n",
      FormatRegisters(options, registers).AsString());
}

TEST(FormatRegisters, RFlagsValuesExtended) {
  std::vector<Register> registers;
  registers.push_back(CreateRegisterWithValue(RegisterID::kX64_rflags, 0));
  SetRegisterValue(&registers.back(), X86_FLAG_MASK(RflagsCF) | X86_FLAG_MASK(RflagsPF) |
                                          X86_FLAG_MASK(RflagsAF) | X86_FLAG_MASK(RflagsZF) |
                                          X86_FLAG_MASK(RflagsTF) | X86_FLAG_MASK(RflagsDF) |
                                          // Extended flags
                                          (0b10 << kRflagsIOPLShift) | X86_FLAG_MASK(RflagsNT) |
                                          X86_FLAG_MASK(RflagsVM) | X86_FLAG_MASK(RflagsVIF) |
                                          X86_FLAG_MASK(RflagsID));

  FormatRegisterOptions options;
  options.arch = debug_ipc::Arch::kX64;
  options.extended = true;

  EXPECT_EQ(
      "General Purpose Registers\n"
      "  rflags  0x002a6555 CF=1, PF=1, AF=1, ZF=1, SF=0, TF=1, IF=0, DF=1, "
      "OF=0\n"
      "                     IOPL=2, NT=1, RF=0, VM=1, AC=0, VIF=1, VIP=0, "
      "ID=1\n"
      "\n",
      FormatRegisters(options, registers).AsString());
}

TEST(FormatRegisters, CPSRValues) {
  std::vector<Register> registers;
  registers.push_back(CreateRegisterWithValue(RegisterID::kARMv8_cpsr, 0));
  SetRegisterValue(&registers.back(), ARM64_FLAG_MASK(Cpsr, C) | ARM64_FLAG_MASK(Cpsr, N));

  FormatRegisterOptions options;
  options.arch = debug_ipc::Arch::kArm64;

  EXPECT_EQ(
      "General Purpose Registers\n"
      "  cpsr  0xa0000000 V=0, C=1, Z=0, N=1\n"
      "\n",
      FormatRegisters(options, registers).AsString());

  // Check out extended
  SetRegisterValue(&registers.back(), ARM64_FLAG_MASK(Cpsr, C) | ARM64_FLAG_MASK(Cpsr, N) |
                                          // Extended flags.
                                          ARM64_FLAG_MASK(Cpsr, EL) | ARM64_FLAG_MASK(Cpsr, I) |
                                          ARM64_FLAG_MASK(Cpsr, A) | ARM64_FLAG_MASK(Cpsr, IL) |
                                          ARM64_FLAG_MASK(Cpsr, PAN) | ARM64_FLAG_MASK(Cpsr, UAO));

  options.extended = true;
  EXPECT_EQ(
      "General Purpose Registers\n"
      "  cpsr  0xa0d00181 V=0, C=1, Z=0, N=1\n"
      "                   EL=1, F=0, I=1, A=1, D=0, IL=1, SS=0, PAN=1, UAO=1\n"
      "\n",
      FormatRegisters(options, registers).AsString());
}

TEST(FormatRegisters, DebugRegisters_x86) {
  std::vector<Register> registers;
  registers.push_back(CreateRegisterWithValue(RegisterID::kX64_dr0, 0x1234));
  registers.push_back(CreateRegisterWithValue(RegisterID::kX64_dr1, 0x1234567));
  registers.push_back(CreateRegisterWithValue(RegisterID::kX64_dr2, 0x123456789ab));
  registers.push_back(CreateRegisterWithValue(RegisterID::kX64_dr3, 0x123456789abcdef));

  registers.push_back(CreateRegisterWithValue(RegisterID::kX64_dr6, 0xaffa));
  registers.push_back(CreateRegisterWithValue(RegisterID::kX64_dr7, 0xaaaa26aa));

  FormatRegisterOptions options;
  options.arch = debug_ipc::Arch::kX64;

  EXPECT_EQ(
      "Debug Registers\n"
      "  dr0             0x1234 = 4660\n"
      "  dr1          0x1234567 \n"
      "  dr2      0x123456789ab \n"
      "  dr3  0x123456789abcdef \n"
      "  dr6         0x0000affa B0=0, B1=1, B2=0, B3=1, BD=1, BS=0, BT=1\n"
      "  dr7         0xaaaa26aa L0=0, G0=1, L1=0, G1=1, L2=0, G2=1, L3=0, G4=1, LE=0, GE=1, GD=1\n"
      "                         R/W0=2, LEN0=2, R/W1=2, LEN1=2, R/W2=2, LEN2=2, R/W3=2, LEN3=2\n"
      "\n",
      FormatRegisters(options, registers).AsString());
}

TEST(FormatRegisters, DebugRegisters_arm64) {
  std::vector<Register> registers;
  registers.push_back(CreateRegisterWithValue(
      RegisterID::kARMv8_dbgbcr0_el1,
      ARM64_FLAG_MASK(DBGBCR, PMC) | ARM64_FLAG_MASK(DBGBCR, HMC) | ARM64_FLAG_MASK(DBGBCR, LBN)));
  registers.push_back(CreateRegisterWithValue(
      RegisterID::kARMv8_dbgbcr15_el1, ARM64_FLAG_MASK(DBGBCR, E) | ARM64_FLAG_MASK(DBGBCR, BAS) |
                                           ARM64_FLAG_MASK(DBGBCR, SSC) |
                                           ARM64_FLAG_MASK(DBGBCR, BT)));
  registers.push_back(CreateRegisterWithValue(RegisterID::kARMv8_dbgbvr0_el1, 0xdeadbeefaabbccdd));
  registers.push_back(CreateRegisterWithValue(RegisterID::kARMv8_dbgbvr15_el1, 0xaabbccdd11223344));

  uint32_t value = 0;
  ARM64_DBGWCR_PAC_SET(&value, 0b01);
  ARM64_DBGWCR_BAS_SET(&value, 0xab);
  ARM64_DBGWCR_SSC_SET(&value, 0b11);
  ARM64_DBGWCR_WT_SET(&value, 0b1);
  registers.push_back(CreateRegisterWithValue(RegisterID::kARMv8_dbgwcr2_el1, value));

  value = 0;
  ARM64_DBGWCR_E_SET(&value, 1);
  ARM64_DBGWCR_LSC_SET(&value, 0b11);
  ARM64_DBGWCR_HMC_SET(&value, 1);
  ARM64_DBGWCR_LBN_SET(&value, 0b1011);
  ARM64_DBGWCR_MSK_SET(&value, 0b11011);
  registers.push_back(CreateRegisterWithValue(RegisterID::kARMv8_dbgwcr14_el1, value));

  registers.push_back(CreateRegisterWithValue(
      RegisterID::kARMv8_id_aa64dfr0_el1,
      ARM64_FLAG_MASK(ID_AA64DFR0_EL1, DV) | ARM64_FLAG_MASK(ID_AA64DFR0_EL1, PMUV) |
          ARM64_FLAG_MASK(ID_AA64DFR0_EL1, BRP) | ARM64_FLAG_MASK(ID_AA64DFR0_EL1, WRP) |
          ARM64_FLAG_MASK(ID_AA64DFR0_EL1, PMSV)));
  registers.push_back(CreateRegisterWithValue(
      RegisterID::kARMv8_mdscr_el1,
      ARM64_FLAG_MASK(MDSCR_EL1, SS) | ARM64_FLAG_MASK(MDSCR_EL1, TDCC) |
          ARM64_FLAG_MASK(MDSCR_EL1, MDE) | ARM64_FLAG_MASK(MDSCR_EL1, TXU) |
          ARM64_FLAG_MASK(MDSCR_EL1, RXfull)));

  registers.push_back(CreateRegisterWithValue(RegisterID::kARMv8_dbgwvr2_el1, 0x9988776655443322));
  registers.push_back(CreateRegisterWithValue(RegisterID::kARMv8_dbgwvr14_el1, 0x1133557799aaccee));

  FormatRegisterOptions options;
  options.arch = debug_ipc::Arch::kArm64;

  // clang-format off
  EXPECT_EQ(
      "Debug Registers\n"
      "  id_aa64dfr0         0xf00f0ff0f DV=15, TV=0, PMUV=15, BRP=16, WRP=16, CTX_CMP=1, PMSV=15\n"
      "        mdscr          0x44009001 SS=1, TDCC=1, KDE=0, HDE=0, MDE=1, RAZ/WI=0, TDA=0, INTdis=0, TXU=1, RXO=0, TXfull=0, RXfull=1\n"
      "      dbgbcr0          0x000f2006 E=0, PMC=3, BAS=0, HMC=1, SSC=0, LBN=15, BT=0\n"
      "     dbgbcr15          0x00f0c1e1 E=1, PMC=0, BAS=15, HMC=0, SSC=3, LBN=0, BT=15\n"
      "      dbgbvr0  0xdeadbeefaabbccdd \n"
      "     dbgbvr15  0xaabbccdd11223344 \n"
      "      dbgwcr2          0x0010d562 E=0, PAC=1, LSC=0, BAS=0xab, HMC=0, SSC=3, LBN=0, WT=1, MASK=0x0\n"
      "     dbgwcr14          0x1b0b2019 E=1, PAC=0, LSC=3, BAS=0x0, HMC=1, SSC=0, LBN=11, WT=0, MASK=0x1b\n"
      "      dbgwvr2  0x9988776655443322 \n"
      "     dbgwvr14  0x1133557799aaccee \n"
      "\n",
      FormatRegisters(options, registers).AsString());
  // clang-format on
}

}  // namespace zxdb
