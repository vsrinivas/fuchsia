// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string.h>

#include <fidl/test/coding/llcpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fidl/epitaph.h>
#include <lib/fidl/llcpp/server.h>
#include <lib/sync/completion.h>
#include <lib/zx/channel.h>
#include <zircon/types.h>
#include <zxtest/zxtest.h>

namespace {

using ::llcpp::fidl::test::coding::Example;

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

  constexpr char data[] = "TwoWay() sync managed";
  auto server = std::make_unique<Server>(data, sizeof(data));
  auto server_binding = fidl::BindServer(loop.dispatcher(), std::move(remote), server.get());
  ASSERT_TRUE(server_binding.is_ok());

  sync_completion_t done;
  auto result = client->TwoWay(fidl::StringView(data, sizeof(data)),
                               [data, &done](fidl::StringView out) {
                                 ASSERT_EQ(sizeof(data), out.size());
                                 EXPECT_EQ(0, strncmp(out.data(), data, sizeof(data)));
                                 sync_completion_signal(&done);
                               });
  ASSERT_TRUE(result.ok());
  ASSERT_OK(sync_completion_wait(&done, ZX_TIME_INFINITE));

  server_binding.value().Unbind();
}

TEST(GenAPITestCase, TwoWayAsyncCallerAllocated) {
  class ResponseContext : public Example::TwoWayResponseContext {
   public:
    ResponseContext(sync_completion_t* done, const char* data, size_t size)
        : done_(done),
          data_(data),
          size_(size) {}
    void OnError() override {
      sync_completion_signal(done_);
      FAIL();
    }
    void OnReply(fidl::DecodedMessage<Example::TwoWayResponse> msg) override {
      auto& out = msg.message()->out;
      ASSERT_EQ(size_, out.size());
      EXPECT_EQ(0, strncmp(out.data(), data_, size_));
      sync_completion_signal(done_);
    }
    sync_completion_t* done_;
    const char* data_;
    size_t size_;
  };

  zx::channel local, remote;
  ASSERT_OK(zx::channel::create(0, &local, &remote));

  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  ASSERT_OK(loop.StartThread());
  fidl::Client<Example> client(std::move(local), loop.dispatcher());

  constexpr char data[] = "TwoWay() sync caller-allocated";
  auto server = std::make_unique<Server>(data, sizeof(data));
  auto server_binding = fidl::BindServer(loop.dispatcher(), std::move(remote), server.get());
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

  constexpr char data[] = "OnEvent() managed";
  sync_completion_t done;
  Example::AsyncEventHandlers handlers {
    .on_event = [data, &done](fidl::StringView out) {
      ASSERT_EQ(sizeof(data), out.size());
      EXPECT_EQ(0, strncmp(out.data(), data, sizeof(data)));
      sync_completion_signal(&done);
    }
  };
  fidl::Client<Example> client(std::move(local), loop.dispatcher(), std::move(handlers));

  auto server = std::make_unique<Server>(data, sizeof(data));
  auto server_binding = fidl::BindServer(loop.dispatcher(), std::move(remote), server.get());
  ASSERT_TRUE(server_binding.is_ok());

  // Wait for the event from the server.
  ASSERT_OK(server_binding.value()->OnEvent(fidl::StringView(data, sizeof(data))));
  ASSERT_OK(sync_completion_wait(&done, ZX_TIME_INFINITE));

  server_binding.value().Unbind();
}

TEST(GenAPITestCase, EventInPlace) {
  zx::channel local, remote;
  ASSERT_OK(zx::channel::create(0, &local, &remote));

  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  ASSERT_OK(loop.StartThread());

  constexpr char data[] = "OnEvent() in-place";
  sync_completion_t done;
  Example::AsyncEventHandlers handlers {
    .on_event = [data, &done](fidl::DecodedMessage<Example::OnEventResponse> msg) {
      auto& out = msg.message()->out;
      ASSERT_EQ(sizeof(data), out.size());
      EXPECT_EQ(0, strncmp(out.data(), data, sizeof(data)));
      sync_completion_signal(&done);
    }
  };
  fidl::Client<Example> client(std::move(local), loop.dispatcher(), std::move(handlers));

  auto server = std::make_unique<Server>(data, sizeof(data));
  auto server_binding = fidl::BindServer(loop.dispatcher(), std::move(remote), server.get());
  ASSERT_TRUE(server_binding.is_ok());

  // Wait for the event from the server.
  fidl::Buffer<Example::OnEventResponse> buffer;
  ASSERT_OK(server_binding.value()->OnEvent(buffer.view(), fidl::StringView(data, sizeof(data))));
  ASSERT_OK(sync_completion_wait(&done, ZX_TIME_INFINITE));

  server_binding.value().Unbind();
}

TEST(GenAPITestCase, EventNotHandled) {
  zx::channel local, remote;
  ASSERT_OK(zx::channel::create(0, &local, &remote));

  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  ASSERT_OK(loop.StartThread());

  sync_completion_t done;
  fidl::OnClientUnboundFn on_unbound =
      [&done](fidl::UnboundReason reason, zx_status_t status, zx::channel) {
        EXPECT_EQ(fidl::UnboundReason::kInternalError, reason);
        EXPECT_EQ(ZX_ERR_NOT_SUPPORTED, status);
        sync_completion_signal(&done);
      };
  fidl::Client<Example> client(std::move(local), loop.dispatcher(), std::move(on_unbound));

  constexpr char data[] = "OnEvent() unhandled";
  auto server = std::make_unique<Server>(data, sizeof(data));
  auto server_binding = fidl::BindServer(loop.dispatcher(), std::move(remote), server.get());
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
  zx_handle_t local_handle = local.get();

  sync_completion_t unbound;
  fidl::OnClientUnboundFn on_unbound = [&](fidl::UnboundReason reason, zx_status_t status,
                                           zx::channel channel) {
                                         EXPECT_EQ(fidl::UnboundReason::kPeerClosed, reason);
                                         EXPECT_EQ(ZX_ERR_BAD_STATE, status);
                                         EXPECT_EQ(local_handle, channel.get());
                                         sync_completion_signal(&unbound);
                                       };
  fidl::Client<Example> client(std::move(local), loop.dispatcher(), std::move(on_unbound));

  // Send an epitaph and wait for on_unbound to run.
  ASSERT_OK(fidl_epitaph_write(remote.get(), ZX_ERR_BAD_STATE));
  EXPECT_OK(sync_completion_wait(&unbound, ZX_TIME_INFINITE));
}

}  // namespace
