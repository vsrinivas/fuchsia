// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/llcpptest.handlerights.test/cpp/wire.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/cpp/task.h>
#include <lib/async/wait.h>
#include <lib/fidl/llcpp/server.h>

#include <gtest/gtest.h>

namespace test = ::llcpptest_handlerights_test;

class HandleRightsServer : public fidl::WireServer<test::HandleRights> {
 public:
  void SyncGetHandleWithTooFewRights(
      SyncGetHandleWithTooFewRightsRequestView request,
      SyncGetHandleWithTooFewRightsCompleter::Sync& completer) override {
    zx::event ev;
    ASSERT_EQ(ZX_OK, zx::event::create(0, &ev));
    ASSERT_EQ(ZX_OK, ev.replace(ZX_RIGHT_TRANSFER, &ev));
    completer.Reply(std::move(ev));
  }
  void AsyncGetHandleWithTooFewRights(
      AsyncGetHandleWithTooFewRightsRequestView request,
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
      SyncGetHandleWithTooManyRightsRequestView request,
      SyncGetHandleWithTooManyRightsCompleter::Sync& completer) override {
    zx::event ev;
    ASSERT_EQ(ZX_OK, zx::event::create(0, &ev));
    completer.Reply(std::move(ev));
  }
  void AsyncGetHandleWithTooManyRights(
      AsyncGetHandleWithTooManyRightsRequestView request,
      AsyncGetHandleWithTooManyRightsCompleter::Sync& completer) override {
    zx::event ev;
    ASSERT_EQ(ZX_OK, zx::event::create(0, &ev));
    completer.Reply(std::move(ev));
  }
  void SyncGetHandleWithWrongType(SyncGetHandleWithWrongTypeRequestView request,
                                  SyncGetHandleWithWrongTypeCompleter::Sync& completer) override {
    zx::event ev;
    ASSERT_EQ(ZX_OK, zx::event::create(0, &ev));
    completer.Reply(zx::channel(ev.release()));
  }
  void AsyncGetHandleWithWrongType(AsyncGetHandleWithWrongTypeRequestView request,
                                   AsyncGetHandleWithWrongTypeCompleter::Sync& completer) override {
    zx::event ev;
    ASSERT_EQ(ZX_OK, zx::event::create(0, &ev));
    completer.Reply(zx::channel(ev.release()));
  }
  void SendEventWithTransferAndSignal(
      SendEventWithTransferAndSignalRequestView request,
      SendEventWithTransferAndSignalCompleter::Sync& completer) override {
    zx_info_handle_basic_t info;
    ASSERT_EQ(ZX_OK,
              request->h.get_info(ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr));
    ASSERT_EQ(ZX_RIGHT_TRANSFER | ZX_RIGHT_SIGNAL, info.rights);
    ASSERT_EQ(ZX_OBJ_TYPE_EVENT, info.type);
  }
  void SendChannel(SendChannelRequestView request, SendChannelCompleter::Sync& completer) override {
    ASSERT_TRUE(false);  // Never should get here.
  }

 private:
  async_dispatcher_t* dispatcher_;
};

class HandleRightsTest : public ::testing::Test {
 protected:
  void SetUp() override {
    loop_ = std::make_unique<async::Loop>(&kAsyncLoopConfigAttachToCurrentThread);
    ASSERT_EQ(loop_->StartThread("test_llcpp_handle_rights_server"), ZX_OK);

    auto endpoints = fidl::CreateEndpoints<test::HandleRights>();
    ASSERT_EQ(endpoints.status_value(), ZX_OK);
    client_end_ = std::move(endpoints->client);
    fidl::BindServer(loop_->dispatcher(), std::move(endpoints->server),
                     std::make_unique<HandleRightsServer>());
  }

  fidl::WireSyncClient<test::HandleRights> SyncClient() {
    EXPECT_TRUE(client_end_.is_valid());
    return fidl::WireSyncClient<test::HandleRights>(std::move(client_end_));
  }

  fidl::WireSharedClient<test::HandleRights> AsyncClient(
      std::shared_ptr<fidl::WireAsyncEventHandler<test::HandleRights>> handler) {
    EXPECT_TRUE(client_end_.is_valid());
    return fidl::WireSharedClient<test::HandleRights>(std::move(client_end_), loop_->dispatcher(),
                                                      handler.get(),
                                                      fidl::ShareUntilTeardown(std::move(handler)));
  }

 private:
  std::unique_ptr<async::Loop> loop_;
  fidl::ClientEnd<test::HandleRights> client_end_;
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
  ASSERT_EQ(resp.reason(), ::fidl::Reason::kTransportError);
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
  ASSERT_EQ(resp.reason(), ::fidl::Reason::kTransportError);
}

TEST_F(HandleRightsTest, AsyncSendTooFewRights) {
  auto client = AsyncClient({});
  zx::event ev;
  ASSERT_EQ(ZX_OK, zx::event::create(0, &ev));
  ASSERT_EQ(ZX_OK, ev.replace(ZX_RIGHT_TRANSFER, &ev));
  auto resp = client->SendEventWithTransferAndSignal(std::move(ev));
  // The channel is closed after a rights error on the sending side.
  ASSERT_EQ(resp.status(), ZX_ERR_INVALID_ARGS);
  ASSERT_EQ(resp.reason(), ::fidl::Reason::kTransportError);
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
  ASSERT_EQ(resp.reason(), ::fidl::Reason::kTransportError);
}
