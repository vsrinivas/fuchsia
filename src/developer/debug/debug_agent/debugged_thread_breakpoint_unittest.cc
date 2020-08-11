// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>

#include "src/developer/debug/debug_agent/arch.h"
#include "src/developer/debug/debug_agent/mock_debug_agent_harness.h"
#include "src/developer/debug/debug_agent/mock_exception_handle.h"
#include "src/developer/debug/debug_agent/mock_process.h"
#include "src/developer/debug/debug_agent/mock_thread.h"
#include "src/developer/debug/debug_agent/mock_thread_handle.h"

namespace debug_agent {

TEST(DebuggedThreadBreakpoint, NormalException) {
  MockDebugAgentHarness harness;

  constexpr zx_koid_t kProcKoid = 12;
  MockProcess* process = harness.AddProcess(kProcKoid);
  constexpr zx_koid_t kThreadKoid = 23;
  MockThread* thread = process->AddThread(kThreadKoid);

  // Trigger the exception
  constexpr uint64_t kAddress = 0xdeadbeef;
  thread->SendException(kAddress, debug_ipc::ExceptionType::kPageFault);

  // We should've received an exception notification.
  ASSERT_EQ(harness.stream_backend()->exceptions().size(), 1u);
  EXPECT_EQ(harness.stream_backend()->exceptions()[0].type, debug_ipc::ExceptionType::kPageFault);
  EXPECT_EQ(harness.stream_backend()->exceptions()[0].hit_breakpoints.size(), 0u);

  auto& thread_record = harness.stream_backend()->exceptions()[0].thread;
  EXPECT_EQ(thread_record.process_koid, kProcKoid);
  EXPECT_EQ(thread_record.thread_koid, kThreadKoid);
  EXPECT_EQ(thread_record.state, debug_ipc::ThreadRecord::State::kBlocked);
  EXPECT_EQ(thread_record.blocked_reason, debug_ipc::ThreadRecord::BlockedReason::kException);
  EXPECT_EQ(thread_record.stack_amount, debug_ipc::ThreadRecord::StackAmount::kMinimal);
}

TEST(DebuggedThreadBreakpoint, SoftwareBreakpoint) {
  MockDebugAgentHarness harness;

  constexpr zx_koid_t kProcKoid = 12;
  MockProcess* process = harness.AddProcess(kProcKoid);
  constexpr zx_koid_t kThreadKoid = 23;
  MockThread* thread = process->AddThread(kThreadKoid);

  // Set an exception for a software breakpoint instruction. Since no breakpoint has been installed,
  // this will look like a hardcoded breakpoint instruction.
  constexpr uint64_t kBreakpointAddress = 0xdeadbeef;
  const uint64_t kExceptionAddress =
      kBreakpointAddress + arch::kExceptionOffsetForSoftwareBreakpoint;
  thread->SendException(kExceptionAddress, debug_ipc::ExceptionType::kSoftwareBreakpoint);

  // Validate the exception notification.
  ASSERT_EQ(harness.stream_backend()->exceptions().size(), 1u);
  auto exception = harness.stream_backend()->exceptions()[0];
  EXPECT_EQ(exception.type, debug_ipc::ExceptionType::kSoftwareBreakpoint);
  EXPECT_EQ(exception.hit_breakpoints.size(), 0u);

  // Resume the thread to clear the exception.
  harness.Resume();

  // Provide backing memory for the breakpoint. This is needed for the software breakpoint to be
  // installed. It doesn't matter what the contents is, only that a read will succeed.
  process->mock_process_handle().mock_memory().AddMemory(kBreakpointAddress, {0, 0, 0, 0});

  // Add a breakpoint on that address and throw the same exception as above.
  constexpr uint32_t kBreakpointId = 1;
  harness.AddOrChangeBreakpoint(kBreakpointId, kProcKoid, kBreakpointAddress);
  thread->SendException(kExceptionAddress, debug_ipc::ExceptionType::kSoftwareBreakpoint);

  // Now the exception notification should reference the hit breakpoint.
  ASSERT_EQ(harness.stream_backend()->exceptions().size(), 2u);
  exception = harness.stream_backend()->exceptions()[1];

  EXPECT_EQ(exception.type, debug_ipc::ExceptionType::kSoftwareBreakpoint);
  ASSERT_EQ(exception.hit_breakpoints.size(), 1u);
  EXPECT_EQ(exception.hit_breakpoints[0].id, kBreakpointId);

  // The breakpoint stats should be up-to-date.
  Breakpoint* breakpoint = harness.debug_agent()->GetBreakpoint(kBreakpointId);
  ASSERT_TRUE(breakpoint);
  EXPECT_EQ(1u, breakpoint->stats().hit_count);
}

TEST(DebuggedThreadBreakpoint, HardwareBreakpoint) {
  MockDebugAgentHarness harness;

  constexpr zx_koid_t kProcKoid = 12;
  MockProcess* process = harness.AddProcess(kProcKoid);
  constexpr zx_koid_t kThreadKoid = 23;
  MockThread* thread = process->AddThread(kThreadKoid);

  // Set the exception information the arch provider is going to return.
  constexpr uint64_t kAddress = 0xdeadbeef;

  // Add a breakpoint on that address.
  constexpr uint32_t kBreakpointId = 1;
  harness.AddOrChangeBreakpoint(kBreakpointId, kProcKoid, kAddress,
                                debug_ipc::BreakpointType::kHardware);

  // Trigger an exception.
  thread->SendException(kAddress, debug_ipc::ExceptionType::kHardwareBreakpoint);

  // Validate the exception notification.
  ASSERT_EQ(harness.stream_backend()->exceptions().size(), 1u);
  auto exception = harness.stream_backend()->exceptions()[0];
  EXPECT_EQ(exception.type, debug_ipc::ExceptionType::kHardwareBreakpoint);
  EXPECT_EQ(exception.hit_breakpoints.size(), 1u);
  EXPECT_EQ(exception.hit_breakpoints[0].id, kBreakpointId);

  // The breakpoint stats should be up-to-date.
  Breakpoint* breakpoint = harness.debug_agent()->GetBreakpoint(kBreakpointId);
  ASSERT_TRUE(breakpoint);
  EXPECT_EQ(1u, breakpoint->stats().hit_count);
}

TEST(DebuggedThreadBreakpoint, Watchpoint) {
  MockDebugAgentHarness harness;

  constexpr zx_koid_t kProcKoid = 12;
  MockProcess* process = harness.AddProcess(kProcKoid);
  constexpr zx_koid_t kThreadKoid = 23;
  MockThread* thread = process->AddThread(kThreadKoid);

  // Add a watchpoint.
  const debug_ipc::AddressRange kRange = {0x1000, 0x1008};
  constexpr uint32_t kBreakpointId = 99;
  ASSERT_EQ(ZX_OK, harness.AddOrChangeBreakpoint(kBreakpointId, kProcKoid, kThreadKoid, kRange,
                                                 debug_ipc::BreakpointType::kWrite));

  // Set the exception information in the debug registers to return. This should indicate the
  // watchpoint that was set up and triggered.
  const uint64_t kAddress = kRange.begin();
  DebugRegisters debug_regs;
  auto set_result = debug_regs.SetWatchpoint(debug_ipc::BreakpointType::kWrite, kRange, 4);
  ASSERT_TRUE(set_result);
  debug_regs.SetForHitWatchpoint(set_result->slot);
  thread->mock_thread_handle().SetDebugRegisters(debug_regs);

  // Trigger an exception.
  thread->SendException(kAddress, debug_ipc::ExceptionType::kWatchpoint);

  // Validate the expection information.
  auto exception = harness.stream_backend()->exceptions()[0];
  EXPECT_EQ(exception.type, debug_ipc::ExceptionType::kWatchpoint);
  ASSERT_EQ(exception.hit_breakpoints.size(), 1u);
  EXPECT_EQ(exception.hit_breakpoints[0].id, kBreakpointId);

  // The breakpoint stats should be up-to-date.
  Breakpoint* breakpoint = harness.debug_agent()->GetBreakpoint(kBreakpointId);
  ASSERT_TRUE(breakpoint);
  EXPECT_EQ(1u, breakpoint->stats().hit_count);
}

}  // namespace debug_agent
