// Copyright 2020 The Fuchsia Authors. All rights reserved.
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
using ::fidl_testing::TestProtocol;
using ::fidl_testing::TestResponseContext;

//
// Client binding/transaction bookkeeping tests
//

TEST(ClientBindingTestCase, AsyncTxn) {
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  ASSERT_OK(loop.StartThread());

  auto endpoints = fidl::CreateEndpoints<TestProtocol>();
  ASSERT_OK(endpoints.status_value());
  auto [local, remote] = std::move(*endpoints);

  sync_completion_t unbound;
  WireSharedClient<TestProtocol> client;

  class EventHandler : public fidl::WireAsyncEventHandler<TestProtocol> {
   public:
    EventHandler(sync_completion_t& unbound, ClientBaseSpy& spy) : unbound_(unbound), spy_(spy) {}

    void on_fidl_error(::fidl::UnbindInfo info) override {
      EXPECT_EQ(fidl::Reason::kPeerClosed, info.reason());
      EXPECT_EQ(ZX_ERR_PEER_CLOSED, info.status());
      EXPECT_EQ("FIDL endpoint was unbound due to peer closed, status: ZX_ERR_PEER_CLOSED (-24)",
                info.FormatDescription());
      EXPECT_EQ(0, spy_.GetTxidCount());
      sync_completion_signal(&unbound_);
    }

   private:
    sync_completion_t& unbound_;
    ClientBaseSpy& spy_;
  };

  ClientBaseSpy spy;
  client.Bind(std::move(local), loop.dispatcher(), std::make_unique<EventHandler>(unbound, spy));
  spy.set_client(client);

  // Generate a txid for a ResponseContext. Send a "response" message with the same txid from the
  // remote end of the channel.
  TestResponseContext context(&spy);
  spy.PrepareAsyncTxn(&context);
  EXPECT_TRUE(spy.IsPending(context.Txid()));
  fidl_message_header_t hdr;
  fidl::InitTxnHeader(&hdr, context.Txid(), 0, fidl::MessageDynamicFlags::kStrictMethod);
  ASSERT_OK(remote.channel().write(0, &hdr, sizeof(fidl_message_header_t), nullptr, 0));

  // Trigger unbound handler.
  remote.reset();
  EXPECT_OK(sync_completion_wait(&unbound, ZX_TIME_INFINITE));
}

TEST(ClientBindingTestCase, ParallelAsyncTxns) {
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  ASSERT_OK(loop.StartThread());

  auto endpoints = fidl::CreateEndpoints<TestProtocol>();
  ASSERT_OK(endpoints.status_value());
  auto [local, remote] = std::move(*endpoints);

  sync_completion_t unbound;
  WireSharedClient<TestProtocol> client;

  class EventHandler : public fidl::WireAsyncEventHandler<TestProtocol> {
   public:
    EventHandler(sync_completion_t& unbound, ClientBaseSpy& spy) : unbound_(unbound), spy_(spy) {}

    void on_fidl_error(::fidl::UnbindInfo info) override {
      EXPECT_EQ(fidl::Reason::kPeerClosed, info.reason());
      EXPECT_EQ(ZX_ERR_PEER_CLOSED, info.status());
      EXPECT_EQ(0, spy_.GetTxidCount());
      sync_completion_signal(&unbound_);
    }

   private:
    sync_completion_t& unbound_;
    ClientBaseSpy& spy_;
  };

  ClientBaseSpy spy;
  client.Bind(std::move(local), loop.dispatcher(), std::make_unique<EventHandler>(unbound, spy));
  spy.set_client(client);

  // In parallel, simulate 10 async transactions and send "response" messages from the remote end
  // of the channel.
  std::vector<std::unique_ptr<TestResponseContext>> contexts;
  std::thread threads[10];
  for (int i = 0; i < 10; ++i) {
    contexts.emplace_back(std::make_unique<TestResponseContext>(&spy));
    threads[i] = std::thread([context = contexts[i].get(), remote = &remote.channel(), &spy] {
      spy.PrepareAsyncTxn(context);
      EXPECT_TRUE(spy.IsPending(context->Txid()));
      fidl_message_header_t hdr;
      fidl::InitTxnHeader(&hdr, context->Txid(), 0, fidl::MessageDynamicFlags::kStrictMethod);
      ASSERT_OK(remote->write(0, &hdr, sizeof(fidl_message_header_t), nullptr, 0));
    });
  }
  for (int i = 0; i < 10; ++i)
    threads[i].join();

  // Trigger unbound handler.
  remote.reset();
  EXPECT_OK(sync_completion_wait(&unbound, ZX_TIME_INFINITE));
}

