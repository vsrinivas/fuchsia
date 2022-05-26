// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/test.unknown.interactions/cpp/driver/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/cpp/task.h>
#include <lib/sync/cpp/completion.h>

#include <algorithm>
#include <future>
#include <optional>

#include <zxtest/zxtest.h>

#include "sdk/lib/fidl_driver/tests/transport/scoped_fake_driver.h"

namespace {
namespace test = ::test_unknown_interactions;

class UnknownInteractions : public ::zxtest::Test {
 protected:
  void SetUp() override {
    auto dispatcher = fdf::Dispatcher::Create(
        FDF_DISPATCHER_OPTION_ALLOW_SYNC_CALLS,
        [&](fdf_dispatcher_t* dispatcher) { dispatcher_shutdown_.Signal(); });
    ASSERT_TRUE(dispatcher.is_ok());
    dispatcher_ = std::move(dispatcher.value());

    auto endpoints = fdf::CreateEndpoints<test::UnknownInteractionsDriverProtocol>();
    ASSERT_OK(endpoints.status_value());
    client_end_ = std::move(endpoints->client);
    server_end_ = std::move(endpoints->server);
  }

  void TearDown() override {
    if (client_) {
      libsync::Completion unbound;
      async::PostTask(AsyncDispatcher(),
                      [&unbound, client = std::move(client_.value())]() { unbound.Signal(); });
      ASSERT_OK(unbound.Wait());
    }
    dispatcher_->ShutdownAsync();
    ASSERT_OK(dispatcher_shutdown_.Wait());
  }

  // Take the server end. May only be called once. Once this has been called,
  // |TakeServerChannel| is no longer allowed.
  fdf::ServerEnd<test::UnknownInteractionsDriverProtocol> TakeServerEnd() {
    EXPECT_TRUE(server_end_.is_valid());
    return std::move(server_end_);
  }

  // Take the server end channel directly. May only be called once. Once this
  // has been called, |TakeServerEnd| is no longer allowed.
  fdf::Channel TakeServerChannel() {
    EXPECT_TRUE(server_end_.is_valid());
    return server_end_.TakeHandle();
  }

  // Take the client channel directly. May only be called once. Once this has
  // been called, |AsyncClient| is no longer allowed.
  fdf::Channel TakeClientChannel() {
    EXPECT_TRUE(client_end_.is_valid());
    return client_end_.TakeHandle();
  }

  // Retrieve the client to use for async FDF calls. May be called more than
  // once. If the client has not been used yet, bind it. Once this has been
  // called, |TakeClientChannel| is no longer allowed.
  fdf::Client<test::UnknownInteractionsDriverProtocol>& AsyncClient() {
    if (client_) {
      return client_.value();
    }
    EXPECT_TRUE(client_end_.is_valid());
    auto client_end = std::move(client_end_);
    auto& client = client_.emplace();
    auto* dispatcher = Dispatcher();

    libsync::Completion bound;
    EXPECT_OK(async::PostTask(AsyncDispatcher(), [&bound, &client_end, &client, dispatcher]() {
      client.Bind(std::move(client_end), dispatcher);
      bound.Signal();
    }));
    EXPECT_OK(bound.Wait());
    return client;
  }

  // Run code using the AsyncClient on the dispatcher thread and wait for it to complete.
  void WithAsyncClientBlocking(
      fit::function<void(fdf::Client<test::UnknownInteractionsDriverProtocol>&)>&& func) {
    auto& client = AsyncClient();
    libsync::Completion done;
    async::PostTask(AsyncDispatcher(), [&done, &client, func = std::move(func)]() {
      func(client);
      done.Signal();
    });
    ASSERT_OK(done.Wait());
  }

  // Run code using the AsyncClient on the dispatcher thread. Caller is
  // responsible for synchronizing and making sure the func doesn't outlive any
  // borrowed references.
  void WithAsyncClient(
      fit::function<void(fdf::Client<test::UnknownInteractionsDriverProtocol>&)>&& func) {
    auto& client = AsyncClient();
    async::PostTask(AsyncDispatcher(), [&client, func = std::move(func)]() { func(client); });
  }

  fdf_dispatcher_t* Dispatcher() { return dispatcher_.value().get(); }

