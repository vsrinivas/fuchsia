// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "src/developer/debug/debug_agent/watchpoint.h"

#include <gtest/gtest.h>

#include "src/developer/debug/debug_agent/breakpoint.h"
#include "src/developer/debug/debug_agent/mock_process.h"
#include "src/developer/debug/debug_agent/mock_thread.h"

namespace debug_agent {
namespace {

using AddressRange = ::debug_ipc::AddressRange;

class MockProcessDelegate : public Breakpoint::ProcessDelegate {
 public:
  zx_status_t RegisterBreakpoint(Breakpoint* bp, zx_koid_t process_koid,
                                 uint64_t address) override {
    return ZX_OK;
  }
  void UnregisterBreakpoint(Breakpoint* bp, zx_koid_t process_koid, uint64_t address) override {}
};

class MockArchProvider : public arch::ArchProvider {
 public:
  arch::WatchpointInstallationResult InstallWatchpoint(zx::thread* thread,
                                                       const AddressRange& range) override {
    installs_.push_back({thread->get(), range});
    return arch::WatchpointInstallationResult(ZX_OK, range_to_return_, slot_to_return_);
  }

  zx_status_t UninstallWatchpoint(zx::thread* thread, const AddressRange& range) override {
    uninstalls_.push_back({thread->get(), range});
    return ZX_OK;
  }

  const std::vector<std::pair<zx_koid_t, AddressRange>>& installs() const { return installs_; }
  const std::vector<std::pair<zx_koid_t, AddressRange>>& uninstalls() const { return uninstalls_; }

  void set_range_to_return(debug_ipc::AddressRange r) { range_to_return_ = r; }
  void set_slot_to_return(int slot) { slot_to_return_ = slot; }

 private:
  std::vector<std::pair<zx_koid_t, AddressRange>> installs_;
  std::vector<std::pair<zx_koid_t, AddressRange>> uninstalls_;

