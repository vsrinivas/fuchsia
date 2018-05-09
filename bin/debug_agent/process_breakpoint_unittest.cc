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

  zx_status_t RegisterBreakpoint(Breakpoint*, zx_koid_t, uint64_t) override {
    return ZX_OK;
  }
  void UnregisterBreakpoint(Breakpoint*, zx_koid_t, uint64_t) override {}
};

constexpr uintptr_t BreakpointFakeMemory::kAddress;
constexpr size_t BreakpointFakeMemory::kDataSize;
const char
    BreakpointFakeMemory::kOriginalData[BreakpointFakeMemory::kDataSize] = {
        0x01, 0x02, 0x03, 0x04};

}  // namespace

TEST(ProcessBreakpoint, InstallAndFixup) {
  BreakpointFakeMemory mem;
  TestProcessDelegate process_delegate;
  Breakpoint main_breakpoint(&process_delegate);

  ProcessBreakpoint bp(&main_breakpoint, mem.memory(), 1,
                       BreakpointFakeMemory::kAddress);
  ASSERT_EQ(ZX_OK, bp.Init());

  // Should have written the breakpoint instruction to the buffer.
  EXPECT_TRUE(mem.StartsWithBreak());

  // Make a memory block that contains the address set as the breakpoint.
  // Offset it by kBlockOffset to make sure non-aligned cases are handled.
  debug_ipc::MemoryBlock block;
  constexpr size_t kBlockOffset = 4;
  block.address = BreakpointFakeMemory::kAddress - kBlockOffset;
  block.valid = true;
  block.size = 16;
  block.data.resize(block.size);

  // Fill with current memory contents (including breakpoint instruction).
  memcpy(&block.data[kBlockOffset], mem.memory()->data(),
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
  BreakpointFakeMemory mem;
  TestProcessDelegate process_delegate;
  Breakpoint main_breakpoint(&process_delegate);

  ProcessBreakpoint bp(&main_breakpoint, mem.memory(), 1,
                       BreakpointFakeMemory::kAddress);
  ASSERT_EQ(ZX_OK, bp.Init());

  // The breakpoint should be installed.
  EXPECT_TRUE(mem.StartsWithBreak());

  // Begin stepping over the breakpoint from two threads at the same time.
  // The memory should be back to original.
  zx_koid_t kThread1Koid = 1;
  bp.BeginStepOver(kThread1Koid);
  EXPECT_TRUE(mem.IsOriginal());
  zx_koid_t kThread2Koid = 2;
  bp.BeginStepOver(kThread2Koid);
  EXPECT_TRUE(mem.IsOriginal());

  // In real life, the thread would now single-step over the breakpoint. It
  // would trigger a hardware breakpoint at the next instruction.

  EXPECT_TRUE(
      bp.BreakpointStepHasException(kThread1Koid, ZX_EXCP_HW_BREAKPOINT));

  // Since one thread is still stepping, the memory should still be original.
  EXPECT_TRUE(mem.IsOriginal());

  // As soon as the second breakpoint is resolved, the breakpoint instruction
  // should be put back.
  EXPECT_TRUE(
      bp.BreakpointStepHasException(kThread2Koid, ZX_EXCP_HW_BREAKPOINT));
  EXPECT_TRUE(mem.StartsWithBreak());
}

}  // namespace debug_agent
