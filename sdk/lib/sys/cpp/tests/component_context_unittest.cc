// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fdio/directory.h>
#include <lib/gtest/real_loop_fixture.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/zx/channel.h>

#include "echo_server.h"
#include "gtest/gtest.h"

class ComponentContextTest : public gtest::RealLoopFixture {
 public:
  void SetUp() override {
    ASSERT_EQ(ZX_OK, zx::channel::create(0, &outgoing_client_, &outgoing_server_));
  }
 protected:
  void QueryEcho() {
    fdio_service_connect_at(
        outgoing_client_.get(), "svc/fidl.examples.echo.Echo",
        echo_client_.NewRequest(dispatcher()).TakeChannel().release());

    echo_client_->EchoString("hello", [this](fidl::StringPtr value) { echo_result_ = *value; });
  }

  void PublishEcho() {
    ASSERT_EQ(ZX_OK, context_->outgoing()->AddPublicService(echo_impl_.GetHandler(dispatcher())));
  }

  zx::channel outgoing_client_;
  zx::channel outgoing_server_;
  std::unique_ptr<sys::ComponentContext> context_;

  EchoImpl echo_impl_;
  fidl::examples::echo::EchoPtr echo_client_;
  std::string echo_result_ = "no callback";
};

TEST_F(ComponentContextTest, ServeInConstructor) {
  // Try connecting to a service and call it before it's published.
  QueryEcho();

  // Starts serving outgoing directory immediately. Will process Echo request
  // the next time async loop will run.
  context_ = std::make_unique<sys::ComponentContext>(
      sys::ServiceDirectory::CreateFromNamespace(), std::move(outgoing_server_));

  // Now publish the service. It's not too late as long as the run loop hasn't
  // run after ComponentContext creation.
  PublishEcho();

  // Echo connection requests should be processed now.
  RunLoopUntilIdle();

  EXPECT_EQ("hello", echo_result_);
}

TEST_F(ComponentContextTest, DelayedServe) {
  // Doesn't start serving outgoing directory.
  context_ = std::make_unique<sys::ComponentContext>(sys::ServiceDirectory::CreateFromNamespace());

  // Try connecting to a service and call it before it's published.
  QueryEcho();
  RunLoopUntilIdle();

  // Now publish the service and start serving the directory.
  PublishEcho();
  context_->outgoing()->Serve(std::move(outgoing_server_));

  RunLoopUntilIdle();

  EXPECT_EQ("hello", echo_result_);
}
