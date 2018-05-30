// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zx/channel.h>

#include "gtest/gtest.h"
#include "lib/fidl/cpp/internal/message_reader.h"
#include "lib/fidl/cpp/test/async_loop_for_test.h"

namespace fidl {
namespace internal {
namespace {

class CopyingMessageHandler : public MessageHandler {
 public:
  int message_count_ = 0;
  int channel_gone_count_ = 0;
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

  void OnChannelGone() override { ++channel_gone_count_; }
};

class StatusMessageHandler : public MessageHandler {
 public:
  zx_status_t status = ZX_OK;

  zx_status_t OnMessage(Message message) override { return status; }
};

class CallbackMessageHandler : public MessageHandler {
 public:
  fit::function<zx_status_t(Message)> callback;

  zx_status_t OnMessage(Message message) override {
    return callback(std::move(message));
  }
};

class DestructionCounter {
 public:
  DestructionCounter(int* counter) : counter_(counter) {}

  DestructionCounter(DestructionCounter&& other) : counter_(other.counter_) {
    other.counter_ = nullptr;
  }

  DestructionCounter(const DestructionCounter&) = delete;
  DestructionCounter& operator=(const DestructionCounter&) = delete;
  DestructionCounter& operator=(DestructionCounter&& other) = delete;

  ~DestructionCounter() {
    if (counter_)
      ++(*counter_);
  }

