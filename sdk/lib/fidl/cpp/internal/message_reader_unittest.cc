// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/fidl/cpp/internal/message_reader.h"

#include <lib/zx/channel.h>
#include <zircon/errors.h>
#include <zircon/fidl.h>

#include <fidl/test/misc/cpp/fidl.h>
#include <zxtest/zxtest.h>

#include "lib/fidl/cpp/binding.h"
#include "lib/fidl/cpp/test/async_loop_for_test.h"
#include "lib/fidl/txn_header.h"

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
    handles_ = std::vector<zx_handle_t>(handles.data(), handles.data() + handles.actual());
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

  zx_status_t OnMessage(Message message) override { return callback(std::move(message)); }
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

class EchoServer : public fidl::test::misc::Echo {
 public:
  explicit EchoServer(fidl::InterfaceRequest<fidl::test::misc::Echo> request)
      : binding_(this, std::move(request)) {}

  void EchoString(fidl::StringPtr value, EchoStringCallback callback) override { callback(value); }

  void Close() { binding_.Close(10); }

 private:
  fidl::Binding<fidl::test::misc::Echo> binding_;
};

template <size_t N>
struct StringMessage {
  FIDL_ALIGNDECL
  fidl_message_header_t hdr;
  std::array<char, N> data;
};

constexpr size_t kMessageDataStart = 16;

template <size_t N>
StringMessage<N> CreateFidlMessage(const char (&data)[N]) {
  StringMessage<N> msg;
  fidl_init_txn_header(&msg.hdr, 0, 0);
  memcpy(&msg.data, data, N);
  return msg;
}

