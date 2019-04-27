// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string.h>

#include <zircon/syscalls/exception.h>

#include "gtest/gtest.h"
#include "src/developer/debug/debug_agent/breakpoint.h"
#include "src/developer/debug/debug_agent/debugged_thread.h"
#include "src/developer/debug/debug_agent/mock_arch_provider.h"
#include "src/developer/debug/debug_agent/mock_process.h"
#include "src/developer/debug/debug_agent/process_breakpoint.h"
#include "src/developer/debug/debug_agent/process_memory_accessor.h"

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

  void InjectMockProcess(std::unique_ptr<MockProcess> proc) {
    procs_[proc->koid()] = std::move(proc);
  }

  // This only gets called if Breakpoint.SetSettings() is called.
  zx_status_t RegisterBreakpoint(Breakpoint* bp, zx_koid_t koid,
                                 uint64_t address) override {
    auto found = bps_.find(address);
    if (found == bps_.end()) {
      auto pbp = std::make_unique<ProcessBreakpoint>(bp, procs_[koid].get(),
                                                     mem_.memory(), address);

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
    if (found == bps_.end())
      GTEST_FAIL();

    bool still_used = found->second->UnregisterBreakpoint(bp);
    if (!still_used)
      bps_.erase(found);
  }

 private:
  BreakpointFakeMemory mem_;

  std::map<uint64_t, std::unique_ptr<ProcessBreakpoint>> bps_;
  std::map<zx_koid_t, std::unique_ptr<MockProcess>> procs_;
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
  main_breakpoint.set_type(debug_ipc::BreakpointType::kSoftware);
  zx_koid_t process_koid = 0x1234;
  MockProcess process(process_koid);

  ProcessBreakpoint bp(&main_breakpoint, &process,
                       process_delegate.mem().memory(),
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
  main_breakpoint.set_type(debug_ipc::BreakpointType::kSoftware);

  zx_koid_t process_koid = 0x1234;
  MockProcess process(process_koid);
  ProcessBreakpoint bp(&main_breakpoint, &process,
                       process_delegate.mem().memory(),
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

  EXPECT_TRUE(bp.BreakpointStepHasException(
      kThread1Koid, debug_ipc::NotifyException::Type::kSingleStep));

  // Since one thread is still stepping, the memory should still be original.
  EXPECT_TRUE(process_delegate.mem().IsOriginal());

  // As soon as the second breakpoint is resolved, the breakpoint instruction
  // should be put back.
  EXPECT_TRUE(bp.BreakpointStepHasException(
      kThread2Koid, debug_ipc::NotifyException::Type::kSingleStep));
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
  zx_status_t status = main_breakpoint1->SetSettings(
      debug_ipc::BreakpointType::kSoftware, settings);
  ASSERT_EQ(ZX_OK, status);

  std::unique_ptr<Breakpoint> main_breakpoint2 =
      std::make_unique<Breakpoint>(&process_delegate);
  constexpr uint32_t kBreakpointId2 = 13;
  settings.breakpoint_id = kBreakpointId2;
  status = main_breakpoint2->SetSettings(debug_ipc::BreakpointType::kSoftware,
                                         settings);
  ASSERT_EQ(ZX_OK, status);

  // There should only be one address with a breakpoint.
  ASSERT_EQ(1u, process_delegate.bps().size());
  EXPECT_EQ(BreakpointFakeMemory::kAddress,
            process_delegate.bps().begin()->first);

  // Hitting the ProcessBreakpoint should update both Breakpoints.
  std::vector<debug_ipc::BreakpointStats> stats;
  process_delegate.bps().begin()->second->OnHit(
      debug_ipc::BreakpointType::kSoftware, &stats);
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

TEST(ProcessBreakpoint, HWBreakpointForAllThreads) {
  constexpr zx_koid_t kProcessId = 0x1234;
  constexpr zx_koid_t kThreadId1 = 0x1;
  constexpr zx_koid_t kThreadId2 = 0x2;
  constexpr zx_koid_t kThreadId3 = 0x3;
  constexpr uint32_t kBreakpointId1 = 0x1;
  constexpr uint64_t kAddress = 0x80000000;

  auto process = std::make_unique<MockProcess>(kProcessId);
  process->AddThread(kThreadId1);
  process->AddThread(kThreadId2);
  process->AddThread(kThreadId3);
  TestProcessDelegate process_delegate;
  process_delegate.InjectMockProcess(std::move(process));

  // Any calls to the architecture will be routed to this instance.
  ScopedMockArchProvider scoped_arch_provider;
  MockArchProvider* arch_provider = scoped_arch_provider.get_provider();

  auto breakpoint = std::make_unique<Breakpoint>(&process_delegate);
  debug_ipc::BreakpointSettings settings1 = {};
  settings1.breakpoint_id = kBreakpointId1;
  // This location is for all threads.
  settings1.locations.push_back({kProcessId, 0, kAddress});
  zx_status_t status =
      breakpoint->SetSettings(debug_ipc::BreakpointType::kHardware, settings1);
  ASSERT_EQ(status, ZX_OK);

  // Should have installed the breakpoint.
  ASSERT_EQ(process_delegate.bps().size(), 1u);
  auto& process_bp = process_delegate.bps().begin()->second;
  ASSERT_EQ(process_bp->address(), kAddress);

  // It should have installed a HW breakpoint for each thread.
  EXPECT_FALSE(process_bp->SoftwareBreakpointInstalled());
  EXPECT_TRUE(process_bp->HardwareBreakpointInstalled());
  EXPECT_EQ(arch_provider->BreakpointInstallCount(kAddress), 3u);

  // Deleting the breakpoint should remove the process breakpoint.
  breakpoint.reset();
  EXPECT_EQ(arch_provider->BreakpointUninstallCount(kAddress), 3u);
  EXPECT_EQ(process_delegate.bps().size(), 0u);
}

TEST(ProcessBreakpoint, HWBreakpointWithThreadId) {
  constexpr zx_koid_t kProcessId = 0x1234;
  constexpr zx_koid_t kThreadId1 = 0x1;
  constexpr zx_koid_t kThreadId2 = 0x2;
  constexpr zx_koid_t kThreadId3 = 0x3;
  constexpr uint32_t kBreakpointId1 = 0x1;
  constexpr uint32_t kBreakpointId2 = 0x2;
  constexpr uint32_t kSwBreakpointId = 0x3;
  constexpr uint64_t kAddress = BreakpointFakeMemory::kAddress;
  constexpr uint64_t kOtherAddress = 0x8fffffff;

  auto process = std::make_unique<MockProcess>(kProcessId);
  process->AddThread(kThreadId1);
  process->AddThread(kThreadId2);
  process->AddThread(kThreadId3);
  TestProcessDelegate process_delegate;
  process_delegate.InjectMockProcess(std::move(process));

  // Any calls to the architecture will be routed to this instance.
  ScopedMockArchProvider scoped_arch_provider;
  MockArchProvider* arch_provider = scoped_arch_provider.get_provider();

  auto breakpoint1 = std::make_unique<Breakpoint>(&process_delegate);
  debug_ipc::BreakpointSettings settings1 = {};
  settings1.breakpoint_id = kBreakpointId1;
  settings1.locations.push_back({kProcessId, kThreadId1, kAddress});
  zx_status_t status =
      breakpoint1->SetSettings(debug_ipc::BreakpointType::kHardware, settings1);
  ASSERT_EQ(status, ZX_OK);
  // Should have installed the process breakpoint.
  ASSERT_EQ(process_delegate.bps().size(), 1u);
  auto& process_bp = process_delegate.bps().begin()->second;
  ASSERT_EQ(process_bp->address(), kAddress);
  // This should have installed HW breakpoint for only one thread.
  // This should have installed only a HW breakpoint.
  ASSERT_EQ(arch_provider->TotalBreakpointInstallCalls(), 1u);
  ASSERT_EQ(arch_provider->BreakpointInstallCount(kAddress), 1u);
  ASSERT_EQ(arch_provider->TotalBreakpointUninstallCalls(), 0u);
  EXPECT_FALSE(process_bp->SoftwareBreakpointInstalled());
  EXPECT_TRUE(process_bp->HardwareBreakpointInstalled());

  // Register another breakpoint.
  auto breakpoint2 = std::make_unique<Breakpoint>(&process_delegate);
  debug_ipc::BreakpointSettings settings2 = {};
  settings2.breakpoint_id = kBreakpointId2;
  settings2.locations.push_back({kProcessId, kThreadId2, kAddress});
  // This breakpoint has another location for another thread.
  // In practice, this should not happen, but it's important that no HW
  // breakpoint get installed if for the wrong location.
  settings2.locations.push_back({kProcessId, kThreadId3, kOtherAddress});
  breakpoint2->SetSettings(debug_ipc::BreakpointType::kHardware, settings2);
  // Registering this breakpoint should create a new ProcessBreakpoint.
  ASSERT_EQ(process_delegate.bps().size(), 2u);
  auto& process_bp2 = (process_delegate.bps().begin()++)->second;
  ASSERT_EQ(process_bp2->address(), kOtherAddress);
  // Registering the second breakpoint should install for the new thread in
  // the old location and one in the new location.
  ASSERT_EQ(arch_provider->TotalBreakpointInstallCalls(), 3u);
  ASSERT_EQ(arch_provider->BreakpointInstallCount(kAddress), 2u);
  ASSERT_EQ(arch_provider->BreakpointInstallCount(kOtherAddress), 1u);
  ASSERT_EQ(arch_provider->TotalBreakpointUninstallCalls(), 0u);
  EXPECT_FALSE(process_bp->SoftwareBreakpointInstalled());

  // Unregistering a breakpoint should only uninstall the HW breakpoint for
  // one thread.
  breakpoint1.reset();
  ASSERT_EQ(arch_provider->TotalBreakpointInstallCalls(), 3u);
  ASSERT_EQ(arch_provider->TotalBreakpointUninstallCalls(), 1u);
  ASSERT_EQ(arch_provider->BreakpointUninstallCount(kAddress), 1u);
  ASSERT_EQ(arch_provider->BreakpointUninstallCount(kOtherAddress), 0u);
  EXPECT_FALSE(process_bp->SoftwareBreakpointInstalled());
  EXPECT_FALSE(process_bp->SoftwareBreakpointInstalled());
  EXPECT_TRUE(process_bp->HardwareBreakpointInstalled());
  EXPECT_TRUE(process_bp2->HardwareBreakpointInstalled());

  // Adding a SW breakpoint should not install HW locations.
  auto sw_breakpoint = std::make_unique<Breakpoint>(&process_delegate);
  debug_ipc::BreakpointSettings sw_settings = {};
  sw_settings.breakpoint_id = kSwBreakpointId;
  sw_settings.locations.push_back({kProcessId, 0, kAddress});
  sw_breakpoint->SetSettings(debug_ipc::BreakpointType::kSoftware, sw_settings);
  // Should have installed only a SW breakpoint.
  ASSERT_EQ(arch_provider->TotalBreakpointInstallCalls(), 3u);
  ASSERT_EQ(arch_provider->TotalBreakpointUninstallCalls(), 1u);
  EXPECT_TRUE(process_bp->SoftwareBreakpointInstalled());

  // Unregistering should remove the other hw breakpoint.
  // And also the second process breakpoint.
  breakpoint2.reset();
  ASSERT_EQ(arch_provider->TotalBreakpointInstallCalls(), 3u);
  ASSERT_EQ(arch_provider->TotalBreakpointUninstallCalls(), 3u);
  ASSERT_EQ(arch_provider->BreakpointUninstallCount(kAddress), 2u);
  ASSERT_EQ(arch_provider->BreakpointUninstallCount(kOtherAddress), 1u);
  EXPECT_FALSE(process_bp->HardwareBreakpointInstalled());
  EXPECT_TRUE(process_bp->SoftwareBreakpointInstalled());
  ASSERT_EQ(process_delegate.bps().size(), 1u);
  EXPECT_EQ(process_delegate.bps().begin()->second->address(), kAddress);

  // Removing the SW breakpoint should work and would delete the final process
  // breakpoint.
  sw_breakpoint.reset();
  ASSERT_EQ(arch_provider->TotalBreakpointInstallCalls(), 3u);
  ASSERT_EQ(arch_provider->TotalBreakpointUninstallCalls(), 3u);
  EXPECT_EQ(process_delegate.bps().size(), 0u);
}

}  // namespace debug_agent
