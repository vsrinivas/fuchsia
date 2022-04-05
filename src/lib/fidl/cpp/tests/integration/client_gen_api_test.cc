// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fidl.test.coding.fuchsia/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/fit/defer.h>
#include <lib/sync/cpp/completion.h>

#include <zxtest/zxtest.h>

namespace {

using ::fidl_test_coding_fuchsia::Example;

// An integration-style test that verifies that user-supplied async callbacks
// attached using |Then| with client lifetime are not invoked when the client is
// destroyed by the user (i.e. explicit cancellation) instead of due to errors.
template <typename ClientType>
void ThenWithClientLifetimeTest() {
  auto endpoints = fidl::CreateEndpoints<Example>();
  ASSERT_OK(endpoints.status_value());
  auto [local, remote] = std::move(*endpoints);
  async::Loop loop(&kAsyncLoopConfigNeverAttachToThread);
  ClientType client(std::move(local), loop.dispatcher());
  bool destroyed = false;

  client->TwoWay({"foo"}).Then(
      [observer = fit::defer([&] { destroyed = true; })](fidl::Result<Example::TwoWay>& result) {
        ADD_FATAL_FAILURE("Should not be invoked");
      });
  // Immediately start cancellation.
  client = {};
  ASSERT_FALSE(destroyed);
  loop.RunUntilIdle();

  // The callback should be destroyed without being called.
  ASSERT_TRUE(destroyed);
}

TEST(Client, ThenWithClientLifetime) { ThenWithClientLifetimeTest<fidl::Client<Example>>(); }

TEST(SharedClient, ThenWithClientLifetime) {
  ThenWithClientLifetimeTest<fidl::SharedClient<Example>>();
}

// An integration-style test that verifies that user-supplied async callbacks
// that takes |fidl::WireUnownedResult| are correctly notified when the binding
// is torn down by the user (i.e. explicit cancellation).
template <typename ClientType>
void ThenExactlyOnceTest() {
  auto endpoints = fidl::CreateEndpoints<Example>();
  ASSERT_OK(endpoints.status_value());
  auto [local, remote] = std::move(*endpoints);
  async::Loop loop(&kAsyncLoopConfigNeverAttachToThread);
  ClientType client(std::move(local), loop.dispatcher());
  bool called = false;
  bool destroyed = false;
  auto callback_destruction_observer = fit::defer([&] { destroyed = true; });

  client->TwoWay({"foo"}).ThenExactlyOnce([observer = std::move(callback_destruction_observer),
                                           &called](fidl::Result<Example::TwoWay>& result) {
    called = true;
    EXPECT_FALSE(result.is_ok());
    EXPECT_STATUS(ZX_ERR_CANCELED, result.error_value().status());
    EXPECT_EQ(fidl::Reason::kUnbind, result.error_value().reason());
  });
  // Immediately start cancellation.
  client = {};
  loop.RunUntilIdle();

  ASSERT_TRUE(called);
  // The callback should be destroyed after being called.
  ASSERT_TRUE(destroyed);
}

TEST(Client, ThenExactlyOnce) { ThenExactlyOnceTest<fidl::Client<Example>>(); }

TEST(SharedClient, ThenExactlyOnce) { ThenExactlyOnceTest<fidl::SharedClient<Example>>(); }

}  // namespace