TEST(ClientBindingTestCase, ForgetAsyncTxn) {
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  ASSERT_OK(loop.StartThread());

  auto endpoints = fidl::CreateEndpoints<TestProtocol>();
  ASSERT_OK(endpoints.status_value());
  auto [local, remote] = std::move(*endpoints);

  WireSharedClient<TestProtocol> client(std::move(local), loop.dispatcher());

  // Generate a txid for a ResponseContext.
  ClientBaseSpy spy{client};
  TestResponseContext context(&spy);
  spy.PrepareAsyncTxn(&context);
  EXPECT_TRUE(spy.IsPending(context.Txid()));

  // Forget the transaction.
  spy.ForgetAsyncTxn(&context);
  EXPECT_EQ(0, spy.GetTxidCount());
}

TEST(ClientBindingTestCase, UnknownResponseTxid) {
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  ASSERT_OK(loop.StartThread());

  auto endpoints = fidl::CreateEndpoints<TestProtocol>();
  ASSERT_OK(endpoints.status_value());
  auto [local, remote] = std::move(*endpoints);

  sync_completion_t unbound;
  WireSharedClient<TestProtocol> client;

  class EventHandler : public fidl::WireAsyncEventHandler<TestProtocol> {
   public:
    EventHandler(sync_completion_t& unbound, ClientBaseSpy& spy) : unbound_(unbound), spy_(spy) {}

    void on_fidl_error(::fidl::UnbindInfo info) override {
      EXPECT_EQ(fidl::Reason::kUnexpectedMessage, info.reason());
      EXPECT_EQ(ZX_ERR_NOT_FOUND, info.status());
      EXPECT_EQ(
          "FIDL endpoint was unbound due to unexpected message, "
          "status: ZX_ERR_NOT_FOUND (-25), detail: unknown txid",
          info.FormatDescription());
      EXPECT_EQ(0, spy_.GetTxidCount());
      sync_completion_signal(&unbound_);
    }

   private:
    sync_completion_t& unbound_;
    ClientBaseSpy& spy_;
  };

  ClientBaseSpy spy;
  client.Bind(std::move(local), loop.dispatcher(), std::make_unique<EventHandler>(unbound, spy));
  spy.set_client(client);

  // Send a "response" message for which there was no outgoing request.
  ASSERT_EQ(0, spy.GetTxidCount());
  fidl_message_header_t hdr;
  fidl::InitTxnHeader(&hdr, 1, 0, fidl::MessageDynamicFlags::kStrictMethod);
  ASSERT_OK(remote.channel().write(0, &hdr, sizeof(fidl_message_header_t), nullptr, 0));

  // on_unbound should be triggered by the erroneous response.
  EXPECT_OK(sync_completion_wait(&unbound, ZX_TIME_INFINITE));
}

