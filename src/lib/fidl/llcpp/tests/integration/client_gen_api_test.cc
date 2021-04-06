// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fidl/epitaph.h>
#include <lib/fidl/llcpp/server.h>
#include <lib/sync/completion.h>
#include <lib/zx/channel.h>
#include <string.h>
#include <zircon/types.h>

#include <fidl/test/coding/fuchsia/llcpp/fidl.h>
#include <zxtest/zxtest.h>

namespace {

using ::fidl_test_coding_fuchsia::Example;

class Server : public Example::Interface {
 public:
  explicit Server(const char* data, size_t size) : data_(data), size_(size) {}

  void TwoWay(fidl::StringView in, TwoWayCompleter::Sync& completer) override {
    ASSERT_EQ(size_, in.size());
    EXPECT_EQ(0, strncmp(data_, in.data(), size_));
    completer.Reply(std::move(in));
  }

  void OneWay(fidl::StringView, OneWayCompleter::Sync&) override {}

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
  fidl::Client<Example> client(std::move(local), loop.dispatcher());

  static constexpr char data[] = "TwoWay() sync managed";
  auto server_binding = fidl::BindServer(loop.dispatcher(), std::move(remote),
                                         std::make_unique<Server>(data, strlen(data)));
  ASSERT_TRUE(server_binding.is_ok());

  sync_completion_t done;
  auto result = client->TwoWay(fidl::StringView(data), [&done](Example::TwoWayResponse* response) {
    ASSERT_EQ(strlen(data), response->out.size());
    EXPECT_EQ(0, strncmp(response->out.data(), data, strlen(data)));
    sync_completion_signal(&done);
  });
  ASSERT_TRUE(result.ok());
  ASSERT_OK(sync_completion_wait(&done, ZX_TIME_INFINITE));

  server_binding.value().Unbind();
}

TEST(GenAPITestCase, TwoWayAsyncCallerAllocated) {
  class ResponseContext final : public fidl::WireResponseContext<Example::TwoWay> {
   public:
    ResponseContext(sync_completion_t* done, const char* data, size_t size)
        : done_(done), data_(data), size_(size) {}

    void OnError() override {
      sync_completion_signal(done_);
      FAIL();
    }

    void OnReply(Example::TwoWayResponse* message) override {
      auto& out = message->out;
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
  fidl::Client<Example> client(std::move(local), loop.dispatcher());

  static constexpr char data[] = "TwoWay() sync caller-allocated";
  auto server_binding = fidl::BindServer(loop.dispatcher(), std::move(remote),
                                         std::make_unique<Server>(data, strlen(data)));
  ASSERT_TRUE(server_binding.is_ok());

  sync_completion_t done;
  fidl::Buffer<Example::TwoWayRequest> buffer;
  ResponseContext context(&done, data, strlen(data));
  auto result = client->TwoWay(buffer.view(), fidl::StringView(data), &context);
  ASSERT_TRUE(result.ok());
  ASSERT_OK(sync_completion_wait(&done, ZX_TIME_INFINITE));

  server_binding.value().Unbind();
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

    void OnEvent(Example::OnEventResponse* event) {
      ASSERT_EQ(strlen(data), event->out.size());
      EXPECT_EQ(0, strncmp(event->out.data(), data, strlen(data)));
      sync_completion_signal(&done_);
    }

   private:
    sync_completion_t done_;
  };

  auto event_handler = std::make_shared<EventHandler>();
  fidl::Client<Example> client(std::move(local), loop.dispatcher(), event_handler);

  auto server_binding = fidl::BindServer(loop.dispatcher(), std::move(remote),
                                         std::make_unique<Server>(data, strlen(data)));
  ASSERT_TRUE(server_binding.is_ok());

  // Wait for the event from the server.
  ASSERT_OK(server_binding.value()->OnEvent(fidl::StringView(data)));
  ASSERT_OK(sync_completion_wait(&event_handler->done(), ZX_TIME_INFINITE));

  server_binding.value().Unbind();
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

    void Unbound(fidl::UnbindInfo info) override {
      EXPECT_EQ(fidl::UnbindInfo::kPeerClosed, info.reason);
      EXPECT_EQ(ZX_ERR_BAD_STATE, info.status);
      sync_completion_signal(&unbound_);
    };

   private:
    sync_completion_t& unbound_;
  };

  fidl::Client<Example> client(std::move(local), loop.dispatcher(),
                               std::make_shared<EventHandler>(unbound));

  // Send an epitaph and wait for on_unbound to run.
  ASSERT_OK(fidl_epitaph_write(remote.channel().get(), ZX_ERR_BAD_STATE));
  EXPECT_OK(sync_completion_wait(&unbound, ZX_TIME_INFINITE));
}

TEST(GenAPITestCase, UnbindInfoEncodeError) {
  class ErrorServer : public Example::Interface {
   public:
    explicit ErrorServer() {}

    void TwoWay(fidl::StringView in, TwoWayCompleter::Sync& completer) override {
      // Fail to send the reply due to an encoding error (the buffer is too
      // small).
      fidl::BufferSpan empty;
      EXPECT_EQ(ZX_ERR_BUFFER_TOO_SMALL, completer.Reply(empty, std::move(in)).status());
      completer.Close(ZX_OK);  // This should not panic.
    }

    void OneWay(fidl::StringView, OneWayCompleter::Sync&) override {}
  };

  auto endpoints = fidl::CreateEndpoints<Example>();
  ASSERT_OK(endpoints.status_value());
  auto [local, remote] = std::move(*endpoints);

  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  ASSERT_OK(loop.StartThread());
  fidl::Client<Example> client(std::move(local), loop.dispatcher());

  sync_completion_t done;
  fidl::OnUnboundFn<ErrorServer> on_unbound =
      [&done](ErrorServer*, fidl::UnbindInfo info,
              fidl::ServerEnd<fidl_test_coding_fuchsia::Example>) {
        EXPECT_EQ(fidl::UnbindInfo::kEncodeError, info.reason);
        EXPECT_EQ(ZX_ERR_BUFFER_TOO_SMALL, info.status);
        sync_completion_signal(&done);
      };
  auto server = std::make_unique<ErrorServer>();
  auto server_binding =
      fidl::BindServer(loop.dispatcher(), std::move(remote), server.get(), std::move(on_unbound));
  ASSERT_TRUE(server_binding.is_ok());

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
    EventHandler(sync_completion_t& done) : done_(done) {}

    void OnEvent(Example::OnEventResponse* event) override {
      FAIL();
      sync_completion_signal(&done_);
    }

    void Unbound(fidl::UnbindInfo info) override {
      EXPECT_EQ(fidl::UnbindInfo::kDecodeError, info.reason);
      sync_completion_signal(&done_);
    };

   private:
    sync_completion_t& done_;
  };

