// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <utility>
#include <vector>

#include "garnet/bin/debug_agent/breakpoint.h"
#include "gtest/gtest.h"

namespace debug_agent {

using CallPair = std::pair<zx_koid_t, uint64_t>;
using CallVector = std::vector<CallPair>;

class TestProcessDelegate : public Breakpoint::ProcessDelegate {
 public:
  const CallVector& register_calls() const { return register_calls_; }
  const CallVector& unregister_calls() const { return unregister_calls_; }

  void Clear() {
    register_calls_.clear();
    unregister_calls_.clear();
  }

  zx_status_t RegisterBreakpoint(Breakpoint*, zx_koid_t process_koid,
                                 uint64_t address) override {
    register_calls_.push_back(std::make_pair(process_koid, address));
    return ZX_OK;
  }
  void UnregisterBreakpoint(Breakpoint*, zx_koid_t process_koid,
                            uint64_t address) override {
    unregister_calls_.push_back(std::make_pair(process_koid, address));
  }

 private:
  CallVector register_calls_;
  CallVector unregister_calls_;
};

TEST(Breakpoint, Registration) {
  TestProcessDelegate delegate;
  Breakpoint bp(&delegate);

  debug_ipc::BreakpointSettings settings;
  settings.breakpoint_id = 1;
  settings.locations.resize(1);

  constexpr zx_koid_t kProcess1 = 1;
  constexpr uint64_t kAddress1 = 0x1234;

  debug_ipc::ProcessBreakpointSettings& pr_settings = settings.locations.back();
  pr_settings.process_koid = kProcess1;
  pr_settings.thread_koid = 0;
  pr_settings.address = kAddress1;

  // Apply the settings.
  ASSERT_EQ(ZX_OK, bp.SetSettings(settings));
  EXPECT_EQ(CallVector({CallPair{kProcess1, kAddress1}}),
            delegate.register_calls());
  EXPECT_TRUE(delegate.unregister_calls().empty());

  delegate.Clear();

  // Change the settings to move the breakpoint.
  constexpr zx_koid_t kProcess2 = 2;
  constexpr uint64_t kAddress2 = 0x5678;
  pr_settings.process_koid = kProcess2;
  pr_settings.thread_koid = 0;
  pr_settings.address = kAddress2;

  ASSERT_EQ(ZX_OK, bp.SetSettings(settings));
  EXPECT_EQ(CallVector({CallPair{kProcess2, kAddress2}}),
            delegate.register_calls());
  EXPECT_EQ(CallVector({CallPair{kProcess1, kAddress1}}),
            delegate.unregister_calls());
}

}  // namespace debug_agent