TEST(ClientBindingTestCase, Events) {
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  ASSERT_OK(loop.StartThread());

  auto endpoints = fidl::CreateEndpoints<TestProtocol>();
  ASSERT_OK(endpoints.status_value());
  auto [local, remote] = std::move(*endpoints);

  sync_completion_t unbound;
  WireSharedClient<TestProtocol> client;

  class EventHandler : public fidl::WireAsyncEventHandler<TestProtocol> {
   public:
    EventHandler(sync_completion_t& unbound) : unbound_(unbound) {}

    void on_fidl_error(::fidl::UnbindInfo info) override {
      EXPECT_EQ(fidl::Reason::kPeerClosed, info.reason());
      EXPECT_EQ(ZX_ERR_PEER_CLOSED, info.status());
      EXPECT_EQ(10, event_count());  // Expect 10 events.
      sync_completion_signal(&unbound_);
    }

   private:
    sync_completion_t& unbound_;
  };

  client.Bind(std::move(local), loop.dispatcher(), std::make_unique<EventHandler>(unbound));

  // In parallel, send 10 event messages from the remote end of the channel.
  std::thread threads[10];
  for (int i = 0; i < 10; ++i) {
    threads[i] = std::thread([remote = &remote.channel()] {
      fidl_message_header_t hdr;
      fidl::InitTxnHeader(&hdr, 0, 0, fidl::MessageDynamicFlags::kStrictMethod);
      ASSERT_OK(remote->write(0, &hdr, sizeof(fidl_message_header_t), nullptr, 0));
    });
  }
  for (int i = 0; i < 10; ++i)
    threads[i].join();

  // Trigger unbound handler.
  remote.reset();
  EXPECT_OK(sync_completion_wait(&unbound, ZX_TIME_INFINITE));
}

TEST(ClientBindingTestCase, UnbindWhileActiveChannelRefs) {
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  ASSERT_OK(loop.StartThread());

  auto endpoints = fidl::CreateEndpoints<TestProtocol>();
  ASSERT_OK(endpoints.status_value());
  auto [local, remote] = std::move(*endpoints);

  sync_completion_t unbound;

  class EventHandler : public fidl::WireAsyncEventHandler<TestProtocol> {
   public:
    explicit EventHandler(sync_completion_t& unbound) : unbound_(unbound) {}

    void on_fidl_error(::fidl::UnbindInfo info) override {
      // Manually-initiated teardown is not an error.
      FAIL();
      sync_completion_signal(&unbound_);
    }

    ~EventHandler() override { sync_completion_signal(&unbound_); }

   private:
    sync_completion_t& unbound_;
  };

  WireSharedClient<TestProtocol> client(std::move(local), loop.dispatcher(),
                                        std::make_unique<EventHandler>(unbound));

  // Create a strong reference to the channel.
  using ::fidl_testing::ClientChecker;
  std::shared_ptr channel = ClientChecker::GetTransport(client);

  // |AsyncTeardown| and the teardown notification should not be blocked by the
  // channel reference.
  client.AsyncTeardown();
  EXPECT_OK(sync_completion_wait(&unbound, ZX_TIME_INFINITE));

  // Check that the channel handle is still valid.
  EXPECT_OK(zx_object_get_info(channel->get<fidl::internal::ChannelTransport>()->get(),
                               ZX_INFO_HANDLE_VALID, nullptr, 0, nullptr, nullptr));
}

class OnCanceledTestResponseContext : public internal::ResponseContext {
 public:
  explicit OnCanceledTestResponseContext(sync_completion_t* done)
      : internal::ResponseContext(0), done_(done) {}
  std::optional<fidl::UnbindInfo> OnRawResult(
      fidl::IncomingHeaderAndMessage&& msg,
      fidl::internal::MessageStorageViewBase* storage_view) override {
    if (!msg.ok() && msg.reason() == fidl::Reason::kUnbind) {
      // We expect cancellation.
      sync_completion_signal(done_);
      delete this;
      return std::nullopt;
    }
    ADD_FAILURE("Should not be reached");
    delete this;
    return std::nullopt;
  }
  sync_completion_t* done_;
};

TEST(ClientBindingTestCase, ReleaseOutstandingTxnsOnDestroy) {
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  ASSERT_OK(loop.StartThread());

  auto endpoints = fidl::CreateEndpoints<TestProtocol>();
  ASSERT_OK(endpoints.status_value());
  auto [local, remote] = std::move(*endpoints);

  auto* client = new WireSharedClient<TestProtocol>(std::move(local), loop.dispatcher());
  ClientBaseSpy spy{*client};

  // Create and register a response context which will signal when deleted.
  sync_completion_t done;
  spy.PrepareAsyncTxn(new OnCanceledTestResponseContext(&done));

  // Delete the client and ensure that the response context is deleted.
  delete client;
  EXPECT_OK(sync_completion_wait(&done, ZX_TIME_INFINITE));
}

