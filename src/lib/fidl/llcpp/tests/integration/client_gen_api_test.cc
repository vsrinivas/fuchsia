// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/test.basic.protocol/cpp/wire.h>
#include <fidl/test.empty.protocol/cpp/wire.h>
#include <fidl/test.transitional/cpp/wire.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/cpp/time.h>
#include <lib/fidl/cpp/wire/server.h>
#include <lib/fidl/epitaph.h>
#include <lib/fit/defer.h>
#include <lib/sync/completion.h>
#include <lib/sync/cpp/completion.h>
#include <lib/zx/channel.h>
#include <lib/zx/eventpair.h>
#include <lib/zx/time.h>
#include <string.h>
#include <zircon/types.h>

#include <type_traits>

#include <zxtest/zxtest.h>

namespace {

using ::test_basic_protocol::Values;

class Server : public fidl::WireServer<Values> {
 public:
  explicit Server(const char* data, size_t size) : data_(data), size_(size) {}

  void Echo(EchoRequestView request, EchoCompleter::Sync& completer) override {
    ASSERT_EQ(size_, request->s.size());
    EXPECT_EQ(0, strncmp(data_, request->s.data(), size_));
    two_way_count_.fetch_add(1);
    completer.Reply(request->s);
  }

  void OneWay(OneWayRequestView request, OneWayCompleter::Sync&) override {
    ASSERT_EQ(size_, request->in.size());
    EXPECT_EQ(0, strncmp(data_, request->in.data(), size_));
    one_way_count_.fetch_add(1);
  }

  unsigned int two_way_count() const { return two_way_count_.load(); }

  unsigned int one_way_count() const { return one_way_count_.load(); }