const auto test_msg0 = CreateFidlMessage("hello");
const uint32_t test_msg0_size = 21;
const auto test_msg1 = CreateFidlMessage(", world");
const uint32_t test_msg1_size = 23;
const auto test_msg2 = CreateFidlMessage("!");
const uint32_t test_msg2_size = 17;

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

  EXPECT_EQ(ZX_OK, h2.write(0, &test_msg0, test_msg0_size, nullptr, 0));
  EXPECT_EQ(0, handler.message_count_);

  EXPECT_EQ(ZX_OK, reader.WaitAndDispatchOneMessageUntil(zx::time::infinite()));
  EXPECT_EQ(1, handler.message_count_);
  EXPECT_EQ(test_msg0_size, handler.bytes_.size());
  EXPECT_EQ('h', handler.bytes_[kMessageDataStart]);
  EXPECT_EQ(0u, handler.handles_.size());

  EXPECT_EQ(ZX_OK, h2.write(0, &test_msg1, test_msg1_size, nullptr, 0));
  EXPECT_EQ(ZX_OK, loop.RunUntilIdle());

  EXPECT_EQ(2, handler.message_count_);
  EXPECT_EQ(test_msg1_size, handler.bytes_.size());
  EXPECT_EQ(',', handler.bytes_[kMessageDataStart]);
  EXPECT_EQ(0u, handler.handles_.size());

  int error_count = 0;
  reader.set_error_handler([&error_count](zx_status_t status) {
    EXPECT_EQ(ZX_ERR_PEER_CLOSED, status);
    ++error_count;
  });
  EXPECT_TRUE(reader.has_error_handler());

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
  reader.set_error_handler([&error_count](zx_status_t status) {
    EXPECT_EQ(ZX_ERR_INTERNAL, status);
    ++error_count;
  });

  fidl::test::AsyncLoopForTest loop;

  zx::channel h1, h2;
  EXPECT_EQ(ZX_OK, zx::channel::create(0, &h1, &h2));
  reader.Bind(std::move(h1));

  EXPECT_EQ(ZX_OK, h2.write(0, &test_msg0, test_msg0_size, nullptr, 0));

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

  EXPECT_FALSE(reader.has_error_handler());

  fidl::test::AsyncLoopForTest loop;

  zx::channel h1, h2;
  EXPECT_EQ(ZX_OK, zx::channel::create(0, &h1, &h2));
  reader.Bind(std::move(h1));

  EXPECT_EQ(ZX_OK, h2.write(0, &test_msg0, test_msg0_size, nullptr, 0));

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
  reader.set_error_handler([&error_count](zx_status_t status) {
    EXPECT_EQ(ZX_ERR_PEER_CLOSED, status);
    ++error_count;
  });

  EXPECT_TRUE(reader.has_error_handler());
  EXPECT_EQ(0, error_count);
  EXPECT_EQ(ZX_ERR_BAD_STATE, reader.WaitAndDispatchOneMessageUntil(zx::time()));
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
  EXPECT_EQ(ZX_ERR_TIMED_OUT, reader.WaitAndDispatchOneMessageUntil(zx::time()));
  EXPECT_EQ(0, error_count);
  EXPECT_TRUE(reader.is_bound());

  reader.Bind(std::move(h2));

  EXPECT_EQ(0, error_count);
  EXPECT_TRUE(reader.is_bound());
  EXPECT_EQ(ZX_ERR_PEER_CLOSED, reader.WaitAndDispatchOneMessageUntil(zx::time()));
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
  reader.set_error_handler([&error_count](zx_status_t status) {
    EXPECT_EQ(ZX_ERR_INVALID_ARGS, status);
    ++error_count;
  });
  EXPECT_TRUE(reader.has_error_handler());

  fidl::test::AsyncLoopForTest loop;

  zx::channel h1, h2;
  EXPECT_EQ(ZX_OK, zx::channel::create(0, &h1, &h2));
  reader.Bind(std::move(h1));

  EXPECT_EQ(ZX_OK, h2.write(0, &test_msg0, test_msg0_size, nullptr, 0));

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

  EXPECT_EQ(ZX_OK, h2.write(0, &test_msg0, test_msg0_size, nullptr, 0));
  EXPECT_EQ(ZX_OK, loop.RunUntilIdle());

  EXPECT_EQ(0, error_count);
  EXPECT_FALSE(reader.is_bound());
  EXPECT_EQ(0, logger.message_count_);

  reader.set_message_handler(nullptr);
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
    EXPECT_EQ(ZX_ERR_BUFFER_TOO_SMALL,
              reader.channel().read(ZX_CHANNEL_READ_MAY_DISCARD, nullptr, nullptr, 0, 0,
                                    &actual_bytes, &actual_handles));
    return ZX_OK;
  };

  int error_count = 0;
  reader.set_error_handler([&error_count](zx_status_t status) {
    EXPECT_EQ(ZX_ERR_CANCELED, status);
    ++error_count;
  });
  EXPECT_TRUE(reader.has_error_handler());

  fidl::test::AsyncLoopForTest loop;
  reader.Bind(std::move(h1));

  EXPECT_EQ(ZX_OK, h2.write(0, &test_msg0, test_msg0_size, nullptr, 0));
  EXPECT_EQ(ZX_OK, h2.write(0, &test_msg1, test_msg1_size, nullptr, 0));
  EXPECT_EQ(0, error_count);
  EXPECT_EQ(0, message_count);

  EXPECT_EQ(ZX_OK, loop.RunUntilIdle());

  EXPECT_EQ(0, error_count);
  EXPECT_EQ(1, message_count);
  EXPECT_TRUE(reader.is_bound());

  // The reader should still be listening to the channel.

  CopyingMessageHandler logger;
  reader.set_message_handler(&logger);

  auto msg = CreateFidlMessage("again!");
  EXPECT_EQ(ZX_OK, h2.write(0, &msg, 22, nullptr, 0));
  EXPECT_EQ(ZX_OK, loop.RunUntilIdle());

  EXPECT_EQ(0, error_count);
  EXPECT_EQ(1, logger.message_count_);
  EXPECT_EQ(22u, logger.bytes_.size());
  EXPECT_EQ('a', logger.bytes_[kMessageDataStart]);

  reader.set_message_handler(nullptr);
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
    EXPECT_EQ(ZX_ERR_BUFFER_TOO_SMALL,
              reader.channel().read(ZX_CHANNEL_READ_MAY_DISCARD, nullptr, nullptr, 0, 0,
                                    &actual_bytes, &actual_handles));
    reader.Unbind();
    return ZX_OK;
  };

  int error_count = 0;
  reader.set_error_handler([&error_count](zx_status_t status) {
    EXPECT_EQ(ZX_ERR_INVALID_ARGS, status);
    ++error_count;
  });
  EXPECT_TRUE(reader.has_error_handler());

  fidl::test::AsyncLoopForTest loop;
  reader.Bind(std::move(h1));

  EXPECT_EQ(ZX_OK, h2.write(0, &test_msg0, test_msg0_size, nullptr, 0));
  EXPECT_EQ(ZX_OK, h2.write(0, &test_msg1, test_msg1_size, nullptr, 0));
  EXPECT_EQ(0, error_count);
  EXPECT_EQ(0, message_count);

  EXPECT_EQ(ZX_OK, loop.RunUntilIdle());

  EXPECT_EQ(0, error_count);
  EXPECT_EQ(1, message_count);
  EXPECT_FALSE(reader.is_bound());

  // The handler unbound the channel, so the reader should not be listening to
  // the channel anymore.

  EXPECT_EQ(ZX_ERR_PEER_CLOSED, h2.write(0, &test_msg0, test_msg0_size, nullptr, 0));

  reader.set_message_handler(nullptr);
}

TEST(MessageReader, NoHandler) {
  MessageReader reader;

  int error_count = 0;
  reader.set_error_handler([&error_count](zx_status_t status) {
    EXPECT_EQ(ZX_ERR_CANCELED, status);
    ++error_count;
  });
  EXPECT_TRUE(reader.has_error_handler());

  zx::channel h1, h2;
  EXPECT_EQ(ZX_OK, zx::channel::create(0, &h1, &h2));

  fidl::test::AsyncLoopForTest loop;
  reader.Bind(std::move(h1));

  EXPECT_EQ(ZX_OK, h2.write(0, &test_msg0, test_msg0_size, nullptr, 0));
  EXPECT_EQ(0, error_count);

  EXPECT_EQ(ZX_OK, loop.RunUntilIdle());

  EXPECT_EQ(0, error_count);
  EXPECT_TRUE(reader.is_bound());
}