class OnErrorTestResponseContext : public internal::ResponseContext {
 public:
  explicit OnErrorTestResponseContext(sync_completion_t* done, fidl::Reason expected_reason)
      : internal::ResponseContext(0), done_(done), expected_reason_(expected_reason) {}
  std::optional<fidl::UnbindInfo> OnRawResult(
      fidl::IncomingHeaderAndMessage&& msg,
      fidl::internal::MessageStorageViewBase* storage_view) override {
    EXPECT_TRUE(!msg.ok());
    EXPECT_EQ(expected_reason_, msg.error().reason());
    sync_completion_signal(done_);
    delete this;
    return std::nullopt;
  }
  sync_completion_t* done_;
  fidl::Reason expected_reason_;
};

TEST(ClientBindingTestCase, ReleaseOutstandingTxnsOnPeerClosed) {
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  ASSERT_OK(loop.StartThread());

  auto endpoints = fidl::CreateEndpoints<TestProtocol>();
  ASSERT_OK(endpoints.status_value());
  auto [local, remote] = std::move(*endpoints);

  WireSharedClient<TestProtocol> client(std::move(local), loop.dispatcher());

  // Create and register a response context which will signal when deleted.
  sync_completion_t done;
  ClientBaseSpy spy{client};
  spy.PrepareAsyncTxn(new OnErrorTestResponseContext(&done, fidl::Reason::kPeerClosed));

  // Close the server end and wait for the transaction context to be released.
  remote.reset();
  EXPECT_OK(sync_completion_wait(&done, ZX_TIME_INFINITE));
}

// Test receiving different values of epitaphs.
class ClientReceiveEpitaphTest : public fidl_testing::AsyncLoopAndEndpointsFixture {
  void SetUp() override {
    ASSERT_NO_FAILURES(AsyncLoopAndEndpointsFixture::SetUp());
    ASSERT_OK(loop().StartThread());
  }
};

TEST_F(ClientReceiveEpitaphTest, OkEpitpah) {
  auto [local, remote] = std::move(endpoints());
  sync_completion_t unbound;

  class EventHandler : public fidl::WireAsyncEventHandler<TestProtocol> {
   public:
    explicit EventHandler(sync_completion_t& unbound) : unbound_(unbound) {}

    void on_fidl_error(::fidl::UnbindInfo info) override {
      // An epitaph value of ZX_OK is defined to indicate normal closure.
      EXPECT_TRUE(info.is_peer_closed());
      EXPECT_FALSE(info.is_user_initiated());
      EXPECT_EQ(fidl::Reason::kPeerClosed, info.reason());
      EXPECT_EQ(ZX_OK, info.status());
      sync_completion_signal(&unbound_);
    }

   private:
    sync_completion_t& unbound_;
  };

  WireSharedClient<TestProtocol> client(std::move(local), loop().dispatcher(),
                                        std::make_unique<EventHandler>(unbound));

  // Send an epitaph and wait for on_unbound to run.
  ASSERT_OK(fidl_epitaph_write(remote.channel().get(), ZX_OK));
  EXPECT_OK(sync_completion_wait(&unbound, ZX_TIME_INFINITE));
}

TEST_F(ClientReceiveEpitaphTest, NonOkEpitaph) {
  auto [local, remote] = std::move(endpoints());
  sync_completion_t unbound;

  class EventHandler : public fidl::WireAsyncEventHandler<TestProtocol> {
   public:
    explicit EventHandler(sync_completion_t& unbound) : unbound_(unbound) {}

    void on_fidl_error(::fidl::UnbindInfo info) override {
      EXPECT_TRUE(info.is_peer_closed());
      EXPECT_FALSE(info.is_user_initiated());
      EXPECT_EQ(fidl::Reason::kPeerClosed, info.reason());
      EXPECT_EQ(ZX_ERR_BAD_STATE, info.status());
      sync_completion_signal(&unbound_);
    }

   private:
    sync_completion_t& unbound_;
  };

  WireSharedClient<TestProtocol> client(std::move(local), loop().dispatcher(),
                                        std::make_unique<EventHandler>(unbound));

  // Send an epitaph and wait for on_unbound to run.
  ASSERT_OK(fidl_epitaph_write(remote.channel().get(), ZX_ERR_BAD_STATE));
  EXPECT_OK(sync_completion_wait(&unbound, ZX_TIME_INFINITE));
}

