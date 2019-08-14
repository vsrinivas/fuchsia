// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/syscalls/exception.h>

#include <gtest/gtest.h>

#include "src/developer/debug/ipc/decode_exception.h"
#include "src/developer/debug/shared/arch_x86.h"

namespace debug_ipc {
namespace {

class ArmTestInfo : public Arm64ExceptionInfo {
 public:
  uint32_t esr = 0;

  std::optional<uint32_t> FetchESR() override { return esr; }
};

class X64TestInfo : public X64ExceptionInfo {
 public:
  X64ExceptionInfo::DebugRegs regs;
  uint64_t watchpoint = 0;

  bool AddrIsWatchpoint(uint64_t addr) override { return addr == watchpoint; }
  std::optional<X64ExceptionInfo::DebugRegs> FetchDebugRegs() override { return regs; }
};

}  // namespace

TEST(DecodeException, Arm64) {
  ArmTestInfo info;

  // Exceptions that require no decoding.
  EXPECT_EQ(NotifyException::Type::kSoftware, DecodeException(ZX_EXCP_SW_BREAKPOINT, &info));
  EXPECT_EQ(NotifyException::Type::kGeneral, DecodeException(ZX_EXCP_GENERAL, &info));
  EXPECT_EQ(NotifyException::Type::kGeneral, DecodeException(ZX_EXCP_FATAL_PAGE_FAULT, &info));
  EXPECT_EQ(NotifyException::Type::kGeneral, DecodeException(ZX_EXCP_UNDEFINED_INSTRUCTION, &info));
  EXPECT_EQ(NotifyException::Type::kGeneral, DecodeException(ZX_EXCP_UNALIGNED_ACCESS, &info));
  EXPECT_EQ(NotifyException::Type::kGeneral, DecodeException(ZX_EXCP_THREAD_STARTING, &info));
  EXPECT_EQ(NotifyException::Type::kGeneral, DecodeException(ZX_EXCP_THREAD_EXITING, &info));
  EXPECT_EQ(NotifyException::Type::kGeneral, DecodeException(ZX_EXCP_POLICY_ERROR, &info));
  EXPECT_EQ(NotifyException::Type::kGeneral, DecodeException(ZX_EXCP_PROCESS_STARTING, &info));

  // Hardware breakpoints. The meaty stuff.
  info.esr = 0b110000 << 26;
  EXPECT_EQ(NotifyException::Type::kHardware, DecodeException(ZX_EXCP_HW_BREAKPOINT, &info));
  info.esr = 0b110001 << 26;
  EXPECT_EQ(NotifyException::Type::kHardware, DecodeException(ZX_EXCP_HW_BREAKPOINT, &info));

  info.esr = 0b110010 << 26;
  EXPECT_EQ(NotifyException::Type::kSingleStep, DecodeException(ZX_EXCP_HW_BREAKPOINT, &info));
  info.esr = 0b110011 << 26;
  EXPECT_EQ(NotifyException::Type::kSingleStep, DecodeException(ZX_EXCP_HW_BREAKPOINT, &info));
}

TEST(DecodeException, X64) {
  X64TestInfo info;

  // Exceptions that require no decoding.
  EXPECT_EQ(NotifyException::Type::kSoftware, DecodeException(ZX_EXCP_SW_BREAKPOINT, &info));
  EXPECT_EQ(NotifyException::Type::kGeneral, DecodeException(ZX_EXCP_GENERAL, &info));
  EXPECT_EQ(NotifyException::Type::kGeneral, DecodeException(ZX_EXCP_FATAL_PAGE_FAULT, &info));
  EXPECT_EQ(NotifyException::Type::kGeneral, DecodeException(ZX_EXCP_UNDEFINED_INSTRUCTION, &info));
  EXPECT_EQ(NotifyException::Type::kGeneral, DecodeException(ZX_EXCP_UNALIGNED_ACCESS, &info));
  EXPECT_EQ(NotifyException::Type::kGeneral, DecodeException(ZX_EXCP_THREAD_STARTING, &info));
  EXPECT_EQ(NotifyException::Type::kGeneral, DecodeException(ZX_EXCP_THREAD_EXITING, &info));
  EXPECT_EQ(NotifyException::Type::kGeneral, DecodeException(ZX_EXCP_POLICY_ERROR, &info));
  EXPECT_EQ(NotifyException::Type::kGeneral, DecodeException(ZX_EXCP_PROCESS_STARTING, &info));

  // Hardware breakpoints. The meaty stuff.
  info.regs.dr0 = 0x1111111111111111;
  info.regs.dr1 = 0x2222222222222222;
  info.regs.dr2 = 0x3333333333333333;
  info.regs.dr3 = 0x4444444444444444;

  info.regs.dr6 = X86_FLAG_MASK(DR6B0);
  EXPECT_EQ(NotifyException::Type::kHardware, DecodeException(ZX_EXCP_HW_BREAKPOINT, &info));
  info.watchpoint = info.regs.dr0;
  EXPECT_EQ(NotifyException::Type::kWatchpoint, DecodeException(ZX_EXCP_HW_BREAKPOINT, &info));

  info.regs.dr6 = X86_FLAG_MASK(DR6B1);
  EXPECT_EQ(NotifyException::Type::kHardware, DecodeException(ZX_EXCP_HW_BREAKPOINT, &info));
  info.watchpoint = info.regs.dr1;
  EXPECT_EQ(NotifyException::Type::kWatchpoint, DecodeException(ZX_EXCP_HW_BREAKPOINT, &info));

  info.regs.dr6 = X86_FLAG_MASK(DR6B2);
  EXPECT_EQ(NotifyException::Type::kHardware, DecodeException(ZX_EXCP_HW_BREAKPOINT, &info));
  info.watchpoint = info.regs.dr2;
  EXPECT_EQ(NotifyException::Type::kWatchpoint, DecodeException(ZX_EXCP_HW_BREAKPOINT, &info));

  info.regs.dr6 = X86_FLAG_MASK(DR6B3);
  EXPECT_EQ(NotifyException::Type::kHardware, DecodeException(ZX_EXCP_HW_BREAKPOINT, &info));
  info.watchpoint = info.regs.dr3;
  EXPECT_EQ(NotifyException::Type::kWatchpoint, DecodeException(ZX_EXCP_HW_BREAKPOINT, &info));

  info.watchpoint = 0;
  info.regs.dr6 = X86_FLAG_MASK(DR6BS);
  EXPECT_EQ(NotifyException::Type::kSingleStep, DecodeException(ZX_EXCP_HW_BREAKPOINT, &info));
}

}  // namespace debug_ipc
