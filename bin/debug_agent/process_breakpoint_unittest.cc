// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string.h>
#include <zircon/syscalls/exception.h>

#include "garnet/bin/debug_agent/arch.h"
#include "garnet/bin/debug_agent/breakpoint.h"
#include "garnet/bin/debug_agent/process_breakpoint.h"
#include "garnet/bin/debug_agent/process_memory_accessor.h"
#include "gtest/gtest.h"

namespace debug_agent {

namespace {

// Provides a fake view of memory with the given initial contents.
class FakeMemory : public ProcessMemoryAccessor {
 public:
  FakeMemory(intptr_t address, const char* data, size_t data_len)
      : address_(address) {
    data_.assign(data, data + data_len);
  }

  const char* data() const { return &data_[0]; }

  zx_status_t ReadProcessMemory(uintptr_t address, void* buffer, size_t len,
                                size_t* actual) override {
    *actual = 0;
    if (address < address_ || address + len > address_ + data_.size())
      return ZX_ERR_NO_MEMORY;  // We require everything to be mapped.

    memcpy(buffer, &data_[address - address_], len);
    *actual = len;
    return ZX_OK;
  }

  zx_status_t WriteProcessMemory(uintptr_t address, const void* buffer,
                                 size_t len, size_t* actual) override {
    *actual = 0;
    if (address < address_ || address + len > address_ + data_.size())
      return ZX_ERR_NO_MEMORY;  // We require everything to be mapped.

    memcpy(&data_[address - address_], buffer, len);
    *actual = len;
    return ZX_OK;
  }

 private:
  uintptr_t address_;
  std::vector<char> data_;
};

// Provides a buffer of known memory for tests below.
class BreakpointFakeMemory {
 public:
  // Make a fake memory buffer with enough room to hold a break instruction.
  static constexpr uintptr_t kAddress = 0x123456780;
  static constexpr size_t kDataSize = 4;
  static_assert(kDataSize >= sizeof(arch::BreakInstructionType),
                "Make data bigger for this platform.");
  static const char kOriginalData[kDataSize];

  BreakpointFakeMemory() : memory_(kAddress, kOriginalData, kDataSize) {}
  ~BreakpointFakeMemory() {}

  FakeMemory* memory() { return &memory_; }

  // Returns the memory pointer read out as the type required for the
  // breakpoint instruction.
  arch::BreakInstructionType AsInstructionType() const {
    return *reinterpret_cast<const arch::BreakInstructionType*>(memory_.data());
  }

  // Returns true if the buffer starts with a breakpoint instruction for the
  // current platform.
  bool StartsWithBreak() const {
    return AsInstructionType() == arch::kBreakInstruction;
  }

  // Returns true if the buffer is in its original state.
  bool IsOriginal() const {
    return memcmp(memory_.data(), kOriginalData, kDataSize) == 0;
  }

 private:
  FakeMemory memory_;
};

// A no-op process delegate.
class TestProcessDelegate : public Breakpoint::ProcessDelegate {
 public:
  TestProcessDelegate() = default;

  BreakpointFakeMemory& mem() { return mem_; }
  std::map<uint64_t, std::unique_ptr<ProcessBreakpoint>>& bps() { return bps_; }

