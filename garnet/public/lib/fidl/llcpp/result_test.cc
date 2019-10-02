// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/wait.h>
#include <lib/fidl-async/cpp/bind.h>
#include <zircon/fidl.h>
#include <zircon/status.h>

#include <llcpptest/result/test/llcpp/fidl.h>

#include "gtest/gtest.h"

namespace test = ::llcpp::llcpptest::result::test;

namespace {
zx_status_t kErrorStatus = 271;
}  // namespace

class Server : public test::ErrorMethods::Interface {
 public:
  void NoArgsPrimitiveError(bool should_error,
                            NoArgsPrimitiveErrorCompleter::Sync completer) override {
    if (should_error) {
      return completer.ReplyError(kErrorStatus);
    } else {
      return completer.ReplySuccess();
    }
  }
  void ManyArgsCustomError(bool should_error,
                           ManyArgsCustomErrorCompleter::Sync completer) override {
    if (should_error) {
      return completer.ReplyError(test::MyError::REALLY_BAD_ERROR);
    } else {
      return completer.ReplySuccess(1, 2, 3);
    }
  }
};

class ResultTest : public ::testing::Test {
 protected:
  virtual void SetUp() {
    loop_ = std::make_unique<async::Loop>(&kAsyncLoopConfigAttachToCurrentThread);
    ASSERT_EQ(loop_->StartThread("test_llcpp_result_server"), ZX_OK);

    zx::channel server_end;
    ASSERT_EQ(zx::channel::create(0, &client_end_, &server_end), ZX_OK);
    server_ = std::make_unique<Server>();
    fidl::Bind(loop_->dispatcher(), std::move(server_end), server_.get());
  }

  virtual void TearDown() {
    loop_->Quit();
    loop_->JoinThreads();
  }

  test::ErrorMethods::SyncClient TakeClient() {
    EXPECT_TRUE(client_end_.is_valid());
    return test::ErrorMethods::SyncClient(std::move(client_end_));
  }

 private:
  std::unique_ptr<async::Loop> loop_;
  std::unique_ptr<Server> server_;
  zx::channel client_end_;
};

TEST_F(ResultTest, OwnedPrimitiveError) {
  auto client = TakeClient();
  auto resp = client.NoArgsPrimitiveError(true);
  EXPECT_TRUE(resp.ok());
  EXPECT_TRUE(resp->result.is_err());
  EXPECT_EQ(resp->result.err(), kErrorStatus);
}

TEST_F(ResultTest, OwnedCustomError) {
  auto client = TakeClient();
  auto resp = client.ManyArgsCustomError(true);
  EXPECT_TRUE(resp.ok());
  EXPECT_TRUE(resp->result.is_err());
  EXPECT_EQ(resp->result.err(), test::MyError::REALLY_BAD_ERROR);
}

TEST_F(ResultTest, OwnedSuccessNoArgs) {
  auto client = TakeClient();
  auto resp = client.NoArgsPrimitiveError(false);
  ASSERT_TRUE(resp.ok());
  ASSERT_TRUE(resp->result.is_response());
}

TEST_F(ResultTest, OwnedSuccessManyArgs) {
  auto client = TakeClient();
  auto resp = client.ManyArgsCustomError(false);
  ASSERT_TRUE(resp.ok());
  ASSERT_TRUE(resp->result.is_response());
  const auto& success = resp->result.response();
  ASSERT_EQ(success.a, 1);
  ASSERT_EQ(success.b, 2);
  ASSERT_EQ(success.c, 3);
}
