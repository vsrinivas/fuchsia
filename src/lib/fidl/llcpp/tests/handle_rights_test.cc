// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/cpp/task.h>
#include <lib/async/wait.h>
#include <lib/fidl-async/cpp/bind.h>
#include <lib/fidl/llcpp/server.h>

#include <gtest/gtest.h>
#include <llcpptest/handlerights/test/llcpp/fidl.h>

namespace test = ::llcpp::llcpptest::handlerights::test;

class HandleRightsServer : public test::HandleRights::Interface {
 public:
  void SyncGetHandleWithTooFewRights(
      SyncGetHandleWithTooFewRightsCompleter::Sync& completer) override {
    zx::event ev;
    ASSERT_EQ(ZX_OK, zx::event::create(0, &ev));
    ASSERT_EQ(ZX_OK, ev.replace(ZX_RIGHT_TRANSFER, &ev));
    completer.Reply(std::move(ev));
  }
  void AsyncGetHandleWithTooFewRights(
      AsyncGetHandleWithTooFewRightsCompleter::Sync& completer) override {
    async::PostDelayedTask(
        dispatcher_,
        [completer = completer.ToAsync()]() mutable {
          zx::event ev;
          ASSERT_EQ(ZX_OK, zx::event::create(0, &ev));
          ASSERT_EQ(ZX_OK, ev.replace(ZX_RIGHT_TRANSFER, &ev));
          completer.Reply(std::move(ev));
        },
        zx::duration::infinite());
  }
  void SyncGetHandleWithTooManyRights(
      SyncGetHandleWithTooManyRightsCompleter::Sync& completer) override {
    zx::event ev;
    ASSERT_EQ(ZX_OK, zx::event::create(0, &ev));
    completer.Reply(std::move(ev));
  }
  void AsyncGetHandleWithTooManyRights(
      AsyncGetHandleWithTooManyRightsCompleter::Sync& completer) override {
    zx::event ev;
    ASSERT_EQ(ZX_OK, zx::event::create(0, &ev));
    completer.Reply(std::move(ev));
  }
  void SyncGetHandleWithWrongType(SyncGetHandleWithWrongTypeCompleter::Sync& completer) override {
    zx::event ev;
    ASSERT_EQ(ZX_OK, zx::event::create(0, &ev));
    completer.Reply(zx::channel(ev.release()));
  }
  void AsyncGetHandleWithWrongType(AsyncGetHandleWithWrongTypeCompleter::Sync& completer) override {
    zx::event ev;
    ASSERT_EQ(ZX_OK, zx::event::create(0, &ev));
    completer.Reply(zx::channel(ev.release()));
  }
  void SendEventWithTransferAndSignal(
      zx::event event, SendEventWithTransferAndSignalCompleter::Sync& completer) override {
    zx_info_handle_basic_t info;
    ASSERT_EQ(ZX_OK, event.get_info(ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr));
    ASSERT_EQ(ZX_RIGHT_TRANSFER | ZX_RIGHT_SIGNAL, info.rights);
    ASSERT_EQ(ZX_OBJ_TYPE_EVENT, info.type);
  }
  void SendChannel(zx::channel channel, SendChannelCompleter::Sync& completer) override {
    ASSERT_TRUE(false);  // Never should get here.
  }

 private:
  async_dispatcher_t* dispatcher_;
};

class HandleRightsTest : public ::testing::Test {
 protected:
  virtual void SetUp() {
    loop_ = std::make_unique<async::Loop>(&kAsyncLoopConfigAttachToCurrentThread);
    ASSERT_EQ(loop_->StartThread("test_llcpp_handle_rights_server"), ZX_OK);

    zx::channel server_end;
    ASSERT_EQ(zx::channel::create(0, &client_end_, &server_end), ZX_OK);
    server_ = std::make_unique<HandleRightsServer>();
    fidl::BindSingleInFlightOnly(loop_->dispatcher(), std::move(server_end), server_.get());
  }

  test::HandleRights::SyncClient SyncClient() {
    EXPECT_TRUE(client_end_.is_valid());
    return test::HandleRights::SyncClient(std::move(client_end_));
  }