TEST(MessageReader, Reset) {
  MessageReader reader;

  int destruction_count = 0;
  DestructionCounter counter(&destruction_count);
  reader.set_error_handler([counter = std::move(counter)](zx_status_t status) {});
  EXPECT_TRUE(reader.has_error_handler());

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
  reader1.set_error_handler([&error_count](zx_status_t status) {
    EXPECT_EQ(ZX_ERR_INTERNAL, status);
    ++error_count;
  });
  EXPECT_TRUE(reader1.has_error_handler());

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

  EXPECT_EQ(ZX_OK, h2.write(0, &test_msg0, test_msg0_size, nullptr, 0));

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

  EXPECT_EQ(ZX_OK, h2.write(0, &test_msg0, test_msg0_size, nullptr, 0));
  EXPECT_EQ(ZX_OK, h2.write(0, &test_msg1, test_msg1_size, nullptr, 0));

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

  EXPECT_EQ(ZX_OK, h2.write(0, &test_msg0, test_msg0_size, nullptr, 0));
  EXPECT_EQ(ZX_OK, h2.write(0, &test_msg1, test_msg1_size, nullptr, 0));
  EXPECT_EQ(ZX_OK, h2.write(0, &test_msg2, test_msg2_size, nullptr, 0));

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

  EXPECT_EQ(ZX_OK, h2.write(0, &test_msg0, test_msg0_size, nullptr, 0));
  EXPECT_EQ(ZX_OK, h2.write(0, &test_msg1, test_msg1_size, nullptr, 0));
  EXPECT_EQ(ZX_OK, h2.write(0, &test_msg2, test_msg2_size, nullptr, 0));
  ;

  EXPECT_TRUE(reader.is_bound());

  EXPECT_EQ(ZX_OK, loop.RunUntilIdle());

  EXPECT_FALSE(reader.is_bound());

  // The handler unbound the reader in a nested callstack, which means the
  // reader should have read two of the messages and its endpoint should be
  // closed.

  EXPECT_EQ(2, read_count);
  EXPECT_EQ(ZX_ERR_PEER_CLOSED, h2.write(0, "\n", 1, nullptr, 0));
}

TEST(MessageReader, ReentrantErrorHandler) {
  fidl::test::AsyncLoopForTest loop;

  fidl::test::misc::EchoPtr echo_ptr;
  EchoServer server(echo_ptr.NewRequest());

  echo_ptr.set_error_handler([](zx_status_t status) {});

  auto* echo_ptr_ptr = echo_ptr.get();
  echo_ptr_ptr->EchoString("Some string",
                           [echo_ptr = std::move(echo_ptr)](fidl::StringPtr echoed_value) {});
  server.Close();
  loop.RunUntilIdle();
}

TEST(MessageReader, Close) {
  fidl::test::AsyncLoopForTest loop;

  zx::channel h1, h2;
  EXPECT_EQ(ZX_OK, zx::channel::create(0, &h1, &h2));

  MessageReader client, server;
  client.Bind(std::move(h1));
  EXPECT_TRUE(client.is_bound());
  server.Bind(std::move(h2));
  EXPECT_TRUE(server.is_bound());

  zx_status_t error = 0;
  client.set_error_handler([&error](zx_status_t remote_error) { error = remote_error; });

  constexpr zx_status_t kSysError = 0xabDECADE;

  EXPECT_EQ(ZX_OK, server.Close(kSysError));
  EXPECT_FALSE(server.is_bound());

  // should only be able to call Close successfully once
  EXPECT_EQ(ZX_ERR_BAD_STATE, server.Close(kSysError));

  loop.RunUntilIdle();
  EXPECT_EQ(kSysError, error);
  EXPECT_FALSE(client.is_bound());
}

TEST(MessageReader, MagicNumberCheck) {
  fidl::test::AsyncLoopForTest loop;

  zx::channel h1, writer;
  EXPECT_EQ(ZX_OK, zx::channel::create(0, &h1, &writer));

  MessageReader reader;
  reader.Bind(std::move(h1));
  EXPECT_TRUE(reader.is_bound());

  zx_status_t error = 0;
  reader.set_error_handler([&error](zx_status_t remote_error) { error = remote_error; });
  EXPECT_TRUE(reader.has_error_handler());

  fidl_message_header_t header;
  header.magic_number = 0xFF;
  EXPECT_EQ(ZX_OK, writer.write(0, &header, sizeof(header), nullptr, 0));

  loop.RunUntilIdle();
  EXPECT_EQ(error, ZX_ERR_PROTOCOL_NOT_SUPPORTED);
}

}  // namespace
}  // namespace internal
}  // namespace fidl
