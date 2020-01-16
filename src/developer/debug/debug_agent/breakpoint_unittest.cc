// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/debug_agent/breakpoint.h"

#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "src/developer/debug/debug_agent/test_utils.h"

namespace debug_agent {
namespace {

using CallPair = std::pair<zx_koid_t, uint64_t>;
using CallVector = std::vector<CallPair>;

using WPPair = std::pair<zx_koid_t, debug_ipc::AddressRange>;
using WPVector = std::vector<WPPair>;

class TestProcessDelegate : public Breakpoint::ProcessDelegate {
 public:
  const CallVector& register_calls() const { return register_calls_; }
  const CallVector& unregister_calls() const { return unregister_calls_; }

  const WPVector& wp_register_calls() const { return wp_register_calls_; }
  const WPVector& wp_unregister_calls() const { return wp_unregister_calls_; }

  void Clear() {
    register_calls_.clear();
    unregister_calls_.clear();
  }

  zx_status_t RegisterBreakpoint(Breakpoint*, zx_koid_t process_koid, uint64_t address) override {
    register_calls_.push_back(std::make_pair(process_koid, address));
    return ZX_OK;
  }

  void UnregisterBreakpoint(Breakpoint*, zx_koid_t process_koid, uint64_t address) override {
    unregister_calls_.push_back(std::make_pair(process_koid, address));
  }

  zx_status_t RegisterWatchpoint(Breakpoint*, zx_koid_t process_koid,
                                 const debug_ipc::AddressRange& range) override {
    wp_register_calls_.push_back(std::make_pair(process_koid, range));
    return ZX_OK;
  }

  void UnregisterWatchpoint(Breakpoint*, zx_koid_t process_koid,
                            const debug_ipc::AddressRange& range) override {
    wp_unregister_calls_.push_back(std::make_pair(process_koid, range));
  }

 private:
  CallVector register_calls_;
  CallVector unregister_calls_;

