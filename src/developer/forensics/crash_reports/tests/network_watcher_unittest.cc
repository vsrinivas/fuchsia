// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/crash_reports/network_watcher.h"

#include <optional>

#include <gtest/gtest.h>

#include "src/developer/forensics/crash_reports/network_watcher.h"
#include "src/developer/forensics/testing/stubs/network_reachability_provider.h"
#include "src/developer/forensics/testing/unit_test_fixture.h"

namespace forensics {
namespace crash_reports {
namespace {

class NetworkWatcherTest : public UnitTestFixture {
 public:
  NetworkWatcherTest() : network_watcher_(dispatcher(), services()) {
    SetUpNetworkReachabilityProvider();
    RunLoopUntilIdle();
  }

 protected:
  void SetUpNetworkReachabilityProvider() {
    network_reachability_provider_ = std::make_unique<stubs::NetworkReachabilityProvider>();
    InjectServiceProvider(network_reachability_provider_.get());
  }

  std::unique_ptr<stubs::NetworkReachabilityProvider> network_reachability_provider_;
  NetworkWatcher network_watcher_;
};

TEST_F(NetworkWatcherTest, CallbacksAreExecuted) {
  const size_t kNumCallbacks{5u};
  std::vector<std::optional<bool>> network_is_reachable_results(kNumCallbacks, std::nullopt);

  for (size_t i = 0; i < kNumCallbacks; ++i) {
    network_watcher_.Register([&network_is_reachable_results, i](const bool network_is_reachable) {
      network_is_reachable_results[i] = network_is_reachable;
    });
  }

  network_reachability_provider_->TriggerOnNetworkReachable(false);
  RunLoopUntilIdle();

  for (auto& network_is_reachable : network_is_reachable_results) {
    ASSERT_TRUE(network_is_reachable.has_value());
    EXPECT_FALSE(network_is_reachable.value());
    network_is_reachable = std::nullopt;
  }

  network_reachability_provider_->TriggerOnNetworkReachable(true);
  RunLoopUntilIdle();

  for (auto& network_is_reachable : network_is_reachable_results) {
    ASSERT_TRUE(network_is_reachable.has_value());
    EXPECT_TRUE(network_is_reachable.value());
    network_is_reachable = std::nullopt;
  }
}

}  // namespace
}  // namespace crash_reports
}  // namespace forensics
