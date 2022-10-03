// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fidl/cpp/wire/client.h>
#include <lib/fidl/cpp/wire/client_base.h>
#include <lib/fidl/cpp/wire/connect_service.h>
#include <lib/fidl/epitaph.h>
#include <lib/fidl/txn_header.h>
#include <lib/sync/completion.h>
#include <lib/zx/channel.h>
#include <lib/zx/time.h>

#include <mutex>
#include <thread>
#include <vector>

#include <zxtest/zxtest.h>

#include "src/lib/fidl/llcpp/tests/dispatcher/async_loop_and_endpoints_fixture.h"
#include "src/lib/fidl/llcpp/tests/dispatcher/client_checkers.h"
#include "src/lib/fidl/llcpp/tests/dispatcher/fake_sequence_dispatcher.h"
#include "src/lib/fidl/llcpp/tests/dispatcher/lsan_disabler.h"
#include "src/lib/fidl/llcpp/tests/dispatcher/mock_client_impl.h"
#include "src/lib/fidl/llcpp/tests/dispatcher/test_messages.h"

namespace fidl {
namespace {

using ::fidl_testing::ClientBaseSpy;
using ::fidl_testing::ClientChecker;
using ::fidl_testing::TestProtocol;
using ::fidl_testing::TestResponseContext;

// |NormalTeardownObserver| monitors the destruction of an event handler, which
// signals the completion of teardown.
//
// The class also asserts that teardown is initiated by the user, as opposed to
// being triggered by any error.
class NormalTeardownObserver {
 public:
  // Returns the event handler that may be used to observe the completion of
  // unbinding. This method must be called at most once.
  std::unique_ptr<fidl::WireAsyncEventHandler<TestProtocol>> GetEventHandler() {
    ZX_ASSERT(event_handler_);
    return std::move(event_handler_);
  }

  zx_status_t Wait(zx_duration_t timeout = zx::duration::infinite().get()) {
    return sync_completion_wait(&did_teardown_, timeout);
  }

  bool IsTeardown() { return Wait(zx::duration::infinite_past().get()) == ZX_OK; }

 private:
  class EventHandler : public fidl::WireAsyncEventHandler<TestProtocol> {
   public:
    explicit EventHandler(sync_completion_t& did_teardown) : did_teardown_(did_teardown) {}

    void on_fidl_error(::fidl::UnbindInfo error) override {
      ZX_PANIC("Error happened: %s", error.FormatDescription().c_str());
    }

    ~EventHandler() override { sync_completion_signal(&did_teardown_); }

   private:
    sync_completion_t& did_teardown_;
  };

