// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <async/loop.h>
#include <fidl/cpp/message_buffer.h>
#include <fidl/cpp/message_builder.h>
#include <zx/channel.h>

#include "gtest/gtest.h"
#include "lib/fidl/cpp/bindings2/internal/proxy_controller.h"
#include "lib/fidl/cpp/bindings2/internal/stub.h"
#include "lib/fidl/cpp/bindings2/internal/stub_controller.h"
#include "lib/fidl/cpp/bindings2/string.h"
#include "lib/fidl/cpp/bindings2/test/fidl_types.h"
#include "lib/fidl/cpp/test/loop_config.h"

namespace fidl {
namespace internal {
namespace {

class CallbackStub : public Stub {
 public:
  std::function<zx_status_t(Message, PendingResponse)> callback;

  zx_status_t Dispatch(Message message, PendingResponse response) override {
    return callback(std::move(message), std::move(response));
  }
};

class CallbackMessageHandler : public MessageHandler {
 public:
  std::function<zx_status_t(Message)> callback;

  zx_status_t OnMessage(Message message) override {
    return callback(std::move(message));
  }
};

TEST(StubController, Trivial) {
  StubController controller;
}

TEST(StubController, NoResponse) {
  zx::channel h1, h2;
  EXPECT_EQ(ZX_OK, zx::channel::create(0, &h1, &h2));

  async::Loop loop(&kTestLoopConfig);

  StubController stub_ctrl;
  EXPECT_EQ(ZX_OK, stub_ctrl.reader().Bind(std::move(h1)));

  ProxyController proxy_ctrl;
  EXPECT_EQ(ZX_OK, proxy_ctrl.reader().Bind(std::move(h2)));

  auto stub = std::make_unique<CallbackStub>();

  int callback_count = 0;
  stub->callback = [&callback_count](Message message,
                                     PendingResponse response) {
    ++callback_count;
    EXPECT_EQ(5u, message.ordinal());
    EXPECT_FALSE(response.needs_response());
    EXPECT_EQ(ZX_ERR_BAD_STATE, response.Send(nullptr));
    return ZX_OK;
  };

  stub_ctrl.set_stub(std::move(stub));

  MessageBuilder builder(&unbounded_nonnullable_string_message_type);
  builder.header()->ordinal = 5u;
  StringPtr string("hello!");
  EXPECT_TRUE(Build(&builder, string));

  EXPECT_EQ(ZX_OK, proxy_ctrl.Send(&builder, nullptr));
  EXPECT_EQ(0, callback_count);
  loop.RunUntilIdle();
  EXPECT_EQ(1, callback_count);
}

TEST(StubController, Response) {
  zx::channel h1, h2;
  EXPECT_EQ(ZX_OK, zx::channel::create(0, &h1, &h2));

  async::Loop loop(&kTestLoopConfig);

  StubController stub_ctrl;
  EXPECT_EQ(ZX_OK, stub_ctrl.reader().Bind(std::move(h1)));

  ProxyController proxy_ctrl;
  EXPECT_EQ(ZX_OK, proxy_ctrl.reader().Bind(std::move(h2)));

  auto stub = std::make_unique<CallbackStub>();

  int callback_count = 0;
  stub->callback = [&callback_count](Message message,
                                     PendingResponse response) {
    ++callback_count;
    EXPECT_EQ(5u, message.ordinal());
    EXPECT_TRUE(response.needs_response());
    MessageBuilder builder(&unbounded_nonnullable_string_message_type);
    builder.header()->ordinal = 42u;
    StringPtr string("welcome!");
    EXPECT_TRUE(Build(&builder, string));
    EXPECT_EQ(ZX_OK, response.Send(&builder));
    return ZX_OK;
  };

  stub_ctrl.set_stub(std::move(stub));

  MessageBuilder builder(&unbounded_nonnullable_string_message_type);
  builder.header()->ordinal = 5u;
  StringPtr string("hello!");
  EXPECT_TRUE(Build(&builder, string));

  int response_count = 0;
  auto handler = std::make_unique<CallbackMessageHandler>();
  handler->callback = [&response_count](Message message) {
    ++response_count;
    EXPECT_EQ(42u, message.ordinal());
    return ZX_OK;
  };

  EXPECT_EQ(ZX_OK, proxy_ctrl.Send(&builder, std::move(handler)));
  EXPECT_EQ(0, callback_count);
  EXPECT_EQ(0, response_count);
  loop.RunUntilIdle();
  EXPECT_EQ(1, callback_count);
  EXPECT_EQ(1, response_count);
}

TEST(StubController, ResponseAfterUnbind) {
  zx::channel h1, h2;
  EXPECT_EQ(ZX_OK, zx::channel::create(0, &h1, &h2));

  async::Loop loop(&kTestLoopConfig);

  StubController stub_ctrl;
  EXPECT_EQ(ZX_OK, stub_ctrl.reader().Bind(std::move(h1)));

  ProxyController proxy_ctrl;
  EXPECT_EQ(ZX_OK, proxy_ctrl.reader().Bind(std::move(h2)));

  auto stub = std::make_unique<CallbackStub>();

  int callback_count = 0;
  stub->callback = [&callback_count, &stub_ctrl](Message message,
                                                 PendingResponse response) {
    ++callback_count;

    stub_ctrl.reader().Unbind();

    EXPECT_EQ(5u, message.ordinal());
    EXPECT_TRUE(response.needs_response());
    MessageBuilder builder(&unbounded_nonnullable_string_message_type);
    builder.header()->ordinal = 42u;
    StringPtr string("welcome!");
    EXPECT_TRUE(Build(&builder, string));
    EXPECT_EQ(ZX_ERR_BAD_STATE, response.Send(&builder));
    return ZX_OK;
  };

  stub_ctrl.set_stub(std::move(stub));

  MessageBuilder builder(&unbounded_nonnullable_string_message_type);
  builder.header()->ordinal = 5u;
  StringPtr string("hello!");
  EXPECT_TRUE(Build(&builder, string));

  int response_count = 0;
  auto handler = std::make_unique<CallbackMessageHandler>();
  handler->callback = [&response_count](Message message) {
    ++response_count;
    return ZX_OK;
  };

  EXPECT_EQ(ZX_OK, proxy_ctrl.Send(&builder, std::move(handler)));
  EXPECT_EQ(0, callback_count);
  EXPECT_EQ(0, response_count);
  loop.RunUntilIdle();
  EXPECT_EQ(1, callback_count);
  EXPECT_EQ(0, response_count);
}

TEST(StubController, ResponseAfterDestroy) {
  zx::channel h1, h2;
  EXPECT_EQ(ZX_OK, zx::channel::create(0, &h1, &h2));

  async::Loop loop(&kTestLoopConfig);

  auto stub_ctrl = std::make_unique<StubController>();
  EXPECT_EQ(ZX_OK, stub_ctrl->reader().Bind(std::move(h1)));

  ProxyController proxy_ctrl;
  EXPECT_EQ(ZX_OK, proxy_ctrl.reader().Bind(std::move(h2)));

  auto stub = std::make_unique<CallbackStub>();

  int callback_count = 0;
  stub->callback = [&callback_count, &stub_ctrl](Message message,
                                                 PendingResponse response) {
    ++callback_count;

    stub_ctrl.reset();

    EXPECT_EQ(5u, message.ordinal());
    EXPECT_TRUE(response.needs_response());
    MessageBuilder builder(&unbounded_nonnullable_string_message_type);
    builder.header()->ordinal = 42u;
    StringPtr string("welcome!");
    EXPECT_TRUE(Build(&builder, string));
    EXPECT_EQ(ZX_ERR_BAD_STATE, response.Send(&builder));
    return ZX_OK;
  };

  stub_ctrl->set_stub(std::move(stub));

  MessageBuilder builder(&unbounded_nonnullable_string_message_type);
  builder.header()->ordinal = 5u;
  StringPtr string("hello!");
  EXPECT_TRUE(Build(&builder, string));

  int response_count = 0;
  auto handler = std::make_unique<CallbackMessageHandler>();
  handler->callback = [&response_count](Message message) {
    ++response_count;
    return ZX_OK;
  };

  EXPECT_EQ(ZX_OK, proxy_ctrl.Send(&builder, std::move(handler)));
  EXPECT_EQ(0, callback_count);
  EXPECT_EQ(0, response_count);
  loop.RunUntilIdle();
  EXPECT_EQ(1, callback_count);
  EXPECT_EQ(0, response_count);
}

TEST(StubController, BadResponse) {
  zx::channel h1, h2;
  EXPECT_EQ(ZX_OK, zx::channel::create(0, &h1, &h2));

  async::Loop loop(&kTestLoopConfig);

  StubController stub_ctrl;
  EXPECT_EQ(ZX_OK, stub_ctrl.reader().Bind(std::move(h1)));

  int error_count = 0;
  stub_ctrl.reader().set_error_handler([&error_count]() { ++error_count; });

  ProxyController proxy_ctrl;
  EXPECT_EQ(ZX_OK, proxy_ctrl.reader().Bind(std::move(h2)));

  auto stub = std::make_unique<CallbackStub>();

  int callback_count = 0;
  stub->callback = [&callback_count](Message message,
                                     PendingResponse response) {
    ++callback_count;
    EXPECT_EQ(5u, message.ordinal());
    EXPECT_TRUE(response.needs_response());
    MessageBuilder builder(&unbounded_nonnullable_string_message_type);
    builder.header()->ordinal = 42u;
    // Bad message format.
    EXPECT_EQ(ZX_ERR_INVALID_ARGS, response.Send(&builder));
    return ZX_OK;
  };

  stub_ctrl.set_stub(std::move(stub));

  MessageBuilder builder(&unbounded_nonnullable_string_message_type);
  builder.header()->ordinal = 5u;
  StringPtr string("hello!");
  EXPECT_TRUE(Build(&builder, string));

  int response_count = 0;
  auto handler = std::make_unique<CallbackMessageHandler>();
  handler->callback = [&response_count](Message message) {
    ++response_count;
    return ZX_OK;
  };

  EXPECT_EQ(ZX_OK, proxy_ctrl.Send(&builder, std::move(handler)));
  EXPECT_EQ(0, callback_count);
  EXPECT_EQ(0, response_count);
  EXPECT_EQ(0, error_count);
  loop.RunUntilIdle();
  EXPECT_EQ(1, callback_count);
  EXPECT_EQ(0, response_count);
  EXPECT_EQ(0, error_count);
}

TEST(StubController, BadMessage) {
  zx::channel h1, h2;
  EXPECT_EQ(ZX_OK, zx::channel::create(0, &h1, &h2));

  async::Loop loop(&kTestLoopConfig);

  StubController stub_ctrl;
  EXPECT_EQ(ZX_OK, stub_ctrl.reader().Bind(std::move(h1)));

  int error_count = 0;
  stub_ctrl.reader().set_error_handler([&error_count]() { ++error_count; });

  auto stub = std::make_unique<CallbackStub>();

  int callback_count = 0;
  stub->callback = [&callback_count](Message message,
                                     PendingResponse response) {
    ++callback_count;
    return ZX_OK;
  };

  stub_ctrl.set_stub(std::move(stub));

  EXPECT_EQ(ZX_OK, h2.write(0, "a", 1, nullptr, 0));

  EXPECT_EQ(0, callback_count);
  EXPECT_EQ(0, error_count);
  loop.RunUntilIdle();
  EXPECT_EQ(0, callback_count);
  EXPECT_EQ(1, error_count);
}

}  // namespace
}  // namespace internal
}  // namespace fidl
