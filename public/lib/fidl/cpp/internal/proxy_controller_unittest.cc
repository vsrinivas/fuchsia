// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fidl/cpp/message_buffer.h>
#include <lib/fidl/cpp/message_builder.h>
#include <lib/zx/channel.h>

#include "gtest/gtest.h"
#include "lib/fidl/cpp/internal/proxy_controller.h"
#include "lib/fidl/cpp/string.h"
#include "lib/fidl/cpp/test/async_loop_for_test.h"
#include "lib/fidl/cpp/test/fidl_types.h"

namespace fidl {
namespace internal {
namespace {

class CallbackMessageHandler : public MessageHandler {
 public:
  fit::function<zx_status_t(Message)> callback;

  zx_status_t OnMessage(Message message) override {
    return callback(std::move(message));
  }
};

TEST(ProxyController, Trivial) { ProxyController controller; }

TEST(ProxyController, Send) {
  zx::channel h1, h2;
  EXPECT_EQ(ZX_OK, zx::channel::create(0, &h1, &h2));

  fidl::test::AsyncLoopForTest loop;

  ProxyController controller;
  EXPECT_EQ(ZX_OK, controller.reader().Bind(std::move(h1)));

  Encoder encoder(5u);
  StringPtr string("hello!");
  string.Encode(&encoder, encoder.Alloc(sizeof(fidl_string_t)));

  EXPECT_EQ(ZX_OK, controller.Send(&unbounded_nonnullable_string_message_type,
                                   encoder.GetMessage(), nullptr));

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

  fidl::test::AsyncLoopForTest loop;

  ProxyController controller;
  EXPECT_EQ(ZX_OK, controller.reader().Bind(std::move(h1)));

  Encoder encoder(3u);
  StringPtr string("hello!");
  string.Encode(&encoder, encoder.Alloc(sizeof(fidl_string_t)));

  int callback_count = 0;
  auto handler = std::make_unique<CallbackMessageHandler>();
  handler->callback = [&callback_count](Message message) {
    ++callback_count;
    EXPECT_EQ(42u, message.ordinal());
    return ZX_OK;
  };

  EXPECT_EQ(ZX_OK, controller.Send(&unbounded_nonnullable_string_message_type,
                                   encoder.GetMessage(), std::move(handler)));

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

  fidl::test::AsyncLoopForTest loop;

  ProxyController controller;
  EXPECT_EQ(ZX_OK, controller.reader().Bind(std::move(h1)));

  Encoder encoder(3u);
  // Bad message format.

  int error_count = 0;
  controller.reader().set_error_handler([&error_count]() { ++error_count; });

  EXPECT_EQ(0, error_count);
  EXPECT_EQ(ZX_ERR_INVALID_ARGS,
            controller.Send(&unbounded_nonnullable_string_message_type,
                            encoder.GetMessage(), nullptr));
  EXPECT_EQ(0, error_count);
  loop.RunUntilIdle();
  EXPECT_EQ(0, error_count);

  EXPECT_EQ(ZX_OK, controller.reader().Unbind().replace(ZX_RIGHT_WAIT, &h1));
  EXPECT_EQ(ZX_OK, controller.reader().Bind(std::move(h1)));

  encoder.Reset(35u);
  StringPtr string("hello!");
  string.Encode(&encoder, encoder.Alloc(sizeof(fidl_string_t)));

  EXPECT_EQ(0, error_count);
  EXPECT_EQ(ZX_ERR_ACCESS_DENIED,
            controller.Send(&unbounded_nonnullable_string_message_type,
                            encoder.GetMessage(), nullptr));
  EXPECT_EQ(0, error_count);
}

TEST(ProxyController, BadReply) {
  zx::channel h1, h2;
  EXPECT_EQ(ZX_OK, zx::channel::create(0, &h1, &h2));

  fidl::test::AsyncLoopForTest loop;

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

  fidl::test::AsyncLoopForTest loop;

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

TEST(ProxyController, Move) {
  zx::channel h1, h2;
  EXPECT_EQ(ZX_OK, zx::channel::create(0, &h1, &h2));

  fidl::test::AsyncLoopForTest loop;

  ProxyController controller1;
  EXPECT_EQ(ZX_OK, controller1.reader().Bind(std::move(h1)));

  Encoder encoder(3u);
  StringPtr string("hello!");
  string.Encode(&encoder, encoder.Alloc(sizeof(fidl_string_t)));

  int callback_count = 0;
  auto handler = std::make_unique<CallbackMessageHandler>();
  handler->callback = [&callback_count](Message message) {
    ++callback_count;
    EXPECT_EQ(42u, message.ordinal());
    return ZX_OK;
  };

  EXPECT_EQ(ZX_OK, controller1.Send(&unbounded_nonnullable_string_message_type,
                                    encoder.GetMessage(), std::move(handler)));

  EXPECT_EQ(0, callback_count);
  loop.RunUntilIdle();
  EXPECT_EQ(0, callback_count);

  MessageBuffer buffer;
  Message message = buffer.CreateEmptyMessage();
  EXPECT_EQ(ZX_OK, message.Read(h2.get(), 0));
  EXPECT_NE(0u, message.txid());
  EXPECT_EQ(3u, message.ordinal());

  ProxyController controller2 = std::move(controller1);
  EXPECT_FALSE(controller1.reader().is_bound());
  EXPECT_TRUE(controller2.reader().is_bound());

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

TEST(ProxyController, Reset) {
  zx::channel h1, h2;
  EXPECT_EQ(ZX_OK, zx::channel::create(0, &h1, &h2));

  fidl::test::AsyncLoopForTest loop;

  ProxyController controller;
  EXPECT_EQ(ZX_OK, controller.reader().Bind(std::move(h1)));

  Encoder encoder(3u);
  StringPtr string("hello!");
  string.Encode(&encoder, encoder.Alloc(sizeof(fidl_string_t)));

  int callback_count = 0;
  auto handler = std::make_unique<CallbackMessageHandler>();
  handler->callback = [&callback_count](Message message) {
    ++callback_count;
    EXPECT_EQ(42u, message.ordinal());
    return ZX_OK;
  };

  EXPECT_EQ(ZX_OK, controller.Send(&unbounded_nonnullable_string_message_type,
                                   encoder.GetMessage(), std::move(handler)));

  EXPECT_EQ(0, callback_count);
  loop.RunUntilIdle();
  EXPECT_EQ(0, callback_count);

  MessageBuffer buffer;
  Message message = buffer.CreateEmptyMessage();
  EXPECT_EQ(ZX_OK, message.Read(h2.get(), 0));
  EXPECT_NE(0u, message.txid());
  EXPECT_EQ(3u, message.ordinal());

  controller.Reset();
  EXPECT_FALSE(controller.reader().is_bound());

  zx_txid_t txid = message.txid();
  fidl_message_header_t header = {};
  header.txid = txid;
  header.ordinal = 42u;

  EXPECT_EQ(ZX_ERR_PEER_CLOSED,
            h2.write(0, &header, sizeof(fidl_message_header_t), nullptr, 0));

  EXPECT_EQ(0, callback_count);
  loop.RunUntilIdle();
  EXPECT_EQ(0, callback_count);
}

}  // namespace
}  // namespace internal
}  // namespace fidl