  debug_ipc::AddressRange range_to_return_;
  int slot_to_return_ = 0;
};

// If |thread| is null, it means a process-wide breakpoint.
debug_ipc::ProcessBreakpointSettings CreateLocation(const MockProcess& process,
                                                    const MockThread* thread,
                                                    const debug_ipc::AddressRange& range) {
  debug_ipc::ProcessBreakpointSettings location = {};
  location.process_koid = process.koid();
  if (thread)
    location.thread_koid = thread->koid();
  location.address = 0;
  location.address_range = range;

  return location;
}

bool ContainsKoids(const Watchpoint& watchpoint, const std::vector<zx_koid_t>& koids) {
  auto& installed_koids = watchpoint.installed_threads();
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

// Tests -------------------------------------------------------------------------------------------

const AddressRange kAddressRange = {0x1000, 0x2000};

TEST(Watchpoint, SimpleInstallAndRemove) {
  auto arch_provider = std::make_shared<MockArchProvider>();
  auto object_provider = std::make_shared<ObjectProvider>();

  MockProcess process(nullptr, 0x1, "process", arch_provider, object_provider);
  MockThread* thread1 = process.AddThread(0x1001);

  MockProcessDelegate process_delegate;

  debug_ipc::BreakpointSettings settings;
  settings.locations.push_back(CreateLocation(process, thread1, kAddressRange));

  Breakpoint breakpoint1(&process_delegate);
  breakpoint1.SetSettings(debug_ipc::BreakpointType::kWatchpoint, settings);

  Watchpoint watchpoint(&breakpoint1, &process, arch_provider, kAddressRange);
  ASSERT_EQ(watchpoint.breakpoints().size(), 1u);

  // Update should install one thread
  ASSERT_EQ(watchpoint.Update(), ZX_OK);
  ASSERT_TRUE(ContainsKoids(watchpoint, {thread1->koid()}));

  ASSERT_EQ(arch_provider->installs().size(), 1u);
  EXPECT_EQ(arch_provider->installs()[0].first, thread1->koid());
  EXPECT_EQ(arch_provider->installs()[0].second, kAddressRange);
  EXPECT_EQ(arch_provider->uninstalls().size(), 0u);

  // Binding again to the same breakpoint should fail.

  ASSERT_EQ(watchpoint.RegisterBreakpoint(&breakpoint1), ZX_ERR_ALREADY_BOUND);
  ASSERT_EQ(watchpoint.breakpoints().size(), 1u);

  // Unregistering the breakpoint should issue an uninstall. As there are no more breakpoints,
  // unregistering should return false.

  ASSERT_FALSE(watchpoint.UnregisterBreakpoint(&breakpoint1));
  ASSERT_EQ(watchpoint.breakpoints().size(), 0u);
  ASSERT_TRUE(ContainsKoids(watchpoint, {}));

  ASSERT_EQ(arch_provider->installs().size(), 1u);
  EXPECT_EQ(arch_provider->uninstalls().size(), 1u);
  EXPECT_EQ(arch_provider->uninstalls()[0].first, thread1->koid());
  EXPECT_EQ(arch_provider->uninstalls()[0].second, kAddressRange);

  // Registering again should add the breakpoint again.

  ASSERT_EQ(watchpoint.RegisterBreakpoint(&breakpoint1), ZX_OK);
  ASSERT_EQ(watchpoint.breakpoints().size(), 1u);
  ASSERT_TRUE(ContainsKoids(watchpoint, {thread1->koid()}));

  ASSERT_EQ(arch_provider->installs().size(), 2u);
  EXPECT_EQ(arch_provider->installs()[1].first, thread1->koid());
  EXPECT_EQ(arch_provider->installs()[1].second, kAddressRange);
  EXPECT_EQ(arch_provider->uninstalls().size(), 1u);

  // Create two other threads.

  MockThread* thread2 = process.AddThread(0x1002);
  MockThread* thread3 = process.AddThread(0x1003);

  // Create a breakpoint that targets the second thread with the correct size.
  // Registering the breakpoint should only add one more install.

  debug_ipc::BreakpointSettings settings2;
  settings2.locations.push_back(CreateLocation(process, thread2, kAddressRange));
  debug_ipc::AddressRange address_range2(kAddressRange.begin(), kAddressRange.end() + 8);
  settings2.locations.push_back(CreateLocation(process, thread3, address_range2));

  Breakpoint breakpoint2(&process_delegate);
  breakpoint2.SetSettings(debug_ipc::BreakpointType::kWatchpoint, settings2);

  ASSERT_EQ(watchpoint.RegisterBreakpoint(&breakpoint2), ZX_OK);
  ASSERT_EQ(watchpoint.breakpoints().size(), 2u);
  ASSERT_TRUE(ContainsKoids(watchpoint, {thread1->koid(), thread2->koid()}));

  ASSERT_EQ(arch_provider->installs().size(), 3u);
  EXPECT_EQ(arch_provider->installs()[2].first, thread2->koid());
  EXPECT_EQ(arch_provider->installs()[2].second, kAddressRange);
  EXPECT_EQ(arch_provider->uninstalls().size(), 1u);

  // Removing the first breakpoint should remove the first install.
  // Returning true means that there are more breakpoints registered.

  ASSERT_TRUE(watchpoint.UnregisterBreakpoint(&breakpoint1));
  ASSERT_EQ(watchpoint.breakpoints().size(), 1u);
  ASSERT_TRUE(ContainsKoids(watchpoint, {thread2->koid()}));

  ASSERT_EQ(arch_provider->installs().size(), 3u);
  EXPECT_EQ(arch_provider->uninstalls().size(), 2u);
  EXPECT_EQ(arch_provider->uninstalls()[1].first, thread1->koid());
  EXPECT_EQ(arch_provider->uninstalls()[1].second, kAddressRange);

  // Add a location for another address to the already bound breakpoint.
  // Add a location to the already bound breakpoint.

  settings2.locations.push_back(CreateLocation(process, thread3, kAddressRange));
  breakpoint2.SetSettings(debug_ipc::BreakpointType::kWatchpoint, settings2);

  // Updating should've only installed for the third thread.

  ASSERT_EQ(watchpoint.Update(), ZX_OK);
  ASSERT_TRUE(ContainsKoids(watchpoint, {thread2->koid(), thread3->koid()}));

  ASSERT_EQ(arch_provider->installs().size(), 4u);
  EXPECT_EQ(arch_provider->installs()[3].first, thread3->koid());
  EXPECT_EQ(arch_provider->installs()[3].second, kAddressRange);
  EXPECT_EQ(arch_provider->uninstalls().size(), 2u);

  // Create moar threads.

  MockThread* thread4 = process.AddThread(0x1004);
  MockThread* thread5 = process.AddThread(0x1005);
  MockThread* thread6 = process.AddThread(0x1006);

  // Create a breakpoint that spans all locations.

  debug_ipc::BreakpointSettings settings3;
  settings3.locations.push_back(CreateLocation(process, nullptr, kAddressRange));

  Breakpoint breakpoint3(&process_delegate);
  breakpoint3.SetSettings(debug_ipc::BreakpointType::kWatchpoint, settings3);

  // Registering the breakpoint should add a breakpoint for all threads, but only updating the ones
  // that are not currently installed.

  ASSERT_EQ(watchpoint.RegisterBreakpoint(&breakpoint3), ZX_OK);
  ASSERT_EQ(watchpoint.breakpoints().size(), 2u);
  ASSERT_TRUE(ContainsKoids(watchpoint, {thread1->koid(), thread2->koid(), thread3->koid(),
                                         thread4->koid(), thread5->koid(), thread6->koid()}));

  ASSERT_EQ(arch_provider->installs().size(), 8u);
  EXPECT_EQ(arch_provider->installs()[4].first, thread1->koid());
  EXPECT_EQ(arch_provider->installs()[4].second, kAddressRange);
  EXPECT_EQ(arch_provider->installs()[5].first, thread4->koid());
  EXPECT_EQ(arch_provider->installs()[5].second, kAddressRange);
  EXPECT_EQ(arch_provider->installs()[6].first, thread5->koid());
  EXPECT_EQ(arch_provider->installs()[6].second, kAddressRange);
  EXPECT_EQ(arch_provider->installs()[7].first, thread6->koid());
  EXPECT_EQ(arch_provider->installs()[7].second, kAddressRange);
  EXPECT_EQ(arch_provider->uninstalls().size(), 2u);

  // Removing the other breakpoint should not remove installs.

  ASSERT_TRUE(watchpoint.UnregisterBreakpoint(&breakpoint2));
  ASSERT_EQ(watchpoint.breakpoints().size(), 1u);

  ASSERT_TRUE(ContainsKoids(watchpoint, {thread1->koid(), thread2->koid(), thread3->koid(),
                                         thread4->koid(), thread5->koid(), thread6->koid()}));

  ASSERT_EQ(arch_provider->installs().size(), 8u);
  EXPECT_EQ(arch_provider->uninstalls().size(), 2u);

  // Removing the last breakpoint should remove all the installations.
  // Returns false because there are no more registered breakpoints.

  ASSERT_FALSE(watchpoint.UnregisterBreakpoint(&breakpoint3));
  ASSERT_EQ(watchpoint.breakpoints().size(), 0u);

  ASSERT_TRUE(ContainsKoids(watchpoint, {}));

  ASSERT_EQ(arch_provider->installs().size(), 8u);
  EXPECT_EQ(arch_provider->uninstalls().size(), 8u);
  EXPECT_EQ(arch_provider->uninstalls()[2].first, thread1->koid());
  EXPECT_EQ(arch_provider->uninstalls()[2].second, kAddressRange);
  EXPECT_EQ(arch_provider->uninstalls()[3].first, thread2->koid());
  EXPECT_EQ(arch_provider->uninstalls()[3].second, kAddressRange);
  EXPECT_EQ(arch_provider->uninstalls()[4].first, thread3->koid());
  EXPECT_EQ(arch_provider->uninstalls()[4].second, kAddressRange);
  EXPECT_EQ(arch_provider->uninstalls()[5].first, thread4->koid());
  EXPECT_EQ(arch_provider->uninstalls()[5].second, kAddressRange);
  EXPECT_EQ(arch_provider->uninstalls()[6].first, thread5->koid());
  EXPECT_EQ(arch_provider->uninstalls()[6].second, kAddressRange);
  EXPECT_EQ(arch_provider->uninstalls()[7].first, thread6->koid());
  EXPECT_EQ(arch_provider->uninstalls()[7].second, kAddressRange);
}

TEST(Watchpoint, InstalledRanges) {
  auto arch_provider = std::make_shared<MockArchProvider>();
  auto object_provider = std::make_shared<ObjectProvider>();

  MockProcess process(nullptr, 0x1, "process", arch_provider, object_provider);
  MockThread* thread1 = process.AddThread(0x1001);

  MockProcessDelegate process_delegate;

  debug_ipc::BreakpointSettings settings;
  settings.locations.push_back(CreateLocation(process, thread1, kAddressRange));

  Breakpoint breakpoint1(&process_delegate);
  breakpoint1.SetSettings(debug_ipc::BreakpointType::kWatchpoint, settings);

  Watchpoint watchpoint(&breakpoint1, &process, arch_provider, kAddressRange);
  ASSERT_EQ(watchpoint.breakpoints().size(), 1u);

  const debug_ipc::AddressRange kSubRange = {0x900, 0x2100};
  constexpr int kSlot = 1;
  arch_provider->set_range_to_return(kSubRange);
  arch_provider->set_slot_to_return(kSlot);

  // Update should install one thread
  ASSERT_EQ(watchpoint.Update(), ZX_OK);
  ASSERT_TRUE(ContainsKoids(watchpoint, {thread1->koid()}));

  // There should be an install with a different sub range.
  ASSERT_EQ(arch_provider->installs().size(), 1u);
  EXPECT_EQ(arch_provider->installs()[0].first, thread1->koid());
  EXPECT_EQ(arch_provider->installs()[0].second, kAddressRange);
  EXPECT_EQ(arch_provider->uninstalls().size(), 0u);

  // The installed and actual range should be differnt.
  {
    auto& installations = watchpoint.installed_threads();

    ASSERT_EQ(installations.size(), 1u);

    auto it = installations.find(thread1->koid());
    ASSERT_NE(it, installations.end());
    EXPECT_EQ(it->second.status, ZX_OK) << zx_status_get_string(it->second.status);
    EXPECT_EQ(it->second.installed_range, kSubRange);
    EXPECT_EQ(it->second.slot, kSlot);
  }
}

}  // namespace
}  // namespace debug_agent