  sync_completion_t did_teardown_;
  std::unique_ptr<fidl::WireAsyncEventHandler<TestProtocol>> event_handler_ =
      std::make_unique<EventHandler>(did_teardown_);
};

TEST(WireSharedClient, Teardown) {
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  ASSERT_OK(loop.StartThread());

  auto endpoints = fidl::CreateEndpoints<TestProtocol>();
  ASSERT_OK(endpoints.status_value());
  auto [local, remote] = std::move(*endpoints);

  NormalTeardownObserver observer;
  WireSharedClient<TestProtocol> client(std::move(local), loop.dispatcher(),
                                        observer.GetEventHandler());

  // Teardown the client and wait for unbind completion notification to happen.
  client.AsyncTeardown();
  EXPECT_OK(observer.Wait());
}

TEST(WireSharedClient, TeardownOnDestroy) {
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  ASSERT_OK(loop.StartThread());

  auto endpoints = fidl::CreateEndpoints<TestProtocol>();
  ASSERT_OK(endpoints.status_value());
  auto [local, remote] = std::move(*endpoints);

  NormalTeardownObserver observer;
  auto* client = new WireSharedClient<TestProtocol>(std::move(local), loop.dispatcher(),
                                                    observer.GetEventHandler());

  // Delete the client and wait for unbind completion notification to happen.
  delete client;
  EXPECT_OK(observer.Wait());
}

TEST(WireSharedClient, NotifyTeardownViaTeardownObserver) {
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  ASSERT_OK(loop.StartThread());

  auto endpoints = fidl::CreateEndpoints<TestProtocol>();
  ASSERT_OK(endpoints.status_value());
  auto [local, remote] = std::move(*endpoints);

  sync_completion_t torn_down_;
  WireSharedClient<TestProtocol> client(
      std::move(local), loop.dispatcher(),
      fidl::ObserveTeardown([&torn_down_] { sync_completion_signal(&torn_down_); }));

  client.AsyncTeardown();
  EXPECT_OK(sync_completion_wait(&torn_down_, zx::duration::infinite().get()));
}

// Cloned clients should operate on the same |ClientImpl|.
TEST(WireSharedClient, Clone) {
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  ASSERT_OK(loop.StartThread());

  auto endpoints = fidl::CreateEndpoints<TestProtocol>();
  ASSERT_OK(endpoints.status_value());

  sync_completion_t did_teardown;
  WireSharedClient<TestProtocol> client;

  class EventHandler : public fidl::WireAsyncEventHandler<TestProtocol> {
   public:
    EventHandler(sync_completion_t& did_teardown, ClientBaseSpy& spy)
        : did_teardown_(did_teardown), spy_(spy) {}

    void on_fidl_error(::fidl::UnbindInfo info) override {
      EXPECT_EQ(fidl::Reason::kPeerClosed, info.reason());
      EXPECT_EQ(ZX_ERR_PEER_CLOSED, info.status());
    }

    ~EventHandler() override {
      // All the transactions should be finished by the time the connection is dropped.
      EXPECT_EQ(0, spy_.GetTxidCount());
      sync_completion_signal(&did_teardown_);
    }

   private:
    sync_completion_t& did_teardown_;
    ClientBaseSpy& spy_;
  };

  ClientBaseSpy spy;
  client.Bind(std::move(endpoints->client), loop.dispatcher(),
              std::make_unique<EventHandler>(did_teardown, spy));
  spy.set_client(client);

  // Create 20 clones of the client, and verify that they can all send messages
  // through the same internal |ClientImpl|.
  constexpr size_t kNumClones = 20;
  std::vector<std::unique_ptr<TestResponseContext>> contexts;
  for (size_t i = 0; i < kNumClones; i++) {
    auto clone = client.Clone();
    // Both clone and the client should delegate to the same underlying binding.
    EXPECT_EQ(ClientChecker::GetClientBase(client), ClientChecker::GetClientBase(clone));
    contexts.emplace_back(std::make_unique<TestResponseContext>(&spy));
    // Generate a txid for a ResponseContext.
    spy.PrepareAsyncTxn(contexts.back().get());
    EXPECT_TRUE(spy.IsPending(contexts.back()->Txid()));
    // Send a "response" message with the same txid from the remote end of the channel.
    fidl_message_header_t hdr;
    fidl::InitTxnHeader(&hdr, contexts.back()->Txid(), 0, fidl::MessageDynamicFlags::kStrictMethod);
    ASSERT_OK(
        endpoints->server.channel().write(0, &hdr, sizeof(fidl_message_header_t), nullptr, 0));
  }

  // Trigger teardown handler.
  endpoints->server.channel().reset();
  EXPECT_OK(sync_completion_wait(&did_teardown, ZX_TIME_INFINITE));
}

// This test performs the following repeatedly:
// - Clone a |fidl::WireSharedClient| to another |fidl::WireSharedClient| variable.
// - Destroy the original by letting it go out of scope.
// - Verify that the new client shares the same internal |ClientBase|, which
//   stores all the transaction state.
TEST(WireSharedClient, CloneCanExtendClientLifetime) {
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);

  auto endpoints = fidl::CreateEndpoints<TestProtocol>();
  ASSERT_OK(endpoints.status_value());

  // We expect normal teardown because it should be triggered by |outer_clone|
  // going out of scope.
  NormalTeardownObserver observer;

  {
    using fidl_testing::ClientChecker;
    fidl::internal::ClientBase* client_ptr = nullptr;
    fidl::WireSharedClient<TestProtocol> outer_clone;
    ASSERT_CLIENT_IMPL_NULL(outer_clone);

    {
      fidl::WireSharedClient<TestProtocol> inner_clone;
      ASSERT_CLIENT_IMPL_NULL(inner_clone);

      {
        fidl::WireSharedClient client(std::move(endpoints->client), loop.dispatcher(),
                                      observer.GetEventHandler());
        ASSERT_CLIENT_IMPL_NOT_NULL(client);
        client_ptr = ClientChecker::GetClientBase(client);

        ASSERT_OK(loop.RunUntilIdle());
        ASSERT_FALSE(observer.IsTeardown());

        // Extend the client lifetime to |inner_clone|.
        inner_clone = client.Clone();
      }

      ASSERT_CLIENT_IMPL_NOT_NULL(inner_clone);
      ASSERT_EQ(ClientChecker::GetClientBase(inner_clone), client_ptr);

      ASSERT_OK(loop.RunUntilIdle());
      ASSERT_FALSE(observer.IsTeardown());

      // Extend the client lifetime to |outer_clone|.
      outer_clone = inner_clone.Clone();
    }

    ASSERT_CLIENT_IMPL_NOT_NULL(outer_clone);
    ASSERT_EQ(ClientChecker::GetClientBase(outer_clone), client_ptr);

    ASSERT_OK(loop.RunUntilIdle());
    ASSERT_FALSE(observer.IsTeardown());
  }