 private:
  const char* data_;
  size_t size_;
  std::atomic_uint two_way_count_ = 0;
  std::atomic_uint one_way_count_ = 0;
};

TEST(GenAPITestCase, EchoAsyncManaged) {
  auto endpoints = fidl::CreateEndpoints<Values>();
  ASSERT_OK(endpoints.status_value());
  auto [local, remote] = std::move(*endpoints);

  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  ASSERT_OK(loop.StartThread());
  fidl::WireSharedClient<Values> client(std::move(local), loop.dispatcher());

  static constexpr char data[] = "Echo() sync managed";
  auto server_binding = fidl::BindServer(loop.dispatcher(), std::move(remote),
                                         std::make_unique<Server>(data, strlen(data)));

  sync_completion_t done;
  client->Echo(fidl::StringView(data))
      .ThenExactlyOnce([&done](fidl::WireUnownedResult<Values::Echo>& result) {
        ASSERT_OK(result.status());
        ASSERT_EQ(strlen(data), result.value().s.size());
        EXPECT_EQ(0, strncmp(result.value().s.data(), data, strlen(data)));
        sync_completion_signal(&done);
      });
  ASSERT_OK(sync_completion_wait(&done, ZX_TIME_INFINITE));

  server_binding.Unbind();
}

TEST(GenAPITestCase, EchoAsyncResponseContext) {
  class ResponseContext final : public fidl::WireResponseContext<Values::Echo> {
   public:
    ResponseContext(libsync::Completion* done, const char* data, size_t size)
        : done_(done), data_(data), size_(size) {}

    void OnResult(fidl::WireUnownedResult<Values::Echo>& result) override {
      ASSERT_OK(result.status());
      auto& out = result.value().s;
      ASSERT_EQ(size_, out.size());
      EXPECT_EQ(0, strncmp(out.data(), data_, size_));
      done_->Signal();
    }

   private:
    libsync::Completion* done_;
    const char* data_;
    size_t size_;
  };

  auto endpoints = fidl::CreateEndpoints<Values>();
  ASSERT_OK(endpoints.status_value());
  auto [local, remote] = std::move(*endpoints);

  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  ASSERT_OK(loop.StartThread());
  fidl::WireSharedClient<Values> client(std::move(local), loop.dispatcher());

  static constexpr char kData[] = "Echo() sync response context";
  fidl::BindServer(loop.dispatcher(), std::move(remote),
                   std::make_unique<Server>(kData, strlen(kData)));

  libsync::Completion done;
  ResponseContext context(&done, kData, strlen(kData));
  client->Echo(fidl::StringView(kData)).ThenExactlyOnce(&context);
  ASSERT_OK(done.Wait(zx::duration::infinite()));
}

TEST(GenAPITestCase, EchoAsyncCallerAllocated) {
  class ResponseContext final : public fidl::WireResponseContext<Values::Echo> {
   public:
    ResponseContext(sync_completion_t* done, const char* data, size_t size)
        : done_(done), data_(data), size_(size) {}

    void OnResult(fidl::WireUnownedResult<Values::Echo>& result) override {
      ASSERT_OK(result.status());
      auto& out = result.value().s;
      ASSERT_EQ(size_, out.size());
      EXPECT_EQ(0, strncmp(out.data(), data_, size_));
      sync_completion_signal(done_);
    }

   private:
    sync_completion_t* done_;
    const char* data_;
    size_t size_;
  };

  auto endpoints = fidl::CreateEndpoints<Values>();
  ASSERT_OK(endpoints.status_value());
  auto [local, remote] = std::move(*endpoints);

  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  ASSERT_OK(loop.StartThread());
  fidl::WireSharedClient<Values> client(std::move(local), loop.dispatcher());

  static constexpr char data[] = "Echo() sync caller-allocated";
  auto server_binding = fidl::BindServer(loop.dispatcher(), std::move(remote),
                                         std::make_unique<Server>(data, strlen(data)));

  sync_completion_t done;
  fidl::AsyncClientBuffer<Values::Echo> buffer;
  ResponseContext context(&done, data, strlen(data));
  client.buffer(buffer.view())->Echo(fidl::StringView(data)).ThenExactlyOnce(&context);
  ASSERT_OK(sync_completion_wait(&done, ZX_TIME_INFINITE));

  server_binding.Unbind();
}

TEST(GenAPITestCase, EchoSyncManaged) {
  auto endpoints = fidl::CreateEndpoints<Values>();
  ASSERT_OK(endpoints.status_value());
  auto [local, remote] = std::move(*endpoints);

  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  ASSERT_OK(loop.StartThread());
  fidl::WireSharedClient<Values> client(std::move(local), loop.dispatcher());

  static constexpr char kData[] = "Echo() sync managed";
  auto server = std::make_shared<Server>(kData, strlen(kData));
  fidl::BindServer(loop.dispatcher(), std::move(remote), server);

  fidl::WireResult result = client.sync()->Echo(fidl::StringView(kData));
  ASSERT_OK(result.status());
  ASSERT_EQ(strlen(kData), result.value().s.size());
  EXPECT_EQ(0, strncmp(result.value().s.data(), kData, strlen(kData)));
  EXPECT_EQ(1, server->two_way_count());
  EXPECT_EQ(0, server->one_way_count());
}

TEST(GenAPITestCase, OneWaySyncManaged) {
  auto endpoints = fidl::CreateEndpoints<Values>();
  ASSERT_OK(endpoints.status_value());
  auto [local, remote] = std::move(*endpoints);

  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  fidl::WireClient<Values> client(std::move(local), loop.dispatcher());

  static constexpr char kData[] = "OneWay() sync managed";
  auto server = std::make_shared<Server>(kData, strlen(kData));
  fidl::BindServer(loop.dispatcher(), std::move(remote), server);

  fidl::Status result = client.sync()->OneWay(fidl::StringView(kData));
  EXPECT_OK(result.status());
  ASSERT_OK(loop.RunUntilIdle());
  EXPECT_EQ(1, server->one_way_count());
  EXPECT_EQ(0, server->two_way_count());
}

TEST(GenAPITestCase, AsyncEventHandlerExhaustivenessNotRequired) {
  class EventHandlerNone : public fidl::WireAsyncEventHandler<test_basic_protocol::TwoEvents> {};
  class EventHandlerA : public fidl::WireAsyncEventHandler<test_basic_protocol::TwoEvents> {
    void EventA(fidl::WireEvent<test_basic_protocol::TwoEvents::EventA>*) override {}
  };
  class EventHandlerB : public fidl::WireAsyncEventHandler<test_basic_protocol::TwoEvents> {
    void EventB(fidl::WireEvent<test_basic_protocol::TwoEvents::EventB>*) override {}
  };
  class EventHandlerAll : public fidl::WireAsyncEventHandler<test_basic_protocol::TwoEvents> {
    void EventA(fidl::WireEvent<test_basic_protocol::TwoEvents::EventA>*) override {}
    void EventB(fidl::WireEvent<test_basic_protocol::TwoEvents::EventB>*) override {}
  };
  class EventHandlerAllTransitional
      : public fidl::WireSyncEventHandler<test_transitional::TransitionalEvent> {};
  static_assert(!std::is_abstract_v<EventHandlerNone>);
  static_assert(!std::is_abstract_v<EventHandlerA>);
  static_assert(!std::is_abstract_v<EventHandlerB>);
  static_assert(!std::is_abstract_v<EventHandlerAll>);
  static_assert(!std::is_abstract_v<EventHandlerAllTransitional>);
}

TEST(GenAPITestCase, EventManaged) {
  auto endpoints = fidl::CreateEndpoints<Values>();
  ASSERT_OK(endpoints.status_value());
  auto [local, remote] = std::move(*endpoints);

  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  ASSERT_OK(loop.StartThread());

  static constexpr char data[] = "OnEvent() managed";
  class EventHandler : public fidl::WireAsyncEventHandler<Values> {
   public:
    EventHandler() = default;

    sync_completion_t& done() { return done_; }

    void OnValueEvent(fidl::WireEvent<Values::OnValueEvent>* event) override {
      ASSERT_EQ(strlen(data), event->s.size());
      EXPECT_EQ(0, strncmp(event->s.data(), data, strlen(data)));
      sync_completion_signal(&done_);
    }

   private:
    sync_completion_t done_;
  };

  auto event_handler = std::make_shared<EventHandler>();
  fidl::WireSharedClient<Values> client(std::move(local), loop.dispatcher(), event_handler.get(),
                                        fidl::ShareUntilTeardown(event_handler));

  auto server_binding = fidl::BindServer(loop.dispatcher(), std::move(remote),
                                         std::make_unique<Server>(data, strlen(data)));

  // Wait for the event from the server.
  ASSERT_OK(fidl::WireSendEvent(server_binding)->OnValueEvent(fidl::StringView(data)));
  ASSERT_OK(sync_completion_wait(&event_handler->done(), ZX_TIME_INFINITE));

  server_binding.Unbind();
}

TEST(GenAPITestCase, ConsumeEventsWhenEventHandlerIsAbsent) {
  auto endpoints = fidl::CreateEndpoints<test_basic_protocol::ResourceEvent>();
  ASSERT_OK(endpoints.status_value());
  auto [local, remote] = std::move(*endpoints);

  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  ASSERT_OK(loop.StartThread());

  fidl::WireSharedClient<test_basic_protocol::ResourceEvent> client(std::move(local),
                                                                    loop.dispatcher());

  // Send an unhandled event. The event should be silently discarded since
  // the user did not provide an event handler.
  zx::eventpair ep1, ep2;
  ASSERT_OK(zx::eventpair::create(0, &ep1, &ep2));
  ASSERT_OK(fidl::WireSendEvent(remote)->OnResourceEvent(std::move(ep1)));
  zx_signals_t observed;
  ASSERT_OK(zx_object_wait_one(ep2.get(), ZX_EVENTPAIR_PEER_CLOSED, ZX_TIME_INFINITE, &observed));
  ASSERT_EQ(ZX_EVENTPAIR_PEER_CLOSED, observed);
}

TEST(GenAPITestCase, ConsumeEventsWhenEventHandlerMethodIsAbsent) {
  auto endpoints = fidl::CreateEndpoints<test_basic_protocol::ResourceEvent>();
  ASSERT_OK(endpoints.status_value());
  auto [local, remote] = std::move(*endpoints);

  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  ASSERT_OK(loop.StartThread());

  class EventHandler : public fidl::WireAsyncEventHandler<test_basic_protocol::ResourceEvent> {};

  fidl::WireSharedClient<test_basic_protocol::ResourceEvent> client(
      std::move(local), loop.dispatcher(), std::make_unique<EventHandler>());

  // Send an unhandled event. The event should be silently discarded since
  // the user did not provide a handler method for |OnResourceEvent|.
  zx::eventpair ep1, ep2;
  ASSERT_OK(zx::eventpair::create(0, &ep1, &ep2));
  ASSERT_OK(fidl::WireSendEvent(remote)->OnResourceEvent(std::move(ep1)));
  zx_signals_t observed;
  ASSERT_OK(zx_object_wait_one(ep2.get(), ZX_EVENTPAIR_PEER_CLOSED, ZX_TIME_INFINITE, &observed));
  ASSERT_EQ(ZX_EVENTPAIR_PEER_CLOSED, observed);
}

// This is test is almost identical to ClientBindingTestCase.Epitaph in llcpp_client_test.cc but
// validates the part of the flow that's handled in the generated code.
TEST(GenAPITestCase, Epitaph) {
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  ASSERT_OK(loop.StartThread());

  auto endpoints = fidl::CreateEndpoints<test_empty_protocol::Empty>();
  ASSERT_OK(endpoints.status_value());
  auto [local, remote] = std::move(*endpoints);

  sync_completion_t unbound;

  class EventHandler : public fidl::WireAsyncEventHandler<test_empty_protocol::Empty> {
   public:
    explicit EventHandler(sync_completion_t& unbound) : unbound_(unbound) {}

    void on_fidl_error(fidl::UnbindInfo info) override {
      EXPECT_EQ(fidl::Reason::kPeerClosed, info.reason());
      EXPECT_EQ(ZX_ERR_BAD_STATE, info.status());
      sync_completion_signal(&unbound_);
    }

   private:
    sync_completion_t& unbound_;
  };

  fidl::WireSharedClient<test_empty_protocol::Empty> client(
      std::move(local), loop.dispatcher(), std::make_unique<EventHandler>(unbound));

  // Send an epitaph and wait for on_unbound to run.
  ASSERT_OK(fidl_epitaph_write(remote.channel().get(), ZX_ERR_BAD_STATE));
  EXPECT_OK(sync_completion_wait(&unbound, ZX_TIME_INFINITE));
}

TEST(GenAPITestCase, UnbindInfoEncodeError) {
  class ErrorServer : public fidl::WireServer<Values> {
   public:
    explicit ErrorServer() = default;

    void Echo(EchoRequestView request, EchoCompleter::Sync& completer) override {
      // Fail to send the reply due to an encoding error (the buffer is too small).
      // The buffer still needs to be properly aligned.
      constexpr size_t kSmallSize = 8;
      FIDL_ALIGNDECL uint8_t small_buffer[kSmallSize];
      static_assert(sizeof(fidl::WireResponse<Values::Echo>) > kSmallSize);
      fidl::BufferSpan too_small(small_buffer, std::size(small_buffer));
      completer.buffer(too_small).Reply(request->s);
      EXPECT_EQ(ZX_ERR_BUFFER_TOO_SMALL, completer.result_of_reply().status());
      completer.Close(ZX_OK);  // This should not panic.
    }

    void OneWay(OneWayRequestView, OneWayCompleter::Sync&) override {}
  };

  auto endpoints = fidl::CreateEndpoints<Values>();
  ASSERT_OK(endpoints.status_value());
  auto [local, remote] = std::move(*endpoints);

  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  ASSERT_OK(loop.StartThread());
  fidl::WireSharedClient<Values> client(std::move(local), loop.dispatcher());

  sync_completion_t done;
  fidl::OnUnboundFn<ErrorServer> on_unbound = [&done](ErrorServer*, fidl::UnbindInfo info,
                                                      fidl::ServerEnd<Values>) {
    EXPECT_EQ(fidl::Reason::kEncodeError, info.reason());
    EXPECT_EQ(ZX_ERR_BUFFER_TOO_SMALL, info.status());
    sync_completion_signal(&done);
  };
  auto server = std::make_unique<ErrorServer>();
  auto server_binding =
      fidl::BindServer(loop.dispatcher(), std::move(remote), server.get(), std::move(on_unbound));

  // Make a synchronous call which should fail as a result of the server end closing.
  fidl::WireResult result = client.sync()->Echo(fidl::StringView(""));
  ASSERT_FALSE(result.ok());
  EXPECT_EQ(ZX_ERR_PEER_CLOSED, result.status());

  // Wait for the unbound handler to run.
  ASSERT_OK(sync_completion_wait(&done, ZX_TIME_INFINITE));
}

TEST(GenAPITestCase, UnbindInfoDecodeError) {
  auto endpoints = fidl::CreateEndpoints<Values>();
  ASSERT_OK(endpoints.status_value());
  auto [local, remote] = std::move(*endpoints);

  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  ASSERT_OK(loop.StartThread());
  sync_completion_t done;

  class EventHandler : public fidl::WireAsyncEventHandler<Values> {
   public:
    explicit EventHandler(sync_completion_t& done) : done_(done) {}

    void OnValueEvent(fidl::WireEvent<Values::OnValueEvent>* event) override {
      FAIL();
      sync_completion_signal(&done_);
    }

    void on_fidl_error(fidl::UnbindInfo info) override {
      EXPECT_EQ(fidl::Reason::kDecodeError, info.reason());
      sync_completion_signal(&done_);
    }

   private:
    sync_completion_t& done_;
  };

  fidl::WireSharedClient<Values> client(std::move(local), loop.dispatcher(),
                                        std::make_unique<EventHandler>(done));

  // Set up an Values.OnEvent() message but send it without the payload. This should trigger a
  // decoding error.
  fidl::internal::TransactionalEvent<Values::OnValueEvent> resp(fidl::StringView(""));
  fidl::unstable::OwnedEncodedMessage<fidl::internal::TransactionalEvent<Values::OnValueEvent>>
      encoded(&resp);
  ASSERT_TRUE(encoded.ok());
  auto bytes = encoded.GetOutgoingMessage().CopyBytes();
  ASSERT_OK(remote.channel().write(0, bytes.data(), sizeof(fidl_message_header_t), nullptr, 0));

  ASSERT_OK(sync_completion_wait(&done, ZX_TIME_INFINITE));
}

// After a client is unbound, no more calls can be made on that client.
TEST(GenAPITestCase, UnbindPreventsSubsequentCalls) {
  // Use a server to count the number of |OneWay| calls.
  class Server : public fidl::WireServer<Values> {
   public:
    Server() = default;

    void Echo(EchoRequestView request, EchoCompleter::Sync& completer) override {
      ZX_PANIC("Not used in this test");
    }

    void OneWay(OneWayRequestView, OneWayCompleter::Sync&) override { num_one_way_.fetch_add(1); }

    int num_one_way() const { return num_one_way_.load(); }

   private:
    std::atomic<int> num_one_way_ = 0;
  };

  auto endpoints = fidl::CreateEndpoints<Values>();

  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  fidl::WireSharedClient<Values> client(std::move(endpoints->client), loop.dispatcher());

  auto server = std::make_shared<Server>();
  auto server_binding = fidl::BindServer(loop.dispatcher(), std::move(endpoints->server), server);

  ASSERT_OK(loop.RunUntilIdle());
  EXPECT_EQ(0, server->num_one_way());

  ASSERT_OK(client->OneWay("foo").status());

  ASSERT_OK(loop.RunUntilIdle());
  EXPECT_EQ(1, server->num_one_way());

  client.AsyncTeardown();
  ASSERT_OK(loop.RunUntilIdle());
  EXPECT_EQ(1, server->num_one_way());

  ASSERT_EQ(ZX_ERR_CANCELED, client->OneWay("foo").status());
  ASSERT_OK(loop.RunUntilIdle());
  EXPECT_EQ(1, server->num_one_way());
}

fidl::Endpoints<Values> CreateEndpointsWithoutClientWriteRight() {
  zx::result endpoints = fidl::CreateEndpoints<Values>();
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

  return fidl::Endpoints<Values>{std::move(client_end), std::move(server_end)};
}

class ExpectErrorResponseContext final : public fidl::WireResponseContext<Values::Echo> {
 public:
  explicit ExpectErrorResponseContext(sync_completion_t* did_error, zx_status_t expected_status,
                                      fidl::Reason expected_reason)
      : did_error_(did_error),
        expected_status_(expected_status),
        expected_reason_(expected_reason) {}

