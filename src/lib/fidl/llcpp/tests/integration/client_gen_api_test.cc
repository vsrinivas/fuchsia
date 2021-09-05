// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fidl.test.coding.fuchsia/cpp/wire.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/cpp/time.h>
#include <lib/fidl/epitaph.h>
#include <lib/fidl/llcpp/server.h>
#include <lib/fit/defer.h>
#include <lib/stdcompat/type_traits.h>
#include <lib/sync/completion.h>
#include <lib/sync/cpp/completion.h>
#include <lib/zx/channel.h>
#include <lib/zx/eventpair.h>
#include <string.h>
#include <zircon/types.h>

#include <zxtest/zxtest.h>

namespace {

using ::fidl_test_coding_fuchsia::Example;

class Server : public fidl::WireServer<Example> {
 public:
  explicit Server(const char* data, size_t size) : data_(data), size_(size) {}

  void TwoWay(TwoWayRequestView request, TwoWayCompleter::Sync& completer) override {
    ASSERT_EQ(size_, request->in.size());
    EXPECT_EQ(0, strncmp(data_, request->in.data(), size_));
    completer.Reply(request->in);
  }

  void OneWay(OneWayRequestView, OneWayCompleter::Sync&) override {}

 private:
  const char* data_;
  size_t size_;
};

TEST(GenAPITestCase, TwoWayAsyncManaged) {
  auto endpoints = fidl::CreateEndpoints<Example>();
  ASSERT_OK(endpoints.status_value());
  auto [local, remote] = std::move(*endpoints);

  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  ASSERT_OK(loop.StartThread());
  fidl::WireSharedClient<Example> client(std::move(local), loop.dispatcher());

  static constexpr char data[] = "TwoWay() sync managed";
  auto server_binding = fidl::BindServer(loop.dispatcher(), std::move(remote),
                                         std::make_unique<Server>(data, strlen(data)));

  sync_completion_t done;
  client->TwoWay(fidl::StringView(data),
                 [&done](fidl::WireUnownedResult<Example::TwoWay>&& result) {
                   ASSERT_OK(result.status());
                   ASSERT_EQ(strlen(data), result->out.size());
                   EXPECT_EQ(0, strncmp(result->out.data(), data, strlen(data)));
                   sync_completion_signal(&done);
                 });
  ASSERT_OK(sync_completion_wait(&done, ZX_TIME_INFINITE));

  server_binding.Unbind();
}

TEST(GenAPITestCase, TwoWayAsyncCallerAllocated) {
  class ResponseContext final : public fidl::WireResponseContext<Example::TwoWay> {
   public:
    ResponseContext(sync_completion_t* done, const char* data, size_t size)
        : done_(done), data_(data), size_(size) {}

    void OnResult(fidl::WireUnownedResult<Example::TwoWay>&& result) override {
      ASSERT_OK(result.status());
      auto& out = result->out;
      ASSERT_EQ(size_, out.size());
      EXPECT_EQ(0, strncmp(out.data(), data_, size_));
      sync_completion_signal(done_);
    }

   private:
    sync_completion_t* done_;
    const char* data_;
    size_t size_;
  };

  auto endpoints = fidl::CreateEndpoints<Example>();
  ASSERT_OK(endpoints.status_value());
  auto [local, remote] = std::move(*endpoints);

  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  ASSERT_OK(loop.StartThread());
  fidl::WireSharedClient<Example> client(std::move(local), loop.dispatcher());

  static constexpr char data[] = "TwoWay() sync caller-allocated";
  auto server_binding = fidl::BindServer(loop.dispatcher(), std::move(remote),
                                         std::make_unique<Server>(data, strlen(data)));

  sync_completion_t done;
  fidl::Buffer<fidl::WireRequest<Example::TwoWay>> buffer;
  ResponseContext context(&done, data, strlen(data));
  client->TwoWay(buffer.view(), fidl::StringView(data), &context);
  ASSERT_OK(sync_completion_wait(&done, ZX_TIME_INFINITE));

  server_binding.Unbind();
}

TEST(GenAPITestCase, EventManaged) {
  auto endpoints = fidl::CreateEndpoints<Example>();
  ASSERT_OK(endpoints.status_value());
  auto [local, remote] = std::move(*endpoints);

  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  ASSERT_OK(loop.StartThread());

  static constexpr char data[] = "OnEvent() managed";
  class EventHandler : public fidl::WireAsyncEventHandler<Example> {
   public:
    EventHandler() = default;

    sync_completion_t& done() { return done_; }

    void OnEvent(fidl::WireResponse<Example::OnEvent>* event) {
      ASSERT_EQ(strlen(data), event->out.size());
      EXPECT_EQ(0, strncmp(event->out.data(), data, strlen(data)));
      sync_completion_signal(&done_);
    }

   private:
    sync_completion_t done_;
  };

  auto event_handler = std::make_shared<EventHandler>();
  fidl::WireSharedClient<Example> client(std::move(local), loop.dispatcher(), event_handler.get(),
                                         fidl::ShareUntilTeardown(event_handler));

  auto server_binding = fidl::BindServer(loop.dispatcher(), std::move(remote),
                                         std::make_unique<Server>(data, strlen(data)));

  // Wait for the event from the server.
  ASSERT_OK(server_binding->OnEvent(fidl::StringView(data)));
  ASSERT_OK(sync_completion_wait(&event_handler->done(), ZX_TIME_INFINITE));

  server_binding.Unbind();
}

TEST(GenAPITestCase, ConsumeEventsWhenEventHandlerIsAbsent) {
  auto endpoints = fidl::CreateEndpoints<Example>();
  ASSERT_OK(endpoints.status_value());
  auto [local, remote] = std::move(*endpoints);

  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  ASSERT_OK(loop.StartThread());

  fidl::WireSharedClient<Example> client(std::move(local), loop.dispatcher());
  fidl::WireEventSender<Example> event_sender(std::move(remote));

  // Send an unhandled event. The event should be silently discarded since
  // the user did not provide an event handler.
  zx::eventpair ep1, ep2;
  ASSERT_OK(zx::eventpair::create(0, &ep1, &ep2));
  ASSERT_OK(event_sender.OnResourceEvent(std::move(ep1)));
  zx_signals_t observed;
  ASSERT_OK(zx_object_wait_one(ep2.get(), ZX_EVENTPAIR_PEER_CLOSED, ZX_TIME_INFINITE, &observed));
  ASSERT_EQ(ZX_EVENTPAIR_PEER_CLOSED, observed);
}

TEST(GenAPITestCase, ConsumeEventsWhenEventHandlerMethodIsAbsent) {
  auto endpoints = fidl::CreateEndpoints<Example>();
  ASSERT_OK(endpoints.status_value());
  auto [local, remote] = std::move(*endpoints);

  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  ASSERT_OK(loop.StartThread());

  class EventHandler : public fidl::WireAsyncEventHandler<Example> {};

  fidl::WireSharedClient<Example> client(std::move(local), loop.dispatcher(),
                                         std::make_unique<EventHandler>());
  fidl::WireEventSender<Example> event_sender(std::move(remote));

  // Send an unhandled event. The event should be silently discarded since
  // the user did not provide a handler method for |OnResourceEvent|.
  zx::eventpair ep1, ep2;
  ASSERT_OK(zx::eventpair::create(0, &ep1, &ep2));
  ASSERT_OK(event_sender.OnResourceEvent(std::move(ep1)));
  zx_signals_t observed;
  ASSERT_OK(zx_object_wait_one(ep2.get(), ZX_EVENTPAIR_PEER_CLOSED, ZX_TIME_INFINITE, &observed));
  ASSERT_EQ(ZX_EVENTPAIR_PEER_CLOSED, observed);
}

// This is test is almost identical to ClientBindingTestCase.Epitaph in llcpp_client_test.cc but
// validates the part of the flow that's handled in the generated code.
TEST(GenAPITestCase, Epitaph) {
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  ASSERT_OK(loop.StartThread());

  auto endpoints = fidl::CreateEndpoints<Example>();
  ASSERT_OK(endpoints.status_value());
  auto [local, remote] = std::move(*endpoints);

  sync_completion_t unbound;

  class EventHandler : public fidl::WireAsyncEventHandler<Example> {
   public:
    explicit EventHandler(sync_completion_t& unbound) : unbound_(unbound) {}

    void on_fidl_error(fidl::UnbindInfo info) override {
      EXPECT_EQ(fidl::Reason::kPeerClosed, info.reason());
      EXPECT_EQ(ZX_ERR_BAD_STATE, info.status());
      sync_completion_signal(&unbound_);
    };

   private:
    sync_completion_t& unbound_;
  };

  fidl::WireSharedClient<Example> client(std::move(local), loop.dispatcher(),
                                         std::make_unique<EventHandler>(unbound));

  // Send an epitaph and wait for on_unbound to run.
  ASSERT_OK(fidl_epitaph_write(remote.channel().get(), ZX_ERR_BAD_STATE));
  EXPECT_OK(sync_completion_wait(&unbound, ZX_TIME_INFINITE));
}

TEST(GenAPITestCase, UnbindInfoEncodeError) {
  class ErrorServer : public fidl::WireServer<Example> {
   public:
    explicit ErrorServer() = default;

    void TwoWay(TwoWayRequestView request, TwoWayCompleter::Sync& completer) override {
      // Fail to send the reply due to an encoding error (the buffer is too small).
      // The buffer still needs to be properly aligned.
      constexpr size_t kSmallSize = 8;
      FIDL_ALIGNDECL uint8_t small_buffer[kSmallSize];
      static_assert(sizeof(fidl::WireResponse<Example::TwoWay>) > kSmallSize);
      fidl::BufferSpan too_small(small_buffer, std::size(small_buffer));
      EXPECT_EQ(ZX_ERR_BUFFER_TOO_SMALL, completer.Reply(too_small, request->in).status());
      completer.Close(ZX_OK);  // This should not panic.
    }

    void OneWay(OneWayRequestView, OneWayCompleter::Sync&) override {}
  };

  auto endpoints = fidl::CreateEndpoints<Example>();
  ASSERT_OK(endpoints.status_value());
  auto [local, remote] = std::move(*endpoints);

  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  ASSERT_OK(loop.StartThread());
  fidl::WireSharedClient<Example> client(std::move(local), loop.dispatcher());

  sync_completion_t done;
  fidl::OnUnboundFn<ErrorServer> on_unbound =
      [&done](ErrorServer*, fidl::UnbindInfo info,
              fidl::ServerEnd<fidl_test_coding_fuchsia::Example>) {
        EXPECT_EQ(fidl::Reason::kEncodeError, info.reason());
        EXPECT_EQ(ZX_ERR_BUFFER_TOO_SMALL, info.status());
        sync_completion_signal(&done);
      };
  auto server = std::make_unique<ErrorServer>();
  auto server_binding =
      fidl::BindServer(loop.dispatcher(), std::move(remote), server.get(), std::move(on_unbound));

  // Make a synchronous call which should fail as a result of the server end closing.
  auto result = client->TwoWay_Sync(fidl::StringView(""));
  ASSERT_FALSE(result.ok());
  EXPECT_EQ(ZX_ERR_PEER_CLOSED, result.status());

  // Wait for the unbound handler to run.
  ASSERT_OK(sync_completion_wait(&done, ZX_TIME_INFINITE));
}

TEST(GenAPITestCase, UnbindInfoDecodeError) {
  auto endpoints = fidl::CreateEndpoints<Example>();
  ASSERT_OK(endpoints.status_value());
  auto [local, remote] = std::move(*endpoints);

  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  ASSERT_OK(loop.StartThread());
  sync_completion_t done;

  class EventHandler : public fidl::WireAsyncEventHandler<Example> {
   public:
    explicit EventHandler(sync_completion_t& done) : done_(done) {}

    void OnEvent(fidl::WireResponse<Example::OnEvent>* event) override {
      FAIL();
      sync_completion_signal(&done_);
    }

    void on_fidl_error(fidl::UnbindInfo info) override {
      EXPECT_EQ(fidl::Reason::kDecodeError, info.reason());
      sync_completion_signal(&done_);
    };

   private:
    sync_completion_t& done_;
  };

  fidl::WireSharedClient<Example> client(std::move(local), loop.dispatcher(),
                                         std::make_unique<EventHandler>(done));

  // Set up an Example.OnEvent() message but send it without the payload. This should trigger a
  // decoding error.
  fidl::WireResponse<Example::OnEvent> resp{fidl::StringView("")};
  fidl::OwnedEncodedMessage<fidl::WireResponse<Example::OnEvent>> encoded(&resp);
  ASSERT_TRUE(encoded.ok());
  auto bytes = encoded.GetOutgoingMessage().CopyBytes();
  ASSERT_OK(remote.channel().write(0, bytes.data(), sizeof(fidl_message_header_t), nullptr, 0));

  ASSERT_OK(sync_completion_wait(&done, ZX_TIME_INFINITE));
}

// After a client is unbound, no more calls can be made on that client.
TEST(GenAPITestCase, UnbindPreventsSubsequentCalls) {
  // Use a server to count the number of |OneWay| calls.
  class Server : public fidl::WireServer<Example> {
   public:
    Server() = default;

    void TwoWay(TwoWayRequestView request, TwoWayCompleter::Sync& completer) override {
      ZX_PANIC("Not used in this test");
    }

    void OneWay(OneWayRequestView, OneWayCompleter::Sync&) override { num_one_way_.fetch_add(1); }

    int num_one_way() const { return num_one_way_.load(); }

   private:
    std::atomic<int> num_one_way_ = 0;
  };

  auto endpoints = fidl::CreateEndpoints<Example>();

  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  fidl::WireSharedClient<Example> client(std::move(endpoints->client), loop.dispatcher());

  auto server = std::make_unique<Server>();
  auto* server_ptr = server.get();
  auto server_binding =
      fidl::BindServer(loop.dispatcher(), std::move(endpoints->server), std::move(server));

  ASSERT_OK(loop.RunUntilIdle());
  EXPECT_EQ(0, server_ptr->num_one_way());

  ASSERT_OK(client->OneWay("foo").status());

  ASSERT_OK(loop.RunUntilIdle());
  EXPECT_EQ(1, server_ptr->num_one_way());

  client.AsyncTeardown();
  ASSERT_OK(loop.RunUntilIdle());
  EXPECT_EQ(1, server_ptr->num_one_way());

  ASSERT_EQ(ZX_ERR_CANCELED, client->OneWay("foo").status());
  ASSERT_OK(loop.RunUntilIdle());
  EXPECT_EQ(1, server_ptr->num_one_way());
}

fidl::Endpoints<Example> CreateEndpointsWithoutClientWriteRight() {
  zx::status endpoints = fidl::CreateEndpoints<Example>();
  EXPECT_OK(endpoints.status_value());
  if (!endpoints.is_ok())
    return {};

  auto [client_end, server_end] = std::move(*endpoints);
  {
    zx::channel client_channel_non_writable;
    EXPECT_OK(
        client_end.channel().replace(ZX_RIGHT_READ | ZX_RIGHT_WAIT, &client_channel_non_writable));
    client_end.channel() = std::move(client_channel_non_writable);
  }

  return fidl::Endpoints<Example>{std::move(client_end), std::move(server_end)};
}

class ExpectErrorResponseContext final : public fidl::WireResponseContext<Example::TwoWay> {
 public:
  explicit ExpectErrorResponseContext(sync_completion_t* did_error, zx_status_t expected_status,
                                      fidl::Reason expected_reason)
      : did_error_(did_error),
        expected_status_(expected_status),
        expected_reason_(expected_reason) {}