  // Verify that teardown still happens when all the clients
  // referencing the same connection go out of scope.
  ASSERT_OK(loop.RunUntilIdle());
  ASSERT_TRUE(observer.IsTeardown());
}

// Calling |AsyncTeardown| explicitly will cause all clones to unbind.
TEST(WireSharedClient, CloneSupportsExplicitTeardown) {
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);

  auto endpoints = fidl::CreateEndpoints<TestProtocol>();
  ASSERT_OK(endpoints.status_value());

  // We expect normal teardown because we are explicitly calling
  // |AsyncTeardown|.
  NormalTeardownObserver observer;
  fidl::WireSharedClient client(std::move(endpoints->client), loop.dispatcher(),
                                observer.GetEventHandler());
  fidl::WireSharedClient<TestProtocol> clone = client.Clone();

  ASSERT_OK(loop.RunUntilIdle());
  ASSERT_FALSE(observer.IsTeardown());

  using ::fidl_testing::ClientChecker;
  // The channel being managed is still alive.
  ASSERT_NOT_NULL(ClientChecker::GetTransport(clone).get());

  // Now we call |AsyncTeardown| on the main client, the clone would be torn
  // down too.
  client.AsyncTeardown();

  ASSERT_OK(loop.RunUntilIdle());
  EXPECT_TRUE(observer.IsTeardown());
  EXPECT_NULL(ClientChecker::GetTransport(clone).get());
  EXPECT_NULL(ClientChecker::GetTransport(client).get());
}

class WireSharedClientTest : public fidl_testing::AsyncLoopAndEndpointsFixture {};

TEST_F(WireSharedClientTest, DefaultConstruction) {
  WireSharedClient<TestProtocol> client;
  EXPECT_FALSE(client.is_valid());
}

TEST_F(WireSharedClientTest, InvalidAccess) {
  WireSharedClient<TestProtocol> client;
  ASSERT_DEATH([&] { client.operator->(); });
  ASSERT_DEATH([&] {
    fidl::Arena arena;
    client.buffer(arena);
  });
  ASSERT_DEATH([&] { client.sync(); });
  ASSERT_DEATH([&] { client.AsyncTeardown(); });
}

TEST_F(WireSharedClientTest, Move) {
  WireSharedClient<TestProtocol> client;
  client.Bind(std::move(endpoints().client), loop().dispatcher());
  EXPECT_TRUE(client.is_valid());

  WireSharedClient<TestProtocol> client2 = std::move(client);
  EXPECT_FALSE(client.is_valid());
  EXPECT_TRUE(client2.is_valid());
  ASSERT_DEATH([&] { client.operator->(); });
}

TEST_F(WireSharedClientTest, CanMakeCallOnAnotherThread) {
  auto [local, remote] = std::move(endpoints());

  WireSharedClient client(std::move(local), loop().dispatcher());

  std::thread foreign_thread([&] {
    fidl_testing::GoodMessage message;
    fidl::OutgoingMessage outgoing = message.message();
    (void)client->OneWayMethod(outgoing);
  });
  foreign_thread.join();
}

TEST_F(WireSharedClientTest, CanDestroyOnSameSequence) {
  fidl_testing::RunWithLsanDisabled([&] {
    auto [local, remote] = std::move(endpoints());
    fidl_testing::FakeSequenceDispatcher fake_dispatcher(loop().dispatcher());

    fake_dispatcher.SetSequenceId({.value = 1});
    WireSharedClient client(std::move(local), &fake_dispatcher);
    loop().RunUntilIdle();

    ASSERT_NO_DEATH([&] { client = {}; });
  });
}

TEST_F(WireSharedClientTest, CanDestroyOnAnotherSequence) {
  fidl_testing::RunWithLsanDisabled([&] {
    auto [local, remote] = std::move(endpoints());
    fidl_testing::FakeSequenceDispatcher fake_dispatcher(loop().dispatcher());

    fake_dispatcher.SetSequenceId({.value = 1});
    WireSharedClient client(std::move(local), &fake_dispatcher);
    loop().RunUntilIdle();

    fake_dispatcher.SetSequenceId({.value = 2});
    ASSERT_NO_DEATH([&] { client = {}; });
  });
}

}  // namespace
}  // namespace fidl