  WPVector wp_register_calls_;
  WPVector wp_unregister_calls_;
};

debug_ipc::ProcessBreakpointSettings CreateLocation(zx_koid_t process_koid, zx_koid_t thread_koid,
                                                    const debug_ipc::AddressRange& address_range) {
  debug_ipc::ProcessBreakpointSettings settings = {};
  settings.process_koid = process_koid;
  settings.thread_koid = thread_koid;
  settings.address_range = address_range;

  return settings;
}

// Tests -------------------------------------------------------------------------------------------

TEST(Breakpoint, Registration) {
  TestProcessDelegate delegate;
  Breakpoint bp(&delegate);

  debug_ipc::BreakpointSettings settings;
  settings.id = 1;
  settings.locations.resize(1);

  constexpr zx_koid_t kProcess1 = 1;
  constexpr uint64_t kAddress1 = 0x1234;

  debug_ipc::ProcessBreakpointSettings& pr_settings = settings.locations.back();
  pr_settings.process_koid = kProcess1;
  pr_settings.thread_koid = 0;
  pr_settings.address = kAddress1;

  // Apply the settings.
  ASSERT_EQ(ZX_OK, bp.SetSettings(debug_ipc::BreakpointType::kSoftware, settings));
  EXPECT_EQ(CallVector({CallPair{kProcess1, kAddress1}}), delegate.register_calls());
  EXPECT_TRUE(delegate.unregister_calls().empty());

  delegate.Clear();

  // Change the settings to move the breakpoint.
  constexpr zx_koid_t kProcess2 = 2;
  constexpr uint64_t kAddress2 = 0x5678;
  pr_settings.process_koid = kProcess2;
  pr_settings.thread_koid = 0;
  pr_settings.address = kAddress2;

  ASSERT_EQ(ZX_OK, bp.SetSettings(debug_ipc::BreakpointType::kSoftware, settings));
  EXPECT_EQ(CallVector({CallPair{kProcess2, kAddress2}}), delegate.register_calls());
  EXPECT_EQ(CallVector({CallPair{kProcess1, kAddress1}}), delegate.unregister_calls());

  // Add a new location
  delegate.Clear();

  // Add the old breakpoint and a new one
  debug_ipc::ProcessBreakpointSettings old_pr_settings;
  old_pr_settings.process_koid = kProcess1;
  old_pr_settings.thread_koid = 0;
  old_pr_settings.address = kAddress1;

  constexpr zx_koid_t kProcess3 = 3;
  constexpr uint64_t kAddress3 = 0x9ABC;

  debug_ipc::ProcessBreakpointSettings new_pr_settings;
  new_pr_settings.process_koid = kProcess3;
  new_pr_settings.thread_koid = 0;
  new_pr_settings.address = kAddress3;

  settings.locations.clear();
  settings.locations.push_back(old_pr_settings);
  settings.locations.push_back(new_pr_settings);

  ASSERT_EQ(ZX_OK, bp.SetSettings(debug_ipc::BreakpointType::kSoftware, settings));

  EXPECT_EQ(CallVector({{kProcess1, kAddress1}, {kProcess3, kAddress3}}),
            delegate.register_calls());
  EXPECT_EQ(CallVector({{kProcess2, kAddress2}}), delegate.unregister_calls());
}

// The destructor should clear breakpoint locations.
TEST(Breakpoint, Destructor) {
  TestProcessDelegate delegate;
  std::unique_ptr<Breakpoint> bp = std::make_unique<Breakpoint>(&delegate);

  debug_ipc::BreakpointSettings settings;
  settings.id = 1;
  settings.locations.resize(1);

  constexpr zx_koid_t kProcess1 = 1;
  constexpr uint64_t kAddress1 = 0x1234;

  debug_ipc::ProcessBreakpointSettings& pr_settings = settings.locations.back();
  pr_settings.process_koid = kProcess1;
  pr_settings.thread_koid = 0;
  pr_settings.address = kAddress1;

  // Apply the settings.
  ASSERT_EQ(ZX_OK, bp->SetSettings(debug_ipc::BreakpointType::kSoftware, settings));
  EXPECT_EQ(CallVector({CallPair{kProcess1, kAddress1}}), delegate.register_calls());
  EXPECT_TRUE(delegate.unregister_calls().empty());

  delegate.Clear();

  // Delete the breakpoint to make sure the locations get updated.
  delegate.Clear();
  bp.reset();
  EXPECT_EQ(CallVector({CallPair{kProcess1, kAddress1}}), delegate.unregister_calls());
}

TEST(Breakpoint, HitCount) {
  TestProcessDelegate delegate;
  std::unique_ptr<Breakpoint> bp = std::make_unique<Breakpoint>(&delegate);

  constexpr uint32_t kBreakpointId = 12;
  debug_ipc::BreakpointSettings settings;
  settings.id = kBreakpointId;
  settings.locations.resize(1);

  constexpr zx_koid_t kProcess1 = 1;
  constexpr uint64_t kAddress1 = 0x1234;

  debug_ipc::ProcessBreakpointSettings& pr_settings = settings.locations.back();
  pr_settings.process_koid = kProcess1;
  pr_settings.thread_koid = 0;
  pr_settings.address = kAddress1;

  // Apply the settings.
  ASSERT_EQ(ZX_OK, bp->SetSettings(debug_ipc::BreakpointType::kSoftware, settings));
  delegate.Clear();

  EXPECT_EQ(kBreakpointId, bp->stats().id);
  EXPECT_EQ(0u, bp->stats().hit_count);

  EXPECT_EQ(Breakpoint::HitResult::kHit, bp->OnHit());
  EXPECT_EQ(kBreakpointId, bp->stats().id);
  EXPECT_EQ(1u, bp->stats().hit_count);
  EXPECT_FALSE(bp->stats().should_delete);

  EXPECT_EQ(Breakpoint::HitResult::kHit, bp->OnHit());
  EXPECT_EQ(kBreakpointId, bp->stats().id);
  EXPECT_EQ(2u, bp->stats().hit_count);
  EXPECT_FALSE(bp->stats().should_delete);
}

TEST(Breakpoint, OneShot) {
  TestProcessDelegate delegate;
  std::unique_ptr<Breakpoint> bp = std::make_unique<Breakpoint>(&delegate);

  constexpr uint32_t kBreakpointId = 12;
  debug_ipc::BreakpointSettings settings;
  settings.id = kBreakpointId;
  settings.one_shot = true;
  settings.locations.resize(1);

  constexpr zx_koid_t kProcess = 1;
  constexpr uint64_t kAddress = 0x1234;

  debug_ipc::ProcessBreakpointSettings& pr_settings = settings.locations.back();
  pr_settings.process_koid = kProcess;
  pr_settings.thread_koid = 0;
  pr_settings.address = kAddress;

  // Apply the settings.
  ASSERT_EQ(ZX_OK, bp->SetSettings(debug_ipc::BreakpointType::kSoftware, settings));
  delegate.Clear();

  EXPECT_EQ(kBreakpointId, bp->stats().id);
  EXPECT_EQ(0u, bp->stats().hit_count);
  EXPECT_FALSE(bp->stats().should_delete);

  // The hit count and "should delete" flag should be set.
  EXPECT_EQ(Breakpoint::HitResult::kOneShotHit, bp->OnHit());
  EXPECT_EQ(kBreakpointId, bp->stats().id);
  EXPECT_EQ(1u, bp->stats().hit_count);
  EXPECT_TRUE(bp->stats().should_delete);
}

TEST(Breakpoint, WatchpointLocations) {
  TestProcessDelegate process_delegate;
  Breakpoint breakpoint(&process_delegate);

  constexpr zx_koid_t kProcess1Koid = 0x1;
  constexpr zx_koid_t kProcess2Koid = 0x2;
  constexpr debug_ipc::AddressRange kProcess1Range = {0x100, 0x200};
  constexpr debug_ipc::AddressRange kProcess2Range = {0x400, 0x800};

  debug_ipc::BreakpointSettings settings;
  settings.id = 1;
  settings.locations.push_back(CreateLocation(kProcess1Koid, 0, kProcess1Range));
  settings.locations.push_back(CreateLocation(kProcess2Koid, 0, kProcess2Range));

  ASSERT_ZX_EQ(breakpoint.SetSettings(debug_ipc::BreakpointType::kReadWrite, settings),
               ZX_ERR_NOT_SUPPORTED);

  ASSERT_ZX_EQ(breakpoint.SetSettings(debug_ipc::BreakpointType::kWrite, settings), ZX_OK);

  EXPECT_EQ(
      process_delegate.wp_register_calls(),
      WPVector({WPPair{kProcess1Koid, kProcess1Range}, WPPair{kProcess2Koid, kProcess2Range}}));
  EXPECT_EQ(process_delegate.wp_unregister_calls(), WPVector{});
}

using BPType = debug_ipc::BreakpointType;

TEST(Breakpoint, DoesExceptionApply) {
  EXPECT_TRUE(Breakpoint::DoesExceptionApply(BPType::kSoftware, BPType::kSoftware));
  EXPECT_FALSE(Breakpoint::DoesExceptionApply(BPType::kSoftware, BPType::kHardware));
  EXPECT_FALSE(Breakpoint::DoesExceptionApply(BPType::kSoftware, BPType::kReadWrite));
  EXPECT_FALSE(Breakpoint::DoesExceptionApply(BPType::kSoftware, BPType::kWrite));

  EXPECT_FALSE(Breakpoint::DoesExceptionApply(BPType::kHardware, BPType::kSoftware));
  EXPECT_TRUE(Breakpoint::DoesExceptionApply(BPType::kHardware, BPType::kHardware));
  EXPECT_FALSE(Breakpoint::DoesExceptionApply(BPType::kHardware, BPType::kReadWrite));
  EXPECT_FALSE(Breakpoint::DoesExceptionApply(BPType::kHardware, BPType::kWrite));

  EXPECT_FALSE(Breakpoint::DoesExceptionApply(BPType::kReadWrite, BPType::kSoftware));
  EXPECT_FALSE(Breakpoint::DoesExceptionApply(BPType::kReadWrite, BPType::kHardware));
  EXPECT_TRUE(Breakpoint::DoesExceptionApply(BPType::kReadWrite, BPType::kReadWrite));
  EXPECT_TRUE(Breakpoint::DoesExceptionApply(BPType::kReadWrite, BPType::kWrite));

  EXPECT_FALSE(Breakpoint::DoesExceptionApply(BPType::kWrite, BPType::kSoftware));
  EXPECT_FALSE(Breakpoint::DoesExceptionApply(BPType::kWrite, BPType::kHardware));
  EXPECT_TRUE(Breakpoint::DoesExceptionApply(BPType::kWrite, BPType::kReadWrite));
  EXPECT_TRUE(Breakpoint::DoesExceptionApply(BPType::kWrite, BPType::kWrite));
}

}  // namespace
}  // namespace debug_agent