 private:
  int* counter_ = nullptr;
};

TEST(MessageReader, Trivial) { MessageReader reader; }

TEST(MessageReader, Bind) {
  MessageReader reader;
  EXPECT_EQ(ZX_OK, reader.Bind(zx::channel()));
  EXPECT_FALSE(reader.is_bound());
  EXPECT_EQ(ZX_HANDLE_INVALID, reader.Unbind().get());
}

TEST(MessageReader, Control) {
  CopyingMessageHandler handler;
  MessageReader reader(&handler);

  zx::channel h1, h2;
  EXPECT_EQ(ZX_OK, zx::channel::create(0, &h1, &h2));

  fidl::test::AsyncLoopForTest loop;

  EXPECT_FALSE(reader.is_bound());
  zx_handle_t saved = h1.get();
  reader.Bind(std::move(h1));
  EXPECT_TRUE(reader.is_bound());
  EXPECT_EQ(saved, reader.channel().get());

  EXPECT_EQ(0, handler.channel_gone_count_);
  zx::channel h3 = reader.Unbind();
  EXPECT_EQ(saved, h3.get());
  EXPECT_EQ(1, handler.channel_gone_count_);
  reader.Bind(std::move(h3));

  EXPECT_EQ(ZX_OK, h2.write(0, "hello", 5, nullptr, 0));
  EXPECT_EQ(0, handler.message_count_);

  EXPECT_EQ(ZX_OK, reader.WaitAndDispatchOneMessageUntil(zx::time::infinite()));
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
  reader.set_error_handler([&error_count] { ++error_count; });

  h2.reset();
  EXPECT_EQ(0, error_count);
  EXPECT_EQ(1, handler.channel_gone_count_);
  EXPECT_TRUE(reader.is_bound());

  EXPECT_EQ(ZX_OK, loop.RunUntilIdle());

  EXPECT_EQ(1, error_count);
  EXPECT_EQ(2, handler.channel_gone_count_);
  EXPECT_FALSE(reader.is_bound());
}

TEST(MessageReader, HandlerError) {
  StatusMessageHandler handler;
  handler.status = ZX_ERR_INTERNAL;

  MessageReader reader;
  reader.set_message_handler(&handler);

  int error_count = 0;
  reader.set_error_handler([&error_count] { ++error_count; });

  fidl::test::AsyncLoopForTest loop;

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

TEST(MessageReader, HandlerErrorWithoutErrorHandler) {
  StatusMessageHandler handler;
  handler.status = ZX_ERR_INTERNAL;

  MessageReader reader;
  reader.set_message_handler(&handler);

  fidl::test::AsyncLoopForTest loop;

  zx::channel h1, h2;
  EXPECT_EQ(ZX_OK, zx::channel::create(0, &h1, &h2));
  reader.Bind(std::move(h1));

  EXPECT_EQ(ZX_OK, h2.write(0, "hello", 5, nullptr, 0));

  EXPECT_TRUE(reader.is_bound());

  EXPECT_EQ(ZX_OK, loop.RunUntilIdle());

  EXPECT_FALSE(reader.is_bound());
}

TEST(MessageReader, BindTwice) {
  MessageReader reader;
  fidl::test::AsyncLoopForTest loop;

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

TEST(MessageReader, WaitAndDispatchOneMessageUntilErrors) {
  MessageReader reader;
  int error_count = 0;
  reader.set_error_handler([&error_count] { ++error_count; });

  EXPECT_EQ(0, error_count);
  EXPECT_EQ(ZX_ERR_BAD_STATE,
            reader.WaitAndDispatchOneMessageUntil(zx::time()));
  EXPECT_EQ(0, error_count);

  fidl::test::AsyncLoopForTest loop;

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
  EXPECT_EQ(ZX_ERR_TIMED_OUT,
            reader.WaitAndDispatchOneMessageUntil(zx::time()));
  EXPECT_EQ(0, error_count);
  EXPECT_TRUE(reader.is_bound());

  reader.Bind(std::move(h2));

  EXPECT_EQ(0, error_count);
  EXPECT_TRUE(reader.is_bound());
  EXPECT_EQ(ZX_ERR_PEER_CLOSED,
            reader.WaitAndDispatchOneMessageUntil(zx::time()));
  EXPECT_EQ(1, error_count);
  EXPECT_FALSE(reader.is_bound());
}

TEST(MessageReader, UnbindDuringHandler) {
  MessageReader reader;
  zx::channel stash;

  CallbackMessageHandler handler;
  handler.callback = [&reader, &stash](Message message) {
    stash = reader.Unbind();
    return ZX_OK;
  };

  reader.set_message_handler(&handler);

  int error_count = 0;
  reader.set_error_handler([&error_count] { ++error_count; });

  fidl::test::AsyncLoopForTest loop;

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

TEST(MessageReader, ShouldWaitFromRead) {
  zx::channel h1, h2;
  EXPECT_EQ(ZX_OK, zx::channel::create(0, &h1, &h2));

  CallbackMessageHandler handler;
  MessageReader reader(&handler);

  int message_count = 0;
  handler.callback = [&message_count, &reader](Message message) {
    ++message_count;
    uint32_t actual_bytes, actual_handles;
    EXPECT_EQ(
        ZX_ERR_BUFFER_TOO_SMALL,
        reader.channel().read(ZX_CHANNEL_READ_MAY_DISCARD, nullptr, 0,
                              &actual_bytes, nullptr, 0, &actual_handles));
    return ZX_OK;
  };

  int error_count = 0;
  reader.set_error_handler([&error_count] { ++error_count; });

  fidl::test::AsyncLoopForTest loop;
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

TEST(MessageReader, ShouldWaitFromReadWithUnbind) {
  zx::channel h1, h2;
  EXPECT_EQ(ZX_OK, zx::channel::create(0, &h1, &h2));

  CallbackMessageHandler handler;
  MessageReader reader(&handler);

  int message_count = 0;
  handler.callback = [&message_count, &reader](Message message) {
    ++message_count;
    uint32_t actual_bytes, actual_handles;
    EXPECT_EQ(
        ZX_ERR_BUFFER_TOO_SMALL,
        reader.channel().read(ZX_CHANNEL_READ_MAY_DISCARD, nullptr, 0,
                              &actual_bytes, nullptr, 0, &actual_handles));
    reader.Unbind();
    return ZX_OK;
  };

  int error_count = 0;
  reader.set_error_handler([&error_count] { ++error_count; });

  fidl::test::AsyncLoopForTest loop;
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

TEST(MessageReader, NoHandler) {
  MessageReader reader;

  int error_count = 0;
  reader.set_error_handler([&error_count] { ++error_count; });

  zx::channel h1, h2;
  EXPECT_EQ(ZX_OK, zx::channel::create(0, &h1, &h2));

  fidl::test::AsyncLoopForTest loop;
  reader.Bind(std::move(h1));

  EXPECT_EQ(ZX_OK, h2.write(0, "hello", 5, nullptr, 0));
  EXPECT_EQ(0, error_count);

  EXPECT_EQ(ZX_OK, loop.RunUntilIdle());

  EXPECT_EQ(0, error_count);
  EXPECT_TRUE(reader.is_bound());
}

TEST(MessageReader, Reset) {
  MessageReader reader;

  int destruction_count = 0;
  DestructionCounter counter(&destruction_count);
  reader.set_error_handler([counter = std::move(counter)] {});

  zx::channel h1, h2;
  EXPECT_EQ(ZX_OK, zx::channel::create(0, &h1, &h2));

  fidl::test::AsyncLoopForTest loop;
  reader.Bind(std::move(h1));

  EXPECT_TRUE(reader.is_bound());
  EXPECT_EQ(0, destruction_count);

  reader.Reset();

  EXPECT_FALSE(reader.is_bound());
  EXPECT_EQ(1, destruction_count);
}

TEST(MessageReader, TakeChannelAndErrorHandlerFrom) {
  StatusMessageHandler handler1;
  handler1.status = ZX_OK;

  MessageReader reader1;
  reader1.set_message_handler(&handler1);

  int error_count = 0;
  reader1.set_error_handler([&error_count] { ++error_count; });

  StatusMessageHandler handler2;
  handler2.status = ZX_ERR_INTERNAL;

  MessageReader reader2;
  reader2.set_message_handler(&handler2);

  fidl::test::AsyncLoopForTest loop;

  zx::channel h1, h2;
  EXPECT_EQ(ZX_OK, zx::channel::create(0, &h1, &h2));
  reader1.Bind(std::move(h1));
  EXPECT_EQ(ZX_OK, reader2.TakeChannelAndErrorHandlerFrom(&reader1));
  EXPECT_FALSE(reader1.is_bound());
  EXPECT_TRUE(reader2.is_bound());

  EXPECT_EQ(ZX_OK, h2.write(0, "hello", 5, nullptr, 0));

  EXPECT_EQ(0, error_count);
  EXPECT_FALSE(reader1.is_bound());
  EXPECT_TRUE(reader2.is_bound());

  EXPECT_EQ(ZX_OK, loop.RunUntilIdle());

  EXPECT_EQ(1, error_count);
  EXPECT_FALSE(reader1.is_bound());
  EXPECT_FALSE(reader2.is_bound());
}

TEST(MessageReader, ReentrantDestruction) {
  std::unique_ptr<MessageReader> reader = std::make_unique<MessageReader>();

  int read_count = 0;

  CallbackMessageHandler handler;
  handler.callback = [&reader, &read_count](Message message) {
    ++read_count;
    reader.reset();
    return ZX_OK;
  };

  reader->set_message_handler(&handler);

  fidl::test::AsyncLoopForTest loop;

  zx::channel h1, h2;
  EXPECT_EQ(ZX_OK, zx::channel::create(0, &h1, &h2));
  reader->Bind(std::move(h1));

  EXPECT_EQ(ZX_OK, h2.write(0, "hello", 5, nullptr, 0));
  EXPECT_EQ(ZX_OK, h2.write(0, ", world", 7, nullptr, 0));

  EXPECT_TRUE(reader->is_bound());

  EXPECT_EQ(ZX_OK, loop.RunUntilIdle());

  EXPECT_FALSE(reader);

  // The handler destroyed the reader, which means the reader should have read
  // only one of the messages and its endpoint should be closed.

  EXPECT_EQ(1, read_count);
  EXPECT_EQ(ZX_ERR_PEER_CLOSED, h2.write(0, "!", 1, nullptr, 0));
}

TEST(MessageReader, DoubleReentrantDestruction) {
  std::unique_ptr<MessageReader> reader = std::make_unique<MessageReader>();

  int read_count = 0;

  CallbackMessageHandler handler;
  handler.callback = [&reader, &read_count](Message message) {
    ++read_count;
    if (read_count == 1) {
      reader->WaitAndDispatchOneMessageUntil(zx::time::infinite());
    } else {
      reader.reset();
    }
    return ZX_OK;
  };

  reader->set_message_handler(&handler);

  fidl::test::AsyncLoopForTest loop;

  zx::channel h1, h2;
  EXPECT_EQ(ZX_OK, zx::channel::create(0, &h1, &h2));
  reader->Bind(std::move(h1));

  EXPECT_EQ(ZX_OK, h2.write(0, "hello", 5, nullptr, 0));
  EXPECT_EQ(ZX_OK, h2.write(0, ", world", 7, nullptr, 0));
  EXPECT_EQ(ZX_OK, h2.write(0, "!", 1, nullptr, 0));

  EXPECT_TRUE(reader->is_bound());

  EXPECT_EQ(ZX_OK, loop.RunUntilIdle());

  EXPECT_FALSE(reader);

  // The handler destroyed the reader in a nested callstack, which means the
  // reader should have read two of the messages and its endpoint should be
  // closed.

  EXPECT_EQ(2, read_count);
  EXPECT_EQ(ZX_ERR_PEER_CLOSED, h2.write(0, "\n", 1, nullptr, 0));
}

TEST(MessageReader, DoubleReentrantUnbind) {
  MessageReader reader;

  int read_count = 0;

  CallbackMessageHandler handler;
  handler.callback = [&reader, &read_count](Message message) {
    ++read_count;
    if (read_count == 1) {
      reader.WaitAndDispatchOneMessageUntil(zx::time::infinite());
    } else {
      reader.Unbind();
    }
    return ZX_OK;
  };

  reader.set_message_handler(&handler);

  fidl::test::AsyncLoopForTest loop;

  zx::channel h1, h2;
  EXPECT_EQ(ZX_OK, zx::channel::create(0, &h1, &h2));
  reader.Bind(std::move(h1));

  EXPECT_EQ(ZX_OK, h2.write(0, "hello", 5, nullptr, 0));
  EXPECT_EQ(ZX_OK, h2.write(0, ", world", 7, nullptr, 0));
  EXPECT_EQ(ZX_OK, h2.write(0, "!", 1, nullptr, 0));

  EXPECT_TRUE(reader.is_bound());

  EXPECT_EQ(ZX_OK, loop.RunUntilIdle());

  EXPECT_FALSE(reader.is_bound());

  // The handler unbound the reader in a nested callstack, which means the
  // reader should have read two of the messages and its endpoint should be
  // closed.

  EXPECT_EQ(2, read_count);
  EXPECT_EQ(ZX_ERR_PEER_CLOSED, h2.write(0, "\n", 1, nullptr, 0));
}
}  // namespace
}  // namespace internal
}  // namespace fidl
