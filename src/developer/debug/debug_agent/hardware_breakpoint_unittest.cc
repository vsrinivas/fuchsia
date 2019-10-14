// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "src/developer/debug/debug_agent/hardware_breakpoint.h"

#include <gtest/gtest.h>

#include "src/developer/debug/debug_agent/breakpoint.h"
#include "src/developer/debug/debug_agent/mock_process.h"
#include "src/developer/debug/debug_agent/mock_thread.h"

namespace debug_agent {
namespace {

class MockArchProvider : public arch::ArchProvider {
 public:
  zx_status_t InstallHWBreakpoint(zx::thread* thread, uint64_t address) override {
    installs_.push_back({thread->get(), address});
    return ZX_OK;
  }

  zx_status_t UninstallHWBreakpoint(zx::thread* thread, uint64_t address) override {
    uninstalls_.push_back({thread->get(), address});
    return ZX_OK;
  }

  const std::vector<std::pair<zx_koid_t, uint64_t>>& installs() const { return installs_; }
  const std::vector<std::pair<zx_koid_t, uint64_t>>& uninstalls() const { return uninstalls_; }

 private:
  std::vector<std::pair<zx_koid_t, uint64_t>> installs_;
  std::vector<std::pair<zx_koid_t, uint64_t>> uninstalls_;
};

class MockProcessDelegate : public Breakpoint::ProcessDelegate {
 public:
  zx_status_t RegisterBreakpoint(Breakpoint* bp, zx_koid_t process_koid,
                                 uint64_t address) override {
    return ZX_OK;
  }
  void UnregisterBreakpoint(Breakpoint* bp, zx_koid_t process_koid, uint64_t address) override {}
};

// If |thread| is null, it means a process-wide breakpoint.
debug_ipc::ProcessBreakpointSettings CreateLocation(const MockProcess& process,
                                                    const MockThread* thread, uint64_t address) {
  debug_ipc::ProcessBreakpointSettings location = {};
  location.process_koid = process.koid();
  if (thread)
    location.thread_koid = thread->koid();
  location.address = address;

  return location;
}

bool ContainsKoids(const HardwareBreakpoint& hw_bp, const std::vector<zx_koid_t>& koids) {
  auto& installed_koids = hw_bp.installed_threads();
  if (installed_koids.size() != koids.size()) {
    ADD_FAILURE() << "Expected " << koids.size() << " koids, got " << installed_koids.size();
    return false;
  }

  for (zx_koid_t koid : koids) {
    if (installed_koids.count(koid) == 0) {
      ADD_FAILURE() << "Expected koid " << koid << " to be present.";
      return false;
    }
  }

  return true;
}

constexpr uint64_t kAddress = 0x1234;

// Tests -------------------------------------------------------------------------------------------

TEST(HardwareBreakpoint, SimpleInstallAndRemove) {
  auto arch_provider = std::make_shared<MockArchProvider>();
  auto object_provider = std::make_shared<ObjectProvider>();

  MockProcess process(0x1, "process", arch_provider, object_provider);
  MockThread* thread1 = process.AddThread(0x1001);

  MockProcessDelegate process_delegate;

  debug_ipc::BreakpointSettings settings;
  settings.locations.push_back(CreateLocation(process, thread1, kAddress));

  Breakpoint breakpoint1(&process_delegate);
  breakpoint1.SetSettings(debug_ipc::BreakpointType::kHardware, settings);

  HardwareBreakpoint hw_breakpoint(&breakpoint1, &process, kAddress, arch_provider);
  ASSERT_EQ(hw_breakpoint.breakpoints().size(), 1u);

  // Update should install one thread

  ASSERT_EQ(hw_breakpoint.Update(), ZX_OK);
  ASSERT_TRUE(ContainsKoids(hw_breakpoint, {thread1->koid()}));

  ASSERT_EQ(arch_provider->installs().size(), 1u);
  EXPECT_EQ(arch_provider->installs()[0].first, thread1->koid());
  EXPECT_EQ(arch_provider->installs()[0].second, kAddress);
  EXPECT_EQ(arch_provider->uninstalls().size(), 0u);

  // Binding again to the same breakpoint should fail.

  ASSERT_EQ(hw_breakpoint.RegisterBreakpoint(&breakpoint1), ZX_ERR_ALREADY_BOUND);
  ASSERT_EQ(hw_breakpoint.breakpoints().size(), 1u);

  // Unregistering the breakpoint should issue an uninstall. As there are no more breakpoints,
  // unregistering should return false.

  ASSERT_FALSE(hw_breakpoint.UnregisterBreakpoint(&breakpoint1));
  ASSERT_EQ(hw_breakpoint.breakpoints().size(), 0u);
  ASSERT_TRUE(ContainsKoids(hw_breakpoint, {}));

  ASSERT_EQ(arch_provider->installs().size(), 1u);
  EXPECT_EQ(arch_provider->uninstalls().size(), 1u);
  EXPECT_EQ(arch_provider->uninstalls()[0].first, thread1->koid());
  EXPECT_EQ(arch_provider->uninstalls()[0].second, kAddress);

  // Registering again should add the breakpoint

  ASSERT_EQ(hw_breakpoint.RegisterBreakpoint(&breakpoint1), ZX_OK);
  ASSERT_EQ(hw_breakpoint.breakpoints().size(), 1u);
  ASSERT_TRUE(ContainsKoids(hw_breakpoint, {thread1->koid()}));

  ASSERT_EQ(arch_provider->installs().size(), 2u);
  EXPECT_EQ(arch_provider->installs()[1].first, thread1->koid());
  EXPECT_EQ(arch_provider->installs()[1].second, kAddress);
  EXPECT_EQ(arch_provider->uninstalls().size(), 1u);

  // Create two other threads

  MockThread* thread2 = process.AddThread(0x1002);
  MockThread* thread3 = process.AddThread(0x1003);

  // Create a breakpoint that targets the second thread.
  // Registering the breakpoint should only add one more install

  debug_ipc::BreakpointSettings settings2;
  settings2.locations.push_back(CreateLocation(process, thread2, kAddress));

  Breakpoint breakpoint2(&process_delegate);
  breakpoint2.SetSettings(debug_ipc::BreakpointType::kHardware, settings2);

  ASSERT_EQ(hw_breakpoint.RegisterBreakpoint(&breakpoint2), ZX_OK);
  ASSERT_EQ(hw_breakpoint.breakpoints().size(), 2u);
  ASSERT_TRUE(ContainsKoids(hw_breakpoint, {thread1->koid(), thread2->koid()}));

  ASSERT_EQ(arch_provider->installs().size(), 3u);
  EXPECT_EQ(arch_provider->installs()[2].first, thread2->koid());
  EXPECT_EQ(arch_provider->installs()[2].second, kAddress);
  EXPECT_EQ(arch_provider->uninstalls().size(), 1u);

  // Removing the first breakpoint should remove the first install.
  // Returning true means that there are more breakpoints registered.

  ASSERT_TRUE(hw_breakpoint.UnregisterBreakpoint(&breakpoint1));
  ASSERT_EQ(hw_breakpoint.breakpoints().size(), 1u);
  ASSERT_TRUE(ContainsKoids(hw_breakpoint, {thread2->koid()}));

  ASSERT_EQ(arch_provider->installs().size(), 3u);
  EXPECT_EQ(arch_provider->uninstalls().size(), 2u);
  EXPECT_EQ(arch_provider->uninstalls()[1].first, thread1->koid());
  EXPECT_EQ(arch_provider->uninstalls()[1].second, kAddress);

  // Add a location for another address to the already bound breakpoint.
  // Add a location to the already bound breakpoint.

  settings2.locations.push_back(CreateLocation(process, thread2, kAddress + 0x8000));
  settings2.locations.push_back(CreateLocation(process, thread3, kAddress));
  breakpoint2.SetSettings(debug_ipc::BreakpointType::kHardware, settings2);

  // Updating should've only installed for the third thread.

  ASSERT_EQ(hw_breakpoint.Update(), ZX_OK);
  ASSERT_TRUE(ContainsKoids(hw_breakpoint, {thread2->koid(), thread3->koid()}));

  ASSERT_EQ(arch_provider->installs().size(), 4u);
  EXPECT_EQ(arch_provider->installs()[3].first, thread3->koid());
  EXPECT_EQ(arch_provider->installs()[3].second, kAddress);
  EXPECT_EQ(arch_provider->uninstalls().size(), 2u);

  // Create moar threads.

  MockThread* thread4 = process.AddThread(0x1004);
  MockThread* thread5 = process.AddThread(0x1005);
  MockThread* thread6 = process.AddThread(0x1006);

  // Create a breakpoint that spans all locations.

  debug_ipc::BreakpointSettings settings3;
  settings3.locations.push_back(CreateLocation(process, nullptr, kAddress));

  Breakpoint breakpoint3(&process_delegate);
  breakpoint3.SetSettings(debug_ipc::BreakpointType::kHardware, settings3);

  // Registering the breakpoint should add a breakpoint for all threads, but only updating the ones
  // that are not currently installed.

  ASSERT_EQ(hw_breakpoint.RegisterBreakpoint(&breakpoint3), ZX_OK);
  ASSERT_EQ(hw_breakpoint.breakpoints().size(), 2u);
  ASSERT_TRUE(ContainsKoids(hw_breakpoint, {thread1->koid(), thread2->koid(), thread3->koid(),
                                            thread4->koid(), thread5->koid(), thread6->koid()}));

  ASSERT_EQ(arch_provider->installs().size(), 8u);
  EXPECT_EQ(arch_provider->installs()[4].first, thread1->koid());
  EXPECT_EQ(arch_provider->installs()[4].second, kAddress);
  EXPECT_EQ(arch_provider->installs()[5].first, thread4->koid());
  EXPECT_EQ(arch_provider->installs()[5].second, kAddress);
  EXPECT_EQ(arch_provider->installs()[6].first, thread5->koid());
  EXPECT_EQ(arch_provider->installs()[6].second, kAddress);
  EXPECT_EQ(arch_provider->installs()[7].first, thread6->koid());
  EXPECT_EQ(arch_provider->installs()[7].second, kAddress);
  EXPECT_EQ(arch_provider->uninstalls().size(), 2u);

  // Removing the other breakpoint should not remove installs.

  ASSERT_TRUE(hw_breakpoint.UnregisterBreakpoint(&breakpoint2));
  ASSERT_EQ(hw_breakpoint.breakpoints().size(), 1u);

  ASSERT_TRUE(ContainsKoids(hw_breakpoint, {thread1->koid(), thread2->koid(), thread3->koid(),
                                            thread4->koid(), thread5->koid(), thread6->koid()}));

  ASSERT_EQ(arch_provider->installs().size(), 8u);
  EXPECT_EQ(arch_provider->uninstalls().size(), 2u);

  // Removing the last breakpoint should remove all the installations.
  // Returns false because there are no more registered breakpoints.

  ASSERT_FALSE(hw_breakpoint.UnregisterBreakpoint(&breakpoint3));
  ASSERT_EQ(hw_breakpoint.breakpoints().size(), 0u);

  ASSERT_TRUE(ContainsKoids(hw_breakpoint, {}));

  ASSERT_EQ(arch_provider->installs().size(), 8u);
  EXPECT_EQ(arch_provider->uninstalls().size(), 8u);
  EXPECT_EQ(arch_provider->uninstalls()[2].first, thread1->koid());
  EXPECT_EQ(arch_provider->uninstalls()[2].second, kAddress);
  EXPECT_EQ(arch_provider->uninstalls()[3].first, thread2->koid());
  EXPECT_EQ(arch_provider->uninstalls()[3].second, kAddress);
  EXPECT_EQ(arch_provider->uninstalls()[4].first, thread3->koid());
  EXPECT_EQ(arch_provider->uninstalls()[4].second, kAddress);
  EXPECT_EQ(arch_provider->uninstalls()[5].first, thread4->koid());
  EXPECT_EQ(arch_provider->uninstalls()[5].second, kAddress);
  EXPECT_EQ(arch_provider->uninstalls()[6].first, thread5->koid());
  EXPECT_EQ(arch_provider->uninstalls()[6].second, kAddress);
  EXPECT_EQ(arch_provider->uninstalls()[7].first, thread6->koid());
  EXPECT_EQ(arch_provider->uninstalls()[7].second, kAddress);
}

}  // namespace
}  // namespace debug_agent
