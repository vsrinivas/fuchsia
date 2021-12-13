// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/fuzzing/framework/engine/adapter-client.h"

#include <fuchsia/fuzzer/cpp/fidl.h>

#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "src/lib/files/directory.h"
#include "src/lib/files/file.h"
#include "src/lib/files/path.h"
#include "src/sys/fuzzing/common/input.h"
#include "src/sys/fuzzing/common/options.h"
#include "src/sys/fuzzing/common/signal-coordinator.h"
#include "src/sys/fuzzing/framework/engine/corpus.h"
#include "src/sys/fuzzing/framework/testing/adapter.h"

using fuchsia::fuzzer::TargetAdapterSyncPtr;

namespace fuzzing {

// Test fixtures

class TargetAdapterClientTest : public ::testing::Test {
 protected:
  std::shared_ptr<Options> DefaultOptions() {
    auto options = std::make_shared<Options>();
    TargetAdapterClient::AddDefaults(options.get());
    return options;
  }

  void Configure(const std::shared_ptr<Options>& options) {
    adapter_ = std::make_unique<FakeTargetAdapter>();
    client_ = std::make_unique<TargetAdapterClient>(adapter_->GetHandler());
    client_->Configure(options);
  }

  std::unique_ptr<FakeTargetAdapter> TakeAdapter() { return std::move(adapter_); }
  std::unique_ptr<TargetAdapterClient> TakeClient() { return std::move(client_); }

 private:
  std::unique_ptr<FakeTargetAdapter> adapter_;
  std::unique_ptr<TargetAdapterClient> client_;
};

// Unit tests

TEST_F(TargetAdapterClientTest, AddDefaults) {
  Options options;
  TargetAdapterClient::AddDefaults(&options);
  EXPECT_EQ(options.max_input_size(), kDefaultMaxInputSize);
}

TEST_F(TargetAdapterClientTest, StartAndFinish) {
  Configure(DefaultOptions());
  auto adapter = TakeAdapter();
  auto client = TakeClient();

  Input sent("foo");
  client->Start(&sent);
  EXPECT_EQ(adapter->AwaitSignal(), kStart);
  EXPECT_EQ(adapter->test_input(), sent);
  adapter->SignalPeer(kFinish);
  client->AwaitFinish();
}

TEST_F(TargetAdapterClientTest, StartAndError) {
  Configure(DefaultOptions());
  auto adapter = TakeAdapter();
  auto client = TakeClient();

  Input sent1("foo");
  client->Start(&sent1);
  EXPECT_EQ(adapter->AwaitSignal(), kStart);
  EXPECT_EQ(adapter->test_input(), sent1);
  client->SetError();
  client->AwaitFinish();

  // |Start| after |SetError| is a no-op...
  Input sent2("bar");
  client->Start(&sent2);
  client->AwaitFinish();

  // ...until |ClearError|.
  client->ClearError();
  client->Start(&sent2);
  EXPECT_EQ(adapter->AwaitSignal(), kStart);
  EXPECT_EQ(adapter->test_input(), sent2);
  adapter->SignalPeer(kFinish);
  client->AwaitFinish();
}

TEST_F(TargetAdapterClientTest, StartAndClose) {
  Configure(DefaultOptions());
  auto adapter = TakeAdapter();
  auto client = TakeClient();

  Input sent("foo");
  client->Start(&sent);
  EXPECT_EQ(adapter->AwaitSignal(), kStart);
  client->Close();
  client->AwaitFinish();
}

}  // namespace fuzzing
