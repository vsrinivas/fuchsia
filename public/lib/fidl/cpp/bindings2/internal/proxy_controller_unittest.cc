// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <async/cpp/loop.h>
#include <fidl/cpp/message_buffer.h>
#include <fidl/cpp/message_builder.h>
#include <zx/channel.h>

#include "gtest/gtest.h"
#include "lib/fidl/cpp/bindings2/internal/proxy_controller.h"
#include "lib/fidl/cpp/bindings2/string.h"
#include "lib/fidl/cpp/bindings2/test/fidl_types.h"
#include "lib/fidl/cpp/test/loop_config.h"

namespace fidl {
namespace internal {
namespace {

class CallbackMessageHandler : public MessageHandler {
 public:
  std::function<zx_status_t(Message)> callback;

  zx_status_t OnMessage(Message message) override {
    return callback(std::move(message));
  }
};

TEST(ProxyController, Trivial) {
  ProxyController controller;
}

TEST(ProxyController, Send) {
  zx::channel h1, h2;
  EXPECT_EQ(ZX_OK, zx::channel::create(0, &h1, &h2));

  async::Loop loop(&kTestLoopConfig);

  ProxyController controller;
  EXPECT_EQ(ZX_OK, controller.reader().Bind(std::move(h1)));

  MessageBuilder builder(&unbounded_nonnullable_string_message_type);
  builder.header()->ordinal = 5u;
  StringPtr string("hello!");
  EXPECT_TRUE(Build(&builder, string));

  EXPECT_EQ(ZX_OK, controller.Send(&builder, nullptr));

  MessageBuffer buffer;
  Message message = buffer.CreateEmptyMessage();
  EXPECT_EQ(ZX_OK, message.Read(h2.get(), 0));
  EXPECT_EQ(0u, message.txid());
  EXPECT_EQ(5u, message.ordinal());

  StringView* view = message.GetPayloadAs<StringView>();
  EXPECT_EQ(6u, view->size());
}

TEST(ProxyController, Callback) {
  zx::channel h1, h2;
  EXPECT_EQ(ZX_OK, zx::channel::create(0, &h1, &h2));

  async::Loop loop(&kTestLoopConfig);

  ProxyController controller;
  EXPECT_EQ(ZX_OK, controller.reader().Bind(std::move(h1)));

  MessageBuilder builder(&unbounded_nonnullable_string_message_type);
  builder.header()->ordinal = 3u;
  StringPtr string("hello!");
  EXPECT_TRUE(Build(&builder, string));

  int callback_count = 0;
  auto handler = std::make_unique<CallbackMessageHandler>();
  handler->callback = [&callback_count](Message message) {
    ++callback_count;
    EXPECT_EQ(42u, message.ordinal());
    return ZX_OK;
  };

  EXPECT_EQ(ZX_OK, controller.Send(&builder, std::move(handler)));

  EXPECT_EQ(0, callback_count);
  loop.RunUntilIdle();
  EXPECT_EQ(0, callback_count);

  MessageBuffer buffer;
  Message message = buffer.CreateEmptyMessage();
  EXPECT_EQ(ZX_OK, message.Read(h2.get(), 0));
  EXPECT_NE(0u, message.txid());
  EXPECT_EQ(3u, message.ordinal());

  zx_txid_t txid = message.txid();
  fidl_message_header_t header = {};
  header.txid = txid;
  header.ordinal = 42u;

  EXPECT_EQ(ZX_OK,
            h2.write(0, &header, sizeof(fidl_message_header_t), nullptr, 0));

  EXPECT_EQ(0, callback_count);
  loop.RunUntilIdle();
  EXPECT_EQ(1, callback_count);
}

TEST(ProxyController, BadSend) {
  zx::channel h1, h2;
  EXPECT_EQ(ZX_OK, zx::channel::create(0, &h1, &h2));

  async::Loop loop(&kTestLoopConfig);

  ProxyController controller;
  EXPECT_EQ(ZX_OK, controller.reader().Bind(std::move(h1)));

  MessageBuilder builder(&unbounded_nonnullable_string_message_type);
  builder.header()->ordinal = 3u;
  // Bad message format.

  int error_count = 0;
  controller.reader().set_error_handler([&error_count]() { ++error_count; });

  EXPECT_EQ(0, error_count);
  EXPECT_EQ(ZX_ERR_INVALID_ARGS, controller.Send(&builder, nullptr));
  EXPECT_EQ(0, error_count);
  loop.RunUntilIdle();
  EXPECT_EQ(0, error_count);

  zx_handle_close(controller.reader().channel().get());

  builder.Reset();
  builder.header()->ordinal = 35u;
  StringPtr string("hello!");
  EXPECT_TRUE(Build(&builder, string));

  EXPECT_EQ(0, error_count);
  EXPECT_EQ(ZX_ERR_BAD_HANDLE, controller.Send(&builder, nullptr));
  EXPECT_EQ(0, error_count);
}

TEST(ProxyController, BadReply) {
  zx::channel h1, h2;
  EXPECT_EQ(ZX_OK, zx::channel::create(0, &h1, &h2));

  async::Loop loop(&kTestLoopConfig);

  ProxyController controller;
  EXPECT_EQ(ZX_OK, controller.reader().Bind(std::move(h1)));

  int error_count = 0;
  controller.reader().set_error_handler([&error_count]() { ++error_count; });

  fidl_message_header_t header = {};
  header.txid = 0;
  header.ordinal = 42u;

  EXPECT_EQ(ZX_OK,
            h2.write(0, &header, sizeof(fidl_message_header_t), nullptr, 0));

  EXPECT_EQ(0, error_count);
  loop.RunUntilIdle();
  EXPECT_EQ(1, error_count);

  EXPECT_EQ(ZX_OK, zx::channel::create(0, &h1, &h2));
  EXPECT_EQ(ZX_OK, controller.reader().Bind(std::move(h1)));

  header.txid = 42u;

  EXPECT_EQ(ZX_OK,
            h2.write(0, &header, sizeof(fidl_message_header_t), nullptr, 0));

  EXPECT_EQ(1, error_count);
  loop.RunUntilIdle();
  EXPECT_EQ(2, error_count);
}

TEST(ProxyController, ShortReply) {
  zx::channel h1, h2;
  EXPECT_EQ(ZX_OK, zx::channel::create(0, &h1, &h2));

  async::Loop loop(&kTestLoopConfig);

  ProxyController controller;
  EXPECT_EQ(ZX_OK, controller.reader().Bind(std::move(h1)));

  int error_count = 0;
  controller.reader().set_error_handler([&error_count]() { ++error_count; });

  fidl_message_header_t header = {};
  header.txid = 0;
  header.ordinal = 42u;

  EXPECT_EQ(ZX_OK,
            h2.write(0, &header, sizeof(fidl_message_header_t), nullptr, 0));

  EXPECT_EQ(0, error_count);
  loop.RunUntilIdle();
  EXPECT_EQ(1, error_count);

  EXPECT_EQ(ZX_OK, zx::channel::create(0, &h1, &h2));
  EXPECT_EQ(ZX_OK, controller.reader().Bind(std::move(h1)));

  header.txid = 42u;

  EXPECT_EQ(ZX_OK, h2.write(0, "a", 1, nullptr, 0));

  EXPECT_EQ(1, error_count);
  loop.RunUntilIdle();
  EXPECT_EQ(2, error_count);
}

}  // namespace
}  // namespace internal
}  // namespace fidl