  void OnResult(fidl::WireUnownedResult<Values::Echo>& result) override {
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
  fidl::Endpoints<Values> endpoints;
  ASSERT_NO_FAILURES(endpoints = CreateEndpointsWithoutClientWriteRight());
  auto [client_end, server_end] = std::move(endpoints);

  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  fidl::WireSharedClient<Values> client(std::move(client_end), loop.dispatcher());
  loop.StartThread("client-test");

  libsync::Completion error;
  ExpectErrorResponseContext context(error.get(), ZX_ERR_ACCESS_DENIED,
                                     fidl::Reason::kTransportError);

  fidl::AsyncClientBuffer<Values::Echo> buffer;
  client.buffer(buffer.view())->Echo("foo").ThenExactlyOnce(&context);
  ASSERT_OK(error.Wait());
}

TEST(GenAPITestCase, AsyncNotifySendError) {
  auto do_test = [](auto&& client_instance_indicator) {
    using ClientType = cpp20::remove_cvref_t<decltype(client_instance_indicator)>;
    fidl::Endpoints<Values> endpoints;
    ASSERT_NO_FAILURES(endpoints = CreateEndpointsWithoutClientWriteRight());
    auto [local, remote] = std::move(endpoints);

    async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
    ClientType client(std::move(local), loop.dispatcher());

    libsync::Completion error;
    ExpectErrorResponseContext context(error.get(), ZX_ERR_ACCESS_DENIED,
                                       fidl::Reason::kTransportError);

    fidl::AsyncClientBuffer<Values::Echo> buffer;
    client.buffer(buffer.view())->Echo("foo").ThenExactlyOnce(&context);
    // The context should be asynchronously notified.
    EXPECT_FALSE(error.signaled());
    loop.RunUntilIdle();
    EXPECT_TRUE(error.signaled());
  };

  do_test(fidl::WireClient<Values>{});
  do_test(fidl::WireSharedClient<Values>{});
}

TEST(GenAPITestCase, AsyncNotifyTeardownError) {
  fidl::Endpoints<Values> endpoints;
  ASSERT_NO_FAILURES(endpoints = CreateEndpointsWithoutClientWriteRight());
  auto [local, remote] = std::move(endpoints);

  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  fidl::WireSharedClient<Values> client(std::move(local), loop.dispatcher());
  client.AsyncTeardown();
  loop.RunUntilIdle();

  libsync::Completion error;
  ExpectErrorResponseContext context(error.get(), ZX_ERR_CANCELED, fidl::Reason::kUnbind);

  fidl::AsyncClientBuffer<Values::Echo> buffer;
  client.buffer(buffer.view())->Echo("foo").ThenExactlyOnce(&context);
  EXPECT_FALSE(error.signaled());
  loop.RunUntilIdle();
  EXPECT_TRUE(error.signaled());
}

TEST(GenAPITestCase, SyncNotifyErrorIfDispatcherShutdown) {
  auto do_test = [](auto&& client_instance_indicator) {
    using ClientType = cpp20::remove_cvref_t<decltype(client_instance_indicator)>;
    fidl::Endpoints<Values> endpoints;
    ASSERT_NO_FAILURES(endpoints = CreateEndpointsWithoutClientWriteRight());
    auto [local, remote] = std::move(endpoints);

    async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
    ClientType client(std::move(local), loop.dispatcher());

    libsync::Completion error;
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

    fidl::AsyncClientBuffer<Values::Echo> buffer;
    client.buffer(buffer.view())->Echo("foo").ThenExactlyOnce(&context);
    // If the loop was shutdown, |context| should still be notified, although
    // it has to happen on the current stack frame.
    EXPECT_TRUE(error.signaled());
  };

  do_test(fidl::WireClient<Values>{});
  do_test(fidl::WireSharedClient<Values>{});
}

// An integration-style test that verifies that user-supplied async callbacks
// attached using |Then| with client lifetime are not invoked when the client is
// destroyed by the user (i.e. explicit cancellation) instead of due to errors.
TEST(GenAPITestCase, ThenWithClientLifetime) {
  auto do_test = [](auto&& client_instance_indicator) {
    using ClientType = cpp20::remove_cvref_t<decltype(client_instance_indicator)>;
    auto endpoints = fidl::CreateEndpoints<Values>();
    ASSERT_OK(endpoints.status_value());
    auto [local, remote] = std::move(*endpoints);
    async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);

    struct Receiver {
      ClientType client;
    };
    Receiver receiver{ClientType(std::move(local), loop.dispatcher())};

    bool destroyed = false;
    receiver.client->Echo("foo").Then([observer = fit::defer([&] { destroyed = true; })](
                                          fidl::WireUnownedResult<Values::Echo>& result) {
      ADD_FATAL_FAILURE("Should not be invoked");
    });
    // Immediately start cancellation.
    receiver = {};
    ASSERT_FALSE(destroyed);
    loop.RunUntilIdle();

    // The callback should be destroyed without being called.
    ASSERT_TRUE(destroyed);
  };

