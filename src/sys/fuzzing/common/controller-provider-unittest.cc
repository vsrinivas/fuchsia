// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/fuzzing/common/controller-provider.h"

#include <gtest/gtest.h>

#include "src/sys/fuzzing/common/controller.h"
#include "src/sys/fuzzing/common/testing/registrar.h"
#include "src/sys/fuzzing/common/testing/runner.h"

namespace fuzzing {

using ::fuchsia::fuzzer::ControllerProviderSyncPtr;
using ::fuchsia::fuzzer::ControllerSyncPtr;

// Test fixtures

class ControllerProviderTest : public ::testing::Test {
 protected:
  ControllerProviderSyncPtr GetProvider() {
    ControllerProviderSyncPtr provider;
    provider_.SetRunner(std::make_unique<FakeRunner>());
    provider_.Serve(registrar_.Bind());
    provider.Bind(registrar_.TakeProvider());
    return provider;
  }

 private:
  FakeRegistrar registrar_;
  ControllerProviderImpl provider_;
};

// Unit tests

TEST_F(ControllerProviderTest, PublishAndConnect) {
  auto provider = GetProvider();

  // Should be able to connect...
  ControllerSyncPtr ptr1;
  provider->Connect(ptr1.NewRequest());

  // ...and reconnect.
  ControllerSyncPtr ptr2;
  provider->Connect(ptr2.NewRequest());
}

TEST_F(ControllerProviderTest, Stop) {
  auto provider = GetProvider();
  provider->Stop();
  auto channel = provider.unowned_channel();
  Waiter waiter = [&channel](zx::time deadline) {
    return channel->wait_one(ZX_CHANNEL_PEER_CLOSED, deadline, nullptr);
  };
  auto status = WaitFor("channel to close", &waiter);
  // The local end of the channel may be closed before the wait.
  if (status != ZX_ERR_BAD_HANDLE) {
    EXPECT_EQ(status, ZX_OK);
  }
}

}  // namespace fuzzing