  void OnResult(fidl::WireUnownedResult<Example::TwoWay>&& result) override {
    EXPECT_TRUE(!result.ok());
    EXPECT_STATUS(expected_status_, result.status());
    EXPECT_EQ(expected_reason_, result.error().reason());
    sync_completion_signal(did_error_);
  }

 private:
  sync_completion_t* did_error_;
  zx_status_t expected_status_;
  fidl::Reason expected_reason_;
};

// If writing to the channel fails, the response context ownership should be
// released back to the user with a call to |OnError|.
TEST(GenAPITestCase, ResponseContextOwnershipReleasedOnError) {
  fidl::Endpoints<Example> endpoints;
  ASSERT_NO_FAILURES(endpoints = CreateEndpointsWithoutClientWriteRight());
  auto [client_end, server_end] = std::move(endpoints);

  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  fidl::WireSharedClient<Example> client(std::move(client_end), loop.dispatcher());
  loop.StartThread("client-test");

  sync::Completion error;
  ExpectErrorResponseContext context(error.get(), ZX_ERR_ACCESS_DENIED,
                                     fidl::Reason::kTransportError);

  fidl::Buffer<fidl::WireRequest<Example::TwoWay>> buffer;
  client->TwoWay(buffer.view(), "foo", &context);
  ASSERT_OK(error.Wait());
}

TEST(GenAPITestCase, AsyncNotifySendError) {
  auto do_test = [](auto&& client_instance_indicator) {
    using ClientType = cpp20::remove_cvref_t<decltype(client_instance_indicator)>;
    fidl::Endpoints<Example> endpoints;
    ASSERT_NO_FAILURES(endpoints = CreateEndpointsWithoutClientWriteRight());
    auto [local, remote] = std::move(endpoints);

    async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
    ClientType client(std::move(local), loop.dispatcher());

    sync::Completion error;
    ExpectErrorResponseContext context(error.get(), ZX_ERR_ACCESS_DENIED,
                                       fidl::Reason::kTransportError);

    fidl::Buffer<fidl::WireRequest<Example::TwoWay>> buffer;
    client->TwoWay(buffer.view(), "foo", &context);
    // The context should be asynchronously notified.
    EXPECT_FALSE(error.signaled());
    loop.RunUntilIdle();
    EXPECT_TRUE(error.signaled());
  };

  do_test(fidl::WireClient<Example>{});
  do_test(fidl::WireSharedClient<Example>{});
}

TEST(GenAPITestCase, AsyncNotifyTeardownError) {
  fidl::Endpoints<Example> endpoints;
  ASSERT_NO_FAILURES(endpoints = CreateEndpointsWithoutClientWriteRight());
  auto [local, remote] = std::move(endpoints);

  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  fidl::WireSharedClient<Example> client(std::move(local), loop.dispatcher());
  client.AsyncTeardown();
  loop.RunUntilIdle();

  sync::Completion error;
  ExpectErrorResponseContext context(error.get(), ZX_ERR_CANCELED, fidl::Reason::kUnbind);

  fidl::Buffer<fidl::WireRequest<Example::TwoWay>> buffer;
  client->TwoWay(buffer.view(), "foo", &context);
  EXPECT_FALSE(error.signaled());
  loop.RunUntilIdle();
  EXPECT_TRUE(error.signaled());
}

TEST(GenAPITestCase, SyncNotifyErrorIfDispatcherShutdown) {
  auto do_test = [](auto&& client_instance_indicator) {
    using ClientType = cpp20::remove_cvref_t<decltype(client_instance_indicator)>;
    fidl::Endpoints<Example> endpoints;
    ASSERT_NO_FAILURES(endpoints = CreateEndpointsWithoutClientWriteRight());
    auto [local, remote] = std::move(endpoints);

    async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
    ClientType client(std::move(local), loop.dispatcher());

    sync::Completion error;
    // Note that the reason is |kUnbind| because shutting down the loop will
    // synchronously teardown the client. Once the internal bindings object is
    // destroyed, the client would forget what was the original reason for
    // teardown (kDispatcherError).
    //
    // We may want to improve the post-teardown error fidelity by remembering
    // the reason on the client object.
    ExpectErrorResponseContext context(error.get(), ZX_ERR_CANCELED, fidl::Reason::kUnbind);

    loop.Shutdown();
    EXPECT_FALSE(error.signaled());

    fidl::Buffer<fidl::WireRequest<Example::TwoWay>> buffer;
    client->TwoWay(buffer.view(), "foo", &context);
    // If the loop was shutdown, |context| should still be notified, although
    // it has to happen on the current stack frame.
    EXPECT_TRUE(error.signaled());
  };

  do_test(fidl::WireClient<Example>{});
  do_test(fidl::WireSharedClient<Example>{});
}

// An integration-style test that verifies that user-supplied async callbacks
// that takes |fidl::WireResponse| are not invoked when the binding is torn down
// by the user (i.e. explicit cancellation) instead of due to errors.
TEST(GenAPITestCase, SkipCallingInFlightResponseCallbacksDuringCancellation) {
  auto do_test = [](auto&& client_instance_indicator) {
    using ClientType = cpp20::remove_cvref_t<decltype(client_instance_indicator)>;
    auto endpoints = fidl::CreateEndpoints<Example>();
    ASSERT_OK(endpoints.status_value());
    auto [local, remote] = std::move(*endpoints);

    async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
    ClientType client(std::move(local), loop.dispatcher());
    bool destroyed = false;
    auto callback_destruction_observer = fit::defer([&] { destroyed = true; });

    client->TwoWay("foo", [observer = std::move(callback_destruction_observer)](
                              fidl::WireResponse<Example::TwoWay>* response) {
      ADD_FATAL_FAILURE("Should not be invoked");
    });
    // Immediately start cancellation.
    client = {};
    loop.RunUntilIdle();

    // The callback should be destroyed without being called.
    ASSERT_TRUE(destroyed);
  };

  do_test(fidl::WireClient<Example>{});
  do_test(fidl::WireSharedClient<Example>{});
}

// An integration-style test that verifies that user-supplied async callbacks
// that takes |fidl::WireUnownedResult| are correctly notified when the binding
// is torn down by the user (i.e. explicit cancellation).
TEST(GenAPITestCase, NotifyInFlightResultCallbacksDuringCancellation) {
  auto do_test = [](auto&& client_instance_indicator) {
    using ClientType = cpp20::remove_cvref_t<decltype(client_instance_indicator)>;
    auto endpoints = fidl::CreateEndpoints<Example>();
    ASSERT_OK(endpoints.status_value());
    auto [local, remote] = std::move(*endpoints);

    async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
    ClientType client(std::move(local), loop.dispatcher());
    bool called = false;
    bool destroyed = false;
    auto callback_destruction_observer = fit::defer([&] { destroyed = true; });

    client->TwoWay("foo", [observer = std::move(callback_destruction_observer),
                           &called](fidl::WireUnownedResult<Example::TwoWay>&& result) {
      called = true;
      EXPECT_STATUS(ZX_ERR_CANCELED, result.status());
      EXPECT_EQ(fidl::Reason::kUnbind, result.reason());
    });
    // Immediately start cancellation.
    client = {};
    loop.RunUntilIdle();

    ASSERT_TRUE(called);
    // The callback should be destroyed after being called.
    ASSERT_TRUE(destroyed);
  };

  do_test(fidl::WireClient<Example>{});
  do_test(fidl::WireSharedClient<Example>{});
}

// The client should not notify the user of teardown completion until all
// up-calls to user code have finished. This is essential for a two-phase
// shutdown pattern to prevent use-after-free.
TEST(WireSharedClient, TeardownCompletesAfterUserCallbackReturns) {
  // This invariant should hold regardless of how many threads are on the
  // dispatcher.
  for (int num_threads = 1; num_threads < 4; num_threads++) {
    auto endpoints = fidl::CreateEndpoints<Example>();
    ASSERT_OK(endpoints.status_value());
    auto [local, remote] = std::move(*endpoints);

    async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
    for (int i = 0; i < num_threads; i++) {
      ASSERT_OK(loop.StartThread());
    }

    class EventHandler : public fidl::WireAsyncEventHandler<Example> {
      void OnResourceEvent(fidl::WireResponse<Example::OnResourceEvent>* event) override {
        // Signal to the test that the dispatcher thread has entered into
        // a user callback.
        event_ = zx::eventpair(event->h.release());
        event_.signal_peer(ZX_SIGNAL_NONE, ZX_USER_SIGNAL_0);

        // Block the user callback until |ZX_USER_SIGNAL_1| is observed.
        zx_signals_t observed;
        ASSERT_OK(event_.wait_one(ZX_USER_SIGNAL_1, zx::time::infinite(), &observed));
        ASSERT_EQ(ZX_USER_SIGNAL_1, observed);
      }

     private:
      zx::eventpair event_;
    };

    fidl::WireSharedClient<Example> client(std::move(local), loop.dispatcher(),
                                           std::make_unique<EventHandler>());
    fidl::WireEventSender<Example> event_sender(std::move(remote));

    zx::eventpair ep1, ep2;
    ASSERT_OK(zx::eventpair::create(0, &ep1, &ep2));
    ASSERT_OK(event_sender.OnResourceEvent(std::move(ep1)));

    zx_signals_t observed;
    ASSERT_OK(zx_object_wait_one(ep2.get(), ZX_USER_SIGNAL_0, ZX_TIME_INFINITE, &observed));
    ASSERT_EQ(ZX_USER_SIGNAL_0, observed);

    // Initiate teardown. The |EventHandler| must not be destroyed until the
    // |OnResourceEvent| callback returns.
    client.AsyncTeardown();
    ASSERT_EQ(ZX_ERR_TIMED_OUT,
              ep2.wait_one(ZX_EVENTPAIR_PEER_CLOSED, async::Now(loop.dispatcher()) + zx::msec(250),
                           &observed));

    ep2.signal_peer(ZX_SIGNAL_NONE, ZX_USER_SIGNAL_1);
    ASSERT_OK(ep2.wait_one(ZX_EVENTPAIR_PEER_CLOSED, zx::time::infinite(), &observed));
  }
}

}  // namespace