TEST_F(ClientReceiveEpitaphTest, PeerClosedNoEpitaph) {
  auto [local, remote] = std::move(endpoints());
  sync_completion_t unbound;

  class EventHandler : public fidl::WireAsyncEventHandler<TestProtocol> {
   public:
    explicit EventHandler(sync_completion_t& unbound) : unbound_(unbound) {}

    void on_fidl_error(::fidl::UnbindInfo info) override {
      EXPECT_TRUE(info.is_peer_closed());
      EXPECT_FALSE(info.is_user_initiated());
      EXPECT_EQ(fidl::Reason::kPeerClosed, info.reason());
      // No epitaph is equivalent to ZX_ERR_PEER_CLOSED epitaph.
      EXPECT_EQ(ZX_ERR_PEER_CLOSED, info.status());
      sync_completion_signal(&unbound_);
    }

   private:
    sync_completion_t& unbound_;
  };

  WireSharedClient<TestProtocol> client(std::move(local), loop().dispatcher(),
                                        std::make_unique<EventHandler>(unbound));

  // Close the server end and wait for on_unbound to run.
  remote.reset();
  EXPECT_OK(sync_completion_wait(&unbound, ZX_TIME_INFINITE));
}

//
// Client wrapper tests
//
class WireClientTest : public fidl_testing::AsyncLoopAndEndpointsFixture {};

TEST_F(WireClientTest, DefaultConstruction) {
  WireClient<TestProtocol> client;
  EXPECT_FALSE(client.is_valid());
}

TEST_F(WireClientTest, InvalidAccess) {
  WireClient<TestProtocol> client;
  ASSERT_DEATH([&] { client.operator->(); });
  ASSERT_DEATH([&] {
    fidl::Arena arena;
    client.buffer(arena);
  });
  ASSERT_DEATH([&] { client.sync(); });
}

TEST_F(WireClientTest, Move) {
  WireClient<TestProtocol> client;
  client.Bind(std::move(endpoints().client), loop().dispatcher());
  EXPECT_TRUE(client.is_valid());

  WireClient<TestProtocol> client2 = std::move(client);
  EXPECT_FALSE(client.is_valid());
  EXPECT_TRUE(client2.is_valid());
  ASSERT_DEATH([&] { client.operator->(); });
}

TEST_F(WireClientTest, UseOnDispatcherThread) {
  auto [local, remote] = std::move(endpoints());

  std::optional<fidl::UnbindInfo> error;
  std::thread::id error_handling_thread;
  class EventHandler : public fidl::WireAsyncEventHandler<TestProtocol> {
   public:
    explicit EventHandler(std::optional<fidl::UnbindInfo>& error,
                          std::thread::id& error_handling_thread)
        : error_(error), error_handling_thread_(error_handling_thread) {}
    void on_fidl_error(fidl::UnbindInfo info) override {
      error_handling_thread_ = std::this_thread::get_id();
      error_ = info;
    }

   private:
    std::optional<fidl::UnbindInfo>& error_;
    std::thread::id& error_handling_thread_;
  };
  EventHandler handler(error, error_handling_thread);

  // Create the client on the current thread.
  WireClient client(std::move(local), loop().dispatcher(), &handler);

  // Dispatch messages on the current thread.
  ASSERT_OK(loop().RunUntilIdle());

  // Trigger an error; receive |on_fidl_error| on the same thread.
  ASSERT_FALSE(error.has_value());
  remote.reset();
  ASSERT_OK(loop().RunUntilIdle());
  ASSERT_TRUE(error.has_value());
  ASSERT_EQ(std::this_thread::get_id(), error_handling_thread);

  // Destroy the client on the same thread.
  client = {};
}

TEST_F(WireClientTest, CannotDestroyOnAnotherThread) {
  fidl_testing::RunWithLsanDisabled([&] {
    auto [local, remote] = std::move(endpoints());

    WireClient client(std::move(local), loop().dispatcher());
    remote.reset();

    // Panics when a foreign thread attempts to destroy the client.
#if ZX_DEBUG_ASSERT_IMPLEMENTED
    std::thread foreign_thread(
        [&] { ASSERT_DEATH([&] { fidl_testing::RunWithLsanDisabled([&] { client = {}; }); }); });
    foreign_thread.join();
#endif
  });
}