  async_dispatcher_t* AsyncDispatcher() { return dispatcher_.value().async_dispatcher(); }

 private:
  fidl_driver_testing::ScopedFakeDriver driver;
  libsync::Completion dispatcher_shutdown_;
  std::optional<fdf::Dispatcher> dispatcher_;

  fdf::ClientEnd<test::UnknownInteractionsDriverProtocol> client_end_;
  // If an AsyncClient is bound, it will be stored here.
  std::optional<fdf::Client<test::UnknownInteractionsDriverProtocol>> client_;

  fdf::ServerEnd<test::UnknownInteractionsDriverProtocol> server_end_;
};

template <size_t N>
std::array<uint8_t, N - sizeof(zx_txid_t)> ExcludeTxid(std::array<uint8_t, N> buf) {
  std::array<uint8_t, N - sizeof(zx_txid_t)> without_txid;
  std::memcpy(without_txid.data(), buf.data() + sizeof(zx_txid_t), without_txid.size());
  return without_txid;
}

// Helper for receiving raw data from a channel.
template <uint32_t N>
struct ReadResult {
  // Bytes from the read.
  std::array<uint8_t, N> buf;

  ReadResult() = delete;
  static ReadResult<N> ReadFromChannel(const fdf::Channel& channel, fdf_dispatcher_t* dispatcher) {
    ReadResult<N> result(channel, dispatcher);
    return result;
  }

  // Get the contents of the buffer excluding the transaction ID. This is used
  // to assert on the contents of a message generated by the bindings, where the
  // bindings will pick a txid that isn't known ahead of time.
  std::array<uint8_t, N - sizeof(zx_txid_t)> buf_excluding_txid() { return ExcludeTxid(buf); }

  // Get the transaction id portion of the buffer.
  zx_txid_t txid() {
    zx_txid_t value;
    std::memcpy(&value, buf.data(), sizeof(zx_txid_t));
    return value;
  }

 protected:
  // Constructs a |ReadResult| by waiting for a message to be available on the
  // given |fdf::Channel|, then reading it, asserting that the read is
  // successful and has the expected size and no handles, and stores the read
  // bytes in buf.
  explicit ReadResult(const fdf::Channel& channel, fdf_dispatcher_t* dispatcher) {
    libsync::Completion read_completion;
    auto channel_read = std::make_unique<fdf::ChannelRead>(
        channel.get(), 0,
        [&read_completion](fdf_dispatcher_t* dispatcher, fdf::ChannelRead* channel_read,
                           fdf_status_t status) { read_completion.Signal(); });
    ASSERT_OK(channel_read->Begin(dispatcher));
    ASSERT_OK(read_completion.Wait());

    auto result = channel.Read(0);
    ASSERT_TRUE(result.is_ok());
    ASSERT_EQ(N, result->num_bytes);
    ASSERT_EQ(0, result->handles.size());
    memcpy(buf.data(), result->data, N);
  }
};

template <uint32_t N>
struct TwoWayServerRequest : public ReadResult<N> {
  TwoWayServerRequest() = delete;

  static TwoWayServerRequest<N> ReadFromChannel(const fdf::Channel& channel,
                                                fdf_dispatcher_t* dispatcher) {
    TwoWayServerRequest<N> result(channel, dispatcher);
    return result;
  }

  // Helper to send a reply to the read as a two-way message.
  // Copies the txid (first four) bytes from |buf| into |reply_bytes| and sends
  // the result on the channel, storing the status in |reply_status|.
  template <size_t M>
  void reply(const fdf::Channel& channel, std::array<uint8_t, M> reply_bytes) {
    zx_txid_t txid = this->txid();
    std::memcpy(reply_bytes.data(), &txid, sizeof(zx_txid_t));

    auto arena = fdf::Arena::Create(0, "unknown-interactions-test");
    ASSERT_TRUE(arena.is_ok());
    auto data = arena->Allocate(M);
    memcpy(data, reply_bytes.data(), M);

    auto status =
        channel.Write(0, arena.value(), data, static_cast<uint32_t>(M), cpp20::span<zx_handle_t>());
    ASSERT_TRUE(status.is_ok());
  }

