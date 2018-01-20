// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <async/loop.h>
#include <zx/channel.h>

#include "gtest/gtest.h"
#include "lib/fidl/cpp/bindings2/internal/channel_reader.h"
#include "lib/fidl/cpp/test/loop_config.h"

namespace fidl {
namespace internal {
namespace {

class CopyingMessageHandler : public MessageHandler {
 public:
  int message_count_ = 0;
  std::vector<uint8_t> bytes_;
  std::vector<zx_handle_t> handles_;

  zx_status_t OnMessage(Message message) override {
    ++message_count_;
    auto& bytes = message.bytes();
    bytes_ = std::vector<uint8_t>(bytes.data(), bytes.data() + bytes.actual());
    auto& handles = message.handles();
    handles_ = std::vector<zx_handle_t>(handles.data(),
                                        handles.data() + handles.actual());
    return ZX_OK;
  }
};

class StatusMessageHandler : public MessageHandler {
 public:
  zx_status_t status = ZX_OK;

  zx_status_t OnMessage(Message message) override {
    return status;
  }
};

class CallbackMessageHandler : public MessageHandler {
 public:
  std::function<zx_status_t(Message)> callback;

  zx_status_t OnMessage(Message message) override {
    return callback(std::move(message));
  }
};

TEST(ChannelReader, Trivial) {
  ChannelReader reader;
}

TEST(ChannelReader, Control) {
  CopyingMessageHandler handler;
  ChannelReader reader(&handler);

  zx::channel h1, h2;
  EXPECT_EQ(ZX_OK, zx::channel::create(0, &h1, &h2));

  async::Loop loop(&kTestLoopConfig);

  EXPECT_FALSE(reader.is_bound());
  zx_handle_t saved = h1.get();
  reader.Bind(std::move(h1));
  EXPECT_TRUE(reader.is_bound());
  EXPECT_EQ(saved, reader.channel().get());

  zx::channel h3 = reader.Unbind();
  EXPECT_EQ(saved, h3.get());
  reader.Bind(std::move(h3));

  EXPECT_EQ(ZX_OK, h2.write(0, "hello", 5, nullptr, 0));
  EXPECT_EQ(0, handler.message_count_);

  EXPECT_EQ(ZX_OK, reader.WaitAndDispatchMessageUntil(zx::time::infinite()));
  EXPECT_EQ(1, handler.message_count_);
  EXPECT_EQ(5u, handler.bytes_.size());
  EXPECT_EQ('h', handler.bytes_[0]);
  EXPECT_EQ(0u, handler.handles_.size());

  EXPECT_EQ(ZX_OK, h2.write(0, ", world", 7, nullptr, 0));
  EXPECT_EQ(ZX_OK, loop.RunUntilIdle());

  EXPECT_EQ(2, handler.message_count_);
  EXPECT_EQ(7u, handler.bytes_.size());
  EXPECT_EQ(',', handler.bytes_[0]);
  EXPECT_EQ(0u, handler.handles_.size());

  int error_count = 0;
  reader.set_error_handler([&error_count] {
    ++error_count;
  });

  h2.reset();
  EXPECT_EQ(0, error_count);
  EXPECT_TRUE(reader.is_bound());

  EXPECT_EQ(ZX_OK, loop.RunUntilIdle());

  EXPECT_EQ(1, error_count);
  EXPECT_FALSE(reader.is_bound());
}

TEST(ChannelReader, HandlerError) {
  StatusMessageHandler handler;
  handler.status = ZX_ERR_INTERNAL;

  ChannelReader reader;
  reader.set_message_handler(&handler);

  int error_count = 0;
  reader.set_error_handler([&error_count] {
    ++error_count;
  });

  async::Loop loop(&kTestLoopConfig);

  zx::channel h1, h2;
  EXPECT_EQ(ZX_OK, zx::channel::create(0, &h1, &h2));
  reader.Bind(std::move(h1));

  EXPECT_EQ(ZX_OK, h2.write(0, "hello", 5, nullptr, 0));

  EXPECT_EQ(0, error_count);
  EXPECT_TRUE(reader.is_bound());

  EXPECT_EQ(ZX_OK, loop.RunUntilIdle());

  EXPECT_EQ(1, error_count);
  EXPECT_FALSE(reader.is_bound());
}

TEST(ChannelReader, HandlerErrorWithoutErrorHandler) {
  StatusMessageHandler handler;
  handler.status = ZX_ERR_INTERNAL;

  ChannelReader reader;
  reader.set_message_handler(&handler);

  async::Loop loop(&kTestLoopConfig);

  zx::channel h1, h2;
  EXPECT_EQ(ZX_OK, zx::channel::create(0, &h1, &h2));
  reader.Bind(std::move(h1));

  EXPECT_EQ(ZX_OK, h2.write(0, "hello", 5, nullptr, 0));

  EXPECT_TRUE(reader.is_bound());

  EXPECT_EQ(ZX_OK, loop.RunUntilIdle());

  EXPECT_FALSE(reader.is_bound());
}

TEST(ChannelReader, HandlerStop) {
  StatusMessageHandler handler;
  handler.status = ZX_ERR_STOP;

  ChannelReader reader;
  reader.set_message_handler(&handler);

  int error_count = 0;
  reader.set_error_handler([&error_count] {
    ++error_count;
  });

  async::Loop loop(&kTestLoopConfig);

  zx::channel h1, h2;
  EXPECT_EQ(ZX_OK, zx::channel::create(0, &h1, &h2));
  reader.Bind(std::move(h1));

  EXPECT_EQ(ZX_OK, h2.write(0, "hello", 5, nullptr, 0));

  EXPECT_EQ(0, error_count);
  EXPECT_TRUE(reader.is_bound());

  EXPECT_EQ(ZX_OK, loop.RunUntilIdle());

  EXPECT_EQ(0, error_count);
  EXPECT_TRUE(reader.is_bound());

  // The handler generated ZX_ERR_STOP, which means the reader stopped without
  // calling the error callback or unbinding the channel. However, the reader is
  // no longer listening to the channel.

  CopyingMessageHandler logger;
  reader.set_message_handler(&logger);

  EXPECT_EQ(ZX_OK, h2.write(0, ", world", 7, nullptr, 0));
  EXPECT_EQ(ZX_OK, loop.RunUntilIdle());

  EXPECT_EQ(0, error_count);
  EXPECT_TRUE(reader.is_bound());
  EXPECT_EQ(0, logger.message_count_);
}

TEST(ChannelReader, BindTwice) {
  ChannelReader reader;
  async::Loop loop(&kTestLoopConfig);

  zx::channel h1, h2, j1, j2;
  EXPECT_EQ(ZX_OK, zx::channel::create(0, &h1, &h2));
  EXPECT_EQ(ZX_OK, zx::channel::create(0, &j1, &j2));
  zx_handle_t saved = j1.get();
  reader.Bind(std::move(h1));
  reader.Bind(std::move(j1));

  zx_signals_t pending = ZX_SIGNAL_NONE;
  EXPECT_EQ(ZX_OK, h2.wait_one(ZX_CHANNEL_PEER_CLOSED, zx::time(), &pending));
  EXPECT_TRUE(pending & ZX_CHANNEL_PEER_CLOSED);

  EXPECT_EQ(saved, reader.channel().get());
}

TEST(ChannelReader, WaitAndDispatchMessageUntilErrors) {
  ChannelReader reader;
  int error_count = 0;
  reader.set_error_handler([&error_count] {
    ++error_count;
  });

  EXPECT_EQ(0, error_count);
  EXPECT_EQ(ZX_ERR_BAD_STATE, reader.WaitAndDispatchMessageUntil(zx::time()));
  EXPECT_EQ(0, error_count);

  async::Loop loop(&kTestLoopConfig);

  zx::channel h1, h2;
  EXPECT_EQ(ZX_OK, zx::channel::create(0, &h1, &h2));
  h1.replace(ZX_RIGHT_NONE, &h1);
  EXPECT_EQ(ZX_ERR_ACCESS_DENIED, reader.Bind(std::move(h1)));
  EXPECT_EQ(0, error_count);
  EXPECT_FALSE(reader.is_bound());

  EXPECT_EQ(ZX_OK, zx::channel::create(0, &h1, &h2));
  EXPECT_EQ(ZX_OK, reader.Bind(std::move(h1)));
  EXPECT_EQ(0, error_count);
  EXPECT_TRUE(reader.is_bound());
  EXPECT_EQ(ZX_ERR_TIMED_OUT, reader.WaitAndDispatchMessageUntil(zx::time()));
  EXPECT_EQ(0, error_count);
  EXPECT_TRUE(reader.is_bound());

  zx_handle_close(reader.channel().get());

  EXPECT_EQ(0, error_count);
  EXPECT_TRUE(reader.is_bound());
  EXPECT_EQ(ZX_ERR_BAD_HANDLE, reader.WaitAndDispatchMessageUntil(zx::time()));
  EXPECT_EQ(1, error_count);
  EXPECT_FALSE(reader.is_bound());

  reader.Bind(std::move(h2));

  EXPECT_EQ(1, error_count);
  EXPECT_TRUE(reader.is_bound());
  EXPECT_EQ(ZX_ERR_PEER_CLOSED, reader.WaitAndDispatchMessageUntil(zx::time()));
  EXPECT_EQ(2, error_count);
  EXPECT_FALSE(reader.is_bound());
}

TEST(ChannelReader, UnbindDuringHandler) {
  ChannelReader reader;
  zx::channel stash;

  CallbackMessageHandler handler;
  handler.callback = [&reader, &stash](Message message) {
    stash = reader.Unbind();
    return ZX_OK;
  };

  reader.set_message_handler(&handler);

  int error_count = 0;
  reader.set_error_handler([&error_count] {
    ++error_count;
  });

  async::Loop loop(&kTestLoopConfig);

  zx::channel h1, h2;
  EXPECT_EQ(ZX_OK, zx::channel::create(0, &h1, &h2));
  reader.Bind(std::move(h1));

  EXPECT_EQ(ZX_OK, h2.write(0, "hello", 5, nullptr, 0));

  EXPECT_EQ(0, error_count);
  EXPECT_TRUE(reader.is_bound());
  EXPECT_FALSE(stash.is_valid());

  EXPECT_EQ(ZX_OK, loop.RunUntilIdle());

  EXPECT_EQ(0, error_count);
  EXPECT_FALSE(reader.is_bound());
  EXPECT_TRUE(stash.is_valid());

  // The handler unbound the channel, which means the reader is no longer
  // listening to the channel.

  CopyingMessageHandler logger;
  reader.set_message_handler(&logger);

  EXPECT_EQ(ZX_OK, h2.write(0, ", world", 7, nullptr, 0));
  EXPECT_EQ(ZX_OK, loop.RunUntilIdle());

  EXPECT_EQ(0, error_count);
  EXPECT_FALSE(reader.is_bound());
  EXPECT_EQ(0, logger.message_count_);
}

TEST(ChannelReader, ShouldWaitFromRead) {
  zx::channel h1, h2;
  EXPECT_EQ(ZX_OK, zx::channel::create(0, &h1, &h2));

  CallbackMessageHandler handler;
  ChannelReader reader(&handler);

  int message_count = 0;
  handler.callback = [&message_count, &reader](Message message) {
    ++message_count;
    uint32_t actual_bytes, actual_handles;
    EXPECT_EQ(ZX_ERR_BUFFER_TOO_SMALL,
        reader.channel().read(ZX_CHANNEL_READ_MAY_DISCARD,
                nullptr, 0, &actual_bytes, nullptr, 0, &actual_handles));
    return ZX_OK;
  };

  int error_count = 0;
  reader.set_error_handler([&error_count] {
    ++error_count;
  });

  async::Loop loop(&kTestLoopConfig);
  reader.Bind(std::move(h1));

  EXPECT_EQ(ZX_OK, h2.write(0, "hello", 5, nullptr, 0));
  EXPECT_EQ(ZX_OK, h2.write(0, ", world", 7, nullptr, 0));
  EXPECT_EQ(0, error_count);
  EXPECT_EQ(0, message_count);

  EXPECT_EQ(ZX_OK, loop.RunUntilIdle());

  EXPECT_EQ(0, error_count);
  EXPECT_EQ(1, message_count);
  EXPECT_TRUE(reader.is_bound());

  // The reader should still be listening to the channel.

  CopyingMessageHandler logger;
  reader.set_message_handler(&logger);

  EXPECT_EQ(ZX_OK, h2.write(0, "again!", 6, nullptr, 0));
  EXPECT_EQ(ZX_OK, loop.RunUntilIdle());

  EXPECT_EQ(0, error_count);
  EXPECT_EQ(1, logger.message_count_);
  EXPECT_EQ(6u, logger.bytes_.size());
  EXPECT_EQ('a', logger.bytes_[0]);
}

TEST(ChannelReader, ShouldWaitFromReadWithUnbind) {
  zx::channel h1, h2;
  EXPECT_EQ(ZX_OK, zx::channel::create(0, &h1, &h2));

  CallbackMessageHandler handler;
  ChannelReader reader(&handler);

  int message_count = 0;
  handler.callback = [&message_count, &reader](Message message) {
    ++message_count;
    uint32_t actual_bytes, actual_handles;
    EXPECT_EQ(ZX_ERR_BUFFER_TOO_SMALL,
        reader.channel().read(ZX_CHANNEL_READ_MAY_DISCARD,
                nullptr, 0, &actual_bytes, nullptr, 0, &actual_handles));
    reader.Unbind();
    return ZX_OK;
  };

  int error_count = 0;
  reader.set_error_handler([&error_count] {
    ++error_count;
  });

  async::Loop loop(&kTestLoopConfig);
  reader.Bind(std::move(h1));

  EXPECT_EQ(ZX_OK, h2.write(0, "hello", 5, nullptr, 0));
  EXPECT_EQ(ZX_OK, h2.write(0, ", world", 7, nullptr, 0));
  EXPECT_EQ(0, error_count);
  EXPECT_EQ(0, message_count);

  EXPECT_EQ(ZX_OK, loop.RunUntilIdle());

  EXPECT_EQ(0, error_count);
  EXPECT_EQ(1, message_count);
  EXPECT_FALSE(reader.is_bound());

  // The handler unbound the channel, so the reader should not be listening to
  // the channel anymore.

  EXPECT_EQ(ZX_ERR_PEER_CLOSED, h2.write(0, ", world", 7, nullptr, 0));
}

TEST(ChannelReader, NoHandler) {
  ChannelReader reader;

  int error_count = 0;
  reader.set_error_handler([&error_count] {
    ++error_count;
  });

  zx::channel h1, h2;
  EXPECT_EQ(ZX_OK, zx::channel::create(0, &h1, &h2));

  async::Loop loop(&kTestLoopConfig);
  reader.Bind(std::move(h1));

  EXPECT_EQ(ZX_OK, h2.write(0, "hello", 5, nullptr, 0));
  EXPECT_EQ(0, error_count);

  EXPECT_EQ(ZX_OK, loop.RunUntilIdle());

  EXPECT_EQ(0, error_count);
  EXPECT_TRUE(reader.is_bound());
}

}  // namespace
}  // namespace internal
}  // namespace fidl
