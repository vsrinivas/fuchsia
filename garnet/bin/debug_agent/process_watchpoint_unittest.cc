// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/debug_agent/process_watchpoint.h"

#include <gtest/gtest.h>

#include "garnet/bin/debug_agent/debugged_process.h"
#include "garnet/bin/debug_agent/debugged_thread.h"
#include "garnet/bin/debug_agent/mock_arch_provider.h"
#include "garnet/bin/debug_agent/mock_process.h"
#include "garnet/bin/debug_agent/process_watchpoint.h"
#include "garnet/bin/debug_agent/watchpoint.h"
#include "src/developer/debug/shared/zx_status.h"

namespace debug_agent {

namespace {

// A no-op process delegate.
class TestProcessDelegate : public Watchpoint::ProcessDelegate {
 public:
  using WatchpointMap =
      std::multimap<debug_ipc::AddressRange, std::unique_ptr<ProcessWatchpoint>,
                    debug_ipc::AddressRangeCompare>;

  const WatchpointMap& watchpoint_map() const { return wps_; }

  void InjectMockProcess(std::unique_ptr<MockProcess> proc) {
    procs_[proc->koid()] = std::move(proc);
  }

  // This only gets called if Breakpoint.SetSettings() is called.
  zx_status_t RegisterWatchpoint(
      Watchpoint* wp, zx_koid_t process_koid,
      const debug_ipc::AddressRange& range) override {
    auto proc_it = procs_.find(process_koid);
    FXL_DCHECK(proc_it != procs_.end());

    auto found = wps_.find(range);
    if (found != wps_.end())
      return ZX_ERR_INTERNAL;
    auto pwp =
        std::make_unique<ProcessWatchpoint>(wp, proc_it->second.get(), range);

    zx_status_t status = pwp->Init();
    if (status != ZX_OK) {
      FXL_LOG(ERROR) << "Failure initializing: "
                     << debug_ipc::ZxStatusToString(status);
      return status;
    }

    wps_.insert({range, std::move(pwp)});

    return ZX_OK;
  }

  void UnregisterWatchpoint(Watchpoint* wp, zx_koid_t process_koid,
                            const debug_ipc::AddressRange& range) override {
    // The destructor of the watchpoint will call the arch removal.
    size_t remove_count = wps_.erase(range);
    if (remove_count == 0)
      GTEST_FAIL();
  }

 private:
  WatchpointMap wps_;
  std::map<zx_koid_t, std::unique_ptr<MockProcess>> procs_;
};

// Tests -----------------------------------------------------------------------

constexpr zx_koid_t kProcessId1 = 0x1;
constexpr zx_koid_t kProcessId2 = 0x2;
constexpr zx_koid_t kThreadId11 = 0x1;
constexpr zx_koid_t kThreadId12 = 0x2;
constexpr zx_koid_t kThreadId13 = 0x3;

constexpr zx_koid_t kThreadId21 = 0x4;
constexpr zx_koid_t kThreadId22 = 0x5;
constexpr zx_koid_t kThreadId23 = 0x6;
constexpr zx_koid_t kThreadId24 = 0x7;
constexpr zx_koid_t kThreadId25 = 0x8;

constexpr debug_ipc::AddressRange kAddressRange1 = {0x1000, 0x2000};
constexpr debug_ipc::AddressRange kAddressRange2 = {0x4000, 0x8000};

TEST(ProcessWatchpoint, InstallAndRemove) {
  ScopedMockArchProvider scoped_arch_provider;
  MockArchProvider* arch_provider = scoped_arch_provider.get_provider();

  TestProcessDelegate process_delegate;

  auto process1 = std::make_unique<MockProcess>(kProcessId1);
  process1->AddThread(kThreadId11);
  process1->AddThread(kThreadId12);
  process1->AddThread(kThreadId13);
  process_delegate.InjectMockProcess(std::move(process1));

  auto process2 = std::make_unique<MockProcess>(kProcessId2);
  process2->AddThread(kThreadId21);
  process2->AddThread(kThreadId22);
  process2->AddThread(kThreadId23);
  process2->AddThread(kThreadId24);
  process2->AddThread(kThreadId25);
  process_delegate.InjectMockProcess(std::move(process2));

  auto watchpoint = std::make_unique<Watchpoint>(&process_delegate);

  // Insert the watchpoint for all threads.
  debug_ipc::WatchpointSettings settings = {};
  settings.watchpoint_id = 0x1;
  settings.locations.push_back({kProcessId1, kThreadId11, kAddressRange1});
  settings.locations.push_back({kProcessId1, kThreadId13, kAddressRange1});
  // All of the process 2 threads.
  settings.locations.push_back({kProcessId2, 0, kAddressRange2});

  zx_status_t res = watchpoint->SetSettings(settings);
  ASSERT_EQ(res, ZX_OK) << "Expected ZX_OK, got: "
                        << debug_ipc::ZxStatusToString(res);

  // Should have installed only one process watchpoint per process.
  const auto& watchpoint_map = process_delegate.watchpoint_map();
  ASSERT_EQ(watchpoint_map.count(kAddressRange1), 1u);
  ASSERT_EQ(watchpoint_map.count(kAddressRange2), 1u);

  // It should have installed 2 thread installations for process 1
  EXPECT_EQ(arch_provider->WatchpointInstallCount(kAddressRange1), 2u);

  // It should have installed 5 thread installations for process 1
  EXPECT_EQ(arch_provider->WatchpointInstallCount(kAddressRange2), 5u);

  // Once removed, we expect everything to go away.
  watchpoint.reset();

  ASSERT_EQ(watchpoint_map.count(kAddressRange1), 0u);
  ASSERT_EQ(watchpoint_map.count(kAddressRange2), 0u);
  EXPECT_EQ(arch_provider->WatchpointUninstallCount(kAddressRange1), 2u);
  EXPECT_EQ(arch_provider->WatchpointUninstallCount(kAddressRange2), 5u);
}

}  // namespace

}  // namespace debug_agent
