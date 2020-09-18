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
#include <fidl/test/coding/llcpp/fidl.h>
#include <zxtest/zxtest.h>

namespace {

using ::llcpp::fidl::test::coding::fuchsia::Example;

class Server : public Example::Interface {
 public:
  explicit Server(const char* data, size_t size) : data_(data), size_(size) {}

  void TwoWay(fidl::StringView in, TwoWayCompleter::Sync completer) override {
    ASSERT_EQ(size_, in.size());
    EXPECT_EQ(0, strncmp(data_, in.data(), size_));
    completer.Reply(std::move(in));
  }

  void OneWay(fidl::StringView, OneWayCompleter::Sync) override {}

 private:
  const char* data_;
  size_t size_;
};

TEST(GenAPITestCase, TwoWayAsyncManaged) {
  zx::channel local, remote;
  ASSERT_OK(zx::channel::create(0, &local, &remote));

  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  ASSERT_OK(loop.StartThread());
  fidl::Client<Example> client(std::move(local), loop.dispatcher());

  static constexpr char data[] = "TwoWay() sync managed";
  auto server_binding = fidl::BindServer(loop.dispatcher(), std::move(remote),
                                         std::make_unique<Server>(data, sizeof(data)));
  ASSERT_TRUE(server_binding.is_ok());

  sync_completion_t done;
  auto result = client->TwoWay(fidl::StringView(data, sizeof(data)), [&done](fidl::StringView out) {
    ASSERT_EQ(sizeof(data), out.size());
    EXPECT_EQ(0, strncmp(out.data(), data, sizeof(data)));
    sync_completion_signal(&done);
  });
  ASSERT_TRUE(result.ok());
  ASSERT_OK(sync_completion_wait(&done, ZX_TIME_INFINITE));

  server_binding.value().Unbind();
}

TEST(GenAPITestCase, TwoWayAsyncCallerAllocated) {
  class ResponseContext final : public Example::TwoWayResponseContext {
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

  zx::channel local, remote;
  ASSERT_OK(zx::channel::create(0, &local, &remote));

  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  ASSERT_OK(loop.StartThread());
  fidl::Client<Example> client(std::move(local), loop.dispatcher());

  static constexpr char data[] = "TwoWay() sync caller-allocated";
  auto server_binding = fidl::BindServer(loop.dispatcher(), std::move(remote),
                                         std::make_unique<Server>(data, sizeof(data)));
  ASSERT_TRUE(server_binding.is_ok());

  sync_completion_t done;
  fidl::Buffer<Example::TwoWayRequest> buffer;
  ResponseContext context(&done, data, sizeof(data));
  auto result = client->TwoWay(buffer.view(), fidl::StringView(data, sizeof(data)), &context);
  ASSERT_TRUE(result.ok());
  ASSERT_OK(sync_completion_wait(&done, ZX_TIME_INFINITE));

  server_binding.value().Unbind();
}

TEST(GenAPITestCase, EventManaged) {
  zx::channel local, remote;
  ASSERT_OK(zx::channel::create(0, &local, &remote));

  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  ASSERT_OK(loop.StartThread());

  static constexpr char data[] = "OnEvent() managed";
  sync_completion_t done;
  Example::AsyncEventHandlers handlers{.on_event = [&done](Example::OnEventResponse* message) {
    ASSERT_EQ(sizeof(data), message->out.size());
    EXPECT_EQ(0, strncmp(message->out.data(), data, sizeof(data)));
    sync_completion_signal(&done);
  }};
  fidl::Client<Example> client(std::move(local), loop.dispatcher(), std::move(handlers));

  auto server_binding = fidl::BindServer(loop.dispatcher(), std::move(remote),
                                         std::make_unique<Server>(data, sizeof(data)));
  ASSERT_TRUE(server_binding.is_ok());

  // Wait for the event from the server.
  ASSERT_OK(server_binding.value()->OnEvent(fidl::StringView(data, sizeof(data))));
  ASSERT_OK(sync_completion_wait(&done, ZX_TIME_INFINITE));

  server_binding.value().Unbind();
}

TEST(GenAPITestCase, EventNotHandled) {
  zx::channel local, remote;
  ASSERT_OK(zx::channel::create(0, &local, &remote));

  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  ASSERT_OK(loop.StartThread());

  sync_completion_t done;
  fidl::OnClientUnboundFn on_unbound = [&done](fidl::UnbindInfo info) {
    EXPECT_EQ(fidl::UnbindInfo::kUnexpectedMessage, info.reason);
    EXPECT_EQ(ZX_ERR_NOT_SUPPORTED, info.status);
    sync_completion_signal(&done);
  };
  fidl::Client<Example> client(std::move(local), loop.dispatcher(), std::move(on_unbound));

  static constexpr char data[] = "OnEvent() unhandled";
  auto server_binding = fidl::BindServer(loop.dispatcher(), std::move(remote),
                                         std::make_unique<Server>(data, sizeof(data)));
  ASSERT_TRUE(server_binding.is_ok());

  // Wait for the event from the server.
  ASSERT_OK(server_binding.value()->OnEvent(fidl::StringView(data, sizeof(data))));
  ASSERT_OK(sync_completion_wait(&done, ZX_TIME_INFINITE));

  server_binding.value().Unbind();
}

// This is test is almost identical to ClientBindingTestCase.Epitaph in llcpp_client_test.cc but
// validates the part of the flow that's handled in the generated code.
TEST(GenAPITestCase, Epitaph) {
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  ASSERT_OK(loop.StartThread());

  zx::channel local, remote;
  ASSERT_OK(zx::channel::create(0, &local, &remote));

  sync_completion_t unbound;
  fidl::OnClientUnboundFn on_unbound = [&](fidl::UnbindInfo info) {
    EXPECT_EQ(fidl::UnbindInfo::kPeerClosed, info.reason);
    EXPECT_EQ(ZX_ERR_BAD_STATE, info.status);
    sync_completion_signal(&unbound);
  };
  fidl::Client<Example> client(std::move(local), loop.dispatcher(), std::move(on_unbound));

  // Send an epitaph and wait for on_unbound to run.
  ASSERT_OK(fidl_epitaph_write(remote.get(), ZX_ERR_BAD_STATE));
  EXPECT_OK(sync_completion_wait(&unbound, ZX_TIME_INFINITE));
}

TEST(GenAPITestCase, UnbindInfoEncodeError) {
  class ErrorServer : public Example::Interface {
   public:
    explicit ErrorServer() {}

    void TwoWay(fidl::StringView in, TwoWayCompleter::Sync completer) override {
      // Fail to send the reply due to an encoding error (the buffer is too
      // small).
      fidl::BytePart empty;
      EXPECT_EQ(ZX_ERR_BUFFER_TOO_SMALL, completer.Reply(std::move(empty), std::move(in)).status());
      completer.Close(ZX_OK);  // This should not panic.
    }

    void OneWay(fidl::StringView, OneWayCompleter::Sync) override {}
  };

  zx::channel local, remote;
  ASSERT_OK(zx::channel::create(0, &local, &remote));

  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  ASSERT_OK(loop.StartThread());
  fidl::Client<Example> client(std::move(local), loop.dispatcher());

  sync_completion_t done;
  fidl::OnUnboundFn<ErrorServer> on_unbound = [&done](ErrorServer*, fidl::UnbindInfo info,
                                                      zx::channel) {
    EXPECT_EQ(fidl::UnbindInfo::kEncodeError, info.reason);
    EXPECT_EQ(ZX_ERR_BUFFER_TOO_SMALL, info.status);
    sync_completion_signal(&done);
  };
  auto server = std::make_unique<ErrorServer>();
  auto server_binding =
      fidl::BindServer(loop.dispatcher(), std::move(remote), server.get(), std::move(on_unbound));
  ASSERT_TRUE(server_binding.is_ok());

  // Make a synchronous call which should fail as a result of the server end closing.
  auto result = client->TwoWay_Sync(fidl::StringView("", 0));
  ASSERT_FALSE(result.ok());
  EXPECT_EQ(ZX_ERR_PEER_CLOSED, result.status());

  // Wait for the unbound handler to run.
  ASSERT_OK(sync_completion_wait(&done, ZX_TIME_INFINITE));
}

TEST(GenAPITestCase, UnbindInfoDecodeError) {
  zx::channel local, remote;
  ASSERT_OK(zx::channel::create(0, &local, &remote));

  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  ASSERT_OK(loop.StartThread());

  sync_completion_t done;
  Example::AsyncEventHandlers handlers{.on_event = [&done](Example::OnEventResponse* message) {
    FAIL();
    sync_completion_signal(&done);
  }};
  fidl::OnClientUnboundFn on_unbound = [&done](fidl::UnbindInfo info) {
    EXPECT_EQ(fidl::UnbindInfo::kDecodeError, info.reason);
    sync_completion_signal(&done);
  };
  fidl::Client<Example> client(std::move(local), loop.dispatcher(), std::move(on_unbound),
                               std::move(handlers));

  // Set up an Example.OnEvent() message but send it without the payload. This should trigger a
  // decoding error.
  Example::OnEventResponse resp{fidl::StringView("", 0)};
  auto encoded = fidl::internal::LinearizedAndEncoded<Example::OnEventResponse>(&resp);
  auto& encode_result = encoded.result();
  ASSERT_OK(encode_result.status);
  ASSERT_OK(remote.write(0, encode_result.message.bytes().data(), sizeof(fidl_message_header_t),
                         nullptr, 0));

  ASSERT_OK(sync_completion_wait(&done, ZX_TIME_INFINITE));
}

}  // namespace
