// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "src/developer/debug/debug_agent/watchpoint.h"

#include <zircon/status.h>

#include <gtest/gtest.h>

#include "src/developer/debug/debug_agent/arch.h"
#include "src/developer/debug/debug_agent/breakpoint.h"
#include "src/developer/debug/debug_agent/mock_process.h"
#include "src/developer/debug/debug_agent/mock_thread.h"

namespace debug_agent {
namespace {

using AddressRange = ::debug::AddressRange;

class MockProcessDelegate : public Breakpoint::ProcessDelegate {
 public:
  debug::Status RegisterBreakpoint(Breakpoint*, zx_koid_t, uint64_t) override {
    return debug::Status();
  }
  void UnregisterBreakpoint(Breakpoint* bp, zx_koid_t process_koid, uint64_t address) override {}

  debug::Status RegisterWatchpoint(Breakpoint*, zx_koid_t, const debug::AddressRange&) override {
    return debug::Status();
  }
  void UnregisterWatchpoint(Breakpoint*, zx_koid_t, const debug::AddressRange&) override {}
};

// If |thread| is null, it means a process-wide breakpoint.
debug_ipc::ProcessBreakpointSettings CreateLocation(const MockProcess& process,
                                                    const MockThread* thread,
                                                    const debug::AddressRange& range) {
  debug_ipc::ProcessBreakpointSettings location = {};
  location.id.process = process.koid();
  if (thread)
    location.id.thread = thread->koid();
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
  MockProcess process(nullptr, 0x1, "process");
  MockThread* thread1 = process.AddThread(0x1001);

  MockProcessDelegate process_delegate;

  debug_ipc::BreakpointSettings settings;
  settings.type = debug_ipc::BreakpointType::kWrite;
  settings.locations.push_back(CreateLocation(process, thread1, kAddressRange));

  Breakpoint breakpoint1(&process_delegate);
  breakpoint1.SetSettings(settings);

  Watchpoint watchpoint(debug_ipc::BreakpointType::kWrite, &breakpoint1, &process, kAddressRange);
  ASSERT_EQ(watchpoint.breakpoints().size(), 1u);

  // Update should install one thread
  ASSERT_TRUE(watchpoint.Update().ok());
  ASSERT_TRUE(ContainsKoids(watchpoint, {thread1->koid()}));

  EXPECT_EQ(thread1->mock_thread_handle().WatchpointInstallCount(kAddressRange), 1u);
  EXPECT_EQ(thread1->mock_thread_handle().TotalWatchpointUninstallCalls(), 0u);

  // Binding again to the same breakpoint should fail.

  ASSERT_TRUE(watchpoint.RegisterBreakpoint(&breakpoint1).has_error());
  ASSERT_EQ(watchpoint.breakpoints().size(), 1u);

  // Unregistering the breakpoint should issue an uninstall. As there are no more breakpoints,
  // unregistering should return false.

  ASSERT_FALSE(watchpoint.UnregisterBreakpoint(&breakpoint1));
  ASSERT_EQ(watchpoint.breakpoints().size(), 0u);
  ASSERT_TRUE(ContainsKoids(watchpoint, {}));

  EXPECT_EQ(thread1->mock_thread_handle().WatchpointUninstallCount(kAddressRange), 1u);

  // Registering again should add the breakpoint again.

  ASSERT_TRUE(watchpoint.RegisterBreakpoint(&breakpoint1).ok());
  ASSERT_EQ(watchpoint.breakpoints().size(), 1u);
  ASSERT_TRUE(ContainsKoids(watchpoint, {thread1->koid()}));

  EXPECT_EQ(thread1->mock_thread_handle().WatchpointInstallCount(kAddressRange), 2u);

  // Create two other threads.

  MockThread* thread2 = process.AddThread(0x1002);
  MockThread* thread3 = process.AddThread(0x1003);

  // Create a breakpoint that targets the second thread with the correct size.
  // Registering the breakpoint should only add one more install.

  debug_ipc::BreakpointSettings settings2;
  settings2.type = debug_ipc::BreakpointType::kWrite;
  settings2.locations.push_back(CreateLocation(process, thread2, kAddressRange));
  debug::AddressRange address_range2(kAddressRange.begin(), kAddressRange.end() + 8);
  settings2.locations.push_back(CreateLocation(process, thread3, address_range2));

  Breakpoint breakpoint2(&process_delegate);
  breakpoint2.SetSettings(settings2);

  ASSERT_TRUE(watchpoint.RegisterBreakpoint(&breakpoint2).ok());
  ASSERT_EQ(watchpoint.breakpoints().size(), 2u);
  ASSERT_TRUE(ContainsKoids(watchpoint, {thread1->koid(), thread2->koid()}));
  EXPECT_EQ(thread2->mock_thread_handle().WatchpointInstallCount(kAddressRange), 1u);

  // Removing the first breakpoint should remove the first install.
  // Returning true means that there are more breakpoints registered.

  ASSERT_TRUE(watchpoint.UnregisterBreakpoint(&breakpoint1));
  ASSERT_EQ(watchpoint.breakpoints().size(), 1u);
  ASSERT_TRUE(ContainsKoids(watchpoint, {thread2->koid()}));
  EXPECT_EQ(thread1->mock_thread_handle().WatchpointUninstallCount(kAddressRange), 2u);

  // Add a location for another address to the already bound breakpoint.
  // Add a location to the already bound breakpoint.

  settings2.locations.push_back(CreateLocation(process, thread3, kAddressRange));
  breakpoint2.SetSettings(settings2);

  // Updating should've only installed for the third thread.

  ASSERT_TRUE(watchpoint.Update().ok());
  ASSERT_TRUE(ContainsKoids(watchpoint, {thread2->koid(), thread3->koid()}));
  EXPECT_EQ(thread3->mock_thread_handle().WatchpointInstallCount(kAddressRange), 1u);

  // Create moar threads.

  MockThread* thread4 = process.AddThread(0x1004);
  MockThread* thread5 = process.AddThread(0x1005);
  MockThread* thread6 = process.AddThread(0x1006);

  // Create a breakpoint that spans all locations.

  debug_ipc::BreakpointSettings settings3;
  settings3.type = debug_ipc::BreakpointType::kWrite;
  settings3.locations.push_back(CreateLocation(process, nullptr, kAddressRange));

  Breakpoint breakpoint3(&process_delegate);
  breakpoint3.SetSettings(settings3);

  // Registering the breakpoint should add a breakpoint for all threads, but only updating the ones
  // that are not currently installed.

  ASSERT_TRUE(watchpoint.RegisterBreakpoint(&breakpoint3).ok());
  ASSERT_EQ(watchpoint.breakpoints().size(), 2u);
  ASSERT_TRUE(ContainsKoids(watchpoint, {thread1->koid(), thread2->koid(), thread3->koid(),
                                         thread4->koid(), thread5->koid(), thread6->koid()}));

  EXPECT_EQ(thread4->mock_thread_handle().WatchpointInstallCount(kAddressRange), 1u);
  EXPECT_EQ(thread5->mock_thread_handle().WatchpointInstallCount(kAddressRange), 1u);
  EXPECT_EQ(thread6->mock_thread_handle().WatchpointInstallCount(kAddressRange), 1u);

  // Removing the other breakpoint should not remove installs.

  ASSERT_TRUE(watchpoint.UnregisterBreakpoint(&breakpoint2));
  ASSERT_EQ(watchpoint.breakpoints().size(), 1u);

  ASSERT_TRUE(ContainsKoids(watchpoint, {thread1->koid(), thread2->koid(), thread3->koid(),
                                         thread4->koid(), thread5->koid(), thread6->koid()}));

  EXPECT_EQ(thread1->mock_thread_handle().TotalWatchpointUninstallCalls(), 2u);
  EXPECT_EQ(thread3->mock_thread_handle().TotalWatchpointUninstallCalls(), 0u);
  EXPECT_EQ(thread4->mock_thread_handle().TotalWatchpointUninstallCalls(), 0u);
  EXPECT_EQ(thread5->mock_thread_handle().TotalWatchpointUninstallCalls(), 0u);
  EXPECT_EQ(thread6->mock_thread_handle().TotalWatchpointUninstallCalls(), 0u);

  // Removing the last breakpoint should remove all the installations.
  // Returns false because there are no more registered breakpoints.

  ASSERT_FALSE(watchpoint.UnregisterBreakpoint(&breakpoint3));
  ASSERT_EQ(watchpoint.breakpoints().size(), 0u);

  ASSERT_TRUE(ContainsKoids(watchpoint, {}));

  EXPECT_EQ(thread1->mock_thread_handle().TotalWatchpointUninstallCalls(), 3u);
  EXPECT_EQ(thread2->mock_thread_handle().TotalWatchpointUninstallCalls(), 1u);
  EXPECT_EQ(thread3->mock_thread_handle().TotalWatchpointUninstallCalls(), 1u);
  EXPECT_EQ(thread4->mock_thread_handle().TotalWatchpointUninstallCalls(), 1u);
  EXPECT_EQ(thread5->mock_thread_handle().TotalWatchpointUninstallCalls(), 1u);
  EXPECT_EQ(thread6->mock_thread_handle().TotalWatchpointUninstallCalls(), 1u);
}

TEST(Watchpoint, InstalledRanges) {
  MockProcess process(nullptr, 0x1, "process");
  MockThread* thread1 = process.AddThread(0x1001);

  MockProcessDelegate process_delegate;

  debug_ipc::BreakpointSettings settings;
  settings.type = debug_ipc::BreakpointType::kWrite;
  settings.locations.push_back(CreateLocation(process, thread1, kAddressRange));

  Breakpoint breakpoint1(&process_delegate);
  breakpoint1.SetSettings(settings);

  Watchpoint watchpoint(debug_ipc::BreakpointType::kWrite, &breakpoint1, &process, kAddressRange);
  ASSERT_EQ(watchpoint.breakpoints().size(), 1u);

  const debug::AddressRange kSubRange = {0x900, 0x2100};
  constexpr int kSlot = 1;
  thread1->mock_thread_handle().set_watchpoint_range_to_return(kSubRange);
  thread1->mock_thread_handle().set_watchpoint_slot_to_return(kSlot);

  // Update should install one thread
  ASSERT_TRUE(watchpoint.Update().ok());
  ASSERT_TRUE(ContainsKoids(watchpoint, {thread1->koid()}));

  // There should be an install with a different sub range.
  auto installs = thread1->mock_thread_handle().watchpoint_installs();
  ASSERT_EQ(installs.size(), 1u);
  EXPECT_EQ(installs[0].address_range, kAddressRange);
  EXPECT_EQ(installs[0].type, debug_ipc::BreakpointType::kWrite);
  EXPECT_EQ(thread1->mock_thread_handle().TotalWatchpointUninstallCalls(), 0u);

  // The installed and actual range should be differnt.
  {
    auto& installations = watchpoint.installed_threads();

    ASSERT_EQ(installations.size(), 1u);

    auto it = installations.find(thread1->koid());
    ASSERT_NE(it, installations.end());
    EXPECT_EQ(it->second.range, kSubRange);
    EXPECT_EQ(it->second.slot, kSlot);
  }
}

TEST(Watchpoint, MatchesException) {
  MockProcess process(nullptr, 0x1, "process");
  MockThread* thread1 = process.AddThread(0x1001);
  MockThread* thread2 = process.AddThread(0x1002);

  MockProcessDelegate process_delegate;

  debug_ipc::BreakpointSettings settings;
  settings.type = debug_ipc::BreakpointType::kWrite;
  settings.locations.push_back(CreateLocation(process, thread1, kAddressRange));

  Breakpoint breakpoint1(&process_delegate);
  breakpoint1.SetSettings(settings);

  Watchpoint watchpoint(debug_ipc::BreakpointType::kWrite, &breakpoint1, &process, kAddressRange);
  ASSERT_EQ(watchpoint.breakpoints().size(), 1u);

  const debug::AddressRange kSubRange = {0x900, 0x2100};
  constexpr int kSlot = 1;
  thread1->mock_thread_handle().set_watchpoint_range_to_return(kSubRange);
  thread1->mock_thread_handle().set_watchpoint_slot_to_return(kSlot);

  // Update should install one thread
  ASSERT_TRUE(watchpoint.Update().ok());
  ASSERT_TRUE(ContainsKoids(watchpoint, {thread1->koid()}));

  // There should be an install with a different sub range.
  auto installs = thread1->mock_thread_handle().watchpoint_installs();
  ASSERT_EQ(installs.size(), 1u);
  EXPECT_EQ(installs[0].address_range, kAddressRange);
  EXPECT_EQ(thread1->mock_thread_handle().TotalWatchpointUninstallCalls(), 0u);

  // The installed and actual range should be differnt.
  {
    auto& installations = watchpoint.installed_threads();

    ASSERT_EQ(installations.size(), 1u);

    auto it = installations.find(thread1->koid());
    ASSERT_NE(it, installations.end());
    EXPECT_EQ(it->second.range, kSubRange);
    EXPECT_EQ(it->second.slot, kSlot);

    // Only same slot and within range should work.
    EXPECT_FALSE(watchpoint.MatchesException(thread1->koid(), kSubRange.begin() - 1, kSlot));
    EXPECT_TRUE(watchpoint.MatchesException(thread1->koid(), kSubRange.begin(), kSlot));
    EXPECT_TRUE(watchpoint.MatchesException(thread1->koid(), kSubRange.begin() + 1, kSlot));
    EXPECT_TRUE(watchpoint.MatchesException(thread1->koid(), kSubRange.end() - 1, kSlot));
    EXPECT_FALSE(watchpoint.MatchesException(thread1->koid(), kSubRange.end(), kSlot));

    // Different slot should fail.
    EXPECT_FALSE(watchpoint.MatchesException(thread1->koid(), kSubRange.begin(), kSlot + 1));

    // Different thread should fail.
    EXPECT_FALSE(watchpoint.MatchesException(thread2->koid(), kSubRange.begin(), kSlot));
  }
}

TEST(Watchpoint, DifferentTypes) {
  MockProcess process(nullptr, 0x1, "process");
  MockThread* thread1 = process.AddThread(0x1001);

  MockProcessDelegate process_delegate;

  debug_ipc::BreakpointSettings settings;
  settings.locations.push_back(CreateLocation(process, thread1, kAddressRange));

  {
    Breakpoint breakpoint1(&process_delegate);
    settings.type = debug_ipc::BreakpointType::kWrite;
    breakpoint1.SetSettings(settings);

    Watchpoint watchpoint(debug_ipc::BreakpointType::kWrite, &breakpoint1, &process, kAddressRange);
    ASSERT_EQ(watchpoint.breakpoints().size(), 1u);

    ASSERT_TRUE(watchpoint.Update().ok());
    ASSERT_TRUE(ContainsKoids(watchpoint, {thread1->koid()}));

    // There should be an install with a different sub range.
    auto installs = thread1->mock_thread_handle().watchpoint_installs();
    ASSERT_EQ(installs.size(), 1u);
    EXPECT_EQ(installs[0].address_range, kAddressRange);
    EXPECT_EQ(installs[0].type, debug_ipc::BreakpointType::kWrite);
  }

  {
    Breakpoint breakpoint1(&process_delegate);
    settings.type = debug_ipc::BreakpointType::kReadWrite;
    breakpoint1.SetSettings(settings);

    Watchpoint watchpoint(debug_ipc::BreakpointType::kReadWrite, &breakpoint1, &process,
                          kAddressRange);
    ASSERT_EQ(watchpoint.breakpoints().size(), 1u);

    ASSERT_TRUE(watchpoint.Update().ok());
    ASSERT_TRUE(ContainsKoids(watchpoint, {thread1->koid()}));

    // There should be an install with a different sub range.
    auto installs = thread1->mock_thread_handle().watchpoint_installs();
    ASSERT_EQ(installs.size(), 2u);
    EXPECT_EQ(installs[1].address_range, kAddressRange);
    EXPECT_EQ(installs[1].type, debug_ipc::BreakpointType::kReadWrite);
  }
}

}  // namespace
}  // namespace debug_agent