  fidl::Client<Example> client(std::move(local), loop.dispatcher(),
                               std::make_shared<EventHandler>(done));

  // Set up an Example.OnEvent() message but send it without the payload. This should trigger a
  // decoding error.
  Example::OnEventResponse resp{fidl::StringView("")};
  fidl::OwnedEncodedMessage<Example::OnEventResponse> encoded(&resp);
  ASSERT_TRUE(encoded.ok());
  auto bytes = encoded.GetOutgoingMessage().CopyBytes();
  ASSERT_OK(remote.channel().write(0, bytes.data(), sizeof(fidl_message_header_t), nullptr, 0));

  ASSERT_OK(sync_completion_wait(&done, ZX_TIME_INFINITE));
}

// After a client is unbound, no more calls can be made on that client.
TEST(GenAPITestCase, UnbindPreventsSubsequentCalls) {
  // Use a server to count the number of |OneWay| calls.
  class Server : public Example::Interface {
   public:
    Server() = default;

    void TwoWay(fidl::StringView in, TwoWayCompleter::Sync& completer) override {
      ZX_PANIC("Not used in this test");
    }

    void OneWay(fidl::StringView, OneWayCompleter::Sync&) override { num_one_way_.fetch_add(1); }

    int num_one_way() const { return num_one_way_.load(); }

   private:
    std::atomic<int> num_one_way_ = 0;
  };

  auto endpoints = fidl::CreateEndpoints<Example>();

  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  fidl::Client<Example> client(std::move(endpoints->client), loop.dispatcher());

  auto server = std::make_unique<Server>();
  auto* server_ptr = server.get();
  auto server_binding =
      fidl::BindServer(loop.dispatcher(), std::move(endpoints->server), std::move(server));
  ASSERT_TRUE(server_binding.is_ok());

  ASSERT_OK(loop.RunUntilIdle());
  EXPECT_EQ(0, server_ptr->num_one_way());

  ASSERT_OK(client->OneWay("foo").status());

  ASSERT_OK(loop.RunUntilIdle());
  EXPECT_EQ(1, server_ptr->num_one_way());

  client.Unbind();
  ASSERT_OK(loop.RunUntilIdle());
  EXPECT_EQ(1, server_ptr->num_one_way());

  ASSERT_EQ(ZX_ERR_CANCELED, client->OneWay("foo").status());
  ASSERT_OK(loop.RunUntilIdle());
  EXPECT_EQ(1, server_ptr->num_one_way());
}

}  // namespace