  fidl::Client<test::HandleRights> AsyncClient(test::HandleRights::AsyncEventHandlers handlers) {
    EXPECT_TRUE(client_end_.is_valid());
    return fidl::Client<test::HandleRights>(std::move(client_end_), loop_->dispatcher(), std::move(handlers));
  }

 private:
  std::unique_ptr<async::Loop> loop_;
  std::unique_ptr<HandleRightsServer> server_;
  zx::channel client_end_;
};

TEST_F(HandleRightsTest, SyncGetTooFewRights) {
  auto client = SyncClient();
  auto resp = client.SyncGetHandleWithTooFewRights();
  // The channel is closed after a rights error on the sending side.
  ASSERT_EQ(resp.status(), ZX_ERR_PEER_CLOSED);
}

TEST_F(HandleRightsTest, SyncGetTooManyRights) {
  auto client = SyncClient();
  auto resp = client.SyncGetHandleWithTooManyRights();
  ASSERT_TRUE(resp.ok());
  zx_info_handle_basic_t info;
  ASSERT_EQ(ZX_OK, resp->h.get_info(ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr));
  EXPECT_EQ(ZX_RIGHT_TRANSFER | ZX_RIGHT_SIGNAL, info.rights);
  EXPECT_EQ(ZX_OBJ_TYPE_EVENT, info.type);
}

TEST_F(HandleRightsTest, SyncGetWrongType) {
  auto client = SyncClient();
  auto resp = client.SyncGetHandleWithWrongType();
  // The channel is closed after a object type error on the sending side.
  ASSERT_EQ(resp.status(), ZX_ERR_PEER_CLOSED);
}

TEST_F(HandleRightsTest, SyncSendTooFewRights) {
  auto client = SyncClient();
  zx::event ev;
  ASSERT_EQ(ZX_OK, zx::event::create(0, &ev));
  ASSERT_EQ(ZX_OK, ev.replace(ZX_RIGHT_TRANSFER, &ev));
  auto resp = client.SendEventWithTransferAndSignal(std::move(ev));
  // The channel is closed after a rights error on the sending side.
  ASSERT_EQ(resp.status(), ZX_ERR_INVALID_ARGS);
}

TEST_F(HandleRightsTest, SyncSendTooManyRights) {
  auto client = SyncClient();
  zx::event ev;
  ASSERT_EQ(ZX_OK, zx::event::create(0, &ev));
  auto resp = client.SendEventWithTransferAndSignal(std::move(ev));
  ASSERT_TRUE(resp.ok());
}

TEST_F(HandleRightsTest, SyncSendWrongType) {
  auto client = SyncClient();
  zx::event ev;
  ASSERT_EQ(ZX_OK, zx::event::create(0, &ev));
  // Send the event as a channel (type error).
  auto resp = client.SendChannel(zx::channel(ev.release()));
  // The channel is closed after a type error on the sending side.
  ASSERT_EQ(resp.status(), ZX_ERR_WRONG_TYPE);
}

/* TODO(fxb/65577) Re-enable these tests.
TEST_F(HandleRightsTest, AsyncSendTooFewRights) {
  auto client = AsyncClient({});
  zx::event ev;
  ASSERT_EQ(ZX_OK, zx::event::create(0, &ev));
  ASSERT_EQ(ZX_OK, ev.replace(ZX_RIGHT_TRANSFER, &ev));
  auto resp = client->SendEventWithTransferAndSignal(std::move(ev));
  // The channel is closed after a rights error on the sending side.
  ASSERT_EQ(resp.status(), ZX_ERR_INVALID_ARGS);
}

TEST_F(HandleRightsTest, AsyncSendTooManyRights) {
  auto client = AsyncClient({});
  zx::event ev;
  ASSERT_EQ(ZX_OK, zx::event::create(0, &ev));
  auto resp = client->SendEventWithTransferAndSignal(std::move(ev));
  ASSERT_TRUE(resp.ok());
}

TEST_F(HandleRightsTest, AsyncSendWrongType) {
  auto client = AsyncClient({});
  zx::event ev;
  ASSERT_EQ(ZX_OK, zx::event::create(0, &ev));
  // Send the event as a channel (type error).
  auto resp = client->SendChannel(zx::channel(ev.release()));
  // The channel is closed after a type error on the sending side.
  ASSERT_EQ(resp.status(), ZX_ERR_WRONG_TYPE);
}
*/