  // This only gets called if Breakpoint.SetSettings() is called.
  zx_status_t RegisterBreakpoint(Breakpoint* bp, zx_koid_t koid,
                                 uint64_t address) override {
    auto found = bps_.find(address);
    if (found == bps_.end()) {
      auto pbp =
          std::make_unique<ProcessBreakpoint>(bp, mem_.memory(), koid, address);
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
  void UnregisterBreakpoint(Breakpoint* bp, zx_koid_t,
                            uint64_t address) override {
    auto found = bps_.find(address);
    if (found == bps_.end()) {
      GTEST_FAIL();
      return;
    }

    bool still_used = found->second->UnregisterBreakpoint(bp);
    if (!still_used)
      bps_.erase(found);
  }

 private:
  BreakpointFakeMemory mem_;

  std::map<uint64_t, std::unique_ptr<ProcessBreakpoint>> bps_;
};

constexpr uintptr_t BreakpointFakeMemory::kAddress;
constexpr size_t BreakpointFakeMemory::kDataSize;
const char
    BreakpointFakeMemory::kOriginalData[BreakpointFakeMemory::kDataSize] = {
        0x01, 0x02, 0x03, 0x04};

}  // namespace

TEST(ProcessBreakpoint, InstallAndFixup) {
  TestProcessDelegate process_delegate;
  Breakpoint main_breakpoint(&process_delegate);

  ProcessBreakpoint bp(&main_breakpoint, process_delegate.mem().memory(), 1,
                       BreakpointFakeMemory::kAddress);
  ASSERT_EQ(ZX_OK, bp.Init());

  // Should have written the breakpoint instruction to the buffer.
  EXPECT_TRUE(process_delegate.mem().StartsWithBreak());

  // Make a memory block that contains the address set as the breakpoint.
  // Offset it by kBlockOffset to make sure non-aligned cases are handled.
  debug_ipc::MemoryBlock block;
  constexpr size_t kBlockOffset = 4;
  block.address = BreakpointFakeMemory::kAddress - kBlockOffset;
  block.valid = true;
  block.size = 16;
  block.data.resize(block.size);

  // Fill with current memory contents (including breakpoint instruction).
  memcpy(&block.data[kBlockOffset], process_delegate.mem().memory()->data(),
         BreakpointFakeMemory::kDataSize);

  // FixupMemoryBlock should give back the original data.
  bp.FixupMemoryBlock(&block);
  EXPECT_EQ(
      0, memcmp(&block.data[kBlockOffset], BreakpointFakeMemory::kOriginalData,
                BreakpointFakeMemory::kDataSize));
}

// Attempts to step over the breakpoint from multiple threads at the same
// time.
TEST(ProcessBreakpoint, StepMultiple) {
  TestProcessDelegate process_delegate;
  Breakpoint main_breakpoint(&process_delegate);

  ProcessBreakpoint bp(&main_breakpoint, process_delegate.mem().memory(), 1,
                       BreakpointFakeMemory::kAddress);
  ASSERT_EQ(ZX_OK, bp.Init());

  // The breakpoint should be installed.
  EXPECT_TRUE(process_delegate.mem().StartsWithBreak());

  // Begin stepping over the breakpoint from two threads at the same time.
  // The memory should be back to original.
  zx_koid_t kThread1Koid = 1;
  bp.BeginStepOver(kThread1Koid);
  EXPECT_TRUE(process_delegate.mem().IsOriginal());
  zx_koid_t kThread2Koid = 2;
  bp.BeginStepOver(kThread2Koid);
  EXPECT_TRUE(process_delegate.mem().IsOriginal());

  // In real life, the thread would now single-step over the breakpoint. It
  // would trigger a hardware breakpoint at the next instruction.

  EXPECT_TRUE(
      bp.BreakpointStepHasException(kThread1Koid, ZX_EXCP_HW_BREAKPOINT));

  // Since one thread is still stepping, the memory should still be original.
  EXPECT_TRUE(process_delegate.mem().IsOriginal());

  // As soon as the second breakpoint is resolved, the breakpoint instruction
  // should be put back.
  EXPECT_TRUE(
      bp.BreakpointStepHasException(kThread2Koid, ZX_EXCP_HW_BREAKPOINT));
  EXPECT_TRUE(process_delegate.mem().StartsWithBreak());
}

// This also tests registration and unregistration of ProcessBreakpoints via
// the Breakpoint object.
TEST(ProcessBreakpoint, HitCount) {
  TestProcessDelegate process_delegate;

  constexpr uint32_t kBreakpointId1 = 12;
  debug_ipc::BreakpointSettings settings;
  settings.breakpoint_id = kBreakpointId1;
  settings.locations.resize(1);

  constexpr zx_koid_t kProcess1 = 1;

  debug_ipc::ProcessBreakpointSettings& pr_settings = settings.locations.back();
  pr_settings.process_koid = kProcess1;
  pr_settings.thread_koid = 0;
  pr_settings.address = BreakpointFakeMemory::kAddress;

  // Create a ProcessBreakpoint referencing the two Breakpoint objects
  // (corresponds to two logical breakpoints at the same address).
  std::unique_ptr<Breakpoint> main_breakpoint1 =
      std::make_unique<Breakpoint>(&process_delegate);
  zx_status_t status = main_breakpoint1->SetSettings(settings);
  ASSERT_EQ(ZX_OK, status);

  std::unique_ptr<Breakpoint> main_breakpoint2 =
      std::make_unique<Breakpoint>(&process_delegate);
  constexpr uint32_t kBreakpointId2 = 13;
  settings.breakpoint_id = kBreakpointId2;
  status = main_breakpoint2->SetSettings(settings);
  ASSERT_EQ(ZX_OK, status);

  // There should only be one address with a breakpoint.
  ASSERT_EQ(1u, process_delegate.bps().size());
  EXPECT_EQ(BreakpointFakeMemory::kAddress,
            process_delegate.bps().begin()->first);

  // Hitting the ProcessBreakpoint should update both Breakpoints.
  std::vector<debug_ipc::BreakpointStats> stats;
  process_delegate.bps().begin()->second->OnHit(&stats);
  ASSERT_EQ(2u, stats.size());

  // Order of the vector is not defined so allow either.
  EXPECT_TRUE((stats[0].breakpoint_id == kBreakpointId1 &&
               stats[1].breakpoint_id == kBreakpointId2) ||
              (stats[0].breakpoint_id == kBreakpointId2 &&
               stats[1].breakpoint_id == kBreakpointId1));

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
