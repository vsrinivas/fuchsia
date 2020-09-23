// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "src/developer/debug/debug_agent/software_breakpoint.h"

#include <string.h>
#include <zircon/syscalls/exception.h>

#include <gtest/gtest.h>

#include "src/developer/debug/debug_agent/arch.h"
#include "src/developer/debug/debug_agent/breakpoint.h"
#include "src/developer/debug/debug_agent/debugged_thread.h"
#include "src/developer/debug/debug_agent/mock_process.h"
#include "src/developer/debug/debug_agent/mock_thread.h"
#include "src/developer/debug/shared/logging/debug.h"

namespace debug_agent {

namespace {

// A no-op process delegate.
class TestProcessDelegate : public Breakpoint::ProcessDelegate {
 public:
  TestProcessDelegate() = default;

  std::map<uint64_t, std::unique_ptr<ProcessBreakpoint>>& bps() { return bps_; }

  void InjectMockProcess(std::unique_ptr<MockProcess> proc) {
    procs_[proc->koid()] = std::move(proc);
  }

  // This only gets called if Breakpoint.SetSettings() is called.
  zx_status_t RegisterBreakpoint(Breakpoint* bp, zx_koid_t koid, uint64_t address) override {
    auto found = bps_.find(address);
    if (found == bps_.end()) {
      auto pbp = std::make_unique<SoftwareBreakpoint>(bp, procs_[koid].get(), address);

      zx_status_t status = pbp->Init();
      if (status != ZX_OK) {
        fprintf(stderr, "Failure initializing %d\n", (int)status);
        return status;
      }

      bps_[address] = std::move(pbp);
    } else {
      found->second->RegisterBreakpoint(bp);
    }
    return ZX_OK;
  }
  void UnregisterBreakpoint(Breakpoint* bp, zx_koid_t, uint64_t address) override {
    auto found = bps_.find(address);
    if (found == bps_.end())
      GTEST_FAIL();

    bool still_used = found->second->UnregisterBreakpoint(bp);
    if (!still_used)
      bps_.erase(found);
  }

