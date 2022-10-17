// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_FIDL_LLCPP_TESTS_DISPATCHER_ASYNC_LOOP_AND_ENDPOINTS_FIXTURE_H_
#define SRC_LIB_FIDL_LLCPP_TESTS_DISPATCHER_ASYNC_LOOP_AND_ENDPOINTS_FIXTURE_H_

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fidl/cpp/wire/client.h>
#include <lib/fidl/cpp/wire/client_base.h>
#include <lib/fidl/cpp/wire/connect_service.h>

#include <zxtest/zxtest.h>

#include "mock_client_impl.h"

namespace fidl_testing {

// A test fixture that sets up a pair of endpoints and an async loop.
// Prefer subclassing it to create more specific test fixtures.
class AsyncLoopAndEndpointsFixture : public zxtest::Test {
 public:
  AsyncLoopAndEndpointsFixture() : loop_(&kAsyncLoopConfigNoAttachToCurrentThread) {}

  void SetUp() override {
    zx::result endpoints = fidl::CreateEndpoints<TestProtocol>();
    ASSERT_OK(endpoints.status_value());
    endpoints_ = std::move(*endpoints);
  }

  async::Loop& loop() { return loop_; }

  fidl::Endpoints<TestProtocol>& endpoints() { return endpoints_; }

 private:
  async::Loop loop_;
  fidl::Endpoints<TestProtocol> endpoints_;
};

}  // namespace fidl_testing

#endif  // SRC_LIB_FIDL_LLCPP_TESTS_DISPATCHER_ASYNC_LOOP_AND_ENDPOINTS_FIXTURE_H_