TEST_F(WireClientTest, CannotMakeCallOnAnotherThread) {
  fidl_testing::RunWithLsanDisabled([&] {
    auto [local, remote] = std::move(endpoints());

    WireClient client(std::move(local), loop().dispatcher());

#if ZX_DEBUG_ASSERT_IMPLEMENTED
    std::thread foreign_thread([&] {
      ASSERT_DEATH([&] {
        fidl_testing::RunWithLsanDisabled([&] {
          fidl_testing::GoodMessage message;
          fidl::OutgoingMessage outgoing = message.message();
          (void)client->OneWayMethod(outgoing);
        });
      });
    });
    foreign_thread.join();
#endif
  });
}

TEST_F(WireClientTest, CanDestroyOnSameSequence) {
  auto [local, remote] = std::move(endpoints());
  fidl_testing::FakeSequenceDispatcher fake_dispatcher(loop().dispatcher());

  fake_dispatcher.SetSequenceId({.value = 1});
  WireClient client(std::move(local), &fake_dispatcher);
  loop().RunUntilIdle();

  // Will not panic when another thread attempts to destroy the client,
  // as long as the thread has the same sequence ID.
  std::thread t([&] { ASSERT_NO_DEATH([&] { client = {}; }); });
  t.join();
}

TEST_F(WireClientTest, CannotDestroyOnAnotherSequence) {
  fidl_testing::RunWithLsanDisabled([&] {
    auto [local, remote] = std::move(endpoints());
    fidl_testing::FakeSequenceDispatcher fake_dispatcher(loop().dispatcher());

    fake_dispatcher.SetSequenceId({.value = 1});
    WireClient client(std::move(local), &fake_dispatcher);
    loop().RunUntilIdle();

    // Panics when a thread with a different sequence ID attempts to destroy the client.
#if ZX_DEBUG_ASSERT_IMPLEMENTED
    fake_dispatcher.SetSequenceId({.value = 2});
    ASSERT_DEATH([&] { fidl_testing::RunWithLsanDisabled([&] { client = {}; }); });
#endif
  });
}

TEST_F(WireClientTest, CanShutdownLoopFromAnotherThread) {
  auto [local, remote] = std::move(endpoints());

  WireClient client(std::move(local), loop().dispatcher());

  std::thread foreign_thread([&] { loop().Shutdown(); });
  foreign_thread.join();
}

TEST_F(WireClientTest, CanShutdownLoopFromAnotherThreadWhileWorkingThreadIsRunning) {
  auto [local, remote] = std::move(endpoints());

  loop().StartThread();
  WireClient client(std::move(local), loop().dispatcher());

  // Async teardown work may happen on |foreign_thread| or the worker thread
  // started by |StartThread|, but we should support both.
  std::thread foreign_thread([&] { loop().Shutdown(); });
  foreign_thread.join();
}

TEST_F(WireClientTest, CanShutdownLoopFromAnotherThreadWhileTeardownIsPending) {
  auto [local, remote] = std::move(endpoints());

  WireClient client(std::move(local), loop().dispatcher());
  client = {};

  // Allow any async teardown work to happen on |foreign_thread|.
  std::thread foreign_thread([&] { loop().Shutdown(); });
  foreign_thread.join();
}

TEST_F(WireClientTest, CannotDispatchOnAnotherThread) {
  fidl_testing::RunWithLsanDisabled([&] {
    auto [local, remote] = std::move(endpoints());

    WireClient client(std::move(local), loop().dispatcher());
    remote.reset();

    // Panics when a different thread attempts to dispatch the error.
#if ZX_DEBUG_ASSERT_IMPLEMENTED
    std::thread foreign_thread([&] {
      ASSERT_DEATH([&] { fidl_testing::RunWithLsanDisabled([&] { loop().RunUntilIdle(); }); });
    });
    foreign_thread.join();
#endif
  });
}

}  // namespace
}  // namespace fidl
