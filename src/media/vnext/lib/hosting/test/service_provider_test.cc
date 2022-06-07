// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/vnext/lib/hosting/service_provider.h"

#include <fuchsia/examples/cpp/fidl.h>

#include <gtest/gtest.h>

#include "src/lib/testing/loop_fixture/real_loop_fixture.h"

namespace fmlib {
namespace {

// NOTE: |ServiceProvider| is tested primarily in |nonce_fidl_server_test.cc| and
// |single_fidl_server_test.cc|. These tests provide additional coverage.

class ServiceProviderTest : public gtest::RealLoopFixture {
 public:
  ServiceProviderTest() : thread_(fmlib::Thread::CreateForLoop(loop())) {}

  // The thread on which this test was created.
  Thread& thread() { return thread_; }

 private:
  Thread thread_;
};

class TestServiceBinder : public ServiceBinder {
 public:
  explicit TestServiceBinder(bool& deleted) : deleted_(deleted) {}
  ~TestServiceBinder() override { deleted_ = true; }

  void Bind(zx::channel channel) override {}

 private:
  bool& deleted_;
};

// Tests |ServiceProvider::ConnectToService| when called with an unrecognized service path.
TEST_F(ServiceProviderTest, Punt) {
  ServiceProvider service_provider(thread());

  // Connect.
  fuchsia::examples::EchoPtr echo_ptr =
      service_provider.ConnectToService<fuchsia::examples::Echo>();

  std::optional<zx_status_t> error_status;
  echo_ptr.set_error_handler([&error_status](zx_status_t status) { error_status = status; });

  RunLoopUntilIdle();
  EXPECT_TRUE(error_status.has_value());
  EXPECT_EQ(ZX_ERR_PEER_CLOSED, error_status.value());
}

// Tests |ServiceProvider::ClearRegisteredServices|.
TEST_F(ServiceProviderTest, ClearRegisteredServices) {
  ServiceProvider service_provider(thread());

  bool deleted = false;
  service_provider.RegisterService("matters not", std::make_unique<TestServiceBinder>(deleted));
  RunLoopUntilIdle();
  EXPECT_FALSE(deleted);

  service_provider.ClearRegisteredServices();
  RunLoopUntilIdle();
  EXPECT_TRUE(deleted);
}

}  // namespace
}  // namespace fmlib
