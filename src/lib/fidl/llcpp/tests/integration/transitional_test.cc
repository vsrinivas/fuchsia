// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/llcpptest.transitional.test/cpp/wire.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/wait.h>
#include <lib/fidl-async/cpp/bind.h>
#include <zircon/fidl.h>
#include <zircon/status.h>

#include <gtest/gtest.h>

namespace test = ::llcpptest_transitional_test;

namespace {

class Server : public fidl::WireServer<test::TransitionMethods> {
 public:
  void ImplementedMethod(ImplementedMethodRequestView request,
                         ImplementedMethodCompleter::Sync& txn) override {
    // Reply call to maintain an open connection.
    txn.Reply(fidl::StringView("test reply"));
  }

  void Bind(fidl::ServerEnd<test::TransitionMethods> server, async::Loop* loop) {
    zx_status_t bind_status =
        fidl::BindSingleInFlightOnly(loop->dispatcher(), std::move(server), this);
    EXPECT_EQ(bind_status, ZX_OK);
  }
};

class TransitionalTest : public ::testing::Test {
 protected:
  virtual void SetUp() {
    loop_ = std::make_unique<async::Loop>(&kAsyncLoopConfigAttachToCurrentThread);
    ASSERT_EQ(loop_->StartThread("test_llcpp_transitional_server"), ZX_OK);
    auto endpoints = fidl::CreateEndpoints<test::TransitionMethods>();
    ASSERT_EQ(endpoints.status_value(), ZX_OK);
    client_end_ = std::move(endpoints->client);
    server_ = std::make_unique<Server>();
    server_->Bind(std::move(endpoints->server), loop_.get());
  }

  virtual void TearDown() {
    loop_->Quit();
    loop_->JoinThreads();
  }

  fidl::WireSyncClient<test::TransitionMethods> TakeClient() {
    EXPECT_TRUE(client_end_.is_valid());
    return fidl::WireSyncClient<test::TransitionMethods>(std::move(client_end_));
  }

 private:
  std::unique_ptr<async::Loop> loop_;
  std::unique_ptr<Server> server_;
  fidl::ClientEnd<test::TransitionMethods> client_end_;
};

// The implemented call should succeed.
TEST_F(TransitionalTest, CallImplementedMethod) {
  auto client = TakeClient();
  auto result = client.ImplementedMethod();
  EXPECT_TRUE(result.ok());
  ASSERT_EQ(result.status(), ZX_OK) << zx_status_get_string(result.status());
}

// The unimplemented transitional call should error with not supported in an
// epitaph. However, epitaphs are currently not supported (fxbug.dev/35445) so the
// server closes the connection on an unsupported call. This results in a peer
// connection closed error instead.
TEST_F(TransitionalTest, CallUnimplementedMethod) {
  auto client = TakeClient();
  auto result = client.UnimplementedMethod();
  EXPECT_FALSE(result.ok());
  EXPECT_EQ("FIDL operation failed due to peer closed, status: ZX_ERR_PEER_CLOSED (-24)",
            result.FormatDescription());
  ASSERT_EQ(result.status(), ZX_ERR_PEER_CLOSED) << zx_status_get_string(result.status());
}

}  // namespace
