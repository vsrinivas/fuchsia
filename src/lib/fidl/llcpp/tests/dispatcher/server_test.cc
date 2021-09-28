// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fidl/llcpp/connect_service.h>
#include <lib/fidl/llcpp/server.h>
#include <lib/fidl/llcpp/wire_messaging.h>
#include <lib/sync/completion.h>

#include <mutex>
#include <thread>
#include <vector>

#include <zxtest/zxtest.h>

//
// Mock FIDL protocol and its |WireServer| definition.
//

namespace fidl_test {
namespace {
class TestProtocol {
 public:
  TestProtocol() = delete;

  using WeakEventSender = fidl::internal::WireWeakEventSender<fidl_test::TestProtocol>;
  using EventSender = fidl::WireEventSender<fidl_test::TestProtocol>;
};
}  // namespace
}  // namespace fidl_test

template <>
class ::fidl::internal::WireWeakEventSender<fidl_test::TestProtocol> {
 public:
  explicit WireWeakEventSender(std::weak_ptr<fidl::internal::AsyncServerBinding>&& binding) {}
};

template <>
class ::fidl::WireEventSender<fidl_test::TestProtocol> {
 public:
  explicit WireEventSender(fidl::ServerEnd<fidl_test::TestProtocol> server_end)
      : server_end_(std::move(server_end)) {}

  zx::channel& channel() { return server_end_.channel(); }

 private:
  fidl::ServerEnd<fidl_test::TestProtocol> server_end_;
};

template <>
class ::fidl::WireServer<fidl_test::TestProtocol>
    : public ::fidl::internal::IncomingMessageDispatcher {
 public:
  WireServer() = default;
  ~WireServer() override = default;

  using _EnclosingProtocol = fidl_test::TestProtocol;

 private:
  void dispatch_message(::fidl::IncomingMessage&& msg, ::fidl::Transaction* txn) final {
    std::move(msg).CloseHandles();
    txn->InternalError(::fidl::UnbindInfo::UnknownOrdinal());
  }
};

namespace {

class TestServer : public fidl::WireServer<fidl_test::TestProtocol> {};

//
// Tests covering the error behavior of |BindServer|.
//

TEST(BindServerTestCase, DispatcherWasShutDown) {
  zx::status endpoints = fidl::CreateEndpoints<fidl_test::TestProtocol>();
  ASSERT_OK(endpoints.status_value());
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  ASSERT_OK(loop.StartThread());

  loop.Shutdown();

  ASSERT_DEATH(([&] {
    fidl::BindServer(loop.dispatcher(), std::move(endpoints->server),
                     std::make_unique<TestServer>());
  }));
}

TEST(BindServerTestCase, InsufficientChannelRights) {
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  zx::status endpoints = fidl::CreateEndpoints<fidl_test::TestProtocol>();
  ASSERT_OK(endpoints.status_value());

  auto [client_end, server_end] = std::move(*endpoints);
  zx::channel server_channel_reduced_rights;
  ASSERT_OK(server_end.channel().replace(ZX_RIGHT_NONE, &server_channel_reduced_rights));
  server_end.channel() = std::move(server_channel_reduced_rights);

  sync_completion_t unbound;
  fidl::OnUnboundFn<TestServer> on_unbound = [&](TestServer*, fidl::UnbindInfo info,
                                                 fidl::ServerEnd<fidl_test::TestProtocol>) {
    EXPECT_EQ(info.reason(), fidl::Reason::kDispatcherError);
    EXPECT_EQ(info.status(), ZX_ERR_ACCESS_DENIED);
    sync_completion_signal(&unbound);
  };
  fidl::BindServer(loop.dispatcher(), std::move(server_end), std::make_unique<TestServer>(),
                   std::move(on_unbound));

  ASSERT_OK(loop.RunUntilIdle());
  ASSERT_OK(sync_completion_wait(&unbound, ZX_TIME_INFINITE));
  ASSERT_OK(client_end.channel().wait_one(ZX_CHANNEL_PEER_CLOSED, zx::time::infinite(), nullptr));
}

TEST(BindServerTestCase, PeerAlreadyClosed) {
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  ASSERT_OK(loop.StartThread());

  zx::status endpoints = fidl::CreateEndpoints<fidl_test::TestProtocol>();
  ASSERT_OK(endpoints.status_value());

  endpoints->client.reset();

  sync_completion_t unbound;
  fidl::OnUnboundFn<TestServer> on_unbound = [&](TestServer*, fidl::UnbindInfo info,
                                                 fidl::ServerEnd<fidl_test::TestProtocol>) {
    EXPECT_EQ(info.reason(), fidl::Reason::kPeerClosed);
    EXPECT_EQ(info.status(), ZX_ERR_PEER_CLOSED);
    sync_completion_signal(&unbound);
  };
  fidl::BindServer(loop.dispatcher(), std::move(endpoints->server), std::make_unique<TestServer>(),
                   std::move(on_unbound));

  ASSERT_OK(loop.RunUntilIdle());
  ASSERT_OK(sync_completion_wait(&unbound, ZX_TIME_INFINITE));
}

}  // namespace
