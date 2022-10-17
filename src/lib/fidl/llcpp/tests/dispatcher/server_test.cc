// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fidl/cpp/wire/connect_service.h>
#include <lib/fidl/cpp/wire/server.h>
#include <lib/fidl/cpp/wire/wire_messaging.h>
#include <lib/sync/completion.h>

#include <mutex>
#include <thread>
#include <vector>

#include <zxtest/zxtest.h>

#include "lsan_disabler.h"

//
// Mock FIDL protocol and its |WireServer| definition.
//

namespace fidl_test {
namespace {
class TestProtocol {
 public:
  TestProtocol() = delete;

  using Transport = fidl::internal::ChannelTransport;
  using WeakEventSender = fidl::internal::WireWeakEventSender<fidl_test::TestProtocol>;
};
}  // namespace
}  // namespace fidl_test

template <>
class ::fidl::internal::WireWeakEventSender<fidl_test::TestProtocol> {
 public:
  explicit WireWeakEventSender(std::weak_ptr<fidl::internal::AsyncServerBinding>&& binding) {}
};

template <>
class ::fidl::WireServer<fidl_test::TestProtocol>
    : public ::fidl::internal::IncomingMessageDispatcher {
 public:
  WireServer() = default;
  ~WireServer() override = default;

  using _EnclosingProtocol = fidl_test::TestProtocol;
  using _Transport = fidl::internal::ChannelTransport;

 private:
  void dispatch_message(::fidl::IncomingHeaderAndMessage&& msg, ::fidl::Transaction* txn,
                        internal::MessageStorageViewBase* storage_view) final {
    std::move(msg).CloseHandles();
    txn->InternalError(::fidl::UnbindInfo::UnknownOrdinal(), ::fidl::ErrorOrigin::kReceive);
  }
};

namespace {

class TestServer : public fidl::WireServer<fidl_test::TestProtocol> {};

//
// Tests covering the error behavior of |BindServer|.
//

TEST(BindServerTestCase, DispatcherWasShutDown) {
  zx::result endpoints = fidl::CreateEndpoints<fidl_test::TestProtocol>();
  ASSERT_OK(endpoints.status_value());
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  ASSERT_OK(loop.StartThread());

  loop.Shutdown();

  ASSERT_DEATH(([&] {
    fidl_testing::RunWithLsanDisabled([&] {
      fidl::BindServer(loop.dispatcher(), std::move(endpoints->server),
                       std::make_unique<TestServer>());
    });
  }));
}

TEST(BindServerTestCase, InsufficientChannelRights) {
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  zx::result endpoints = fidl::CreateEndpoints<fidl_test::TestProtocol>();
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

  zx::result endpoints = fidl::CreateEndpoints<fidl_test::TestProtocol>();
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

// Test the behavior of |fidl::internal::[Try]Dispatch| in case of a message
// with an error.
TEST(TryDispatchTestCase, MessageStatusNotOk) {
  class MockTransaction : public fidl::Transaction {
   public:
    bool errored() const { return errored_; }

   private:
    std::unique_ptr<Transaction> TakeOwnership() final { ZX_PANIC("Not used"); }
    zx_status_t Reply(fidl::OutgoingMessage* message, fidl::WriteOptions) final {
      ZX_PANIC("Not used");
    }
    void Close(zx_status_t epitaph) final { ZX_PANIC("Not used"); }
    void InternalError(fidl::UnbindInfo error, fidl::ErrorOrigin origin) final {
      EXPECT_FALSE(errored_);
      EXPECT_EQ(fidl::ErrorOrigin::kReceive, origin);
      EXPECT_EQ(fidl::Reason::kTransportError, error.reason());
      EXPECT_STATUS(ZX_ERR_BAD_HANDLE, error.status());
      errored_ = true;
    }

    bool errored_ = false;
  };

  {
    auto msg =
        fidl::IncomingHeaderAndMessage::Create(fidl::Status::TransportError(ZX_ERR_BAD_HANDLE));
    MockTransaction txn;
    fidl::DispatchResult result =
        fidl::internal::TryDispatch(nullptr, msg, nullptr, &txn, nullptr, nullptr);
    EXPECT_EQ(fidl::DispatchResult::kFound, result);
    EXPECT_TRUE(txn.errored());
  }

  {
    auto msg =
        fidl::IncomingHeaderAndMessage::Create(fidl::Status::TransportError(ZX_ERR_BAD_HANDLE));
    MockTransaction txn;
    fidl::internal::Dispatch(
        nullptr, msg, nullptr, &txn, nullptr, nullptr,
        &::fidl::internal::UnknownMethodHandlerEntry::kClosedProtocolHandlerEntry);
    EXPECT_TRUE(txn.errored());
  }
}

}  // namespace
