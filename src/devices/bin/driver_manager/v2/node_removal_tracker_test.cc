// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/bin/driver_manager/v2/node_removal_tracker.h"

#include <gtest/gtest.h>

#include "src/devices/bin/driver_manager/v2/node.h"

struct NodeBank {
  NodeBank(dfv2::NodeRemovalTracker *tracker) : tracker_(tracker) {}
  void AddNode(dfv2::Collection collection, dfv2::NodeState state) {
    nodes_.push_back(state);
    tracker_->RegisterNode(&nodes_.back(), collection, "node", state);
  }

  void NotifyRemovalComplete() {
    for (auto &n : nodes_) {
      tracker_->NotifyRemovalComplete(&n);
    }
  }

  std::list<dfv2::NodeState> nodes_;
  dfv2::NodeRemovalTracker *tracker_;
};

TEST(NodeRemovalTracker, RegisterOneNode) {
  dfv2::NodeRemovalTracker tracker;
  dfv2::NodeState node1 = dfv2::NodeState::kRunning;
  tracker.RegisterNode(&node1, dfv2::Collection::kBoot, "node", node1);
  int package_callbacks = 0;
  int all_callbacks = 0;
  tracker.set_pkg_callback([&package_callbacks]() { package_callbacks++; });
  tracker.set_all_callback([&all_callbacks]() { all_callbacks++; });
  tracker.NotifyRemovalComplete(&node1);

  EXPECT_EQ(package_callbacks, 1);
  EXPECT_EQ(all_callbacks, 1);
}

TEST(NodeRemovalTracker, RegisterManyNodes) {
  dfv2::NodeRemovalTracker tracker;
  NodeBank node_bank(&tracker);
  node_bank.AddNode(dfv2::Collection::kBoot, dfv2::NodeState::kRunning);
  node_bank.AddNode(dfv2::Collection::kBoot, dfv2::NodeState::kRunning);
  node_bank.AddNode(dfv2::Collection::kPackage, dfv2::NodeState::kRunning);
  node_bank.AddNode(dfv2::Collection::kPackage, dfv2::NodeState::kRunning);
  int package_callbacks = 0;
  int all_callbacks = 0;
  tracker.set_pkg_callback([&package_callbacks]() { package_callbacks++; });
  tracker.set_all_callback([&all_callbacks]() { all_callbacks++; });
  EXPECT_EQ(package_callbacks, 0);
  EXPECT_EQ(all_callbacks, 0);
  node_bank.NotifyRemovalComplete();

  EXPECT_EQ(package_callbacks, 1);
  EXPECT_EQ(all_callbacks, 1);
}

// Make sure package callback is only called when package drivers stop
// and all callback is only called when all drivers stop
TEST(NodeRemovalTracker, CallbacksCallOrder) {
  dfv2::NodeRemovalTracker tracker;
  NodeBank boot_node_bank(&tracker), package_node_bank(&tracker);
  boot_node_bank.AddNode(dfv2::Collection::kBoot, dfv2::NodeState::kRunning);
  boot_node_bank.AddNode(dfv2::Collection::kBoot, dfv2::NodeState::kRunning);
  package_node_bank.AddNode(dfv2::Collection::kPackage, dfv2::NodeState::kRunning);
  package_node_bank.AddNode(dfv2::Collection::kPackage, dfv2::NodeState::kRunning);
  int package_callbacks = 0;
  int all_callbacks = 0;
  tracker.set_pkg_callback([&package_callbacks]() { package_callbacks++; });
  tracker.set_all_callback([&all_callbacks]() { all_callbacks++; });
  EXPECT_EQ(package_callbacks, 0);
  EXPECT_EQ(all_callbacks, 0);

  package_node_bank.NotifyRemovalComplete();

  EXPECT_EQ(package_callbacks, 1);
  EXPECT_EQ(all_callbacks, 0);

  boot_node_bank.NotifyRemovalComplete();

  EXPECT_EQ(package_callbacks, 1);
  EXPECT_EQ(all_callbacks, 1);
}