 protected:
  using ReadResult<N>::ReadResult;
};

template <typename T>
struct ResponseCompleter {
 public:
  void Signal(T&& value) {
    response_ = std::move(value);
    completion_.Signal();
  }

  zx::status<T> WaitForResponse() {
    auto status = completion_.Wait();
    if (status != ZX_OK) {
      return fitx::error(status);
    }
    return fitx::ok(std::move(response_.value()));
  }

 private:
  std::optional<T> response_;
  libsync::Completion completion_;
};

enum class ResultUnionTag : fidl_union_tag_t {
  kSuccess = 1,
  kApplicationError = 2,
  kTransportError = 3,
};

class InlineValue : public std::array<uint8_t, 4> {
 public:
  InlineValue(uint32_t value) { std::memcpy(data(), &value, sizeof(value)); }

  InlineValue(int32_t value) { std::memcpy(data(), &value, sizeof(value)); }
};

// Make an array representing a message with a transaction header and body.
template <typename FidlMethod>
std::array<uint8_t, sizeof(fidl_message_header_t) + sizeof(fidl_xunion_v2_t)> MakeMessage(
    fidl::MessageDynamicFlags dynamic_flags, ResultUnionTag result_union_tag,
    InlineValue inline_value) {
  fidl_message_header_t header{
      // In all test uses, txid is either 0 or excluded from assertions, so set to 0.
      .txid = 0,
      .at_rest_flags = {FIDL_MESSAGE_HEADER_AT_REST_FLAGS_0_USE_VERSION_V2, 0},
      .dynamic_flags =
          static_cast<std::underlying_type_t<fidl::MessageDynamicFlags>>(dynamic_flags),
      .magic_number = kFidlWireFormatMagicNumberInitial,
      .ordinal = fidl::internal::WireOrdinal<FidlMethod>::value,
  };
  fidl_xunion_v2_t body{
      .tag = static_cast<std::underlying_type_t<ResultUnionTag>>(result_union_tag),
      .envelope =
          {
              .num_handles = 0,
              .flags = 1,
          },
  };
  std::memcpy(body.envelope.inline_value, inline_value.data(), sizeof(body.envelope.inline_value));
  std::array<uint8_t, sizeof(fidl_message_header_t) + sizeof(fidl_xunion_v2_t)> result;
  std::memcpy(result.data(), &header, sizeof(fidl_message_header_t));
  std::memcpy(result.data() + sizeof(fidl_message_header_t), &body, sizeof(fidl_xunion_v2_t));
  return result;
}

// Make an array representing a message with just a transaction header.
template <typename FidlMethod>
std::array<uint8_t, sizeof(fidl_message_header_t)> MakeMessage(
    fidl::MessageDynamicFlags dynamic_flags) {
  fidl_message_header_t header{
      // In all test uses, txid is either 0 or excluded from assertions, so set to 0.
      .txid = 0,
      .at_rest_flags = {FIDL_MESSAGE_HEADER_AT_REST_FLAGS_0_USE_VERSION_V2, 0},
      .dynamic_flags =
          static_cast<std::underlying_type_t<fidl::MessageDynamicFlags>>(dynamic_flags),
      .magic_number = kFidlWireFormatMagicNumberInitial,
      .ordinal = fidl::internal::WireOrdinal<FidlMethod>::value,
  };
  std::array<uint8_t, sizeof(fidl_message_header_t)> result;
  std::memcpy(result.data(), &header, sizeof(fidl_message_header_t));
  return result;
}

TEST_F(UnknownInteractions, OneWayStrictAsyncSend) {
  auto server = TakeServerChannel();

  ResponseCompleter<fitx::result<fidl::Error>> response;
  WithAsyncClientBlocking([&response](auto& client) { response.Signal(client->StrictOneWay()); });
  auto result = response.WaitForResponse();
  ASSERT_TRUE(result.is_ok());
  EXPECT_TRUE(result.value().is_ok());

  auto received = ReadResult<16>::ReadFromChannel(server, Dispatcher());
  auto expected = MakeMessage<test::UnknownInteractionsDriverProtocol::StrictOneWay>(
      fidl::MessageDynamicFlags::kStrictMethod);
  EXPECT_EQ(expected, received.buf);
}

TEST_F(UnknownInteractions, OneWayFlexibleAsyncSend) {
  auto server = TakeServerChannel();

  ResponseCompleter<fitx::result<fidl::Error>> response;
  WithAsyncClientBlocking([&response](auto& client) { response.Signal(client->FlexibleOneWay()); });
  auto result = response.WaitForResponse();
  ASSERT_TRUE(result.is_ok());
  EXPECT_TRUE(result.value().is_ok());

  auto received = ReadResult<16>::ReadFromChannel(server, Dispatcher());
  auto expected = MakeMessage<test::UnknownInteractionsDriverProtocol::FlexibleOneWay>(
      fidl::MessageDynamicFlags::kFlexibleMethod);
  EXPECT_EQ(expected, received.buf);
}

TEST_F(UnknownInteractions, TwoWayStrictAsyncSend) {
  auto server = TakeServerChannel();

  ResponseCompleter<fdf::Result<test::UnknownInteractionsDriverProtocol::StrictTwoWay>>
      response_completer;
  WithAsyncClient([&response_completer](auto& client) {
    client->StrictTwoWay().Then(
        [&response_completer](auto& response) { response_completer.Signal(std::move(response)); });
  });

  auto received = TwoWayServerRequest<16>::ReadFromChannel(server, Dispatcher());
  auto expected = ExcludeTxid(MakeMessage<test::UnknownInteractionsDriverProtocol::StrictTwoWay>(
      fidl::MessageDynamicFlags::kStrictMethod));
  EXPECT_EQ(expected, received.buf_excluding_txid());
  EXPECT_NE(0, received.txid());

  auto server_reply = MakeMessage<test::UnknownInteractionsDriverProtocol::StrictTwoWay>(
      fidl::MessageDynamicFlags::kStrictMethod);
  received.reply(server, server_reply);

  auto result = response_completer.WaitForResponse();
  ASSERT_TRUE(result.is_ok());
  EXPECT_TRUE(result.value().is_ok());
}

TEST_F(UnknownInteractions, TwoWayStrictErrAsyncSend) {
  auto server = TakeServerChannel();

  ResponseCompleter<fdf::Result<test::UnknownInteractionsDriverProtocol::StrictTwoWayErr>>
      response_completer;
  WithAsyncClient([&response_completer](auto& client) {
    client->StrictTwoWayErr().Then(
        [&response_completer](auto& response) { response_completer.Signal(std::move(response)); });
  });

  auto received = TwoWayServerRequest<16>::ReadFromChannel(server, Dispatcher());
  auto expected = ExcludeTxid(MakeMessage<test::UnknownInteractionsDriverProtocol::StrictTwoWayErr>(
      fidl::MessageDynamicFlags::kStrictMethod));
  EXPECT_EQ(expected, received.buf_excluding_txid());
  EXPECT_NE(0, received.txid());

  auto server_reply = MakeMessage<test::UnknownInteractionsDriverProtocol::StrictTwoWayErr>(
      fidl::MessageDynamicFlags::kStrictMethod, ResultUnionTag::kSuccess, 0);
  received.reply(server, server_reply);

  auto result = response_completer.WaitForResponse();
  ASSERT_TRUE(result.is_ok());
  EXPECT_TRUE(result.value().is_ok());
}

TEST_F(UnknownInteractions, TwoWayFlexibleAsyncSend) {
  auto server = TakeServerChannel();

  ResponseCompleter<fdf::Result<test::UnknownInteractionsDriverProtocol::FlexibleTwoWay>>
      response_completer;
  WithAsyncClient([&response_completer](auto& client) {
    client->FlexibleTwoWay().Then(
        [&response_completer](auto& response) { response_completer.Signal(std::move(response)); });
  });

  auto received = TwoWayServerRequest<16>::ReadFromChannel(server, Dispatcher());
  auto expected = ExcludeTxid(MakeMessage<test::UnknownInteractionsDriverProtocol::FlexibleTwoWay>(
      fidl::MessageDynamicFlags::kFlexibleMethod));
  EXPECT_EQ(expected, received.buf_excluding_txid());
  EXPECT_NE(0, received.txid());

  auto server_reply = MakeMessage<test::UnknownInteractionsDriverProtocol::FlexibleTwoWay>(
      fidl::MessageDynamicFlags::kFlexibleMethod, ResultUnionTag::kSuccess, 0);
  received.reply(server, server_reply);

  auto result = response_completer.WaitForResponse();
  ASSERT_TRUE(result.is_ok());
  EXPECT_TRUE(result.value().is_ok());
}

TEST_F(UnknownInteractions, TwoWayFlexibleAsyncSendUnknownResponse) {
  auto server = TakeServerChannel();

  ResponseCompleter<fdf::Result<test::UnknownInteractionsDriverProtocol::FlexibleTwoWay>>
      response_completer;
  WithAsyncClient([&response_completer](auto& client) {
    client->FlexibleTwoWay().Then(
        [&response_completer](auto& response) { response_completer.Signal(std::move(response)); });
  });

  auto received = TwoWayServerRequest<16>::ReadFromChannel(server, Dispatcher());
  auto expected = ExcludeTxid(MakeMessage<test::UnknownInteractionsDriverProtocol::FlexibleTwoWay>(
      fidl::MessageDynamicFlags::kFlexibleMethod));
  EXPECT_EQ(expected, received.buf_excluding_txid());
  EXPECT_NE(0, received.txid());

  auto server_reply = MakeMessage<test::UnknownInteractionsDriverProtocol::FlexibleTwoWay>(
      fidl::MessageDynamicFlags::kFlexibleMethod, ResultUnionTag::kTransportError,
      ZX_ERR_NOT_SUPPORTED);
  received.reply(server, server_reply);

  auto result = response_completer.WaitForResponse();
  ASSERT_TRUE(result.is_ok());
  EXPECT_TRUE(result.value().is_error());
  EXPECT_EQ(ZX_ERR_NOT_SUPPORTED, result.value().error_value().status());
  EXPECT_EQ(fidl::Reason::kUnknownInteraction, result.value().error_value().reason());
}

TEST_F(UnknownInteractions, TwoWayFlexibleAsyncSendOtherTransportError) {
  auto server = TakeServerChannel();

  ResponseCompleter<fdf::Result<test::UnknownInteractionsDriverProtocol::FlexibleTwoWay>>
      response_completer;
  WithAsyncClient([&response_completer](auto& client) {
    client->FlexibleTwoWay().Then(
        [&response_completer](auto& response) { response_completer.Signal(std::move(response)); });
  });

  auto received = TwoWayServerRequest<16>::ReadFromChannel(server, Dispatcher());
  auto expected = ExcludeTxid(MakeMessage<test::UnknownInteractionsDriverProtocol::FlexibleTwoWay>(
      fidl::MessageDynamicFlags::kFlexibleMethod));
  EXPECT_EQ(expected, received.buf_excluding_txid());
  EXPECT_NE(0, received.txid());

  auto server_reply = MakeMessage<test::UnknownInteractionsDriverProtocol::FlexibleTwoWay>(
      fidl::MessageDynamicFlags::kFlexibleMethod, ResultUnionTag::kTransportError,
      ZX_ERR_ACCESS_DENIED);
  received.reply(server, server_reply);

  auto result = response_completer.WaitForResponse();
  ASSERT_TRUE(result.is_ok());
  EXPECT_TRUE(result.value().is_error());
  EXPECT_EQ(ZX_ERR_INTERNAL, result.value().error_value().status());
  EXPECT_EQ(fidl::Reason::kDecodeError, result.value().error_value().reason());
}

TEST_F(UnknownInteractions, TwoWayFlexibleAsyncSendOkTransportErr) {
  auto server = TakeServerChannel();

  ResponseCompleter<fdf::Result<test::UnknownInteractionsDriverProtocol::FlexibleTwoWay>>
      response_completer;
  WithAsyncClient([&response_completer](auto& client) {
    client->FlexibleTwoWay().Then(
        [&response_completer](auto& response) { response_completer.Signal(std::move(response)); });
  });

  auto received = TwoWayServerRequest<16>::ReadFromChannel(server, Dispatcher());
  auto expected = ExcludeTxid(MakeMessage<test::UnknownInteractionsDriverProtocol::FlexibleTwoWay>(
      fidl::MessageDynamicFlags::kFlexibleMethod));
  EXPECT_EQ(expected, received.buf_excluding_txid());
  EXPECT_NE(0, received.txid());

  auto server_reply = MakeMessage<test::UnknownInteractionsDriverProtocol::FlexibleTwoWay>(
      fidl::MessageDynamicFlags::kFlexibleMethod, ResultUnionTag::kTransportError, ZX_OK);
  received.reply(server, server_reply);

  auto result = response_completer.WaitForResponse();
  ASSERT_TRUE(result.is_ok());
  EXPECT_TRUE(result.value().is_error());
  EXPECT_EQ(ZX_ERR_INTERNAL, result.value().error_value().status());
  EXPECT_EQ(fidl::Reason::kDecodeError, result.value().error_value().reason());
}

TEST_F(UnknownInteractions, TwoWayFlexibleAsyncSendErrorVariant) {
  auto server = TakeServerChannel();

  ResponseCompleter<fdf::Result<test::UnknownInteractionsDriverProtocol::FlexibleTwoWay>>
      response_completer;
  WithAsyncClient([&response_completer](auto& client) {
    client->FlexibleTwoWay().Then(
        [&response_completer](auto& response) { response_completer.Signal(std::move(response)); });
  });

  auto received = TwoWayServerRequest<16>::ReadFromChannel(server, Dispatcher());
  auto expected = ExcludeTxid(MakeMessage<test::UnknownInteractionsDriverProtocol::FlexibleTwoWay>(
      fidl::MessageDynamicFlags::kFlexibleMethod));
  EXPECT_EQ(expected, received.buf_excluding_txid());
  EXPECT_NE(0, received.txid());

  auto server_reply = MakeMessage<test::UnknownInteractionsDriverProtocol::FlexibleTwoWay>(
      fidl::MessageDynamicFlags::kFlexibleMethod, ResultUnionTag::kApplicationError, 0x100);
  received.reply(server, server_reply);

  auto result = response_completer.WaitForResponse();
  ASSERT_TRUE(result.is_ok());
  EXPECT_TRUE(result.value().is_error());
  EXPECT_EQ(fidl::Reason::kDecodeError, result.value().error_value().reason());
}

TEST_F(UnknownInteractions, TwoWayFlexibleErrAsyncSend) {
  auto server = TakeServerChannel();

  ResponseCompleter<fdf::Result<test::UnknownInteractionsDriverProtocol::FlexibleTwoWayErr>>
      response_completer;
  WithAsyncClient([&response_completer](auto& client) {
    client->FlexibleTwoWayErr().Then(
        [&response_completer](auto& response) { response_completer.Signal(std::move(response)); });
  });

  auto received = TwoWayServerRequest<16>::ReadFromChannel(server, Dispatcher());
  auto expected =
      ExcludeTxid(MakeMessage<test::UnknownInteractionsDriverProtocol::FlexibleTwoWayErr>(
          fidl::MessageDynamicFlags::kFlexibleMethod));
  EXPECT_EQ(expected, received.buf_excluding_txid());
  EXPECT_NE(0, received.txid());

  auto server_reply = MakeMessage<test::UnknownInteractionsDriverProtocol::FlexibleTwoWayErr>(
      fidl::MessageDynamicFlags::kFlexibleMethod, ResultUnionTag::kSuccess, 0);
  received.reply(server, server_reply);

  auto result = response_completer.WaitForResponse();
  ASSERT_TRUE(result.is_ok());
  EXPECT_TRUE(result.value().is_ok());
}

TEST_F(UnknownInteractions, TwoWayFlexibleErrAsyncSendUnknownResponse) {
  auto server = TakeServerChannel();

  ResponseCompleter<fdf::Result<test::UnknownInteractionsDriverProtocol::FlexibleTwoWayErr>>
      response_completer;
  WithAsyncClient([&response_completer](auto& client) {
    client->FlexibleTwoWayErr().Then(
        [&response_completer](auto& response) { response_completer.Signal(std::move(response)); });
  });

  auto received = TwoWayServerRequest<16>::ReadFromChannel(server, Dispatcher());
  auto expected =
      ExcludeTxid(MakeMessage<test::UnknownInteractionsDriverProtocol::FlexibleTwoWayErr>(
          fidl::MessageDynamicFlags::kFlexibleMethod));
  EXPECT_EQ(expected, received.buf_excluding_txid());
  EXPECT_NE(0, received.txid());

  auto server_reply = MakeMessage<test::UnknownInteractionsDriverProtocol::FlexibleTwoWayErr>(
      fidl::MessageDynamicFlags::kFlexibleMethod, ResultUnionTag::kTransportError,
      ZX_ERR_NOT_SUPPORTED);
  received.reply(server, server_reply);

  auto result = response_completer.WaitForResponse();
  ASSERT_TRUE(result.is_ok());
  ASSERT_TRUE(result.value().is_error());
  ASSERT_TRUE(result.value().error_value().is_transport_error());
  EXPECT_EQ(ZX_ERR_NOT_SUPPORTED, result.value().error_value().transport_error().status());
  EXPECT_EQ(fidl::Reason::kUnknownInteraction,
            result.value().error_value().transport_error().reason());
}

TEST_F(UnknownInteractions, TwoWayFlexibleErrAsyncSendOtherTransportError) {
  auto server = TakeServerChannel();

  ResponseCompleter<fdf::Result<test::UnknownInteractionsDriverProtocol::FlexibleTwoWayErr>>
      response_completer;
  WithAsyncClient([&response_completer](auto& client) {
    client->FlexibleTwoWayErr().Then(
        [&response_completer](auto& response) { response_completer.Signal(std::move(response)); });
  });

  auto received = TwoWayServerRequest<16>::ReadFromChannel(server, Dispatcher());
  auto expected =
      ExcludeTxid(MakeMessage<test::UnknownInteractionsDriverProtocol::FlexibleTwoWayErr>(
          fidl::MessageDynamicFlags::kFlexibleMethod));
  EXPECT_EQ(expected, received.buf_excluding_txid());
  EXPECT_NE(0, received.txid());

  auto server_reply = MakeMessage<test::UnknownInteractionsDriverProtocol::FlexibleTwoWayErr>(
      fidl::MessageDynamicFlags::kFlexibleMethod, ResultUnionTag::kTransportError,
      ZX_ERR_ACCESS_DENIED);
  received.reply(server, server_reply);

  auto result = response_completer.WaitForResponse();
  ASSERT_TRUE(result.is_ok());
  ASSERT_TRUE(result.value().is_error());
  ASSERT_TRUE(result.value().error_value().is_transport_error());
  EXPECT_EQ(ZX_ERR_INTERNAL, result.value().error_value().transport_error().status());
  EXPECT_EQ(fidl::Reason::kDecodeError, result.value().error_value().transport_error().reason());
}

TEST_F(UnknownInteractions, TwoWayFlexibleErrAsyncSendErrorVariant) {
  auto server = TakeServerChannel();

  ResponseCompleter<fdf::Result<test::UnknownInteractionsDriverProtocol::FlexibleTwoWayErr>>
      response_completer;
  WithAsyncClient([&response_completer](auto& client) {
    client->FlexibleTwoWayErr().Then(
        [&response_completer](auto& response) { response_completer.Signal(std::move(response)); });
  });

  auto received = TwoWayServerRequest<16>::ReadFromChannel(server, Dispatcher());
  auto expected =
      ExcludeTxid(MakeMessage<test::UnknownInteractionsDriverProtocol::FlexibleTwoWayErr>(
          fidl::MessageDynamicFlags::kFlexibleMethod));
  EXPECT_EQ(expected, received.buf_excluding_txid());
  EXPECT_NE(0, received.txid());

  auto server_reply = MakeMessage<test::UnknownInteractionsDriverProtocol::FlexibleTwoWayErr>(
      fidl::MessageDynamicFlags::kFlexibleMethod, ResultUnionTag::kApplicationError, 0x100);
  received.reply(server, server_reply);

  auto result = response_completer.WaitForResponse();
  ASSERT_TRUE(result.is_ok());
  ASSERT_TRUE(result.value().is_error());
  ASSERT_TRUE(result.value().error_value().is_application_error());
  EXPECT_EQ(0x100, result.value().error_value().application_error());
}
}  // namespace
