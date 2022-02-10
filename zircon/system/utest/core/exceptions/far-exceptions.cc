// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/arch/arm64/system.h>
#include <lib/elf-psabi/sp.h>
#include <lib/zircon-internal/default_stack_size.h>
#include <lib/zx/exception.h>
#include <lib/zx/process.h>
#include <lib/zx/thread.h>
#include <zircon/syscalls/exception.h>

#include <test-utils/test-utils.h>
#include <zxtest/zxtest.h>

namespace {

#if defined(__aarch64__)

constexpr size_t kThreadStackSize = ZIRCON_DEFAULT_STACK_SIZE;
constexpr std::string_view kThreadName = "Crash thread";

void CatchCrash(uintptr_t pc, uintptr_t sp, uintptr_t arg1, zx_exception_report_t& report,
                zx_thread_state_general_regs_t& general_regs) {
  zx::thread crash_thread;
  ASSERT_OK(zx::thread::create(*zx::process::self(), kThreadName.data(), kThreadName.size(), 0,
                               &crash_thread));

  // Set up to receive thread exceptions for the new thread.
  zx::channel exception_channel;
  ASSERT_OK(crash_thread.create_exception_channel(0, &exception_channel));

  // Start it running with a stack and PC at the crash function's entry point.
  ASSERT_OK(crash_thread.start(pc, sp, arg1, 0));

  // Wait for the exception channel to be readable. This will happen when
  // thread crashes and triggers the exception.
  tu_channel_wait_readable(exception_channel.get());

  // Read the exception message.
  zx::exception exc;
  zx_exception_info_t exc_info;
  uint32_t nbytes, nhandles;
  ASSERT_OK(exception_channel.read(0, &exc_info, exc.reset_and_get_address(), sizeof(exc_info), 1,
                                   &nbytes, &nhandles));
  ASSERT_EQ(sizeof(exc_info), nbytes);
  ASSERT_EQ(1, nhandles);

  // Get the FAR from the exception report.
  ASSERT_OK(crash_thread.get_info(ZX_INFO_THREAD_EXCEPTION_REPORT, &report, sizeof(report), nullptr,
                                  nullptr));

  // Get general thread registers.
  ASSERT_OK(
      crash_thread.read_state(ZX_THREAD_STATE_GENERAL_REGS, &general_regs, sizeof(general_regs)));

  // When the exception handle is closed (by the zx::exception destructor at
  // the end of the function), the thread will resume from the exception.  Set
  // it up to "resume" by doing an immediate thread exit.  This should make it
  // safe to assume its stack will never be used again from here on out.  (The
  // stack will also be freed by a destructor at the end of the function.)
  constexpr uint32_t kExceptionState = ZX_EXCEPTION_STATE_THREAD_EXIT;
  ASSERT_OK(exc.set_property(ZX_PROP_EXCEPTION_STATE, &kExceptionState, sizeof(kExceptionState)));
}

#ifdef __clang__
[[clang::no_sanitize("all")]]
#endif
[[noreturn]] void
DoNothing() {
  zx_thread_exit();
}

arch::ArmExceptionSyndromeRegister::ExceptionClass GetEC(uint64_t esr) {
  return arch::ArmExceptionSyndromeRegister::Get().FromValue(esr).ec();
}

TEST(ExceptionsTest, PCAlignmentFault) {
  uintptr_t unaligned_pc = reinterpret_cast<uintptr_t>(DoNothing) + 1;
  std::unique_ptr<std::byte[]> thread_stack = std::make_unique<std::byte[]>(kThreadStackSize);
  uintptr_t sp = compute_initial_stack_pointer(reinterpret_cast<uintptr_t>(thread_stack.get()),
                                               kThreadStackSize);
  zx_exception_report_t report = {};
  zx_thread_state_general_regs_t general_regs = {};

  ASSERT_NO_FATAL_FAILURE(CatchCrash(unaligned_pc, sp, /*arg1=*/0, report, general_regs));
  EXPECT_EQ(report.header.type, ZX_EXCP_UNALIGNED_ACCESS);
  EXPECT_EQ(GetEC(report.context.arch.u.arm_64.esr),
            arch::ArmExceptionSyndromeRegister::ExceptionClass::kPcAlignment);
  EXPECT_EQ(report.context.arch.u.arm_64.far, unaligned_pc);
}

// Making it global static ensures this is in rodata.
static constexpr uint32_t kUdf0 = 0;

TEST(ExceptionsTest, InstructionAbort) {
  // Trigger an instruction abort by attempting to execute instructions on a page
  // without executable permissions. This produces a 4-byte aligned undefined
  // instruction set.
  uintptr_t pc = static_cast<uintptr_t>(kUdf0);
  std::unique_ptr<std::byte[]> thread_stack = std::make_unique<std::byte[]>(kThreadStackSize);
  uintptr_t sp = compute_initial_stack_pointer(reinterpret_cast<uintptr_t>(thread_stack.get()),
                                               kThreadStackSize);
  zx_exception_report_t report = {};
  zx_thread_state_general_regs_t general_regs = {};

  ASSERT_NO_FATAL_FAILURE(CatchCrash(pc, sp, /*arg1=*/0, report, general_regs));
  EXPECT_EQ(report.header.type, ZX_EXCP_FATAL_PAGE_FAULT);
  ASSERT_EQ(GetEC(report.context.arch.u.arm_64.esr),
            arch::ArmExceptionSyndromeRegister::ExceptionClass::kInstructionAbortLowerEl);
  ASSERT_EQ(report.context.arch.u.arm_64.far, pc);
}

#ifdef __clang__
[[clang::no_sanitize("all")]]
#endif
[[noreturn]] void
BadAccess(uintptr_t arg1) {
  *(reinterpret_cast<uint8_t*>(arg1)) = 1;
  zx_thread_exit();
}

TEST(ExceptionsTest, DataAbort) {
  uintptr_t pc = reinterpret_cast<uintptr_t>(BadAccess);
  std::unique_ptr<std::byte[]> thread_stack = std::make_unique<std::byte[]>(kThreadStackSize);
  uintptr_t sp = compute_initial_stack_pointer(reinterpret_cast<uintptr_t>(thread_stack.get()),
                                               kThreadStackSize);
  zx_exception_report_t report = {};
  zx_thread_state_general_regs_t general_regs = {};

  constexpr uintptr_t kJunkPtr = 1;
  ASSERT_NO_FATAL_FAILURE(CatchCrash(pc, sp, /*arg1=*/kJunkPtr, report, general_regs));
  EXPECT_EQ(report.header.type, ZX_EXCP_FATAL_PAGE_FAULT);
  EXPECT_EQ(GetEC(report.context.arch.u.arm_64.esr),
            arch::ArmExceptionSyndromeRegister::ExceptionClass::kDataAbortLowerEl);
  EXPECT_EQ(report.context.arch.u.arm_64.far, kJunkPtr);
}

TEST(ExceptionsTest, SPMisalignment) {
  // For stack pointer misalignment, one might expect for the exception report
  // FAR to include this address. However on aarch64, the FAR is not explicitly
  // set for SP misalignment. Users can instead decode the ESR value to see if
  // whether the FAR or SP contains the faulty address. This is an example test
  // for showing correct usage.
  uintptr_t pc = reinterpret_cast<uintptr_t>(DoNothing);
  std::unique_ptr<std::byte[]> thread_stack = std::make_unique<std::byte[]>(kThreadStackSize);
  uintptr_t sp = compute_initial_stack_pointer(reinterpret_cast<uintptr_t>(thread_stack.get()),
                                               kThreadStackSize);
  --sp;
  zx_exception_report_t report = {};
  zx_thread_state_general_regs_t general_regs = {};

  ASSERT_NO_FATAL_FAILURE(CatchCrash(pc, sp, /*arg1=*/0, report, general_regs));
  EXPECT_EQ(report.header.type, ZX_EXCP_GENERAL);
  EXPECT_EQ(GetEC(report.context.arch.u.arm_64.esr),
            arch::ArmExceptionSyndromeRegister::ExceptionClass::kSpAlignment);
  EXPECT_EQ(report.context.arch.u.arm_64.far, 0, "FAR is not set on SP misalignment");
  EXPECT_EQ(general_regs.sp, sp, "SP holds the faulty address");
}

#endif  // defined(__aarch64__)

}  // namespace
