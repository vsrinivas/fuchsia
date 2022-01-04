// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/fuzzing/framework/engine/coverage-client.h"

#include <fuchsia/fuzzer/cpp/fidl.h>

#include <memory>
#include <thread>

#include <gtest/gtest.h>

#include "src/sys/fuzzing/common/options.h"
#include "src/sys/fuzzing/common/sync-wait.h"
#include "src/sys/fuzzing/framework/coverage/event-queue.h"
#include "src/sys/fuzzing/framework/coverage/provider.h"
#include "src/sys/fuzzing/framework/testing/process.h"

namespace fuzzing {

// Test fixtures.

class CoverageProviderClientTest : public ::testing::Test {
 protected:
  void SetUp() override {
    events_ = std::make_shared<CoverageEventQueue>();
    provider_ = std::make_unique<CoverageProviderImpl>(events_);
  }

  void Connect(CoverageProviderClient& client) {
    auto handler = provider_->GetHandler();
    handler(client.TakeRequest());
  }

  Options StartProcess(uint64_t target_id, FakeProcess& process) {
    events_->AddProcess(target_id, process.IgnoreAll());
    return events_->GetOptions();
  }

  void ResetProvider() { provider_.reset(); }

 private:
  std::shared_ptr<CoverageEventQueue> events_;
  std::unique_ptr<CoverageProviderImpl> provider_;
};

// Unit tests.

TEST_F(CoverageProviderClientTest, Configure) {
  // Configure before connecting...
  auto options = std::make_shared<Options>();
  options->set_seed(1U);

  CoverageProviderClient client;
  client.Configure(options);
  Connect(client);

  FakeProcess process1;
  auto received1 = StartProcess(1, process1);
  EXPECT_EQ(received1.seed(), 1U);

  // ...and after.
  options->set_seed(2U);
  client.Configure(options);

  FakeProcess process2;
  auto received2 = StartProcess(2, process2);
  EXPECT_EQ(received2.seed(), 2U);
}

TEST_F(CoverageProviderClientTest, OnEvent) {
  CoverageProviderClient client;
  Connect(client);

  uint64_t target_id = 0;
  SyncWait sync;
  client.OnEvent([&](CoverageEvent event) {
    target_id = event.target_id;
    sync.Signal();
  });

  FakeProcess process;
  StartProcess(1, process);
  sync.WaitFor("coverage event");
  EXPECT_EQ(target_id, 1U);
}

TEST_F(CoverageProviderClientTest, OnEventWithClose) {
  CoverageProviderClient client;
  Connect(client);
  client.OnEvent([&](CoverageEvent event) { FAIL(); });
  client.Close();
}

TEST_F(CoverageProviderClientTest, OnEventWithPeerClose) {
  CoverageProviderClient client;
  Connect(client);
  client.OnEvent([&](CoverageEvent event) { FAIL(); });
  ResetProvider();
}

}  // namespace fuzzing