  do_test(fidl::WireClient<Values>{});
  do_test(fidl::WireSharedClient<Values>{});
}

// An integration-style test that verifies that user-supplied async callbacks
// that takes |fidl::WireUnownedResult| are correctly notified when the binding
// is torn down by the user (i.e. explicit cancellation).
TEST(GenAPITestCase, ThenExactlyOnce) {
  auto do_test = [](auto&& client_instance_indicator) {
    using ClientType = cpp20::remove_cvref_t<decltype(client_instance_indicator)>;
    auto endpoints = fidl::CreateEndpoints<Values>();
    ASSERT_OK(endpoints.status_value());
    auto [local, remote] = std::move(*endpoints);

    async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
    ClientType client(std::move(local), loop.dispatcher());
    bool called = false;
    bool destroyed = false;
    auto callback_destruction_observer = fit::defer([&] { destroyed = true; });

    client->Echo("foo").ThenExactlyOnce([observer = std::move(callback_destruction_observer),
                                         &called](fidl::WireUnownedResult<Values::Echo>& result) {
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

  do_test(fidl::WireClient<Values>{});
  do_test(fidl::WireSharedClient<Values>{});
}

// The client should not notify the user of teardown completion until all
// up-calls to user code have finished. This is essential for a two-phase
// shutdown pattern to prevent use-after-free.
TEST(WireSharedClient, TeardownCompletesAfterUserCallbackReturns) {
  // This invariant should hold regardless of how many threads are on the
  // dispatcher.
  for (int num_threads = 1; num_threads < 4; num_threads++) {
    auto endpoints = fidl::CreateEndpoints<test_basic_protocol::ResourceEvent>();
    ASSERT_OK(endpoints.status_value());
    auto [local, remote] = std::move(*endpoints);

    async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
    for (int i = 0; i < num_threads; i++) {
      ASSERT_OK(loop.StartThread());
    }

    class EventHandler : public fidl::WireAsyncEventHandler<test_basic_protocol::ResourceEvent> {
      void OnResourceEvent(
          fidl::WireEvent<test_basic_protocol::ResourceEvent::OnResourceEvent>* event) override {
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

    fidl::WireSharedClient<test_basic_protocol::ResourceEvent> client(
        std::move(local), loop.dispatcher(), std::make_unique<EventHandler>());

    zx::eventpair ep1, ep2;
    ASSERT_OK(zx::eventpair::create(0, &ep1, &ep2));
    ASSERT_OK(fidl::WireSendEvent(remote)->OnResourceEvent(std::move(ep1)));

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

// After the first call fails during sending, the client bindings should
// teardown thereby disallowing subsequent calls. In addition, the user should
// receive an error in the event handler.
TEST(AllClients, SendErrorLeadsToBindingTeardown) {
  auto do_test = [](auto&& client_instance_indicator) {
    using ClientType = cpp20::remove_cvref_t<decltype(client_instance_indicator)>;
    fidl::Endpoints<Values> endpoints;
    ASSERT_NO_FAILURES(endpoints = CreateEndpointsWithoutClientWriteRight());
    auto [local, remote] = std::move(endpoints);

    class EventHandler : public fidl::WireAsyncEventHandler<Values> {
     public:
      void on_fidl_error(fidl::UnbindInfo info) override {
        errored_ = true;
        EXPECT_STATUS(ZX_ERR_ACCESS_DENIED, info.status());
        EXPECT_EQ(fidl::Reason::kTransportError, info.reason());
      }

      bool errored() const { return errored_; }

     private:
      bool errored_ = false;
    } event_handler;

    async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
    ClientType client(std::move(local), loop.dispatcher(), &event_handler);

    EXPECT_FALSE(event_handler.errored());
    client->Echo("foo").ThenExactlyOnce([](fidl::WireUnownedResult<Values::Echo>&) {});
    loop.RunUntilIdle();
    EXPECT_TRUE(event_handler.errored());

    bool called = false;
    client->Echo("foo").ThenExactlyOnce([&called](fidl::WireUnownedResult<Values::Echo>& result) {
      called = true;
      EXPECT_EQ(fidl::Reason::kUnbind, result.reason());
      EXPECT_STATUS(ZX_ERR_CANCELED, result.status());
    });
    loop.RunUntilIdle();
    EXPECT_TRUE(called);
  };

  do_test(fidl::WireClient<Values>{});
  do_test(fidl::WireSharedClient<Values>{});
}

// If a call fails due to a peer closed error, the client bindings should still
// process any remaining messages received on the endpoint before tearing down.
TEST(AllClients, DrainAllMessageInPeerClosedSendError) {
  auto do_test = [](auto&& client_instance_indicator) {
    using ClientType = cpp20::remove_cvref_t<decltype(client_instance_indicator)>;
    zx::result endpoints = fidl::CreateEndpoints<Values>();
    ASSERT_OK(endpoints.status_value());
    auto [local, remote] = std::move(*endpoints);

    static constexpr char data[] = "test";
    class EventHandler : public fidl::WireAsyncEventHandler<Values> {
     public:
      EventHandler() = default;

      bool received() const { return received_; }

      void OnValueEvent(fidl::WireEvent<Values::OnValueEvent>* event) override {
        ASSERT_EQ(strlen(data), event->s.size());
        EXPECT_EQ(0, strncmp(event->s.data(), data, strlen(data)));
        received_ = true;
      }

     private:
      bool received_ = false;
    } event_handler;

    async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
    ClientType client(std::move(local), loop.dispatcher(), &event_handler);

    // Send an event and close the server endpoint.
    ASSERT_OK(fidl::WireSendEvent(remote)->OnValueEvent(fidl::StringView(data)));
    remote.reset();

    // The event should not be received unless the |loop| was polled.
    EXPECT_FALSE(event_handler.received());

    // Make a client method call which should fail, but not interfere with
    // reading the event.
    {
      fidl::Status result = client->OneWay("foo");
      EXPECT_EQ(fidl::Reason::kPeerClosed, result.reason());
      EXPECT_STATUS(ZX_ERR_PEER_CLOSED, result.status());
    }
    ASSERT_OK(loop.RunUntilIdle());
    EXPECT_TRUE(event_handler.received());

    // The client binding should still be torn down.
    {
      fidl::Status result = client->OneWay("foo");
      EXPECT_EQ(fidl::Reason::kUnbind, result.reason());
      EXPECT_STATUS(ZX_ERR_CANCELED, result.status());
    }
  };

  do_test(fidl::WireClient<Values>{});
  do_test(fidl::WireSharedClient<Values>{});
}

}  // namespace
