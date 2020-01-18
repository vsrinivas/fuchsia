// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <src/lib/fxl/logging.h>

#include "garnet/bin/trace_manager/tests/trace_manager_test.h"

namespace tracing {
namespace test {

TEST_F(TraceManagerTest, InitToFini) {
  ConnectToControllerService();

  FakeProvider* provider;
  ASSERT_TRUE(AddFakeProvider(kProvider1Pid, kProvider1Name, &provider));
  EXPECT_EQ(fake_provider_bindings().size(), 1u);

  ASSERT_TRUE(InitializeSession());

  ASSERT_TRUE(StartSession());
  VerifyCounts(1, 0);

  ASSERT_TRUE(StopSession());
  VerifyCounts(1, 1);

  ASSERT_TRUE(StartSession());
  VerifyCounts(2, 1);

  ASSERT_TRUE(StopSession());
  VerifyCounts(2, 2);

  ASSERT_TRUE(TerminateSession());
  VerifyCounts(2, 2);
}

TEST_F(TraceManagerTest, InitToFiniWithNoProviders) {
  ConnectToControllerService();

  ASSERT_TRUE(InitializeSession());

  ASSERT_TRUE(StartSession());
  VerifyCounts(1, 0);

  ASSERT_TRUE(StopSession());
  VerifyCounts(1, 1);

  ASSERT_TRUE(StartSession());
  VerifyCounts(2, 1);

  ASSERT_TRUE(StopSession());
  VerifyCounts(2, 2);

  ASSERT_TRUE(TerminateSession());
  VerifyCounts(2, 2);
}

TEST_F(TraceManagerTest, InitToFiniWithProviderAddedAfterSessionStarts) {
  ConnectToControllerService();

  FakeProvider* provider1;
  ASSERT_TRUE(AddFakeProvider(kProvider1Pid, kProvider1Name, &provider1));
  EXPECT_EQ(fake_provider_bindings().size(), 1u);

  ASSERT_TRUE(InitializeSession());

  ASSERT_TRUE(StartSession());
  VerifyCounts(1, 0);

  FakeProvider* provider2;
  ASSERT_TRUE(AddFakeProvider(kProvider2Pid, kProvider2Name, &provider2));
  EXPECT_EQ(fake_provider_bindings().size(), 2u);

  // Given the session a chance to start the new provider before we stop it.
  RunLoopUntilIdle();
  ASSERT_EQ(provider2->state(), FakeProvider::State::kStarting);
  provider2->MarkStarted();
  // Give TraceSession a chance to process the STARTED fifo packet.
  RunLoopUntilIdle();

  ASSERT_TRUE(StopSession());
  VerifyCounts(1, 1);

  ASSERT_TRUE(StartSession());
  VerifyCounts(2, 1);

  ASSERT_TRUE(StopSession());
  VerifyCounts(2, 2);

  ASSERT_TRUE(TerminateSession());
  VerifyCounts(2, 2);
}

TEST_F(TraceManagerTest, InitToFiniWithNoStop) {
  ConnectToControllerService();

  FakeProvider* provider;
  ASSERT_TRUE(AddFakeProvider(kProvider1Pid, kProvider1Name, &provider));
  EXPECT_EQ(fake_provider_bindings().size(), 1u);

  ASSERT_TRUE(InitializeSession());

  ASSERT_TRUE(StartSession());
  VerifyCounts(1, 0);

  ASSERT_TRUE(TerminateSession());
  VerifyCounts(1, 0);
}

}  // namespace test
}  // namespace tracing