 private:
  std::map<uint64_t, std::unique_ptr<ProcessBreakpoint>> bps_;
  std::map<zx_koid_t, std::unique_ptr<MockProcess>> procs_;
};

constexpr uintptr_t kAddress = 0x1234;
constexpr size_t kDataSize = 4u;
const uint8_t kOriginalData[kDataSize] = {0x01, 0x02, 0x03, 0x04};

std::vector<uint8_t> GetOriginalData() {
  return std::vector<uint8_t>(std::begin(kOriginalData), std::end(kOriginalData));
}

// Returns the memory data buffer with the beginning overwritten by a software breakpoint.
std::vector<uint8_t> GetOriginalDataWithBreakpoint() {
  auto result = GetOriginalData();
  memcpy(&result[0], &arch::kBreakInstruction, sizeof(arch::kBreakInstruction));
  return result;
}

// Writes the original data or breakpoint data at |kAddress| to the mock data backing the given
// process handle.
void LoadOriginalMemory(MockProcessHandle& handle) {
  handle.mock_memory().AddMemory(kAddress, GetOriginalData());
}

bool MemoryContains(MockProcessHandle& handle, uint64_t address, const void* data,
                    size_t data_len) {
  if (data_len == 0)
    return true;

  std::unique_ptr<uint8_t[]> read(new uint8_t[data_len]);
  size_t actual = 0;
  if (handle.ReadMemory(address, read.get(), data_len, &actual) != ZX_OK || actual != data_len)
    return false;

  return memcmp(data, &read[0], data_len) == 0;
}

bool MemoryContainsBreak(MockProcessHandle& handle, uint64_t address) {
  return MemoryContains(handle, address, &arch::kBreakInstruction, sizeof(arch::kBreakInstruction));
}
bool MemoryContainsOriginal(MockProcessHandle& handle, uint64_t address) {
  return MemoryContains(handle, address, kOriginalData, sizeof(arch::kBreakInstruction));
}

template <typename T>
std::string ToString(const std::vector<T>& v) {
  std::stringstream ss;
  for (size_t i = 0; i < v.size(); i++) {
    if (i > 0)
      ss << ", ";
    ss << v[i];
  }

  return ss.str();
}

template <typename T>
void CheckVectorContainsElements(const debug_ipc::FileLineFunction& location,
                                 const std::vector<T>& got, const std::vector<T>& expected) {
  ASSERT_EQ(expected.size(), got.size())
      << location.ToString() << ": "
      << "Expected (" << ToString(expected) << "), Got (" << ToString(got) << ").";

  for (size_t i = 0; i < expected.size(); i++) {
    ASSERT_TRUE(expected[i] == got[i])
        << location.ToString() << ": "
        << "Expected (" << ToString(expected) << "), Got (" << ToString(got) << ").";
  }
}

}  // namespace

TEST(ProcessBreakpoint, InstallAndFixup) {
  TestProcessDelegate process_delegate;

  Breakpoint main_breakpoint(&process_delegate);
  debug_ipc::BreakpointSettings settings;
  settings.type = debug_ipc::BreakpointType::kSoftware;
  main_breakpoint.SetSettings(settings);

  zx_koid_t process_koid = 0x1234;
  const std::string process_name = "process";
  MockProcess process(nullptr, process_koid, process_name);

  LoadOriginalMemory(process.mock_process_handle());

  SoftwareBreakpoint bp(&main_breakpoint, &process, kAddress);
  ASSERT_EQ(ZX_OK, bp.Init());

  // Should have written the breakpoint instruction.
  EXPECT_TRUE(MemoryContainsBreak(process.mock_process_handle(), kAddress));

  // Make a memory block that contains the address set as the breakpoint. Offset it by kBlockOffset
  // to make sure non-aligned cases are handled.
  debug_ipc::MemoryBlock block;
  constexpr size_t kBlockOffset = 4;
  block.address = kAddress - kBlockOffset;
  block.valid = true;
  block.size = 16;
  block.data.resize(block.size);

  // Fill with memory contents reflecting the breakpoint instruction.
  auto with_bp = GetOriginalDataWithBreakpoint();
  memcpy(&block.data[kBlockOffset], &with_bp[0], with_bp.size());

  // FixupMemoryBlock should give back the original data.
  bp.FixupMemoryBlock(&block);
  EXPECT_TRUE(
      std::equal(&block.data[kBlockOffset], &block.data[kBlockOffset + kDataSize], kOriginalData));
}

TEST(ProcessBreakpoint, StepSingle) {
  TestProcessDelegate process_delegate;

  Breakpoint main_breakpoint(&process_delegate);
  debug_ipc::BreakpointSettings settings;
  settings.type = debug_ipc::BreakpointType::kSoftware;
  main_breakpoint.SetSettings(settings);

  constexpr zx_koid_t process_koid = 0x1234;
  const std::string process_name = "process";
  MockProcess process(nullptr, process_koid, process_name);

  process.mock_process_handle().mock_memory().AddMemory(kAddress, GetOriginalData());

  // The step over strategy is as follows:
  //  - Thread 1, 2, 3 will hit the breakpoint and attempt a step over.
  //  - Thread 4 will remain oblivious to the breakpoint, as will 5.
  //  - Thread 5 is suspended from the client, so it should not be resumed by the agent during
  //    step over.

  constexpr zx_koid_t kThread1Koid = 1;
  constexpr zx_koid_t kThread2Koid = 2;
  constexpr zx_koid_t kThread3Koid = 3;
  constexpr zx_koid_t kThread4Koid = 4;
  constexpr zx_koid_t kThread5Koid = 5;
  MockThread* mock_thread1 = process.AddThread(kThread1Koid);
  MockThread* mock_thread2 = process.AddThread(kThread2Koid);
  MockThread* mock_thread3 = process.AddThread(kThread3Koid);
  MockThread* mock_thread4 = process.AddThread(kThread4Koid);
  MockThread* mock_thread5 = process.AddThread(kThread5Koid);

  mock_thread5->ClientSuspend();

  SoftwareBreakpoint bp(&main_breakpoint, &process, kAddress);
  ASSERT_EQ(ZX_OK, bp.Init());

  // The breakpoint should be installed.
  EXPECT_TRUE(MemoryContainsBreak(process.mock_process_handle(), kAddress));

  // Thread 1 hits breakpoint ----------------------------------------------------------------------

  bp.BeginStepOver(mock_thread1);

  // Breakpoint should be removed.
  EXPECT_TRUE(MemoryContainsOriginal(process.mock_process_handle(), kAddress));

  // There should be one enqueued step over.
  ASSERT_EQ(process.step_over_queue().size(), 1u);
  ASSERT_EQ(process.step_over_queue()[0].process_breakpoint.get(), &bp);
  ASSERT_EQ(process.step_over_queue()[0].thread.get(), mock_thread1);

  // Only thread 1 should be still running. The rest should be suspended.
  EXPECT_TRUE(mock_thread1->running());
  EXPECT_TRUE(mock_thread2->mock_thread_handle().is_suspended());
  EXPECT_TRUE(mock_thread3->mock_thread_handle().is_suspended());
  EXPECT_TRUE(mock_thread4->mock_thread_handle().is_suspended());
  EXPECT_TRUE(mock_thread5->mock_thread_handle().is_suspended());

  // Only thread 1 should be stepping over.
  EXPECT_TRUE(mock_thread1->stepping_over_breakpoint());
  EXPECT_FALSE(mock_thread2->stepping_over_breakpoint());
  EXPECT_FALSE(mock_thread3->stepping_over_breakpoint());
  EXPECT_FALSE(mock_thread4->stepping_over_breakpoint());
  EXPECT_FALSE(mock_thread5->stepping_over_breakpoint());

  CheckVectorContainsElements(FROM_HERE, bp.CurrentlySuspendedThreads(),
                              {kThread2Koid, kThread3Koid, kThread4Koid, kThread5Koid});

  // Thread 2 hits breakpoint ----------------------------------------------------------------------

  // Now the second thread gets the exception.
  bp.BeginStepOver(mock_thread2);

  // There should be 2 enqueued step overs.
  ASSERT_EQ(process.step_over_queue().size(), 2u);
  ASSERT_EQ(process.step_over_queue()[1].process_breakpoint.get(), &bp);
  ASSERT_EQ(process.step_over_queue()[1].thread.get(), mock_thread2);

  // Only thread 1 should be running.
  EXPECT_TRUE(mock_thread1->running());
  EXPECT_TRUE(mock_thread2->mock_thread_handle().is_suspended());
  EXPECT_TRUE(mock_thread3->mock_thread_handle().is_suspended());
  EXPECT_TRUE(mock_thread4->mock_thread_handle().is_suspended());
  EXPECT_TRUE(mock_thread5->mock_thread_handle().is_suspended());

  // Only thread 1 should be stepping over.
  EXPECT_TRUE(mock_thread1->stepping_over_breakpoint());
  EXPECT_FALSE(mock_thread2->stepping_over_breakpoint());
  EXPECT_FALSE(mock_thread3->stepping_over_breakpoint());
  EXPECT_FALSE(mock_thread4->stepping_over_breakpoint());
  EXPECT_FALSE(mock_thread5->stepping_over_breakpoint());

  CheckVectorContainsElements(FROM_HERE, bp.CurrentlySuspendedThreads(),
                              {kThread2Koid, kThread3Koid, kThread4Koid, kThread5Koid});

  // Thread 3 hits breakpoint ----------------------------------------------------------------------

  bp.BeginStepOver(mock_thread3);

  // There should be 2 enqueued step overs.
  ASSERT_EQ(process.step_over_queue().size(), 3u);
  ASSERT_EQ(process.step_over_queue()[2].process_breakpoint.get(), &bp);
  ASSERT_EQ(process.step_over_queue()[2].thread.get(), mock_thread3);

  // Only thread 1 should be running.
  EXPECT_TRUE(mock_thread1->running());
  EXPECT_TRUE(mock_thread2->mock_thread_handle().is_suspended());
  EXPECT_TRUE(mock_thread3->mock_thread_handle().is_suspended());
  EXPECT_TRUE(mock_thread4->mock_thread_handle().is_suspended());
  EXPECT_TRUE(mock_thread5->mock_thread_handle().is_suspended());

  // Only thread 1 should be stepping over right now.
  EXPECT_TRUE(mock_thread1->stepping_over_breakpoint());
  EXPECT_FALSE(mock_thread2->stepping_over_breakpoint());
  EXPECT_FALSE(mock_thread3->stepping_over_breakpoint());
  EXPECT_FALSE(mock_thread4->stepping_over_breakpoint());
  EXPECT_FALSE(mock_thread5->stepping_over_breakpoint());

  CheckVectorContainsElements(FROM_HERE, bp.CurrentlySuspendedThreads(),
                              {kThread2Koid, kThread3Koid, kThread4Koid, kThread5Koid});

  // The memory should be original.
  EXPECT_TRUE(MemoryContainsOriginal(process.mock_process_handle(), kAddress));

  // Thread 1 steps over ---------------------------------------------------------------------------

  bp.EndStepOver(mock_thread1);

  // There should be 2 enqueued step overs.
  ASSERT_EQ(process.step_over_queue().size(), 2u);
  ASSERT_EQ(process.step_over_queue()[0].process_breakpoint.get(), &bp);
  ASSERT_EQ(process.step_over_queue()[0].thread.get(), mock_thread2);
  ASSERT_EQ(process.step_over_queue()[1].process_breakpoint.get(), &bp);
  ASSERT_EQ(process.step_over_queue()[1].thread.get(), mock_thread3);

  // Only thread 2 should be running.
  EXPECT_TRUE(mock_thread1->mock_thread_handle().is_suspended());
  EXPECT_TRUE(mock_thread2->running());
  EXPECT_TRUE(mock_thread3->mock_thread_handle().is_suspended());
  EXPECT_TRUE(mock_thread4->mock_thread_handle().is_suspended());
  EXPECT_TRUE(mock_thread5->mock_thread_handle().is_suspended());

  // Only thread 2 should be stepping over right now.
  EXPECT_FALSE(mock_thread1->stepping_over_breakpoint());
  EXPECT_TRUE(mock_thread2->stepping_over_breakpoint());
  EXPECT_FALSE(mock_thread3->stepping_over_breakpoint());
  EXPECT_FALSE(mock_thread4->stepping_over_breakpoint());
  EXPECT_FALSE(mock_thread5->stepping_over_breakpoint());

  CheckVectorContainsElements(FROM_HERE, bp.CurrentlySuspendedThreads(),
                              {kThread1Koid, kThread3Koid, kThread4Koid, kThread5Koid});

  // The memory should be original.
  EXPECT_TRUE(MemoryContainsOriginal(process.mock_process_handle(), kAddress));

  // Thread 2 steps over ---------------------------------------------------------------------------

  bp.EndStepOver(mock_thread2);

  // There should be 1 enqueued step overs.
  ASSERT_EQ(process.step_over_queue().size(), 1u);
  ASSERT_EQ(process.step_over_queue()[0].process_breakpoint.get(), &bp);
  ASSERT_EQ(process.step_over_queue()[0].thread.get(), mock_thread3);

  // Only thread 3 should be running, as it's the only one stepping over.
  EXPECT_TRUE(mock_thread1->mock_thread_handle().is_suspended());
  EXPECT_TRUE(mock_thread2->mock_thread_handle().is_suspended());
  EXPECT_TRUE(mock_thread3->running());
  EXPECT_TRUE(mock_thread4->mock_thread_handle().is_suspended());
  EXPECT_TRUE(mock_thread5->mock_thread_handle().is_suspended());

  // Only thread 3 should be stepping over right now.
  EXPECT_FALSE(mock_thread1->stepping_over_breakpoint());
  EXPECT_FALSE(mock_thread2->stepping_over_breakpoint());
  EXPECT_TRUE(mock_thread3->stepping_over_breakpoint());
  EXPECT_FALSE(mock_thread4->stepping_over_breakpoint());
  EXPECT_FALSE(mock_thread5->stepping_over_breakpoint());

  CheckVectorContainsElements(FROM_HERE, bp.CurrentlySuspendedThreads(),
                              {kThread1Koid, kThread2Koid, kThread4Koid, kThread5Koid});

  // Breakpoint remains removed.
  EXPECT_TRUE(MemoryContainsOriginal(process.mock_process_handle(), kAddress));

  // Thread 3 steps over ---------------------------------------------------------------------------

  bp.EndStepOver(mock_thread3);

  // No more enqueued elements.
  ASSERT_EQ(process.step_over_queue().size(), 0u);

  // All threads should be resumed except 5, which was paused by the client.
  EXPECT_TRUE(mock_thread1->running());
  EXPECT_TRUE(mock_thread2->running());
  EXPECT_TRUE(mock_thread3->running());
  EXPECT_TRUE(mock_thread4->running());
  EXPECT_TRUE(mock_thread5->mock_thread_handle().is_suspended());

  // No thread should be stepping over.
  EXPECT_FALSE(mock_thread1->stepping_over_breakpoint());
  EXPECT_FALSE(mock_thread2->stepping_over_breakpoint());
  EXPECT_FALSE(mock_thread3->stepping_over_breakpoint());
  EXPECT_FALSE(mock_thread4->stepping_over_breakpoint());
  EXPECT_FALSE(mock_thread5->stepping_over_breakpoint());

  EXPECT_TRUE(bp.CurrentlySuspendedThreads().empty());

  // Breakpoint is set again.
  EXPECT_TRUE(MemoryContainsBreak(process.mock_process_handle(), kAddress));
}

TEST(ProcessBreakpoint, MultipleBreakpoints) {
  TestProcessDelegate process_delegate1;
  TestProcessDelegate process_delegate2;
  TestProcessDelegate process_delegate3;

  Breakpoint main_breakpoint1(&process_delegate1);
  Breakpoint main_breakpoint2(&process_delegate2);
  Breakpoint main_breakpoint3(&process_delegate3);

  debug_ipc::BreakpointSettings settings;
  settings.type = debug_ipc::BreakpointType::kSoftware;
  main_breakpoint1.SetSettings(settings);
  main_breakpoint2.SetSettings(settings);
  main_breakpoint3.SetSettings(settings);

  constexpr zx_koid_t process_koid = 0x1234;
  const std::string process_name = "process";
  MockProcess process(nullptr, process_koid, process_name);

  constexpr uint64_t kAddress1 = kAddress;
  constexpr uint64_t kAddress2 = kAddress + 0x100;
  constexpr uint64_t kAddress3 = kAddress + 0x200;
  process.mock_process_handle().mock_memory().AddMemory(kAddress1, GetOriginalData());
  process.mock_process_handle().mock_memory().AddMemory(kAddress2, GetOriginalData());
  process.mock_process_handle().mock_memory().AddMemory(kAddress3, GetOriginalData());

  // The step over strategy is as follows:
  // 1. Thread 1 hits breakpoint 1.
  // 2. Thread 2 hits breakpoint 2.
  // 3. Thread 3 hits breakpoint 3.
  // 4. Thread 1 finishes step over.
  // 5. Thread 4 hits breakpoint 2 (somehow).
  // 6. Thread 2 finishes step over.
  // 7. Thread 3 finishes step over.
  // 8. Thread 4 finishes step over.

  constexpr zx_koid_t kThread1Koid = 1;
  constexpr zx_koid_t kThread2Koid = 2;
  constexpr zx_koid_t kThread3Koid = 3;
  constexpr zx_koid_t kThread4Koid = 4;
  constexpr zx_koid_t kThread5Koid = 5;
  MockThread* mock_thread1 = process.AddThread(kThread1Koid);
  MockThread* mock_thread2 = process.AddThread(kThread2Koid);
  MockThread* mock_thread3 = process.AddThread(kThread3Koid);
  MockThread* mock_thread4 = process.AddThread(kThread4Koid);
  MockThread* mock_thread5 = process.AddThread(kThread5Koid);

  SoftwareBreakpoint breakpoint1(&main_breakpoint1, &process, kAddress1);
  SoftwareBreakpoint breakpoint2(&main_breakpoint2, &process, kAddress2);
  SoftwareBreakpoint breakpoint3(&main_breakpoint3, &process, kAddress3);

  ASSERT_EQ(ZX_OK, breakpoint1.Init());
  ASSERT_EQ(ZX_OK, breakpoint2.Init());
  ASSERT_EQ(ZX_OK, breakpoint3.Init());

  // The breakpoint should be installed at all locations.
  EXPECT_TRUE(MemoryContainsBreak(process.mock_process_handle(), kAddress1));
  EXPECT_TRUE(MemoryContainsBreak(process.mock_process_handle(), kAddress2));
  EXPECT_TRUE(MemoryContainsBreak(process.mock_process_handle(), kAddress3));

  // Thread 1 hits breakpoint 1 --------------------------------------------------------------------

  breakpoint1.BeginStepOver(mock_thread1);

  // There should be one enqueued step over.
  ASSERT_EQ(process.step_over_queue().size(), 1u);
  ASSERT_EQ(process.step_over_queue()[0].process_breakpoint.get(), &breakpoint1);
  ASSERT_EQ(process.step_over_queue()[0].thread.get(), mock_thread1);

  // Only thread 1 should be still running. The rest should be suspended.
  EXPECT_TRUE(mock_thread1->running());
  EXPECT_TRUE(mock_thread2->mock_thread_handle().is_suspended());
  EXPECT_TRUE(mock_thread3->mock_thread_handle().is_suspended());
  EXPECT_TRUE(mock_thread4->mock_thread_handle().is_suspended());
  EXPECT_TRUE(mock_thread5->mock_thread_handle().is_suspended());

  // Only thread 1 should be stepping over.
  EXPECT_TRUE(mock_thread1->stepping_over_breakpoint());
  EXPECT_FALSE(mock_thread2->stepping_over_breakpoint());
  EXPECT_FALSE(mock_thread3->stepping_over_breakpoint());
  EXPECT_FALSE(mock_thread4->stepping_over_breakpoint());
  EXPECT_FALSE(mock_thread5->stepping_over_breakpoint());

  CheckVectorContainsElements(FROM_HERE, breakpoint1.CurrentlySuspendedThreads(),
                              {kThread2Koid, kThread3Koid, kThread4Koid, kThread5Koid});
  CheckVectorContainsElements(FROM_HERE, breakpoint2.CurrentlySuspendedThreads(), {});
  CheckVectorContainsElements(FROM_HERE, breakpoint3.CurrentlySuspendedThreads(), {});

  // Breakpoint should be removed. Others breakpoints should remain set.
  EXPECT_TRUE(MemoryContainsOriginal(process.mock_process_handle(), kAddress1));
  EXPECT_TRUE(MemoryContainsBreak(process.mock_process_handle(), kAddress2));
  EXPECT_TRUE(MemoryContainsBreak(process.mock_process_handle(), kAddress3));

  // Thread 2 hits breakpoint 2 --------------------------------------------------------------------

  // Now the second thread gets the exception.
  breakpoint2.BeginStepOver(mock_thread2);

  // There should be 2 enqueued step overs.
  ASSERT_EQ(process.step_over_queue().size(), 2u);
  ASSERT_EQ(process.step_over_queue()[0].process_breakpoint.get(), &breakpoint1);
  ASSERT_EQ(process.step_over_queue()[0].thread.get(), mock_thread1);
  ASSERT_EQ(process.step_over_queue()[1].process_breakpoint.get(), &breakpoint2);
  ASSERT_EQ(process.step_over_queue()[1].thread.get(), mock_thread2);

  // Only thread 1 should be running.
  EXPECT_TRUE(mock_thread1->running());
  EXPECT_TRUE(mock_thread2->mock_thread_handle().is_suspended());
  EXPECT_TRUE(mock_thread3->mock_thread_handle().is_suspended());
  EXPECT_TRUE(mock_thread4->mock_thread_handle().is_suspended());
  EXPECT_TRUE(mock_thread5->mock_thread_handle().is_suspended());

  // Only thread 1 should be stepping over.
  EXPECT_TRUE(mock_thread1->stepping_over_breakpoint());
  EXPECT_FALSE(mock_thread2->stepping_over_breakpoint());
  EXPECT_FALSE(mock_thread3->stepping_over_breakpoint());
  EXPECT_FALSE(mock_thread4->stepping_over_breakpoint());
  EXPECT_FALSE(mock_thread5->stepping_over_breakpoint());

  CheckVectorContainsElements(FROM_HERE, breakpoint1.CurrentlySuspendedThreads(),
                              {kThread2Koid, kThread3Koid, kThread4Koid, kThread5Koid});
  CheckVectorContainsElements(FROM_HERE, breakpoint2.CurrentlySuspendedThreads(), {});
  CheckVectorContainsElements(FROM_HERE, breakpoint3.CurrentlySuspendedThreads(), {});

  // Breakpoint should be removed. Others breakpoints should remain set.
  EXPECT_TRUE(MemoryContainsOriginal(process.mock_process_handle(), kAddress1));
  EXPECT_TRUE(MemoryContainsBreak(process.mock_process_handle(), kAddress2));
  EXPECT_TRUE(MemoryContainsBreak(process.mock_process_handle(), kAddress3));

  // Thread 3 hits breakpoint ----------------------------------------------------------------------

  breakpoint3.BeginStepOver(mock_thread3);

  // There should be 2 enqueued step overs.
  ASSERT_EQ(process.step_over_queue().size(), 3u);
  ASSERT_EQ(process.step_over_queue()[0].process_breakpoint.get(), &breakpoint1);
  ASSERT_EQ(process.step_over_queue()[0].thread.get(), mock_thread1);
  ASSERT_EQ(process.step_over_queue()[1].process_breakpoint.get(), &breakpoint2);
  ASSERT_EQ(process.step_over_queue()[1].thread.get(), mock_thread2);
  ASSERT_EQ(process.step_over_queue()[2].process_breakpoint.get(), &breakpoint3);
  ASSERT_EQ(process.step_over_queue()[2].thread.get(), mock_thread3);

  // Only thread 1 should be running.
  EXPECT_TRUE(mock_thread1->running());
  EXPECT_TRUE(mock_thread2->mock_thread_handle().is_suspended());
  EXPECT_TRUE(mock_thread3->mock_thread_handle().is_suspended());
  EXPECT_TRUE(mock_thread4->mock_thread_handle().is_suspended());
  EXPECT_TRUE(mock_thread5->mock_thread_handle().is_suspended());

  // Only thread 1 should be stepping over right now.
  EXPECT_TRUE(mock_thread1->stepping_over_breakpoint());
  EXPECT_FALSE(mock_thread2->stepping_over_breakpoint());
  EXPECT_FALSE(mock_thread3->stepping_over_breakpoint());
  EXPECT_FALSE(mock_thread4->stepping_over_breakpoint());
  EXPECT_FALSE(mock_thread5->stepping_over_breakpoint());

  CheckVectorContainsElements(FROM_HERE, breakpoint1.CurrentlySuspendedThreads(),
                              {kThread2Koid, kThread3Koid, kThread4Koid, kThread5Koid});
  CheckVectorContainsElements(FROM_HERE, breakpoint2.CurrentlySuspendedThreads(), {});
  CheckVectorContainsElements(FROM_HERE, breakpoint3.CurrentlySuspendedThreads(), {});

  // Breakpoint should be removed. Others breakpoints should remain set.
  EXPECT_TRUE(MemoryContainsOriginal(process.mock_process_handle(), kAddress1));
  EXPECT_TRUE(MemoryContainsBreak(process.mock_process_handle(), kAddress2));
  EXPECT_TRUE(MemoryContainsBreak(process.mock_process_handle(), kAddress3));

  // Thread 1 steps over ---------------------------------------------------------------------------

  breakpoint1.EndStepOver(mock_thread1);

  // There should be 2 enqueued step overs.
  ASSERT_EQ(process.step_over_queue().size(), 2u);
  ASSERT_EQ(process.step_over_queue()[0].process_breakpoint.get(), &breakpoint2);
  ASSERT_EQ(process.step_over_queue()[0].thread.get(), mock_thread2);
  ASSERT_EQ(process.step_over_queue()[1].process_breakpoint.get(), &breakpoint3);
  ASSERT_EQ(process.step_over_queue()[1].thread.get(), mock_thread3);

  // Only thread 2 should be running.
  EXPECT_TRUE(mock_thread1->mock_thread_handle().is_suspended());
  EXPECT_TRUE(mock_thread2->running());
  EXPECT_TRUE(mock_thread3->mock_thread_handle().is_suspended());
  EXPECT_TRUE(mock_thread4->mock_thread_handle().is_suspended());
  EXPECT_TRUE(mock_thread5->mock_thread_handle().is_suspended());

  // Only thread 2 should be stepping over right now.
  EXPECT_FALSE(mock_thread1->stepping_over_breakpoint());
  EXPECT_TRUE(mock_thread2->stepping_over_breakpoint());
  EXPECT_FALSE(mock_thread3->stepping_over_breakpoint());
  EXPECT_FALSE(mock_thread4->stepping_over_breakpoint());
  EXPECT_FALSE(mock_thread5->stepping_over_breakpoint());

  CheckVectorContainsElements(FROM_HERE, breakpoint1.CurrentlySuspendedThreads(), {});
  CheckVectorContainsElements(FROM_HERE, breakpoint2.CurrentlySuspendedThreads(),
                              {kThread1Koid, kThread3Koid, kThread4Koid, kThread5Koid});
  CheckVectorContainsElements(FROM_HERE, breakpoint3.CurrentlySuspendedThreads(), {});

  // Breakpoint 2 should be the only one removed.
  EXPECT_TRUE(MemoryContainsBreak(process.mock_process_handle(), kAddress1));
  EXPECT_TRUE(MemoryContainsOriginal(process.mock_process_handle(), kAddress2));
  EXPECT_TRUE(MemoryContainsBreak(process.mock_process_handle(), kAddress3));

  // Thread 4 hits breakpoint 2 --------------------------------------------------------------------

  breakpoint2.BeginStepOver(mock_thread4);

  // There should be 3 enqueued step overs.
  ASSERT_EQ(process.step_over_queue().size(), 3u);
  ASSERT_EQ(process.step_over_queue()[0].process_breakpoint.get(), &breakpoint2);
  ASSERT_EQ(process.step_over_queue()[0].thread.get(), mock_thread2);
  ASSERT_EQ(process.step_over_queue()[1].process_breakpoint.get(), &breakpoint3);
  ASSERT_EQ(process.step_over_queue()[1].thread.get(), mock_thread3);
  ASSERT_EQ(process.step_over_queue()[2].process_breakpoint.get(), &breakpoint2);
  ASSERT_EQ(process.step_over_queue()[2].thread.get(), mock_thread4);

  // Only thread 2 should be running.
  EXPECT_TRUE(mock_thread1->mock_thread_handle().is_suspended());
  EXPECT_TRUE(mock_thread2->running());
  EXPECT_TRUE(mock_thread3->mock_thread_handle().is_suspended());
  EXPECT_TRUE(mock_thread4->mock_thread_handle().is_suspended());
  EXPECT_TRUE(mock_thread5->mock_thread_handle().is_suspended());

  // Only thread 2 should be stepping over right now.
  EXPECT_FALSE(mock_thread1->stepping_over_breakpoint());
  EXPECT_TRUE(mock_thread2->stepping_over_breakpoint());
  EXPECT_FALSE(mock_thread3->stepping_over_breakpoint());
  EXPECT_FALSE(mock_thread4->stepping_over_breakpoint());
  EXPECT_FALSE(mock_thread5->stepping_over_breakpoint());

  CheckVectorContainsElements(FROM_HERE, breakpoint1.CurrentlySuspendedThreads(), {});
  CheckVectorContainsElements(FROM_HERE, breakpoint2.CurrentlySuspendedThreads(),
                              {kThread1Koid, kThread3Koid, kThread4Koid, kThread5Koid});
  CheckVectorContainsElements(FROM_HERE, breakpoint3.CurrentlySuspendedThreads(), {});

  // Breakpoint 2 should be the only one removed.
  EXPECT_TRUE(MemoryContainsBreak(process.mock_process_handle(), kAddress1));
  EXPECT_TRUE(MemoryContainsOriginal(process.mock_process_handle(), kAddress2));
  EXPECT_TRUE(MemoryContainsBreak(process.mock_process_handle(), kAddress3));

  // Thread 2 steps over ---------------------------------------------------------------------------

  breakpoint2.EndStepOver(mock_thread2);

  // There should be 2 enqueued step overs.
  ASSERT_EQ(process.step_over_queue().size(), 2u);
  ASSERT_EQ(process.step_over_queue()[0].process_breakpoint.get(), &breakpoint3);
  ASSERT_EQ(process.step_over_queue()[0].thread.get(), mock_thread3);
  ASSERT_EQ(process.step_over_queue()[1].process_breakpoint.get(), &breakpoint2);
  ASSERT_EQ(process.step_over_queue()[1].thread.get(), mock_thread4);

  // Only thread 3 should be running, as it's the only one stepping over.
  EXPECT_TRUE(mock_thread1->mock_thread_handle().is_suspended());
  EXPECT_TRUE(mock_thread2->mock_thread_handle().is_suspended());
  EXPECT_TRUE(mock_thread3->running());
  EXPECT_TRUE(mock_thread4->mock_thread_handle().is_suspended());
  EXPECT_TRUE(mock_thread5->mock_thread_handle().is_suspended());

  // Only thread 3 should be stepping over right now.
  EXPECT_FALSE(mock_thread1->stepping_over_breakpoint());
  EXPECT_FALSE(mock_thread2->stepping_over_breakpoint());
  EXPECT_TRUE(mock_thread3->stepping_over_breakpoint());
  EXPECT_FALSE(mock_thread4->stepping_over_breakpoint());
  EXPECT_FALSE(mock_thread5->stepping_over_breakpoint());

  CheckVectorContainsElements(FROM_HERE, breakpoint1.CurrentlySuspendedThreads(), {});
  CheckVectorContainsElements(FROM_HERE, breakpoint2.CurrentlySuspendedThreads(), {});
  CheckVectorContainsElements(FROM_HERE, breakpoint3.CurrentlySuspendedThreads(),
                              {kThread1Koid, kThread2Koid, kThread4Koid, kThread5Koid});
  // Breakpoint 3 should be the only one removed.
  EXPECT_TRUE(MemoryContainsBreak(process.mock_process_handle(), kAddress1));
  EXPECT_TRUE(MemoryContainsBreak(process.mock_process_handle(), kAddress2));
  EXPECT_TRUE(MemoryContainsOriginal(process.mock_process_handle(), kAddress3));

  // Thread 3 steps over ---------------------------------------------------------------------------

  breakpoint3.EndStepOver(mock_thread3);

  // No more enqueued elements.
  ASSERT_EQ(process.step_over_queue().size(), 1u);
  ASSERT_EQ(process.step_over_queue()[0].process_breakpoint.get(), &breakpoint2);
  ASSERT_EQ(process.step_over_queue()[0].thread.get(), mock_thread4);

  // Only thread 4 should be running, as it's the only one stepping over.
  EXPECT_TRUE(mock_thread1->mock_thread_handle().is_suspended());
  EXPECT_TRUE(mock_thread2->mock_thread_handle().is_suspended());
  EXPECT_TRUE(mock_thread3->mock_thread_handle().is_suspended());
  EXPECT_TRUE(mock_thread4->running());
  EXPECT_TRUE(mock_thread5->mock_thread_handle().is_suspended());

  // Only thread 3 should be stepping over right now.
  EXPECT_FALSE(mock_thread1->stepping_over_breakpoint());
  EXPECT_FALSE(mock_thread2->stepping_over_breakpoint());
  EXPECT_FALSE(mock_thread3->stepping_over_breakpoint());
  EXPECT_TRUE(mock_thread4->stepping_over_breakpoint());
  EXPECT_FALSE(mock_thread5->stepping_over_breakpoint());

  CheckVectorContainsElements(FROM_HERE, breakpoint1.CurrentlySuspendedThreads(), {});
  CheckVectorContainsElements(FROM_HERE, breakpoint2.CurrentlySuspendedThreads(),
                              {kThread1Koid, kThread2Koid, kThread3Koid, kThread5Koid});
  CheckVectorContainsElements(FROM_HERE, breakpoint3.CurrentlySuspendedThreads(), {});

  // Breakpoint 2 should be the only one removed.
  EXPECT_TRUE(MemoryContainsBreak(process.mock_process_handle(), kAddress1));
  EXPECT_TRUE(MemoryContainsOriginal(process.mock_process_handle(), kAddress2));
  EXPECT_TRUE(MemoryContainsBreak(process.mock_process_handle(), kAddress3));

  // Thread 4 steps over ---------------------------------------------------------------------------

  breakpoint2.EndStepOver(mock_thread4);

  // All threads should be resumed.
  EXPECT_TRUE(mock_thread1->running());
  EXPECT_TRUE(mock_thread2->running());
  EXPECT_TRUE(mock_thread3->running());
  EXPECT_TRUE(mock_thread4->running());
  EXPECT_TRUE(mock_thread5->running());

  // No thread should be stepping over.
  EXPECT_FALSE(mock_thread1->stepping_over_breakpoint());
  EXPECT_FALSE(mock_thread2->stepping_over_breakpoint());
  EXPECT_FALSE(mock_thread3->stepping_over_breakpoint());
  EXPECT_FALSE(mock_thread4->stepping_over_breakpoint());
  EXPECT_FALSE(mock_thread5->stepping_over_breakpoint());

  EXPECT_TRUE(breakpoint1.CurrentlySuspendedThreads().empty());
  EXPECT_TRUE(breakpoint2.CurrentlySuspendedThreads().empty());
  EXPECT_TRUE(breakpoint3.CurrentlySuspendedThreads().empty());

  // All breakpoints are set again.
  EXPECT_TRUE(MemoryContainsBreak(process.mock_process_handle(), kAddress1));
  EXPECT_TRUE(MemoryContainsBreak(process.mock_process_handle(), kAddress2));
  EXPECT_TRUE(MemoryContainsBreak(process.mock_process_handle(), kAddress3));
}

// It's possible for a thread to try to step over the same breakpoint twice. This normally indicates
// an error removing the breakpoint to step over it. We must not enqueue the same thread twice in
// this error case or it will be forever stuck waiting for itself.
TEST(ProcessBreakpoint, RecursiveStep) {
  TestProcessDelegate process_delegate;

  Breakpoint main_breakpoint(&process_delegate);
  debug_ipc::BreakpointSettings settings;
  settings.type = debug_ipc::BreakpointType::kSoftware;
  main_breakpoint.SetSettings(settings);

  constexpr zx_koid_t process_koid = 0x1234;
  const std::string process_name = "process";
  MockProcess process(nullptr, process_koid, process_name);

  process.mock_process_handle().mock_memory().AddMemory(kAddress, GetOriginalData());

  constexpr zx_koid_t kThreadKoid = 1;
  MockThread* mock_thread = process.AddThread(kThreadKoid);

  SoftwareBreakpoint bp(&main_breakpoint, &process, kAddress);
  ASSERT_EQ(ZX_OK, bp.Init());

  // The breakpoint should be installed.
  EXPECT_TRUE(MemoryContainsBreak(process.mock_process_handle(), kAddress));

  // Hit the breakpoint the first time.
  bp.BeginStepOver(mock_thread);

  // There should be one enqueued step over.
  ASSERT_EQ(process.step_over_queue().size(), 1u);
  ASSERT_EQ(process.step_over_queue()[0].process_breakpoint.get(), &bp);
  ASSERT_EQ(process.step_over_queue()[0].thread.get(), mock_thread);

  // Recursively hits the same breakpoint.
  bp.BeginStepOver(mock_thread);

  // There should still just be one enqueued step over.
  ASSERT_EQ(process.step_over_queue().size(), 1u);
  ASSERT_EQ(process.step_over_queue()[0].process_breakpoint.get(), &bp);
  ASSERT_EQ(process.step_over_queue()[0].thread.get(), mock_thread);
}

// This also tests registration and unregistration of ProcessBreakpoints via the Breakpoint object.
TEST(ProcessBreakpoint, HitCount) {
  TestProcessDelegate process_delegate;

  constexpr zx_koid_t kProcess1 = 1;
  auto owning_process = std::make_unique<MockProcess>(nullptr, kProcess1);
  owning_process->mock_process_handle().mock_memory().AddMemory(kAddress, GetOriginalData());
  process_delegate.InjectMockProcess(std::move(owning_process));

  constexpr uint32_t kBreakpointId1 = 12;
  debug_ipc::BreakpointSettings settings;
  settings.id = kBreakpointId1;
  settings.type = debug_ipc::BreakpointType::kSoftware;
  settings.locations.resize(1);

  debug_ipc::ProcessBreakpointSettings& pr_settings = settings.locations.back();
  pr_settings.process_koid = kProcess1;
  pr_settings.thread_koid = 0;
  pr_settings.address = kAddress;

  // Create a ProcessBreakpoint referencing the two Breakpoint objects
  // (corresponds to two logical breakpoints at the same address).
  std::unique_ptr<Breakpoint> main_breakpoint1 = std::make_unique<Breakpoint>(&process_delegate);
  zx_status_t status = main_breakpoint1->SetSettings(settings);
  ASSERT_EQ(ZX_OK, status);

  std::unique_ptr<Breakpoint> main_breakpoint2 = std::make_unique<Breakpoint>(&process_delegate);
  constexpr uint32_t kBreakpointId2 = 13;
  settings.id = kBreakpointId2;
  status = main_breakpoint2->SetSettings(settings);
  ASSERT_EQ(ZX_OK, status);

  // There should only be one address with a breakpoint.
  ASSERT_EQ(1u, process_delegate.bps().size());
  EXPECT_EQ(kAddress, process_delegate.bps().begin()->first);

  // Hitting the ProcessBreakpoint should update both Breakpoints.
  std::vector<debug_ipc::BreakpointStats> stats;
  process_delegate.bps().begin()->second->OnHit(debug_ipc::BreakpointType::kSoftware, &stats);
  ASSERT_EQ(2u, stats.size());

  // Order of the vector is not defined so allow either.
  EXPECT_TRUE((stats[0].id == kBreakpointId1 && stats[1].id == kBreakpointId2) ||
              (stats[0].id == kBreakpointId2 && stats[1].id == kBreakpointId1));

  // The hit count of both should be 1 (order doesn't matter).
  EXPECT_EQ(1u, stats[0].hit_count);
  EXPECT_EQ(1u, stats[1].hit_count);

  // Unregistering one Breakpoint should keep the ProcessBreakpoint.
  main_breakpoint2.reset();
  ASSERT_EQ(1u, process_delegate.bps().size());

  // Unregistering the other should delete it.
  main_breakpoint1.reset();
  ASSERT_EQ(0u, process_delegate.bps().size());
}

}  // namespace debug_agent
