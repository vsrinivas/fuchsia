// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "src/developer/debug/debug_agent/hardware_breakpoint.h"

#include <gtest/gtest.h>

#include "src/developer/debug/debug_agent/arch.h"
#include "src/developer/debug/debug_agent/breakpoint.h"
#include "src/developer/debug/debug_agent/mock_process.h"
#include "src/developer/debug/debug_agent/mock_thread.h"
#include "src/developer/debug/debug_agent/test_utils.h"

namespace debug_agent {
namespace {

class MockProcessDelegate : public Breakpoint::ProcessDelegate {
 public:
  debug::Status RegisterBreakpoint(Breakpoint* bp, zx_koid_t process_koid,
                                   uint64_t address) override {
    return debug::Status();
  }
  void UnregisterBreakpoint(Breakpoint* bp, zx_koid_t process_koid, uint64_t address) override {}
};

// If |thread| is null, it means a process-wide breakpoint.
debug_ipc::ProcessBreakpointSettings CreateLocation(const MockProcess& process,
                                                    const MockThread* thread, uint64_t address) {
  debug_ipc::ProcessBreakpointSettings location = {};
  location.id.process = process.koid();
  if (thread)
    location.id.thread = thread->koid();
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
  MockProcess process(nullptr, 0x1, "process");
  MockThread* thread1 = process.AddThread(0x1001);

  MockProcessDelegate process_delegate;

  debug_ipc::BreakpointSettings settings;
  settings.type = debug_ipc::BreakpointType::kHardware;
  settings.locations.push_back(CreateLocation(process, thread1, kAddress));

  Breakpoint breakpoint1(&process_delegate);
  breakpoint1.SetSettings(settings);

  HardwareBreakpoint hw_breakpoint(&breakpoint1, &process, kAddress);
  ASSERT_EQ(hw_breakpoint.breakpoints().size(), 1u);

  // Update should install one thread

  ASSERT_TRUE(hw_breakpoint.Update().ok());
  ASSERT_TRUE(ContainsKoids(hw_breakpoint, {thread1->koid()}));

  EXPECT_EQ(thread1->mock_thread_handle().BreakpointInstallCount(kAddress), 1u);
  EXPECT_EQ(thread1->mock_thread_handle().TotalBreakpointUninstallCalls(), 0u);

  // Binding again to the same breakpoint should fail.

  ASSERT_TRUE(hw_breakpoint.RegisterBreakpoint(&breakpoint1).has_error());
  ASSERT_EQ(hw_breakpoint.breakpoints().size(), 1u);

  // Unregistering the breakpoint should issue an uninstall. As there are no more breakpoints,
  // unregistering should return false.

  ASSERT_FALSE(hw_breakpoint.UnregisterBreakpoint(&breakpoint1));
  ASSERT_EQ(hw_breakpoint.breakpoints().size(), 0u);
  ASSERT_TRUE(ContainsKoids(hw_breakpoint, {}));

  // Same number of installs, now have uninstall for the address.
  ASSERT_EQ(thread1->mock_thread_handle().TotalBreakpointInstallCalls(), 1u);
  EXPECT_EQ(thread1->mock_thread_handle().BreakpointUninstallCount(kAddress), 1u);

  // Registering again should add the breakpoint

  ASSERT_TRUE(hw_breakpoint.RegisterBreakpoint(&breakpoint1).ok());
  ASSERT_EQ(hw_breakpoint.breakpoints().size(), 1u);
  ASSERT_TRUE(ContainsKoids(hw_breakpoint, {thread1->koid()}));

  EXPECT_EQ(thread1->mock_thread_handle().BreakpointInstallCount(kAddress), 2u);
  EXPECT_EQ(thread1->mock_thread_handle().TotalBreakpointUninstallCalls(), 1u);

  // Create two other threads

  MockThread* thread2 = process.AddThread(0x1002);
  MockThread* thread3 = process.AddThread(0x1003);

  // Create a breakpoint that targets the second thread.
  // Registering the breakpoint should only add one more install

  debug_ipc::BreakpointSettings settings2;
  settings2.type = debug_ipc::BreakpointType::kHardware;
  settings2.locations.push_back(CreateLocation(process, thread2, kAddress));

  Breakpoint breakpoint2(&process_delegate);
  breakpoint2.SetSettings(settings2);

  ASSERT_TRUE(hw_breakpoint.RegisterBreakpoint(&breakpoint2).ok());
  ASSERT_EQ(hw_breakpoint.breakpoints().size(), 2u);
  ASSERT_TRUE(ContainsKoids(hw_breakpoint, {thread1->koid(), thread2->koid()}));

  EXPECT_EQ(thread2->mock_thread_handle().BreakpointInstallCount(kAddress), 1u);
  EXPECT_EQ(thread2->mock_thread_handle().TotalBreakpointUninstallCalls(), 0u);

  // Removing the first breakpoint should remove the first install.
  // Returning true means that there are more breakpoints registered.

  ASSERT_TRUE(hw_breakpoint.UnregisterBreakpoint(&breakpoint1));
  ASSERT_EQ(hw_breakpoint.breakpoints().size(), 1u);
  ASSERT_TRUE(ContainsKoids(hw_breakpoint, {thread2->koid()}));

  EXPECT_EQ(thread1->mock_thread_handle().BreakpointUninstallCount(kAddress), 2u);

  // Add a location for another address to the already bound breakpoint.
  // Add a location to the already bound breakpoint.

  settings2.locations.push_back(CreateLocation(process, thread2, kAddress + 0x8000));
  settings2.locations.push_back(CreateLocation(process, thread3, kAddress));
  breakpoint2.SetSettings(settings2);

  // Updating should've only installed for the third thread.

  ASSERT_TRUE(hw_breakpoint.Update().ok());
  ASSERT_TRUE(ContainsKoids(hw_breakpoint, {thread2->koid(), thread3->koid()}));

  EXPECT_EQ(thread3->mock_thread_handle().BreakpointInstallCount(kAddress), 1u);

  // Create moar threads.

  MockThread* thread4 = process.AddThread(0x1004);
  MockThread* thread5 = process.AddThread(0x1005);
  MockThread* thread6 = process.AddThread(0x1006);

  // Create a breakpoint that spans all locations.

  debug_ipc::BreakpointSettings settings3;
  settings3.type = debug_ipc::BreakpointType::kHardware;
  settings3.locations.push_back(CreateLocation(process, nullptr, kAddress));

  Breakpoint breakpoint3(&process_delegate);
  breakpoint3.SetSettings(settings3);

  // Registering the breakpoint should add a breakpoint for all threads, but only updating the ones
  // that are not currently installed.

  ASSERT_TRUE(hw_breakpoint.RegisterBreakpoint(&breakpoint3).ok());
  ASSERT_EQ(hw_breakpoint.breakpoints().size(), 2u);
  ASSERT_TRUE(ContainsKoids(hw_breakpoint, {thread1->koid(), thread2->koid(), thread3->koid(),
                                            thread4->koid(), thread5->koid(), thread6->koid()}));

  EXPECT_EQ(thread1->mock_thread_handle().BreakpointInstallCount(kAddress), 3u);
  EXPECT_EQ(thread4->mock_thread_handle().BreakpointInstallCount(kAddress), 1u);
  EXPECT_EQ(thread5->mock_thread_handle().BreakpointInstallCount(kAddress), 1u);
  EXPECT_EQ(thread6->mock_thread_handle().BreakpointInstallCount(kAddress), 1u);

  // Removing the other breakpoint should not remove installs.

  ASSERT_TRUE(hw_breakpoint.UnregisterBreakpoint(&breakpoint2));
  ASSERT_EQ(hw_breakpoint.breakpoints().size(), 1u);

  ASSERT_TRUE(ContainsKoids(hw_breakpoint, {thread1->koid(), thread2->koid(), thread3->koid(),
                                            thread4->koid(), thread5->koid(), thread6->koid()}));

  // Removing the last breakpoint should remove all the installations.
  // Returns false because there are no more registered breakpoints.

  ASSERT_FALSE(hw_breakpoint.UnregisterBreakpoint(&breakpoint3));
  ASSERT_EQ(hw_breakpoint.breakpoints().size(), 0u);

  ASSERT_TRUE(ContainsKoids(hw_breakpoint, {}));

  EXPECT_EQ(thread1->mock_thread_handle().BreakpointUninstallCount(kAddress), 3u);
  EXPECT_EQ(thread2->mock_thread_handle().BreakpointUninstallCount(kAddress), 1u);
  EXPECT_EQ(thread3->mock_thread_handle().BreakpointUninstallCount(kAddress), 1u);
  EXPECT_EQ(thread4->mock_thread_handle().BreakpointUninstallCount(kAddress), 1u);
  EXPECT_EQ(thread5->mock_thread_handle().BreakpointUninstallCount(kAddress), 1u);
  EXPECT_EQ(thread6->mock_thread_handle().BreakpointUninstallCount(kAddress), 1u);
}

TEST(HardwareBreakpoint, StepSimple) {
  constexpr zx_koid_t process_koid = 0x1234;
  const std::string process_name = "process";
  MockProcess process(nullptr, process_koid, process_name);

  MockProcessDelegate process_delegate;
  Breakpoint main_breakpoint(&process_delegate);

  debug_ipc::BreakpointSettings settings;
  settings.type = debug_ipc::BreakpointType::kHardware;
  settings.locations.push_back(CreateLocation(process, nullptr, kAddress));  // All threads.
  main_breakpoint.SetSettings(settings);

  // The step over strategy is as follows:
  // Thread 1, 2, 3 will hit the breakpoint and attempt a step over.
  // Thread 4 will remain oblivious to the breakpoint, as will 5.
  // Thread 5 is IsSuspended from the client, so it should not be resumed by the
  // agent during step over.

  constexpr zx_koid_t kThread1Koid = 1;
  MockThread* mock_thread1 = process.AddThread(kThread1Koid);

  HardwareBreakpoint bp(&main_breakpoint, &process, kAddress);
  ASSERT_TRUE(bp.Init().ok());

  // Should've installed the breakpoint.
  EXPECT_EQ(mock_thread1->mock_thread_handle().BreakpointInstallCount(kAddress), 1u);
  EXPECT_EQ(mock_thread1->mock_thread_handle().TotalBreakpointUninstallCalls(), 0u);

  // Hit the breakpoint ----------------------------------------------------------------------------

  bp.BeginStepOver(mock_thread1);

  // There should be an enqueued step over.
  ASSERT_EQ(process.step_over_queue().size(), 1u);
  ASSERT_EQ(process.step_over_queue()[0].process_breakpoint.get(), &bp);
  ASSERT_EQ(process.step_over_queue()[0].thread.get(), mock_thread1);

  // Thread should be running and stepping over.
  ASSERT_TRUE(mock_thread1->running());
  ASSERT_TRUE(mock_thread1->stepping_over_breakpoint());

  // Breakpoint should've been uninstalled for this thread.
  EXPECT_EQ(mock_thread1->mock_thread_handle().TotalBreakpointInstallCalls(), 1u);
  EXPECT_EQ(mock_thread1->mock_thread_handle().BreakpointUninstallCount(kAddress), 1u);

  // End the step over -----------------------------------------------------------------------------

  bp.EndStepOver(mock_thread1);

  ASSERT_EQ(process.step_over_queue().size(), 0u);

  // Thread should be running and stepping over.
  ASSERT_TRUE(mock_thread1->running());
  ASSERT_FALSE(mock_thread1->stepping_over_breakpoint());

  // It should've reinstalled the breakpoint for this thread.
  EXPECT_EQ(mock_thread1->mock_thread_handle().BreakpointInstallCount(kAddress), 2u);
  EXPECT_EQ(mock_thread1->mock_thread_handle().BreakpointUninstallCount(kAddress), 1u);
}

TEST(HardwareBreakpoint, MultipleSteps) {
  constexpr zx_koid_t process_koid = 0x1234;
  const std::string process_name = "process";
  MockProcess process(nullptr, process_koid, process_name);

  MockProcessDelegate process_delegate;
  Breakpoint main_breakpoint(&process_delegate);

  debug_ipc::BreakpointSettings settings;
  settings.type = debug_ipc::BreakpointType::kHardware;
  settings.locations.push_back(CreateLocation(process, nullptr, kAddress));  // All threads.
  main_breakpoint.SetSettings(settings);

  // The step over strategy is as follows:
  // Thread 1, 2, 3 will hit the breakpoint and attempt a step over.
  // Thread 4 will remain oblivious to the breakpoint, as will 5.
  // Thread 5 is IsSuspended from the client, so it should not be resumed by the
  // agent during step over.

  constexpr zx_koid_t kThread1Koid = 1;
  constexpr zx_koid_t kThread2Koid = 2;
  constexpr zx_koid_t kThread3Koid = 3;
  MockThread* mock_thread1 = process.AddThread(kThread1Koid);
  MockThread* mock_thread2 = process.AddThread(kThread2Koid);
  MockThread* mock_thread3 = process.AddThread(kThread3Koid);

  HardwareBreakpoint bp(&main_breakpoint, &process, kAddress);
  ASSERT_TRUE(bp.Init().ok());

  // Should've installed the breakpoint.
  EXPECT_EQ(mock_thread1->mock_thread_handle().BreakpointInstallCount(kAddress), 1u);
  EXPECT_EQ(mock_thread2->mock_thread_handle().BreakpointInstallCount(kAddress), 1u);
  EXPECT_EQ(mock_thread3->mock_thread_handle().BreakpointInstallCount(kAddress), 1u);
  EXPECT_EQ(mock_thread1->mock_thread_handle().TotalBreakpointUninstallCalls(), 0u);
  EXPECT_EQ(mock_thread2->mock_thread_handle().TotalBreakpointUninstallCalls(), 0u);
  EXPECT_EQ(mock_thread3->mock_thread_handle().TotalBreakpointUninstallCalls(), 0u);

  // Hit the breakpoint ----------------------------------------------------------------------------

  bp.BeginStepOver(mock_thread1);

  // There should be an enqueued step over.
  ASSERT_EQ(process.step_over_queue().size(), 1u);
  ASSERT_EQ(process.step_over_queue()[0].process_breakpoint.get(), &bp);
  ASSERT_EQ(process.step_over_queue()[0].thread.get(), mock_thread1);

  // Thread should be running and stepping over.
  ASSERT_TRUE(mock_thread1->stepping_over_breakpoint());
  ASSERT_FALSE(mock_thread2->stepping_over_breakpoint());
  ASSERT_FALSE(mock_thread3->stepping_over_breakpoint());

  // Breakpoint should've been uninstalled for this thread.
  EXPECT_EQ(mock_thread1->mock_thread_handle().TotalBreakpointInstallCalls(), 1u);
  EXPECT_EQ(mock_thread1->mock_thread_handle().BreakpointUninstallCount(kAddress), 1u);

  // Hit the breakpoint two more times -------------------------------------------------------------

  bp.BeginStepOver(mock_thread2);
  ASSERT_EQ(process.step_over_queue().size(), 2u);
  ASSERT_EQ(process.step_over_queue()[1].process_breakpoint.get(), &bp);
  ASSERT_EQ(process.step_over_queue()[1].thread.get(), mock_thread2);

  bp.BeginStepOver(mock_thread3);
  ASSERT_EQ(process.step_over_queue().size(), 3u);
  ASSERT_EQ(process.step_over_queue()[2].process_breakpoint.get(), &bp);
  ASSERT_EQ(process.step_over_queue()[2].thread.get(), mock_thread3);

  // End the step over -----------------------------------------------------------------------------

  bp.EndStepOver(mock_thread1);

  // Should've started the second step over.
  ASSERT_EQ(process.step_over_queue().size(), 2u);
  ASSERT_EQ(process.step_over_queue()[0].process_breakpoint.get(), &bp);
  ASSERT_EQ(process.step_over_queue()[0].thread.get(), mock_thread2);

  ASSERT_FALSE(mock_thread1->stepping_over_breakpoint());
  ASSERT_TRUE(mock_thread2->stepping_over_breakpoint());
  ASSERT_FALSE(mock_thread3->stepping_over_breakpoint());

  // It should've reinstalled the breakpoint for this thread.
  EXPECT_EQ(mock_thread1->mock_thread_handle().BreakpointInstallCount(kAddress), 2u);

  // It should've uninstalled for the second.
  EXPECT_EQ(mock_thread2->mock_thread_handle().BreakpointUninstallCount(kAddress), 1u);

  // First thread hits again! ----------------------------------------------------------------------

  bp.BeginStepOver(mock_thread1);
  ASSERT_EQ(process.step_over_queue().size(), 3u);
  ASSERT_EQ(process.step_over_queue()[2].process_breakpoint.get(), &bp);
  ASSERT_EQ(process.step_over_queue()[2].thread.get(), mock_thread1);

  // Second thread ends ----------------------------------------------------------------------------

  bp.EndStepOver(mock_thread2);

  // Should've started the third step over.
  ASSERT_EQ(process.step_over_queue().size(), 2u);
  ASSERT_EQ(process.step_over_queue()[0].process_breakpoint.get(), &bp);
  ASSERT_EQ(process.step_over_queue()[0].thread.get(), mock_thread3);

  ASSERT_FALSE(mock_thread1->stepping_over_breakpoint());
  ASSERT_FALSE(mock_thread2->stepping_over_breakpoint());
  ASSERT_TRUE(mock_thread3->stepping_over_breakpoint());

  // It should've reinstalled the breakpoint for this thread.
  EXPECT_EQ(mock_thread2->mock_thread_handle().BreakpointInstallCount(kAddress), 2u);

  // It should've uninstalled for the second.
  EXPECT_EQ(mock_thread3->mock_thread_handle().BreakpointUninstallCount(kAddress), 1u);

  // Third thread ends -----------------------------------------------------------------------------

  bp.EndStepOver(mock_thread3);

  // Should've started the third step over.
  ASSERT_EQ(process.step_over_queue().size(), 1u);
  ASSERT_EQ(process.step_over_queue()[0].process_breakpoint.get(), &bp);
  ASSERT_EQ(process.step_over_queue()[0].thread.get(), mock_thread1);

  ASSERT_TRUE(mock_thread1->stepping_over_breakpoint());
  ASSERT_FALSE(mock_thread2->stepping_over_breakpoint());
  ASSERT_FALSE(mock_thread3->stepping_over_breakpoint());

  // It should've reinstalled the breakpoint for this thread.
  EXPECT_EQ(mock_thread2->mock_thread_handle().BreakpointInstallCount(kAddress), 2u);

  // It should've uninstalled for the second.
  EXPECT_EQ(mock_thread1->mock_thread_handle().BreakpointUninstallCount(kAddress), 2u);

  // First thread ends again -----------------------------------------------------------------------

  bp.EndStepOver(mock_thread1);

  // Should've started the third step over.
  ASSERT_EQ(process.step_over_queue().size(), 0u);

  ASSERT_FALSE(mock_thread1->stepping_over_breakpoint());
  ASSERT_FALSE(mock_thread2->stepping_over_breakpoint());
  ASSERT_FALSE(mock_thread3->stepping_over_breakpoint());

  // It should've reinstalled the breakpoint for this thread.
  EXPECT_EQ(mock_thread1->mock_thread_handle().BreakpointInstallCount(kAddress), 3u);
}

}  // namespace
}  // namespace debug_agent
