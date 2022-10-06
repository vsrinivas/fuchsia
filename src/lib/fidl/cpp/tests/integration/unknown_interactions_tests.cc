// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/test.unknown.interactions/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>

#include <algorithm>
#include <future>
#include <optional>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

struct FakeUnknownMethod {
  static constexpr uint64_t kOrdinal = 0x10ff10ff10ff10ff;
};

namespace fidl::internal {
template <>
struct ::fidl::internal::WireOrdinal<FakeUnknownMethod> {
  static constexpr uint64_t value = FakeUnknownMethod::kOrdinal;
};
}  // namespace fidl::internal

namespace {
namespace test = ::test_unknown_interactions;

class UnknownInteractionsEventHandlerBase
    : public ::fidl::AsyncEventHandler<::test::UnknownInteractionsProtocol>,
      public ::fidl::SyncEventHandler<::test::UnknownInteractionsProtocol> {
  void StrictEvent(
      ::fidl::Event<::test_unknown_interactions::UnknownInteractionsProtocol::StrictEvent>&)
      override {
    ADD_FAILURE() << "StrictEvent called unexpectedly";
  }
  void StrictEventFields(
      ::fidl::Event<::test_unknown_interactions::UnknownInteractionsProtocol::StrictEventFields>&)
      override {
    ADD_FAILURE() << "StrictEventFields called unexpectedly";
  }
  void StrictEventErr(
      ::fidl::Event<::test_unknown_interactions::UnknownInteractionsProtocol::StrictEventErr>&)
      override {
    ADD_FAILURE() << "StrictEventErr called unexpectedly";
  }
  void StrictEventFieldsErr(
      ::fidl::Event<
          ::test_unknown_interactions::UnknownInteractionsProtocol::StrictEventFieldsErr>&)
      override {
    ADD_FAILURE() << "StrictEventFieldsErr called unexpectedly";
  }
  void FlexibleEvent(
      ::fidl::Event<::test_unknown_interactions::UnknownInteractionsProtocol::FlexibleEvent>&)
      override {
    ADD_FAILURE() << "FlexibleEvent called unexpectedly";
  }
  void FlexibleEventFields(
      ::fidl::Event<::test_unknown_interactions::UnknownInteractionsProtocol::FlexibleEventFields>&)
      override {
    ADD_FAILURE() << "FlexibleEventFields called unexpectedly";
  }
  void FlexibleEventErr(
      ::fidl::Event<::test_unknown_interactions::UnknownInteractionsProtocol::FlexibleEventErr>&)
      override {
    ADD_FAILURE() << "FlexibleEventErr called unexpectedly";
  }
  void FlexibleEventFieldsErr(
      ::fidl::Event<
          ::test_unknown_interactions::UnknownInteractionsProtocol::FlexibleEventFieldsErr>&)
      override {
    ADD_FAILURE() << "FlexibleEventFieldsErr called unexpectedly";
  }

  void handle_unknown_event(
      ::fidl::UnknownEventMetadata<::test::UnknownInteractionsProtocol> metadata) override {
    ADD_FAILURE() << "Unexpected flexible unknown event";
  }
};

class UnknownInteractionsServerBase : public ::fidl::Server<::test::UnknownInteractionsProtocol> {
  void StrictOneWay(StrictOneWayRequest& request, StrictOneWayCompleter::Sync& completer) override {
    ADD_FAILURE() << "StrictOneWay called unexpectedly";
  }

  void FlexibleOneWay(FlexibleOneWayRequest& request,
                      FlexibleOneWayCompleter::Sync& completer) override {
    ADD_FAILURE() << "FlexibleOneWay called unexpectedly";
  }

  void StrictTwoWay(StrictTwoWayRequest& request, StrictTwoWayCompleter::Sync& completer) override {
    ADD_FAILURE() << "StrictTwoWay called unexpectedly";
  }

  void StrictTwoWayFields(StrictTwoWayFieldsRequest& request,
                          StrictTwoWayFieldsCompleter::Sync& completer) override {
    ADD_FAILURE() << "StrictTwoWayFields called unexpectedly";
  }

  void FlexibleTwoWay(FlexibleTwoWayRequest& request,
                      FlexibleTwoWayCompleter::Sync& completer) override {
    ADD_FAILURE() << "FlexibleTwoWay called unexpectedly";
  }

  void FlexibleTwoWayFields(FlexibleTwoWayFieldsRequest& request,
                            FlexibleTwoWayFieldsCompleter::Sync& completer) override {
    ADD_FAILURE() << "FlexibleTwoWayFields called unexpectedly";
  }

  void StrictTwoWayErr(StrictTwoWayErrRequest& request,
                       StrictTwoWayErrCompleter::Sync& completer) override {
    ADD_FAILURE() << "StrictTwoWayErr called unexpectedly";
  }

  void StrictTwoWayFieldsErr(StrictTwoWayFieldsErrRequest& request,
                             StrictTwoWayFieldsErrCompleter::Sync& completer) override {
    ADD_FAILURE() << "StrictTwoWayFieldsErr called unexpectedly";
  }

  void FlexibleTwoWayErr(FlexibleTwoWayErrRequest& request,
                         FlexibleTwoWayErrCompleter::Sync& completer) override {
    ADD_FAILURE() << "FlexibleTwoWayErr called unexpectedly";
  }

  void FlexibleTwoWayFieldsErr(FlexibleTwoWayFieldsErrRequest& request,
                               FlexibleTwoWayFieldsErrCompleter::Sync& completer) override {
    ADD_FAILURE() << "FlexibleTwoWayFieldsErr called unexpectedly";
  }

  void handle_unknown_method(
      ::fidl::UnknownMethodMetadata<::test::UnknownInteractionsProtocol> metadata,
      ::fidl::UnknownMethodCompleter::Sync& completer) override {
    ADD_FAILURE() << "Unexpected flexible unknown method";
  }
};

class UnknownInteractions : public ::testing::Test {
 protected:
  void SetUp() override {
    loop_.emplace(&kAsyncLoopConfigAttachToCurrentThread);

    auto endpoints = fidl::CreateEndpoints<test::UnknownInteractionsProtocol>();
    ASSERT_EQ(endpoints.status_value(), ZX_OK);
    client_end_ = std::move(endpoints->client);
    server_end_ = std::move(endpoints->server);
  }

  async::Loop& loop() { return loop_.value(); }

  fidl::ServerEnd<test::UnknownInteractionsProtocol> TakeServerEnd() {
    EXPECT_TRUE(server_end_.is_valid());
    return std::move(server_end_);
  }

  template <typename ServerImpl>
  fidl::ServerBindingRef<typename ServerImpl::_EnclosingProtocol> BindServer(ServerImpl* impl) {
    return ::fidl::BindServer(
        loop().dispatcher(),
        fidl::ServerEnd<typename ServerImpl::_EnclosingProtocol>(TakeServerChannel()), impl);
  }

  zx::channel TakeServerChannel() {
    EXPECT_TRUE(server_end_.is_valid());
    return server_end_.TakeChannel();
  }

  zx::channel TakeClientChannel() {
    EXPECT_TRUE(client_end_.is_valid());
    return client_end_.TakeChannel();
  }

  fidl::SyncClient<test::UnknownInteractionsProtocol> SyncClient() {
    EXPECT_TRUE(client_end_.is_valid());
    return fidl::SyncClient<test::UnknownInteractionsProtocol>(std::move(client_end_));
  }

  fidl::Client<test::UnknownInteractionsProtocol> AsyncClient(
      fidl::AsyncEventHandler<test::UnknownInteractionsProtocol>* event_handler = nullptr) {
    EXPECT_TRUE(client_end_.is_valid());
    return fidl::Client<test::UnknownInteractionsProtocol>(std::move(client_end_),
                                                           loop_->dispatcher(), event_handler);
  }

 private:
  std::optional<async::Loop> loop_;
  fidl::ClientEnd<test::UnknownInteractionsProtocol> client_end_;
  fidl::ServerEnd<test::UnknownInteractionsProtocol> server_end_;
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
  // Status from reading.
  zx_status_t status;
  // Bytes from the read.
  std::array<uint8_t, N> buf;

  ReadResult() = delete;
  static ReadResult<N> ReadFromChannel(const zx::channel& channel) {
    ReadResult<N> result(channel);
    return result;
  }

  // Get the contents of the buffer excluding the transaction ID.
  std::array<uint8_t, N - sizeof(zx_txid_t)> buf_excluding_txid() { return ExcludeTxid(buf); }

  // Get the transaction id portion of the buffer.
  zx_txid_t txid() {
    zx_txid_t value;
    std::memcpy(&value, buf.data(), sizeof(zx_txid_t));
    return value;
  }

 protected:
  // Construct a ReadResult by reading from a channel.
  explicit ReadResult(const zx::channel& channel) {
    status = channel.wait_one(ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED, zx::time::infinite(),
                              nullptr);
    if (status != ZX_OK)
      return;
    uint32_t num_bytes;
    uint32_t num_handles;
    status = channel.read(/* flags= */ 0, buf.data(), /* handles= */ nullptr, N,
                          /* num_handles= */ 0, &num_bytes, &num_handles);
    if (status == ZX_OK) {
      EXPECT_EQ(N, num_bytes);
      EXPECT_EQ(0u, num_handles);
    }
  }
};

template <uint32_t N>
struct TwoWayServerRequest : public ReadResult<N> {
  TwoWayServerRequest() = delete;
  static TwoWayServerRequest<N> ReadFromChannel(const zx::channel& channel) {
    TwoWayServerRequest<N> result(channel);
    return result;
  }

  // Helper to send a reply to the read as a two-way message.
  // Copies the txid (first four) bytes from |buf| into |reply_bytes| and sends
  // the result on the channel, storing the status in |reply_status|.
  template <size_t M>
  void reply(const zx::channel& channel, std::array<uint8_t, M> reply_bytes) {
    std::copy(this->buf.begin(), this->buf.begin() + 4, reply_bytes.begin());
    ASSERT_EQ(ZX_OK, channel.write(/* flags= */ 0, reply_bytes.data(), static_cast<uint32_t>(M),
                                   /* handles= */ nullptr, /* num_handles= */ 0));
  }

 protected:
  using ReadResult<N>::ReadResult;
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
    zx_txid_t txid, fidl::MessageDynamicFlags dynamic_flags, ResultUnionTag result_union_tag,
    InlineValue inline_value) {
  fidl_message_header_t header{
      .txid = txid,
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

// Make an array representing a message with a transaction header and body.
// This version is for tests which don't care about the txid or want a zero
// txid (e.g. one way interactions).
template <typename FidlMethod>
std::array<uint8_t, sizeof(fidl_message_header_t) + sizeof(fidl_xunion_v2_t)> MakeMessage(
    fidl::MessageDynamicFlags dynamic_flags, ResultUnionTag result_union_tag,
    InlineValue inline_value) {
  return MakeMessage<FidlMethod>(0, dynamic_flags, result_union_tag, inline_value);
}

// Make an array representing a message with just a transaction header.
template <typename FidlMethod>
std::array<uint8_t, sizeof(fidl_message_header_t)> MakeMessage(
    zx_txid_t txid, fidl::MessageDynamicFlags dynamic_flags) {
  fidl_message_header_t header{
      .txid = txid,
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

// Make an array representing a message with just a transaction header.
// This version is for tests which don't care about the txid or want a zero
// txid (e.g. one way interactions).
template <typename FidlMethod>
std::array<uint8_t, sizeof(fidl_message_header_t)> MakeMessage(
    fidl::MessageDynamicFlags dynamic_flags) {
  return MakeMessage<FidlMethod>(0, dynamic_flags);
}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
//// Client Side Tests
///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////////
//// One-Way Methods - Async Client
///////////////////////////////////////////////////////////////////////////////

TEST_F(UnknownInteractions, OneWayStrictAsyncSend) {
  auto client = AsyncClient();
  auto server = TakeServerChannel();
  auto result = client->StrictOneWay();
  EXPECT_TRUE(result.is_ok());

  auto received = ReadResult<16>::ReadFromChannel(server);
  EXPECT_EQ(ZX_OK, received.status);

  auto expected = MakeMessage<test::UnknownInteractionsProtocol::StrictOneWay>(
      fidl::MessageDynamicFlags::kStrictMethod);
  EXPECT_THAT(received.buf, ::testing::ContainerEq(expected));
}

TEST_F(UnknownInteractions, OneWayFlexibleAsyncSend) {
  auto client = AsyncClient();
  auto server = TakeServerChannel();
  auto result = client->FlexibleOneWay();
  EXPECT_TRUE(result.is_ok());

  auto received = ReadResult<16>::ReadFromChannel(server);
  EXPECT_EQ(ZX_OK, received.status);

  auto expected = MakeMessage<test::UnknownInteractionsProtocol::FlexibleOneWay>(
      fidl::MessageDynamicFlags::kFlexibleMethod);
  EXPECT_THAT(received.buf, ::testing::ContainerEq(expected));
}

///////////////////////////////////////////////////////////////////////////////
//// Two-Way Methods - Async Client
///////////////////////////////////////////////////////////////////////////////

TEST_F(UnknownInteractions, TwoWayStrictAsyncSend) {
  auto client = AsyncClient();
  auto server = TakeServerChannel();

  client->StrictTwoWay().Then([](auto& response) { EXPECT_TRUE(response.is_ok()); });

  auto received = TwoWayServerRequest<16>::ReadFromChannel(server);
  EXPECT_EQ(ZX_OK, received.status);
  auto expected = ExcludeTxid(MakeMessage<test::UnknownInteractionsProtocol::StrictTwoWay>(
      fidl::MessageDynamicFlags::kStrictMethod));
  EXPECT_THAT(received.buf_excluding_txid(), ::testing::ContainerEq(expected));
  EXPECT_NE(0u, received.txid());

  auto server_reply = MakeMessage<test::UnknownInteractionsProtocol::StrictTwoWay>(
      fidl::MessageDynamicFlags::kStrictMethod);
  received.reply(server, server_reply);

  loop().RunUntilIdle();
}

TEST_F(UnknownInteractions, TwoWayStrictErrAsyncSend) {
  auto client = AsyncClient();
  auto server = TakeServerChannel();

  client->StrictTwoWayErr().Then([](auto& response) { EXPECT_TRUE(response.is_ok()); });

  auto received = TwoWayServerRequest<16>::ReadFromChannel(server);
  EXPECT_EQ(ZX_OK, received.status);
  auto expected = ExcludeTxid(MakeMessage<test::UnknownInteractionsProtocol::StrictTwoWayErr>(
      fidl::MessageDynamicFlags::kStrictMethod));
  EXPECT_THAT(received.buf_excluding_txid(), ::testing::ContainerEq(expected));
  EXPECT_NE(0u, received.txid());

  auto server_reply = MakeMessage<test::UnknownInteractionsProtocol::StrictTwoWayErr>(
      fidl::MessageDynamicFlags::kStrictMethod, ResultUnionTag::kSuccess, 0);
  received.reply(server, server_reply);

  loop().RunUntilIdle();
}

TEST_F(UnknownInteractions, TwoWayFlexibleAsyncSend) {
  auto client = AsyncClient();
  auto server = TakeServerChannel();

  client->FlexibleTwoWay().Then([](auto& response) { EXPECT_TRUE(response.is_ok()); });

  auto received = TwoWayServerRequest<16>::ReadFromChannel(server);
  EXPECT_EQ(ZX_OK, received.status);
  auto expected = ExcludeTxid(MakeMessage<test::UnknownInteractionsProtocol::FlexibleTwoWay>(
      fidl::MessageDynamicFlags::kFlexibleMethod));
  EXPECT_THAT(received.buf_excluding_txid(), ::testing::ContainerEq(expected));
  EXPECT_NE(0u, received.txid());

  auto server_reply = MakeMessage<test::UnknownInteractionsProtocol::FlexibleTwoWay>(
      fidl::MessageDynamicFlags::kFlexibleMethod, ResultUnionTag::kSuccess, 0);
  received.reply(server, server_reply);

  loop().RunUntilIdle();
}

TEST_F(UnknownInteractions, TwoWayFlexibleAsyncSendUnknownResponse) {
  auto client = AsyncClient();
  auto server = TakeServerChannel();

  client->FlexibleTwoWay().Then([](auto& response) {
    ASSERT_TRUE(response.is_error());
    EXPECT_EQ(ZX_ERR_NOT_SUPPORTED, response.error_value().status());
    EXPECT_EQ(fidl::Reason::kUnknownMethod, response.error_value().reason());
  });

  auto received = TwoWayServerRequest<16>::ReadFromChannel(server);
  EXPECT_EQ(ZX_OK, received.status);
  auto expected = ExcludeTxid(MakeMessage<test::UnknownInteractionsProtocol::FlexibleTwoWay>(
      fidl::MessageDynamicFlags::kFlexibleMethod));
  EXPECT_THAT(received.buf_excluding_txid(), ::testing::ContainerEq(expected));
  EXPECT_NE(0u, received.txid());

  auto server_reply = MakeMessage<test::UnknownInteractionsProtocol::FlexibleTwoWay>(
      fidl::MessageDynamicFlags::kFlexibleMethod, ResultUnionTag::kTransportError,
      ZX_ERR_NOT_SUPPORTED);
  received.reply(server, server_reply);

  loop().RunUntilIdle();
}

TEST_F(UnknownInteractions, TwoWayFlexibleAsyncSendOtherTransportError) {
  auto client = AsyncClient();
  auto server = TakeServerChannel();

  client->FlexibleTwoWay().Then([](auto& response) {
    ASSERT_TRUE(response.is_error());
    EXPECT_EQ(ZX_ERR_INVALID_ARGS, response.error_value().status());
    EXPECT_EQ(fidl::Reason::kDecodeError, response.error_value().reason());
  });

  auto received = TwoWayServerRequest<16>::ReadFromChannel(server);
  EXPECT_EQ(ZX_OK, received.status);
  auto expected = ExcludeTxid(MakeMessage<test::UnknownInteractionsProtocol::FlexibleTwoWay>(
      fidl::MessageDynamicFlags::kFlexibleMethod));
  EXPECT_THAT(received.buf_excluding_txid(), ::testing::ContainerEq(expected));
  EXPECT_NE(0u, received.txid());

  auto server_reply = MakeMessage<test::UnknownInteractionsProtocol::FlexibleTwoWay>(
      fidl::MessageDynamicFlags::kFlexibleMethod, ResultUnionTag::kTransportError,
      ZX_ERR_INVALID_ARGS);
  received.reply(server, server_reply);

  loop().RunUntilIdle();
}

TEST_F(UnknownInteractions, TwoWayFlexibleAsyncSendOkTransportError) {
  auto client = AsyncClient();
  auto server = TakeServerChannel();

  client->FlexibleTwoWay().Then([](auto& response) {
    ASSERT_TRUE(response.is_error());
    EXPECT_EQ(ZX_ERR_INVALID_ARGS, response.error_value().status());
    EXPECT_EQ(fidl::Reason::kDecodeError, response.error_value().reason());
  });

  auto received = TwoWayServerRequest<16>::ReadFromChannel(server);
  EXPECT_EQ(ZX_OK, received.status);
  auto expected = ExcludeTxid(MakeMessage<test::UnknownInteractionsProtocol::FlexibleTwoWay>(
      fidl::MessageDynamicFlags::kFlexibleMethod));
  EXPECT_THAT(received.buf_excluding_txid(), ::testing::ContainerEq(expected));
  EXPECT_NE(0u, received.txid());

  auto server_reply = MakeMessage<test::UnknownInteractionsProtocol::FlexibleTwoWay>(
      fidl::MessageDynamicFlags::kFlexibleMethod, ResultUnionTag::kTransportError, ZX_OK);
  received.reply(server, server_reply);

  loop().RunUntilIdle();
}

TEST_F(UnknownInteractions, TwoWayFlexibleAsyncSendErrorVariant) {
  auto client = AsyncClient();
  auto server = TakeServerChannel();

  client->FlexibleTwoWay().Then([](auto& response) {
    ASSERT_TRUE(response.is_error());
    EXPECT_EQ(fidl::Reason::kDecodeError, response.error_value().reason());
  });

  auto received = TwoWayServerRequest<16>::ReadFromChannel(server);
  EXPECT_EQ(ZX_OK, received.status);
  auto expected = ExcludeTxid(MakeMessage<test::UnknownInteractionsProtocol::FlexibleTwoWay>(
      fidl::MessageDynamicFlags::kFlexibleMethod));
  EXPECT_THAT(received.buf_excluding_txid(), ::testing::ContainerEq(expected));
  EXPECT_NE(0u, received.txid());

  auto server_reply = MakeMessage<test::UnknownInteractionsProtocol::FlexibleTwoWay>(
      fidl::MessageDynamicFlags::kFlexibleMethod, ResultUnionTag::kApplicationError, 0x100);
  received.reply(server, server_reply);

  loop().RunUntilIdle();
}

TEST_F(UnknownInteractions, TwoWayFlexibleFieldsAsyncSend) {
  auto client = AsyncClient();
  auto server = TakeServerChannel();

  client->FlexibleTwoWayFields().Then([](auto& response) {
    ASSERT_TRUE(response.is_ok());
    EXPECT_EQ(32, response.value().some_field());
  });

  auto received = TwoWayServerRequest<16>::ReadFromChannel(server);
  EXPECT_EQ(ZX_OK, received.status);
  auto expected = ExcludeTxid(MakeMessage<test::UnknownInteractionsProtocol::FlexibleTwoWayFields>(
      fidl::MessageDynamicFlags::kFlexibleMethod));
  EXPECT_THAT(received.buf_excluding_txid(), ::testing::ContainerEq(expected));
  EXPECT_NE(0u, received.txid());

  auto server_reply = MakeMessage<test::UnknownInteractionsProtocol::FlexibleTwoWayFields>(
      fidl::MessageDynamicFlags::kFlexibleMethod, ResultUnionTag::kSuccess, 32);
  received.reply(server, server_reply);

  loop().RunUntilIdle();
}

TEST_F(UnknownInteractions, TwoWayFlexibleFieldsAsyncSendUnknownResponse) {
  auto client = AsyncClient();
  auto server = TakeServerChannel();

  client->FlexibleTwoWayFields().Then([](auto& response) {
    ASSERT_TRUE(response.is_error());
    EXPECT_EQ(ZX_ERR_NOT_SUPPORTED, response.error_value().status());
    EXPECT_EQ(fidl::Reason::kUnknownMethod, response.error_value().reason());
  });

  auto received = TwoWayServerRequest<16>::ReadFromChannel(server);
  EXPECT_EQ(ZX_OK, received.status);
  auto expected = ExcludeTxid(MakeMessage<test::UnknownInteractionsProtocol::FlexibleTwoWayFields>(
      fidl::MessageDynamicFlags::kFlexibleMethod));
  EXPECT_THAT(received.buf_excluding_txid(), ::testing::ContainerEq(expected));
  EXPECT_NE(0u, received.txid());

  auto server_reply = MakeMessage<test::UnknownInteractionsProtocol::FlexibleTwoWayFields>(
      fidl::MessageDynamicFlags::kFlexibleMethod, ResultUnionTag::kTransportError,
      ZX_ERR_NOT_SUPPORTED);
  received.reply(server, server_reply);

  loop().RunUntilIdle();
}

TEST_F(UnknownInteractions, TwoWayFlexibleErrAsyncSend) {
  auto client = AsyncClient();
  auto server = TakeServerChannel();

  client->FlexibleTwoWayErr().Then([](auto& response) { EXPECT_TRUE(response.is_ok()); });

  auto received = TwoWayServerRequest<16>::ReadFromChannel(server);
  EXPECT_EQ(ZX_OK, received.status);
  auto expected = ExcludeTxid(MakeMessage<test::UnknownInteractionsProtocol::FlexibleTwoWayErr>(
      fidl::MessageDynamicFlags::kFlexibleMethod));
  EXPECT_THAT(received.buf_excluding_txid(), ::testing::ContainerEq(expected));
  EXPECT_NE(0u, received.txid());

  auto server_reply = MakeMessage<test::UnknownInteractionsProtocol::FlexibleTwoWayErr>(
      fidl::MessageDynamicFlags::kFlexibleMethod, ResultUnionTag::kSuccess, 0);
  received.reply(server, server_reply);

  loop().RunUntilIdle();
}

TEST_F(UnknownInteractions, TwoWayFlexibleErrAsyncSendUnknownResponse) {
  auto client = AsyncClient();
  auto server = TakeServerChannel();

  client->FlexibleTwoWayErr().Then([](auto& response) {
    ASSERT_TRUE(response.is_error());
    ASSERT_TRUE(response.error_value().is_framework_error());
    EXPECT_EQ(ZX_ERR_NOT_SUPPORTED, response.error_value().framework_error().status());
    EXPECT_EQ(fidl::Reason::kUnknownMethod, response.error_value().framework_error().reason());
  });

  auto received = TwoWayServerRequest<16>::ReadFromChannel(server);
  EXPECT_EQ(ZX_OK, received.status);
  auto expected = ExcludeTxid(MakeMessage<test::UnknownInteractionsProtocol::FlexibleTwoWayErr>(
      fidl::MessageDynamicFlags::kFlexibleMethod));
  EXPECT_THAT(received.buf_excluding_txid(), ::testing::ContainerEq(expected));
  EXPECT_NE(0u, received.txid());

  auto server_reply = MakeMessage<test::UnknownInteractionsProtocol::FlexibleTwoWayErr>(
      fidl::MessageDynamicFlags::kFlexibleMethod, ResultUnionTag::kTransportError,
      ZX_ERR_NOT_SUPPORTED);
  received.reply(server, server_reply);

  loop().RunUntilIdle();
}

TEST_F(UnknownInteractions, TwoWayFlexibleErrAsyncSendOtherTransportError) {
  auto client = AsyncClient();
  auto server = TakeServerChannel();

  client->FlexibleTwoWayErr().Then([](auto& response) {
    ASSERT_TRUE(response.is_error());
    ASSERT_TRUE(response.error_value().is_framework_error());
    EXPECT_EQ(ZX_ERR_INVALID_ARGS, response.error_value().framework_error().status());
    EXPECT_EQ(fidl::Reason::kDecodeError, response.error_value().framework_error().reason());
  });

  auto received = TwoWayServerRequest<16>::ReadFromChannel(server);
  EXPECT_EQ(ZX_OK, received.status);
  auto expected = ExcludeTxid(MakeMessage<test::UnknownInteractionsProtocol::FlexibleTwoWayErr>(
      fidl::MessageDynamicFlags::kFlexibleMethod));
  EXPECT_THAT(received.buf_excluding_txid(), ::testing::ContainerEq(expected));
  EXPECT_NE(0u, received.txid());

  auto server_reply = MakeMessage<test::UnknownInteractionsProtocol::FlexibleTwoWayErr>(
      fidl::MessageDynamicFlags::kFlexibleMethod, ResultUnionTag::kTransportError,
      ZX_ERR_ACCESS_DENIED);
  received.reply(server, server_reply);

  loop().RunUntilIdle();
}

TEST_F(UnknownInteractions, TwoWayFlexibleErrAsyncSendErrorVariant) {
  auto client = AsyncClient();
  auto server = TakeServerChannel();

  client->FlexibleTwoWayErr().Then([](auto& response) {
    ASSERT_TRUE(response.is_error());
    ASSERT_TRUE(response.error_value().is_domain_error());
    EXPECT_EQ(0x100, response.error_value().domain_error());
  });

  auto received = TwoWayServerRequest<16>::ReadFromChannel(server);
  EXPECT_EQ(ZX_OK, received.status);
  auto expected = ExcludeTxid(MakeMessage<test::UnknownInteractionsProtocol::FlexibleTwoWayErr>(
      fidl::MessageDynamicFlags::kFlexibleMethod));
  EXPECT_THAT(received.buf_excluding_txid(), ::testing::ContainerEq(expected));
  EXPECT_NE(0u, received.txid());

  auto server_reply = MakeMessage<test::UnknownInteractionsProtocol::FlexibleTwoWayErr>(
      fidl::MessageDynamicFlags::kFlexibleMethod, ResultUnionTag::kApplicationError, 0x100);
  received.reply(server, server_reply);

  loop().RunUntilIdle();
}

TEST_F(UnknownInteractions, TwoWayFlexibleFieldsErrAsyncSend) {
  auto client = AsyncClient();
  auto server = TakeServerChannel();

  client->FlexibleTwoWayFieldsErr().Then([](auto& response) {
    ASSERT_TRUE(response.is_ok());
    EXPECT_EQ(32, response.value().some_field());
  });

  auto received = TwoWayServerRequest<16>::ReadFromChannel(server);
  EXPECT_EQ(ZX_OK, received.status);
  auto expected =
      ExcludeTxid(MakeMessage<test::UnknownInteractionsProtocol::FlexibleTwoWayFieldsErr>(
          fidl::MessageDynamicFlags::kFlexibleMethod));
  EXPECT_THAT(received.buf_excluding_txid(), ::testing::ContainerEq(expected));
  EXPECT_NE(0u, received.txid());

  auto server_reply = MakeMessage<test::UnknownInteractionsProtocol::FlexibleTwoWayFieldsErr>(
      fidl::MessageDynamicFlags::kFlexibleMethod, ResultUnionTag::kSuccess, 32);
  received.reply(server, server_reply);

  loop().RunUntilIdle();
}

TEST_F(UnknownInteractions, TwoWayFlexibleFieldsErrAsyncSendUnknownResponse) {
  auto client = AsyncClient();
  auto server = TakeServerChannel();

  client->FlexibleTwoWayFieldsErr().Then([](auto& response) {
    ASSERT_TRUE(response.is_error());
    ASSERT_TRUE(response.error_value().is_framework_error());
    EXPECT_EQ(ZX_ERR_NOT_SUPPORTED, response.error_value().framework_error().status());
    EXPECT_EQ(fidl::Reason::kUnknownMethod, response.error_value().framework_error().reason());
  });

  auto received = TwoWayServerRequest<16>::ReadFromChannel(server);
  EXPECT_EQ(ZX_OK, received.status);
  auto expected =
      ExcludeTxid(MakeMessage<test::UnknownInteractionsProtocol::FlexibleTwoWayFieldsErr>(
          fidl::MessageDynamicFlags::kFlexibleMethod));
  EXPECT_THAT(received.buf_excluding_txid(), ::testing::ContainerEq(expected));
  EXPECT_NE(0u, received.txid());

  auto server_reply = MakeMessage<test::UnknownInteractionsProtocol::FlexibleTwoWayFieldsErr>(
      fidl::MessageDynamicFlags::kFlexibleMethod, ResultUnionTag::kTransportError,
      ZX_ERR_NOT_SUPPORTED);
  received.reply(server, server_reply);

  loop().RunUntilIdle();
}

TEST_F(UnknownInteractions, TwoWayFlexibleFieldsErrAsyncSendErrorVariant) {
  auto client = AsyncClient();
  auto server = TakeServerChannel();

  client->FlexibleTwoWayFieldsErr().Then([](auto& response) {
    ASSERT_TRUE(response.is_error());
    ASSERT_TRUE(response.error_value().is_domain_error());
    EXPECT_EQ(0x100, response.error_value().domain_error());
  });

  auto received = TwoWayServerRequest<16>::ReadFromChannel(server);
  EXPECT_EQ(ZX_OK, received.status);
  auto expected =
      ExcludeTxid(MakeMessage<test::UnknownInteractionsProtocol::FlexibleTwoWayFieldsErr>(
          fidl::MessageDynamicFlags::kFlexibleMethod));
  EXPECT_THAT(received.buf_excluding_txid(), ::testing::ContainerEq(expected));
  EXPECT_NE(0u, received.txid());

  auto server_reply = MakeMessage<test::UnknownInteractionsProtocol::FlexibleTwoWayFieldsErr>(
      fidl::MessageDynamicFlags::kFlexibleMethod, ResultUnionTag::kApplicationError, 0x100);
  received.reply(server, server_reply);

  loop().RunUntilIdle();
}

///////////////////////////////////////////////////////////////////////////////
//// Events - Async Client
///////////////////////////////////////////////////////////////////////////////

TEST_F(UnknownInteractions, ReceiveStrictEventAsync) {
  class EventHandler : public UnknownInteractionsEventHandlerBase {
    void StrictEvent(
        ::fidl::Event<::test_unknown_interactions::UnknownInteractionsProtocol::StrictEvent>&)
        override {
      received_event = true;
    }

   public:
    bool received_event = false;
  };
  EventHandler handler;
  auto client = AsyncClient(&handler);
  auto server = TakeServerChannel();

  auto server_message = MakeMessage<::test::UnknownInteractionsProtocol::StrictEvent>(
      ::fidl::MessageDynamicFlags::kStrictMethod);
  ASSERT_EQ(ZX_OK, server.write(0, &server_message, server_message.size(), nullptr, 0));

  loop().RunUntilIdle();

  EXPECT_TRUE(handler.received_event);
}

TEST_F(UnknownInteractions, ReceiveStrictEventAsyncMismatchedStrictness) {
  class EventHandler : public UnknownInteractionsEventHandlerBase {
    void StrictEvent(
        ::fidl::Event<::test_unknown_interactions::UnknownInteractionsProtocol::StrictEvent>&)
        override {
      received_event = true;
    }

   public:
    bool received_event = false;
  };
  EventHandler handler;
  auto client = AsyncClient(&handler);
  auto server = TakeServerChannel();

  auto server_message = MakeMessage<::test::UnknownInteractionsProtocol::StrictEvent>(
      ::fidl::MessageDynamicFlags::kFlexibleMethod);
  ASSERT_EQ(ZX_OK, server.write(0, &server_message, server_message.size(), nullptr, 0));

  loop().RunUntilIdle();

  EXPECT_TRUE(handler.received_event);
}

TEST_F(UnknownInteractions, ReceiveFlexibleEventAsync) {
  class EventHandler : public UnknownInteractionsEventHandlerBase {
    void FlexibleEvent(
        ::fidl::Event<::test_unknown_interactions::UnknownInteractionsProtocol::FlexibleEvent>&)
        override {
      received_event = true;
    }

   public:
    bool received_event = false;
  };
  EventHandler handler;
  auto client = AsyncClient(&handler);
  auto server = TakeServerChannel();

  auto server_message = MakeMessage<::test::UnknownInteractionsProtocol::FlexibleEvent>(
      ::fidl::MessageDynamicFlags::kFlexibleMethod);
  ASSERT_EQ(ZX_OK, server.write(0, &server_message, server_message.size(), nullptr, 0));

  loop().RunUntilIdle();

  EXPECT_TRUE(handler.received_event);
}

TEST_F(UnknownInteractions, ReceiveFlexibleEventAsyncMismatchedStrictness) {
  class EventHandler : public UnknownInteractionsEventHandlerBase {
    void FlexibleEvent(
        ::fidl::Event<::test_unknown_interactions::UnknownInteractionsProtocol::FlexibleEvent>&)
        override {
      received_event = true;
    }

   public:
    bool received_event = false;
  };
  EventHandler handler;
  auto client = AsyncClient(&handler);
  auto server = TakeServerChannel();

  auto server_message = MakeMessage<::test::UnknownInteractionsProtocol::FlexibleEvent>(
      ::fidl::MessageDynamicFlags::kStrictMethod);
  ASSERT_EQ(ZX_OK, server.write(0, &server_message, server_message.size(), nullptr, 0));

  loop().RunUntilIdle();

  EXPECT_TRUE(handler.received_event);
}

///////////////////////////////////////////////////////////////////////////////
//// Unknown messages - Async Client
///////////////////////////////////////////////////////////////////////////////

TEST_F(UnknownInteractions, UnknownServerSentTwoWayAsyncClient) {
  class EventHandler : public UnknownInteractionsEventHandlerBase {};
  EventHandler handler;
  auto client = AsyncClient(&handler);
  auto server = TakeServerChannel();

  auto server_message =
      MakeMessage<FakeUnknownMethod>(0xABCD, ::fidl::MessageDynamicFlags::kFlexibleMethod);
  ASSERT_EQ(ZX_OK, server.write(0, &server_message, server_message.size(), nullptr, 0));

  loop().RunUntilIdle();

  auto received = ReadResult<16>::ReadFromChannel(server);
  EXPECT_EQ(ZX_ERR_PEER_CLOSED, received.status);
}

TEST_F(UnknownInteractions, UnknownStrictEventAsync) {
  class EventHandler : public UnknownInteractionsEventHandlerBase {};
  EventHandler handler;
  auto client = AsyncClient(&handler);
  auto server = TakeServerChannel();

  auto server_message = MakeMessage<FakeUnknownMethod>(::fidl::MessageDynamicFlags::kStrictMethod);
  ASSERT_EQ(ZX_OK, server.write(0, &server_message, server_message.size(), nullptr, 0));

  loop().RunUntilIdle();

  auto received = ReadResult<16>::ReadFromChannel(server);
  EXPECT_EQ(ZX_ERR_PEER_CLOSED, received.status);
}

TEST_F(UnknownInteractions, UnknownFlexibleEventAsync) {
  class EventHandler : public UnknownInteractionsEventHandlerBase {
    void handle_unknown_event(
        ::fidl::UnknownEventMetadata<::test::UnknownInteractionsProtocol> metadata) override {
      received_event = true;
      ASSERT_EQ(FakeUnknownMethod::kOrdinal, metadata.method_ordinal);
    }

   public:
    bool received_event = false;
  };
  EventHandler handler;
  auto client = AsyncClient(&handler);
  auto server = TakeServerChannel();

  auto server_message =
      MakeMessage<FakeUnknownMethod>(::fidl::MessageDynamicFlags::kFlexibleMethod);
  ASSERT_EQ(ZX_OK, server.write(0, &server_message, server_message.size(), nullptr, 0));

  loop().RunUntilIdle();

  ASSERT_TRUE(handler.received_event);

  // Write again to check that the channel is still open.
  ASSERT_EQ(ZX_OK, server.write(0, &server_message, server_message.size(), nullptr, 0));
}

TEST_F(UnknownInteractions, UnknownStrictEventAsyncAjarProtocol) {
  class EventHandler : public ::fidl::AsyncEventHandler<::test::UnknownInteractionsAjarProtocol> {
    void handle_unknown_event(
        ::fidl::UnknownEventMetadata<::test::UnknownInteractionsAjarProtocol> metadata) override {
      ADD_FAILURE() << "Unexpected flexible unknown event";
    }
  };

  EventHandler handler;
  auto client = fidl::Client<::test::UnknownInteractionsAjarProtocol>(
      ::fidl::ClientEnd<::test::UnknownInteractionsAjarProtocol>(TakeClientChannel()),
      loop().dispatcher(), &handler);
  auto server = TakeServerChannel();

  auto server_message = MakeMessage<FakeUnknownMethod>(::fidl::MessageDynamicFlags::kStrictMethod);
  ASSERT_EQ(ZX_OK, server.write(0, &server_message, server_message.size(), nullptr, 0));

  loop().RunUntilIdle();

  auto received = ReadResult<16>::ReadFromChannel(server);
  EXPECT_EQ(ZX_ERR_PEER_CLOSED, received.status);
}

TEST_F(UnknownInteractions, UnknownFlexibleEventAsyncAjarProtocol) {
  class EventHandler : public ::fidl::AsyncEventHandler<::test::UnknownInteractionsAjarProtocol> {
    void handle_unknown_event(
        ::fidl::UnknownEventMetadata<::test::UnknownInteractionsAjarProtocol> metadata) override {
      received_event = true;
      ASSERT_EQ(FakeUnknownMethod::kOrdinal, metadata.method_ordinal);
    }

   public:
    bool received_event = false;
  };
  EventHandler handler;
  auto client = fidl::Client<::test::UnknownInteractionsAjarProtocol>(
      ::fidl::ClientEnd<::test::UnknownInteractionsAjarProtocol>(TakeClientChannel()),
      loop().dispatcher(), &handler);
  auto server = TakeServerChannel();

  auto server_message =
      MakeMessage<FakeUnknownMethod>(::fidl::MessageDynamicFlags::kFlexibleMethod);
  ASSERT_EQ(ZX_OK, server.write(0, &server_message, server_message.size(), nullptr, 0));

  loop().RunUntilIdle();

  ASSERT_TRUE(handler.received_event);

  // Write again to check that the channel is still open.
  ASSERT_EQ(ZX_OK, server.write(0, &server_message, server_message.size(), nullptr, 0));
}

TEST_F(UnknownInteractions, UnknownStrictEventAsyncClosedProtocol) {
  class EventHandler : public ::fidl::AsyncEventHandler<::test::UnknownInteractionsClosedProtocol> {
  };
  EventHandler handler;
  auto client = fidl::Client<::test::UnknownInteractionsClosedProtocol>(
      ::fidl::ClientEnd<::test::UnknownInteractionsClosedProtocol>(TakeClientChannel()),
      loop().dispatcher(), &handler);
  auto server = TakeServerChannel();

  auto server_message = MakeMessage<FakeUnknownMethod>(::fidl::MessageDynamicFlags::kStrictMethod);
  ASSERT_EQ(ZX_OK, server.write(0, &server_message, server_message.size(), nullptr, 0));

  loop().RunUntilIdle();

  auto received = ReadResult<16>::ReadFromChannel(server);
  EXPECT_EQ(ZX_ERR_PEER_CLOSED, received.status);
}

TEST_F(UnknownInteractions, UnknownFlexibleEventAsyncClosedProtocol) {
  class EventHandler : public ::fidl::AsyncEventHandler<::test::UnknownInteractionsClosedProtocol> {
  };
  EventHandler handler;
  auto client = fidl::Client<::test::UnknownInteractionsClosedProtocol>(
      ::fidl::ClientEnd<::test::UnknownInteractionsClosedProtocol>(TakeClientChannel()),
      loop().dispatcher(), &handler);
  auto server = TakeServerChannel();

  auto server_message =
      MakeMessage<FakeUnknownMethod>(::fidl::MessageDynamicFlags::kFlexibleMethod);
  ASSERT_EQ(ZX_OK, server.write(0, &server_message, server_message.size(), nullptr, 0));

  loop().RunUntilIdle();

  auto received = ReadResult<16>::ReadFromChannel(server);
  EXPECT_EQ(ZX_ERR_PEER_CLOSED, received.status);
}

///////////////////////////////////////////////////////////////////////////////
//// One-Way Methods - Sync Client
///////////////////////////////////////////////////////////////////////////////

TEST_F(UnknownInteractions, OneWayStrictSyncSend) {
  auto client = SyncClient();
  auto server = TakeServerChannel();
  auto result = client->StrictOneWay();
  EXPECT_TRUE(result.is_ok());

  auto received = ReadResult<16>::ReadFromChannel(server);
  EXPECT_EQ(ZX_OK, received.status);

  auto expected = MakeMessage<test::UnknownInteractionsProtocol::StrictOneWay>(
      fidl::MessageDynamicFlags::kStrictMethod);
  EXPECT_THAT(received.buf, ::testing::ContainerEq(expected));
}

TEST_F(UnknownInteractions, OneWayFlexibleSyncSend) {
  auto client = SyncClient();
  auto server = TakeServerChannel();
  auto result = client->FlexibleOneWay();
  EXPECT_TRUE(result.is_ok());

  auto received = ReadResult<16>::ReadFromChannel(server);
  EXPECT_EQ(ZX_OK, received.status);

  auto expected = MakeMessage<test::UnknownInteractionsProtocol::FlexibleOneWay>(
      fidl::MessageDynamicFlags::kFlexibleMethod);
  EXPECT_THAT(received.buf, ::testing::ContainerEq(expected));
}

///////////////////////////////////////////////////////////////////////////////
//// Two-Way Methods - Sync Client
///////////////////////////////////////////////////////////////////////////////

TEST_F(UnknownInteractions, TwoWayStrictSyncSend) {
  auto client = SyncClient();
  auto server = TakeServerChannel();

  auto response_fut = std::async([&client]() { return client->StrictTwoWay(); });

  auto received = TwoWayServerRequest<16>::ReadFromChannel(server);
  EXPECT_EQ(ZX_OK, received.status);
  auto expected = ExcludeTxid(MakeMessage<test::UnknownInteractionsProtocol::StrictTwoWay>(
      fidl::MessageDynamicFlags::kStrictMethod));
  EXPECT_THAT(received.buf_excluding_txid(), ::testing::ContainerEq(expected));
  EXPECT_NE(0u, received.txid());

  auto server_reply = MakeMessage<test::UnknownInteractionsProtocol::StrictTwoWay>(
      fidl::MessageDynamicFlags::kStrictMethod);
  received.reply(server, server_reply);

  auto response = response_fut.get();
  EXPECT_TRUE(response.is_ok());
}

TEST_F(UnknownInteractions, TwoWayStrictErrSyncSend) {
  auto client = SyncClient();
  auto server = TakeServerChannel();

  auto response_fut = std::async([&client]() { return client->StrictTwoWayErr(); });

  auto received = TwoWayServerRequest<16>::ReadFromChannel(server);
  EXPECT_EQ(ZX_OK, received.status);
  auto expected = ExcludeTxid(MakeMessage<test::UnknownInteractionsProtocol::StrictTwoWayErr>(
      fidl::MessageDynamicFlags::kStrictMethod));
  EXPECT_THAT(received.buf_excluding_txid(), ::testing::ContainerEq(expected));
  EXPECT_NE(0u, received.txid());

  auto server_reply = MakeMessage<test::UnknownInteractionsProtocol::StrictTwoWayErr>(
      fidl::MessageDynamicFlags::kStrictMethod, ResultUnionTag::kSuccess, 0);
  received.reply(server, server_reply);

  auto response = response_fut.get();
  EXPECT_TRUE(response.is_ok());
}

TEST_F(UnknownInteractions, TwoWayFlexibleSyncSend) {
  auto client = SyncClient();
  auto server = TakeServerChannel();

  auto response_fut = std::async([&client]() { return client->FlexibleTwoWay(); });

  auto received = TwoWayServerRequest<16>::ReadFromChannel(server);
  EXPECT_EQ(ZX_OK, received.status);
  auto expected = ExcludeTxid(MakeMessage<test::UnknownInteractionsProtocol::FlexibleTwoWay>(
      fidl::MessageDynamicFlags::kFlexibleMethod));
  EXPECT_THAT(received.buf_excluding_txid(), ::testing::ContainerEq(expected));
  EXPECT_NE(0u, received.txid());

  auto server_reply = MakeMessage<test::UnknownInteractionsProtocol::FlexibleTwoWay>(
      fidl::MessageDynamicFlags::kFlexibleMethod, ResultUnionTag::kSuccess, 0);
  received.reply(server, server_reply);

  auto response = response_fut.get();
  EXPECT_TRUE(response.is_ok());
}

TEST_F(UnknownInteractions, TwoWayFlexibleSyncSendUnknownResponse) {
  auto client = SyncClient();
  auto server = TakeServerChannel();

  auto response_fut = std::async([&client]() { return client->FlexibleTwoWay(); });

  auto received = TwoWayServerRequest<16>::ReadFromChannel(server);
  EXPECT_EQ(ZX_OK, received.status);
  auto expected = ExcludeTxid(MakeMessage<test::UnknownInteractionsProtocol::FlexibleTwoWay>(
      fidl::MessageDynamicFlags::kFlexibleMethod));
  EXPECT_THAT(received.buf_excluding_txid(), ::testing::ContainerEq(expected));
  EXPECT_NE(0u, received.txid());

  auto server_reply = MakeMessage<test::UnknownInteractionsProtocol::FlexibleTwoWay>(
      fidl::MessageDynamicFlags::kFlexibleMethod, ResultUnionTag::kTransportError,
      ZX_ERR_NOT_SUPPORTED);
  received.reply(server, server_reply);

  auto response = response_fut.get();
  ASSERT_TRUE(response.is_error());
  EXPECT_EQ(ZX_ERR_NOT_SUPPORTED, response.error_value().status());
  EXPECT_EQ(fidl::Reason::kUnknownMethod, response.error_value().reason());
}

TEST_F(UnknownInteractions, TwoWayFlexibleSyncSendOtherTransportError) {
  auto client = SyncClient();
  auto server = TakeServerChannel();

  auto response_fut = std::async([&client]() { return client->FlexibleTwoWay(); });

  auto received = TwoWayServerRequest<16>::ReadFromChannel(server);
  EXPECT_EQ(ZX_OK, received.status);
  auto expected = ExcludeTxid(MakeMessage<test::UnknownInteractionsProtocol::FlexibleTwoWay>(
      fidl::MessageDynamicFlags::kFlexibleMethod));
  EXPECT_THAT(received.buf_excluding_txid(), ::testing::ContainerEq(expected));
  EXPECT_NE(0u, received.txid());

  auto server_reply = MakeMessage<test::UnknownInteractionsProtocol::FlexibleTwoWay>(
      fidl::MessageDynamicFlags::kFlexibleMethod, ResultUnionTag::kTransportError,
      ZX_ERR_ACCESS_DENIED);
  received.reply(server, server_reply);

  auto response = response_fut.get();
  ASSERT_TRUE(response.is_error());
  EXPECT_EQ(ZX_ERR_INVALID_ARGS, response.error_value().status());
  EXPECT_EQ(fidl::Reason::kDecodeError, response.error_value().reason());
}

TEST_F(UnknownInteractions, TwoWayFlexibleSyncSendOkTransportError) {
  auto client = SyncClient();
  auto server = TakeServerChannel();

  auto response_fut = std::async([&client]() { return client->FlexibleTwoWay(); });

  auto received = TwoWayServerRequest<16>::ReadFromChannel(server);
  EXPECT_EQ(ZX_OK, received.status);
  auto expected = ExcludeTxid(MakeMessage<test::UnknownInteractionsProtocol::FlexibleTwoWay>(
      fidl::MessageDynamicFlags::kFlexibleMethod));
  EXPECT_THAT(received.buf_excluding_txid(), ::testing::ContainerEq(expected));
  EXPECT_NE(0u, received.txid());

  auto server_reply = MakeMessage<test::UnknownInteractionsProtocol::FlexibleTwoWay>(
      fidl::MessageDynamicFlags::kFlexibleMethod, ResultUnionTag::kTransportError, ZX_OK);
  received.reply(server, server_reply);

  auto response = response_fut.get();
  ASSERT_TRUE(response.is_error());
  EXPECT_EQ(ZX_ERR_INVALID_ARGS, response.error_value().status());
  EXPECT_EQ(fidl::Reason::kDecodeError, response.error_value().reason());
}

TEST_F(UnknownInteractions, TwoWayFlexibleSyncSendErrorVariant) {
  auto client = SyncClient();
  auto server = TakeServerChannel();

  auto response_fut = std::async([&client]() { return client->FlexibleTwoWay(); });

  auto received = TwoWayServerRequest<16>::ReadFromChannel(server);
  EXPECT_EQ(ZX_OK, received.status);
  auto expected = ExcludeTxid(MakeMessage<test::UnknownInteractionsProtocol::FlexibleTwoWay>(
      fidl::MessageDynamicFlags::kFlexibleMethod));
  EXPECT_THAT(received.buf_excluding_txid(), ::testing::ContainerEq(expected));
  EXPECT_NE(0u, received.txid());

  auto server_reply = MakeMessage<test::UnknownInteractionsProtocol::FlexibleTwoWay>(
      fidl::MessageDynamicFlags::kFlexibleMethod, ResultUnionTag::kApplicationError, 0x100);
  received.reply(server, server_reply);

  auto response = response_fut.get();
  ASSERT_TRUE(response.is_error());
  EXPECT_EQ(fidl::Reason::kDecodeError, response.error_value().reason());
}

TEST_F(UnknownInteractions, TwoWayFlexibleFieldsSyncSend) {
  auto client = SyncClient();
  auto server = TakeServerChannel();

  auto response_fut = std::async([&client]() { return client->FlexibleTwoWayFields(); });

  auto received = TwoWayServerRequest<16>::ReadFromChannel(server);
  EXPECT_EQ(ZX_OK, received.status);
  auto expected = ExcludeTxid(MakeMessage<test::UnknownInteractionsProtocol::FlexibleTwoWayFields>(
      fidl::MessageDynamicFlags::kFlexibleMethod));
  EXPECT_THAT(received.buf_excluding_txid(), ::testing::ContainerEq(expected));
  EXPECT_NE(0u, received.txid());

  auto server_reply = MakeMessage<test::UnknownInteractionsProtocol::FlexibleTwoWayFields>(
      fidl::MessageDynamicFlags::kFlexibleMethod, ResultUnionTag::kSuccess, 32);
  received.reply(server, server_reply);

  auto response = response_fut.get();
  ASSERT_TRUE(response.is_ok());
  EXPECT_EQ(32, response.value().some_field());
}

TEST_F(UnknownInteractions, TwoWayFlexibleFieldsSyncSendUnknownResponse) {
  auto client = SyncClient();
  auto server = TakeServerChannel();

  auto response_fut = std::async([&client]() { return client->FlexibleTwoWayFields(); });

  auto received = TwoWayServerRequest<16>::ReadFromChannel(server);
  EXPECT_EQ(ZX_OK, received.status);
  auto expected = ExcludeTxid(MakeMessage<test::UnknownInteractionsProtocol::FlexibleTwoWayFields>(
      fidl::MessageDynamicFlags::kFlexibleMethod));
  EXPECT_THAT(received.buf_excluding_txid(), ::testing::ContainerEq(expected));
  EXPECT_NE(0u, received.txid());

  auto server_reply = MakeMessage<test::UnknownInteractionsProtocol::FlexibleTwoWayFields>(
      fidl::MessageDynamicFlags::kFlexibleMethod, ResultUnionTag::kTransportError,
      ZX_ERR_NOT_SUPPORTED);
  received.reply(server, server_reply);

  auto response = response_fut.get();
  ASSERT_TRUE(response.is_error());
  EXPECT_EQ(ZX_ERR_NOT_SUPPORTED, response.error_value().status());
  EXPECT_EQ(fidl::Reason::kUnknownMethod, response.error_value().reason());
}

TEST_F(UnknownInteractions, TwoWayFlexibleErrSyncSend) {
  auto client = SyncClient();
  auto server = TakeServerChannel();

  auto response_fut = std::async([&client]() { return client->FlexibleTwoWayErr(); });

  auto received = TwoWayServerRequest<16>::ReadFromChannel(server);
  EXPECT_EQ(ZX_OK, received.status);
  auto expected = ExcludeTxid(MakeMessage<test::UnknownInteractionsProtocol::FlexibleTwoWayErr>(
      fidl::MessageDynamicFlags::kFlexibleMethod));
  EXPECT_THAT(received.buf_excluding_txid(), ::testing::ContainerEq(expected));
  EXPECT_NE(0u, received.txid());

  auto server_reply = MakeMessage<test::UnknownInteractionsProtocol::FlexibleTwoWayErr>(
      fidl::MessageDynamicFlags::kFlexibleMethod, ResultUnionTag::kSuccess, 0);
  received.reply(server, server_reply);

  auto response = response_fut.get();
  ASSERT_TRUE(response.is_ok());
}

TEST_F(UnknownInteractions, TwoWayFlexibleErrSyncSendUnknownResponse) {
  auto client = SyncClient();
  auto server = TakeServerChannel();

  auto response_fut = std::async([&client]() { return client->FlexibleTwoWayErr(); });

  auto received = TwoWayServerRequest<16>::ReadFromChannel(server);
  EXPECT_EQ(ZX_OK, received.status);
  auto expected = ExcludeTxid(MakeMessage<test::UnknownInteractionsProtocol::FlexibleTwoWayErr>(
      fidl::MessageDynamicFlags::kFlexibleMethod));
  EXPECT_THAT(received.buf_excluding_txid(), ::testing::ContainerEq(expected));
  EXPECT_NE(0u, received.txid());

  auto server_reply = MakeMessage<test::UnknownInteractionsProtocol::FlexibleTwoWayErr>(
      fidl::MessageDynamicFlags::kFlexibleMethod, ResultUnionTag::kTransportError,
      ZX_ERR_NOT_SUPPORTED);
  received.reply(server, server_reply);

  auto response = response_fut.get();
  ASSERT_TRUE(response.is_error());
  ASSERT_TRUE(response.error_value().is_framework_error());
  EXPECT_EQ(ZX_ERR_NOT_SUPPORTED, response.error_value().framework_error().status());
  EXPECT_EQ(fidl::Reason::kUnknownMethod, response.error_value().framework_error().reason());
}

TEST_F(UnknownInteractions, TwoWayFlexibleErrSyncSendOtherTransportError) {
  auto client = SyncClient();
  auto server = TakeServerChannel();

  auto response_fut = std::async([&client]() { return client->FlexibleTwoWayErr(); });

  auto received = TwoWayServerRequest<16>::ReadFromChannel(server);
  EXPECT_EQ(ZX_OK, received.status);
  auto expected = ExcludeTxid(MakeMessage<test::UnknownInteractionsProtocol::FlexibleTwoWayErr>(
      fidl::MessageDynamicFlags::kFlexibleMethod));
  EXPECT_THAT(received.buf_excluding_txid(), ::testing::ContainerEq(expected));
  EXPECT_NE(0u, received.txid());

  auto server_reply = MakeMessage<test::UnknownInteractionsProtocol::FlexibleTwoWayErr>(
      fidl::MessageDynamicFlags::kFlexibleMethod, ResultUnionTag::kTransportError,
      ZX_ERR_ACCESS_DENIED);
  received.reply(server, server_reply);

  auto response = response_fut.get();
  ASSERT_TRUE(response.is_error());
  ASSERT_TRUE(response.error_value().is_framework_error());
  EXPECT_EQ(ZX_ERR_INVALID_ARGS, response.error_value().framework_error().status());
  EXPECT_EQ(fidl::Reason::kDecodeError, response.error_value().framework_error().reason());
}

TEST_F(UnknownInteractions, TwoWayFlexibleErrSyncSendErrorVariant) {
  auto client = SyncClient();
  auto server = TakeServerChannel();

  auto response_fut = std::async([&client]() { return client->FlexibleTwoWayErr(); });

  auto received = TwoWayServerRequest<16>::ReadFromChannel(server);
  EXPECT_EQ(ZX_OK, received.status);
  auto expected = ExcludeTxid(MakeMessage<test::UnknownInteractionsProtocol::FlexibleTwoWayErr>(
      fidl::MessageDynamicFlags::kFlexibleMethod));
  EXPECT_THAT(received.buf_excluding_txid(), ::testing::ContainerEq(expected));
  EXPECT_NE(0u, received.txid());

  auto server_reply = MakeMessage<test::UnknownInteractionsProtocol::FlexibleTwoWayErr>(
      fidl::MessageDynamicFlags::kFlexibleMethod, ResultUnionTag::kApplicationError, 0x100);
  received.reply(server, server_reply);

  auto response = response_fut.get();
  ASSERT_TRUE(response.is_error());
  ASSERT_TRUE(response.error_value().is_domain_error());
  EXPECT_EQ(0x100, response.error_value().domain_error());
}

TEST_F(UnknownInteractions, TwoWayFlexibleFieldsErrSyncSend) {
  auto client = SyncClient();
  auto server = TakeServerChannel();

  auto response_fut = std::async([&client]() { return client->FlexibleTwoWayFieldsErr(); });

  auto received = TwoWayServerRequest<16>::ReadFromChannel(server);
  EXPECT_EQ(ZX_OK, received.status);
  auto expected =
      ExcludeTxid(MakeMessage<test::UnknownInteractionsProtocol::FlexibleTwoWayFieldsErr>(
          fidl::MessageDynamicFlags::kFlexibleMethod));
  EXPECT_THAT(received.buf_excluding_txid(), ::testing::ContainerEq(expected));
  EXPECT_NE(0u, received.txid());

  auto server_reply = MakeMessage<test::UnknownInteractionsProtocol::FlexibleTwoWayFieldsErr>(
      fidl::MessageDynamicFlags::kFlexibleMethod, ResultUnionTag::kSuccess, 32);
  received.reply(server, server_reply);

  auto response = response_fut.get();
  ASSERT_TRUE(response.is_ok());
  EXPECT_EQ(32, response.value().some_field());
}

TEST_F(UnknownInteractions, TwoWayFlexibleFieldsErrSyncSendUnknownResponse) {
  auto client = SyncClient();
  auto server = TakeServerChannel();

  auto response_fut = std::async([&client]() { return client->FlexibleTwoWayFieldsErr(); });

  auto received = TwoWayServerRequest<16>::ReadFromChannel(server);
  EXPECT_EQ(ZX_OK, received.status);
  auto expected =
      ExcludeTxid(MakeMessage<test::UnknownInteractionsProtocol::FlexibleTwoWayFieldsErr>(
          fidl::MessageDynamicFlags::kFlexibleMethod));
  EXPECT_THAT(received.buf_excluding_txid(), ::testing::ContainerEq(expected));
  EXPECT_NE(0u, received.txid());

  auto server_reply = MakeMessage<test::UnknownInteractionsProtocol::FlexibleTwoWayFieldsErr>(
      fidl::MessageDynamicFlags::kFlexibleMethod, ResultUnionTag::kTransportError,
      ZX_ERR_NOT_SUPPORTED);
  received.reply(server, server_reply);

  auto response = response_fut.get();
  ASSERT_TRUE(response.is_error());
  ASSERT_TRUE(response.error_value().is_framework_error());
  EXPECT_EQ(ZX_ERR_NOT_SUPPORTED, response.error_value().framework_error().status());
  EXPECT_EQ(fidl::Reason::kUnknownMethod, response.error_value().framework_error().reason());
}

TEST_F(UnknownInteractions, TwoWayFlexibleFieldsErrSyncSendErrorVariant) {
  auto client = SyncClient();
  auto server = TakeServerChannel();

  auto response_fut = std::async([&client]() { return client->FlexibleTwoWayFieldsErr(); });

  auto received = TwoWayServerRequest<16>::ReadFromChannel(server);
  EXPECT_EQ(ZX_OK, received.status);
  auto expected =
      ExcludeTxid(MakeMessage<test::UnknownInteractionsProtocol::FlexibleTwoWayFieldsErr>(
          fidl::MessageDynamicFlags::kFlexibleMethod));
  EXPECT_THAT(received.buf_excluding_txid(), ::testing::ContainerEq(expected));
  EXPECT_NE(0u, received.txid());

  auto server_reply = MakeMessage<test::UnknownInteractionsProtocol::FlexibleTwoWayFieldsErr>(
      fidl::MessageDynamicFlags::kFlexibleMethod, ResultUnionTag::kApplicationError, 0x100);
  received.reply(server, server_reply);

  auto response = response_fut.get();
  ASSERT_TRUE(response.is_error());
  ASSERT_TRUE(response.error_value().is_domain_error());
  EXPECT_EQ(0x100, response.error_value().domain_error());
}

///////////////////////////////////////////////////////////////////////////////
//// Events - Sync Client
///////////////////////////////////////////////////////////////////////////////

TEST_F(UnknownInteractions, ReceiveStrictEventSync) {
  class EventHandler : public UnknownInteractionsEventHandlerBase {
    void StrictEvent(
        ::fidl::Event<::test_unknown_interactions::UnknownInteractionsProtocol::StrictEvent>&)
        override {
      received_event = true;
    }

   public:
    bool received_event = false;
  };
  EventHandler handler;
  auto client = SyncClient();
  auto server = TakeServerChannel();

  auto server_message = MakeMessage<::test::UnknownInteractionsProtocol::StrictEvent>(
      ::fidl::MessageDynamicFlags::kStrictMethod);
  ASSERT_EQ(ZX_OK, server.write(0, &server_message, server_message.size(), nullptr, 0));

  EXPECT_TRUE(client.HandleOneEvent(handler).ok());

  EXPECT_TRUE(handler.received_event);
}

TEST_F(UnknownInteractions, ReceiveStrictEventSyncMismatchedStrictness) {
  class EventHandler : public UnknownInteractionsEventHandlerBase {
    void StrictEvent(
        ::fidl::Event<::test_unknown_interactions::UnknownInteractionsProtocol::StrictEvent>&)
        override {
      received_event = true;
    }

   public:
    bool received_event = false;
  };
  EventHandler handler;
  auto client = SyncClient();
  auto server = TakeServerChannel();

  auto server_message = MakeMessage<::test::UnknownInteractionsProtocol::StrictEvent>(
      ::fidl::MessageDynamicFlags::kFlexibleMethod);
  ASSERT_EQ(ZX_OK, server.write(0, &server_message, server_message.size(), nullptr, 0));

  EXPECT_TRUE(client.HandleOneEvent(handler).ok());

  EXPECT_TRUE(handler.received_event);
}

TEST_F(UnknownInteractions, ReceiveFlexibleEventSync) {
  class EventHandler : public UnknownInteractionsEventHandlerBase {
    void FlexibleEvent(
        ::fidl::Event<::test_unknown_interactions::UnknownInteractionsProtocol::FlexibleEvent>&)
        override {
      received_event = true;
    }

   public:
    bool received_event = false;
  };
  EventHandler handler;
  auto client = SyncClient();
  auto server = TakeServerChannel();

  auto server_message = MakeMessage<::test::UnknownInteractionsProtocol::FlexibleEvent>(
      ::fidl::MessageDynamicFlags::kFlexibleMethod);
  ASSERT_EQ(ZX_OK, server.write(0, &server_message, server_message.size(), nullptr, 0));

  EXPECT_TRUE(client.HandleOneEvent(handler).ok());

  EXPECT_TRUE(handler.received_event);
}

TEST_F(UnknownInteractions, ReceiveFlexibleEventSyncMismatchedStrictness) {
  class EventHandler : public UnknownInteractionsEventHandlerBase {
    void FlexibleEvent(
        ::fidl::Event<::test_unknown_interactions::UnknownInteractionsProtocol::FlexibleEvent>&)
        override {
      received_event = true;
    }

   public:
    bool received_event = false;
  };
  EventHandler handler;
  auto client = SyncClient();
  auto server = TakeServerChannel();

  auto server_message = MakeMessage<::test::UnknownInteractionsProtocol::FlexibleEvent>(
      ::fidl::MessageDynamicFlags::kStrictMethod);
  ASSERT_EQ(ZX_OK, server.write(0, &server_message, server_message.size(), nullptr, 0));

  EXPECT_TRUE(client.HandleOneEvent(handler).ok());

  EXPECT_TRUE(handler.received_event);
}

///////////////////////////////////////////////////////////////////////////////
//// Unknown messages - Sync Client
///////////////////////////////////////////////////////////////////////////////

TEST_F(UnknownInteractions, UnknownServerSentTwoWaySyncClient) {
  class EventHandler : public UnknownInteractionsEventHandlerBase {};
  EventHandler handler;
  auto client = SyncClient();
  auto server = TakeServerChannel();

  auto server_message =
      MakeMessage<FakeUnknownMethod>(0xABCD, ::fidl::MessageDynamicFlags::kFlexibleMethod);
  ASSERT_EQ(ZX_OK, server.write(0, &server_message, server_message.size(), nullptr, 0));

  auto status = client.HandleOneEvent(handler);
  EXPECT_EQ(ZX_ERR_NOT_SUPPORTED, status.status());
  EXPECT_EQ(fidl::Reason::kUnexpectedMessage, status.reason());
}

TEST_F(UnknownInteractions, UnknownStrictEventSync) {
  class EventHandler : public UnknownInteractionsEventHandlerBase {};
  EventHandler handler;
  auto client = SyncClient();
  auto server = TakeServerChannel();

  auto server_message = MakeMessage<FakeUnknownMethod>(::fidl::MessageDynamicFlags::kStrictMethod);
  ASSERT_EQ(ZX_OK, server.write(0, &server_message, server_message.size(), nullptr, 0));

  auto status = client.HandleOneEvent(handler);
  EXPECT_EQ(ZX_ERR_NOT_SUPPORTED, status.status());
  EXPECT_EQ(fidl::Reason::kUnexpectedMessage, status.reason());
}

TEST_F(UnknownInteractions, UnknownFlexibleEventSync) {
  class EventHandler : public UnknownInteractionsEventHandlerBase {
    void handle_unknown_event(
        ::fidl::UnknownEventMetadata<::test::UnknownInteractionsProtocol> metadata) override {
      received_event = true;
      ASSERT_EQ(FakeUnknownMethod::kOrdinal, metadata.method_ordinal);
    }

   public:
    bool received_event = false;
  };
  EventHandler handler;
  auto client = SyncClient();
  auto server = TakeServerChannel();

  auto server_message =
      MakeMessage<FakeUnknownMethod>(::fidl::MessageDynamicFlags::kFlexibleMethod);
  ASSERT_EQ(ZX_OK, server.write(0, &server_message, server_message.size(), nullptr, 0));

  EXPECT_TRUE(client.HandleOneEvent(handler).ok());

  ASSERT_TRUE(handler.received_event);

  // Write again to check that the channel is still open.
  ASSERT_EQ(ZX_OK, server.write(0, &server_message, server_message.size(), nullptr, 0));
}

TEST_F(UnknownInteractions, UnknownStrictEventSyncAjarProtocol) {
  class EventHandler : public ::fidl::SyncEventHandler<::test::UnknownInteractionsAjarProtocol> {
    void handle_unknown_event(
        ::fidl::UnknownEventMetadata<::test::UnknownInteractionsAjarProtocol> metadata) override {
      ADD_FAILURE() << "Unexpected flexible unknown event";
    }
  };

  EventHandler handler;
  auto client = fidl::SyncClient<::test::UnknownInteractionsAjarProtocol>(
      ::fidl::ClientEnd<::test::UnknownInteractionsAjarProtocol>(TakeClientChannel()));
  auto server = TakeServerChannel();

  auto server_message = MakeMessage<FakeUnknownMethod>(::fidl::MessageDynamicFlags::kStrictMethod);
  ASSERT_EQ(ZX_OK, server.write(0, &server_message, server_message.size(), nullptr, 0));

  auto status = client.HandleOneEvent(handler);
  EXPECT_EQ(ZX_ERR_NOT_SUPPORTED, status.status());
  EXPECT_EQ(fidl::Reason::kUnexpectedMessage, status.reason());
}

TEST_F(UnknownInteractions, UnknownFlexibleEventSyncAjarProtocol) {
  class EventHandler : public ::fidl::SyncEventHandler<::test::UnknownInteractionsAjarProtocol> {
    void handle_unknown_event(
        ::fidl::UnknownEventMetadata<::test::UnknownInteractionsAjarProtocol> metadata) override {
      received_event = true;
      ASSERT_EQ(FakeUnknownMethod::kOrdinal, metadata.method_ordinal);
    }

   public:
    bool received_event = false;
  };
  EventHandler handler;
  auto client = fidl::SyncClient<::test::UnknownInteractionsAjarProtocol>(
      ::fidl::ClientEnd<::test::UnknownInteractionsAjarProtocol>(TakeClientChannel()));
  auto server = TakeServerChannel();

  auto server_message =
      MakeMessage<FakeUnknownMethod>(::fidl::MessageDynamicFlags::kFlexibleMethod);
  ASSERT_EQ(ZX_OK, server.write(0, &server_message, server_message.size(), nullptr, 0));

  EXPECT_TRUE(client.HandleOneEvent(handler).ok());

  ASSERT_TRUE(handler.received_event);

  // Write again to check that the channel is still open.
  ASSERT_EQ(ZX_OK, server.write(0, &server_message, server_message.size(), nullptr, 0));
}

TEST_F(UnknownInteractions, UnknownStrictEventSyncClosedProtocol) {
  class EventHandler : public ::fidl::SyncEventHandler<::test::UnknownInteractionsClosedProtocol> {
  };
  EventHandler handler;
  auto client = fidl::SyncClient<::test::UnknownInteractionsClosedProtocol>(
      ::fidl::ClientEnd<::test::UnknownInteractionsClosedProtocol>(TakeClientChannel()));
  auto server = TakeServerChannel();

  auto server_message = MakeMessage<FakeUnknownMethod>(::fidl::MessageDynamicFlags::kStrictMethod);
  ASSERT_EQ(ZX_OK, server.write(0, &server_message, server_message.size(), nullptr, 0));

  auto status = client.HandleOneEvent(handler);
  EXPECT_EQ(ZX_ERR_NOT_SUPPORTED, status.status());
  EXPECT_EQ(fidl::Reason::kUnexpectedMessage, status.reason());
}

TEST_F(UnknownInteractions, UnknownFlexibleEventSyncClosedProtocol) {
  class EventHandler : public ::fidl::SyncEventHandler<::test::UnknownInteractionsClosedProtocol> {
  };
  EventHandler handler;
  auto client = fidl::SyncClient<::test::UnknownInteractionsClosedProtocol>(
      ::fidl::ClientEnd<::test::UnknownInteractionsClosedProtocol>(TakeClientChannel()));
  auto server = TakeServerChannel();

  auto server_message =
      MakeMessage<FakeUnknownMethod>(::fidl::MessageDynamicFlags::kFlexibleMethod);
  ASSERT_EQ(ZX_OK, server.write(0, &server_message, server_message.size(), nullptr, 0));

  auto status = client.HandleOneEvent(handler);
  EXPECT_EQ(ZX_ERR_NOT_SUPPORTED, status.status());
  EXPECT_EQ(fidl::Reason::kUnexpectedMessage, status.reason());
}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
//// Server Side Tests
///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////////
//// Events - Server
///////////////////////////////////////////////////////////////////////////////

TEST_F(UnknownInteractions, SendStrictEvent) {
  auto client = TakeClientChannel();
  auto server = TakeServerEnd();

  EXPECT_TRUE(fidl::SendEvent(server)->StrictEvent().is_ok());

  auto received = ReadResult<16>::ReadFromChannel(client);
  EXPECT_EQ(ZX_OK, received.status);

  auto expected = MakeMessage<test::UnknownInteractionsProtocol::StrictEvent>(
      fidl::MessageDynamicFlags::kStrictMethod);
  EXPECT_THAT(received.buf, ::testing::ContainerEq(expected));
}

TEST_F(UnknownInteractions, SendFlexibleEvent) {
  auto client = TakeClientChannel();
  auto server = TakeServerEnd();

  EXPECT_TRUE(fidl::SendEvent(server)->FlexibleEvent().is_ok());

  auto received = ReadResult<16>::ReadFromChannel(client);
  EXPECT_EQ(ZX_OK, received.status);

  auto expected = MakeMessage<test::UnknownInteractionsProtocol::FlexibleEvent>(
      fidl::MessageDynamicFlags::kFlexibleMethod);
  EXPECT_THAT(received.buf, ::testing::ContainerEq(expected));
}

///////////////////////////////////////////////////////////////////////////////
//// Two-Way Methods - Server
///////////////////////////////////////////////////////////////////////////////

TEST_F(UnknownInteractions, StrictTwoWayResponse) {
  auto client = TakeClientChannel();
  class Server : public UnknownInteractionsServerBase {
    void StrictTwoWay(StrictTwoWayRequest& request,
                      StrictTwoWayCompleter::Sync& completer) override {
      completer.Reply();
    }
  };
  Server server;
  auto server_binding = BindServer(&server);

  auto client_request = MakeMessage<::test::UnknownInteractionsProtocol::StrictTwoWay>(
      0xABCD, ::fidl::MessageDynamicFlags::kStrictMethod);
  ASSERT_EQ(ZX_OK, client.write(0, &client_request, client_request.size(), nullptr, 0));

  loop().RunUntilIdle();

  auto received = ReadResult<16>::ReadFromChannel(client);
  EXPECT_EQ(ZX_OK, received.status);
  auto expected = MakeMessage<::test::UnknownInteractionsProtocol::StrictTwoWay>(
      0xABCD, ::fidl::MessageDynamicFlags::kStrictMethod);
  EXPECT_THAT(received.buf, ::testing::ContainerEq(expected));
}

TEST_F(UnknownInteractions, StrictTwoWayResponseMismatchedStrictness) {
  auto client = TakeClientChannel();
  class Server : public UnknownInteractionsServerBase {
    void StrictTwoWay(StrictTwoWayRequest& request,
                      StrictTwoWayCompleter::Sync& completer) override {
      completer.Reply();
    }
  };
  Server server;
  auto server_binding = BindServer(&server);

  // Server is not supposed to validate the flexible flag for known methods.
  auto client_request = MakeMessage<::test::UnknownInteractionsProtocol::StrictTwoWay>(
      0xABCD, ::fidl::MessageDynamicFlags::kFlexibleMethod);
  ASSERT_EQ(ZX_OK, client.write(0, &client_request, client_request.size(), nullptr, 0));

  loop().RunUntilIdle();

  auto received = ReadResult<16>::ReadFromChannel(client);
  EXPECT_EQ(ZX_OK, received.status);
  auto expected = MakeMessage<::test::UnknownInteractionsProtocol::StrictTwoWay>(
      0xABCD, ::fidl::MessageDynamicFlags::kStrictMethod);
  EXPECT_THAT(received.buf, ::testing::ContainerEq(expected));
}

TEST_F(UnknownInteractions, StrictTwoWayErrResponse) {
  auto client = TakeClientChannel();
  class Server : public UnknownInteractionsServerBase {
    void StrictTwoWayErr(StrictTwoWayErrRequest& request,
                         StrictTwoWayErrCompleter::Sync& completer) override {
      completer.Reply(fit::ok());
    }
  };
  Server server;
  auto server_binding = BindServer(&server);

  auto client_request = MakeMessage<::test::UnknownInteractionsProtocol::StrictTwoWayErr>(
      0xABCD, ::fidl::MessageDynamicFlags::kStrictMethod);
  ASSERT_EQ(ZX_OK, client.write(0, &client_request, client_request.size(), nullptr, 0));

  loop().RunUntilIdle();

  auto received = ReadResult<32>::ReadFromChannel(client);
  EXPECT_EQ(ZX_OK, received.status);
  auto expected = MakeMessage<::test::UnknownInteractionsProtocol::StrictTwoWayErr>(
      0xABCD, ::fidl::MessageDynamicFlags::kStrictMethod, ResultUnionTag::kSuccess, 0);
  EXPECT_THAT(received.buf, ::testing::ContainerEq(expected));
}

TEST_F(UnknownInteractions, FlexibleTwoWayResponse) {
  auto client = TakeClientChannel();
  class Server : public UnknownInteractionsServerBase {
    void FlexibleTwoWay(FlexibleTwoWayRequest& request,
                        FlexibleTwoWayCompleter::Sync& completer) override {
      completer.Reply();
    }
  };
  Server server;
  auto server_binding = BindServer(&server);

  auto client_request = MakeMessage<::test::UnknownInteractionsProtocol::FlexibleTwoWay>(
      0xABCD, ::fidl::MessageDynamicFlags::kFlexibleMethod);
  ASSERT_EQ(ZX_OK, client.write(0, &client_request, client_request.size(), nullptr, 0));

  loop().RunUntilIdle();

  auto received = ReadResult<32>::ReadFromChannel(client);
  EXPECT_EQ(ZX_OK, received.status);
  auto expected = MakeMessage<::test::UnknownInteractionsProtocol::FlexibleTwoWay>(
      0xABCD, ::fidl::MessageDynamicFlags::kFlexibleMethod, ResultUnionTag::kSuccess, 0);
  EXPECT_THAT(received.buf, ::testing::ContainerEq(expected));
}

TEST_F(UnknownInteractions, FlexibleTwoWayResponseMismatchedStrictness) {
  auto client = TakeClientChannel();
  class Server : public UnknownInteractionsServerBase {
    void FlexibleTwoWay(FlexibleTwoWayRequest& request,
                        FlexibleTwoWayCompleter::Sync& completer) override {
      completer.Reply();
    }
  };
  Server server;
  auto server_binding = BindServer(&server);

  // Server is not supposed to validate the flexible flag for known methods.
  auto client_request = MakeMessage<::test::UnknownInteractionsProtocol::FlexibleTwoWay>(
      0xABCD, ::fidl::MessageDynamicFlags::kStrictMethod);
  ASSERT_EQ(ZX_OK, client.write(0, &client_request, client_request.size(), nullptr, 0));

  loop().RunUntilIdle();

  auto received = ReadResult<32>::ReadFromChannel(client);
  EXPECT_EQ(ZX_OK, received.status);
  auto expected = MakeMessage<::test::UnknownInteractionsProtocol::FlexibleTwoWay>(
      0xABCD, ::fidl::MessageDynamicFlags::kFlexibleMethod, ResultUnionTag::kSuccess, 0);
  EXPECT_THAT(received.buf, ::testing::ContainerEq(expected));
}

TEST_F(UnknownInteractions, FlexibleTwoWayFieldsResponse) {
  auto client = TakeClientChannel();
  class Server : public UnknownInteractionsServerBase {
    void FlexibleTwoWayFields(FlexibleTwoWayFieldsRequest& request,
                              FlexibleTwoWayFieldsCompleter::Sync& completer) override {
      completer.Reply({{.some_field = 42}});
    }
  };
  Server server;
  auto server_binding = BindServer(&server);

  auto client_request = MakeMessage<::test::UnknownInteractionsProtocol::FlexibleTwoWayFields>(
      0xABCD, ::fidl::MessageDynamicFlags::kFlexibleMethod);
  ASSERT_EQ(ZX_OK, client.write(0, &client_request, client_request.size(), nullptr, 0));

  loop().RunUntilIdle();

  auto received = ReadResult<32>::ReadFromChannel(client);
  EXPECT_EQ(ZX_OK, received.status);
  auto expected = MakeMessage<::test::UnknownInteractionsProtocol::FlexibleTwoWayFields>(
      0xABCD, ::fidl::MessageDynamicFlags::kFlexibleMethod, ResultUnionTag::kSuccess, 42);
  EXPECT_THAT(received.buf, ::testing::ContainerEq(expected));
}

TEST_F(UnknownInteractions, FlexibleTwoWayErrResponse) {
  auto client = TakeClientChannel();
  class Server : public UnknownInteractionsServerBase {
    void FlexibleTwoWayErr(FlexibleTwoWayErrRequest& request,
                           FlexibleTwoWayErrCompleter::Sync& completer) override {
      completer.Reply(fit::ok());
    }
  };
  Server server;
  auto server_binding = BindServer(&server);

  auto client_request = MakeMessage<::test::UnknownInteractionsProtocol::FlexibleTwoWayErr>(
      0xABCD, ::fidl::MessageDynamicFlags::kFlexibleMethod);
  ASSERT_EQ(ZX_OK, client.write(0, &client_request, client_request.size(), nullptr, 0));

  loop().RunUntilIdle();

  auto received = ReadResult<32>::ReadFromChannel(client);
  EXPECT_EQ(ZX_OK, received.status);
  auto expected = MakeMessage<::test::UnknownInteractionsProtocol::FlexibleTwoWayErr>(
      0xABCD, ::fidl::MessageDynamicFlags::kFlexibleMethod, ResultUnionTag::kSuccess, 0);
  EXPECT_THAT(received.buf, ::testing::ContainerEq(expected));
}

TEST_F(UnknownInteractions, FlexibleTwoWayErrResponseError) {
  auto client = TakeClientChannel();
  class Server : public UnknownInteractionsServerBase {
    void FlexibleTwoWayErr(FlexibleTwoWayErrRequest& request,
                           FlexibleTwoWayErrCompleter::Sync& completer) override {
      completer.Reply(fit::error(3203));
    }
  };
  Server server;
  auto server_binding = BindServer(&server);

  auto client_request = MakeMessage<::test::UnknownInteractionsProtocol::FlexibleTwoWayErr>(
      0xABCD, ::fidl::MessageDynamicFlags::kFlexibleMethod);
  ASSERT_EQ(ZX_OK, client.write(0, &client_request, client_request.size(), nullptr, 0));

  loop().RunUntilIdle();

  auto received = ReadResult<32>::ReadFromChannel(client);
  EXPECT_EQ(ZX_OK, received.status);
  auto expected = MakeMessage<::test::UnknownInteractionsProtocol::FlexibleTwoWayErr>(
      0xABCD, ::fidl::MessageDynamicFlags::kFlexibleMethod, ResultUnionTag::kApplicationError,
      3203);
  EXPECT_THAT(received.buf, ::testing::ContainerEq(expected));
}

TEST_F(UnknownInteractions, FlexibleTwoWayFieldsErrResponse) {
  auto client = TakeClientChannel();
  class Server : public UnknownInteractionsServerBase {
    void FlexibleTwoWayFieldsErr(FlexibleTwoWayFieldsErrRequest& request,
                                 FlexibleTwoWayFieldsErrCompleter::Sync& completer) override {
      completer.Reply(::fit::ok(
          ::test::UnknownInteractionsProtocolFlexibleTwoWayFieldsErrResponse({.some_field = 42})));
    }
  };
  Server server;
  auto server_binding = BindServer(&server);

  auto client_request = MakeMessage<::test::UnknownInteractionsProtocol::FlexibleTwoWayFieldsErr>(
      0xABCD, ::fidl::MessageDynamicFlags::kFlexibleMethod);
  ASSERT_EQ(ZX_OK, client.write(0, &client_request, client_request.size(), nullptr, 0));

  loop().RunUntilIdle();

  auto received = ReadResult<32>::ReadFromChannel(client);
  EXPECT_EQ(ZX_OK, received.status);
  auto expected = MakeMessage<::test::UnknownInteractionsProtocol::FlexibleTwoWayFieldsErr>(
      0xABCD, ::fidl::MessageDynamicFlags::kFlexibleMethod, ResultUnionTag::kSuccess, 42);
  EXPECT_THAT(received.buf, ::testing::ContainerEq(expected));
}

TEST_F(UnknownInteractions, FlexibleTwoWayFieldsErrResponseError) {
  auto client = TakeClientChannel();
  class Server : public UnknownInteractionsServerBase {
    void FlexibleTwoWayFieldsErr(FlexibleTwoWayFieldsErrRequest& request,
                                 FlexibleTwoWayFieldsErrCompleter::Sync& completer) override {
      completer.Reply(fit::error(3203));
    }
  };
  Server server;
  auto server_binding = BindServer(&server);

  auto client_request = MakeMessage<::test::UnknownInteractionsProtocol::FlexibleTwoWayFieldsErr>(
      0xABCD, ::fidl::MessageDynamicFlags::kFlexibleMethod);
  ASSERT_EQ(ZX_OK, client.write(0, &client_request, client_request.size(), nullptr, 0));

  loop().RunUntilIdle();

  auto received = ReadResult<32>::ReadFromChannel(client);
  EXPECT_EQ(ZX_OK, received.status);
  auto expected = MakeMessage<::test::UnknownInteractionsProtocol::FlexibleTwoWayFieldsErr>(
      0xABCD, ::fidl::MessageDynamicFlags::kFlexibleMethod, ResultUnionTag::kApplicationError,
      3203);
  EXPECT_THAT(received.buf, ::testing::ContainerEq(expected));
}

///////////////////////////////////////////////////////////////////////////////
//// Unknown messages - Server
///////////////////////////////////////////////////////////////////////////////

TEST_F(UnknownInteractions, UnknownStrictOneWay) {
  auto client = TakeClientChannel();
  class Server : public UnknownInteractionsServerBase {};
  Server server;
  auto server_binding = BindServer(&server);

  auto client_request =
      MakeMessage<FakeUnknownMethod>(0, ::fidl::MessageDynamicFlags::kStrictMethod);
  ASSERT_EQ(ZX_OK, client.write(0, &client_request, client_request.size(), nullptr, 0));

  loop().RunUntilIdle();

  auto received = ReadResult<16>::ReadFromChannel(client);
  EXPECT_EQ(ZX_ERR_PEER_CLOSED, received.status);
}

TEST_F(UnknownInteractions, UnknownFlexibleOneWay) {
  auto client = TakeClientChannel();
  class Server : public UnknownInteractionsServerBase {
   public:
    bool ran_unknown_interaction_handler = false;

    void handle_unknown_method(
        ::fidl::UnknownMethodMetadata<::test::UnknownInteractionsProtocol> metadata,
        ::fidl::UnknownMethodCompleter::Sync& completer) override {
      ran_unknown_interaction_handler = true;
      EXPECT_EQ(FakeUnknownMethod::kOrdinal, metadata.method_ordinal);
      EXPECT_EQ(::fidl::UnknownMethodType::kOneWay, metadata.unknown_interaction_type);
    }
  };
  Server server;
  auto server_binding = BindServer(&server);

  auto client_request =
      MakeMessage<FakeUnknownMethod>(0, ::fidl::MessageDynamicFlags::kFlexibleMethod);
  ASSERT_EQ(ZX_OK, client.write(0, &client_request, client_request.size(), nullptr, 0));

  loop().RunUntilIdle();
  EXPECT_TRUE(server.ran_unknown_interaction_handler);

  EXPECT_EQ(ZX_OK, client.write(0, &client_request, client_request.size(), nullptr, 0));
}

TEST_F(UnknownInteractions, UnknownStrictTwoWay) {
  auto client = TakeClientChannel();
  class Server : public UnknownInteractionsServerBase {};
  Server server;
  auto server_binding = BindServer(&server);

  auto client_request =
      MakeMessage<FakeUnknownMethod>(0xABCD, ::fidl::MessageDynamicFlags::kStrictMethod);
  ASSERT_EQ(ZX_OK, client.write(0, &client_request, client_request.size(), nullptr, 0));

  loop().RunUntilIdle();

  auto received = ReadResult<16>::ReadFromChannel(client);
  EXPECT_EQ(ZX_ERR_PEER_CLOSED, received.status);
}

TEST_F(UnknownInteractions, UnknownFlexibleTwoWay) {
  auto client = TakeClientChannel();
  class Server : public UnknownInteractionsServerBase {
   public:
    bool ran_unknown_interaction_handler = false;

    void handle_unknown_method(
        ::fidl::UnknownMethodMetadata<::test::UnknownInteractionsProtocol> metadata,
        ::fidl::UnknownMethodCompleter::Sync& completer) override {
      ran_unknown_interaction_handler = true;
      EXPECT_EQ(FakeUnknownMethod::kOrdinal, metadata.method_ordinal);
      EXPECT_EQ(::fidl::UnknownMethodType::kTwoWay, metadata.unknown_interaction_type);
    }
  };
  Server server;
  auto server_binding = BindServer(&server);

  auto client_request =
      MakeMessage<FakeUnknownMethod>(0xABCD, ::fidl::MessageDynamicFlags::kFlexibleMethod);
  ASSERT_EQ(ZX_OK, client.write(0, &client_request, client_request.size(), nullptr, 0));

  loop().RunUntilIdle();

  EXPECT_TRUE(server.ran_unknown_interaction_handler);

  auto received = ReadResult<32>::ReadFromChannel(client);
  EXPECT_EQ(ZX_OK, received.status);
  auto expected =
      MakeMessage<FakeUnknownMethod>(0xABCD, ::fidl::MessageDynamicFlags::kFlexibleMethod,
                                     ResultUnionTag::kTransportError, ZX_ERR_NOT_SUPPORTED);
  EXPECT_THAT(received.buf, ::testing::ContainerEq(expected));
}

TEST_F(UnknownInteractions, UnknownStrictOneWayAjarProtocol) {
  auto client = TakeClientChannel();
  class Server : public ::fidl::Server<::test::UnknownInteractionsAjarProtocol> {
    void handle_unknown_method(
        ::fidl::UnknownMethodMetadata<::test::UnknownInteractionsAjarProtocol> metadata,
        ::fidl::UnknownMethodCompleter::Sync& completer) override {
      ADD_FAILURE() << "Unexpected flexible unknown interaction";
    }
  };
  Server server;
  auto server_binding = BindServer(&server);

  auto client_request =
      MakeMessage<FakeUnknownMethod>(0, ::fidl::MessageDynamicFlags::kStrictMethod);
  ASSERT_EQ(ZX_OK, client.write(0, &client_request, client_request.size(), nullptr, 0));

  loop().RunUntilIdle();

  auto received = ReadResult<16>::ReadFromChannel(client);
  EXPECT_EQ(ZX_ERR_PEER_CLOSED, received.status);
}

TEST_F(UnknownInteractions, UnknownFlexibleOneWayAjarProtocol) {
  auto client = TakeClientChannel();
  class Server : public ::fidl::Server<::test::UnknownInteractionsAjarProtocol> {
   public:
    bool ran_unknown_interaction_handler = false;

    void handle_unknown_method(
        ::fidl::UnknownMethodMetadata<::test::UnknownInteractionsAjarProtocol> metadata,
        ::fidl::UnknownMethodCompleter::Sync& completer) override {
      ran_unknown_interaction_handler = true;
      EXPECT_EQ(FakeUnknownMethod::kOrdinal, metadata.method_ordinal);
    }
  };
  Server server;
  auto server_binding = BindServer(&server);

  auto client_request =
      MakeMessage<FakeUnknownMethod>(0, ::fidl::MessageDynamicFlags::kFlexibleMethod);
  ASSERT_EQ(ZX_OK, client.write(0, &client_request, client_request.size(), nullptr, 0));

  loop().RunUntilIdle();
  EXPECT_TRUE(server.ran_unknown_interaction_handler);

  EXPECT_EQ(ZX_OK, client.write(0, &client_request, client_request.size(), nullptr, 0));
}

TEST_F(UnknownInteractions, UnknownStrictTwoWayAjarProtocol) {
  auto client = TakeClientChannel();
  class Server : public ::fidl::Server<::test::UnknownInteractionsAjarProtocol> {
    void handle_unknown_method(
        ::fidl::UnknownMethodMetadata<::test::UnknownInteractionsAjarProtocol> metadata,
        ::fidl::UnknownMethodCompleter::Sync& completer) override {
      ADD_FAILURE() << "Unexpected flexible unknown interaction";
    }
  };
  Server server;
  auto server_binding = BindServer(&server);

  auto client_request =
      MakeMessage<FakeUnknownMethod>(0xABCD, ::fidl::MessageDynamicFlags::kStrictMethod);
  ASSERT_EQ(ZX_OK, client.write(0, &client_request, client_request.size(), nullptr, 0));

  loop().RunUntilIdle();

  auto received = ReadResult<16>::ReadFromChannel(client);
  EXPECT_EQ(ZX_ERR_PEER_CLOSED, received.status);
}

TEST_F(UnknownInteractions, UnknownFlexibleTwoWayAjarProtocol) {
  auto client = TakeClientChannel();
  class Server : public ::fidl::Server<::test::UnknownInteractionsAjarProtocol> {
    void handle_unknown_method(
        ::fidl::UnknownMethodMetadata<::test::UnknownInteractionsAjarProtocol> metadata,
        ::fidl::UnknownMethodCompleter::Sync& completer) override {
      ADD_FAILURE() << "Unexpected flexible unknown interaction";
    }
  };
  Server server;
  auto server_binding = BindServer(&server);

  auto client_request =
      MakeMessage<FakeUnknownMethod>(0xABCD, ::fidl::MessageDynamicFlags::kFlexibleMethod);
  ASSERT_EQ(ZX_OK, client.write(0, &client_request, client_request.size(), nullptr, 0));

  loop().RunUntilIdle();

  auto received = ReadResult<16>::ReadFromChannel(client);
  EXPECT_EQ(ZX_ERR_PEER_CLOSED, received.status);
}

TEST_F(UnknownInteractions, UnknownStrictOneWayClosedProtocol) {
  auto client = TakeClientChannel();
  class Server : public ::fidl::Server<::test::UnknownInteractionsClosedProtocol> {};
  Server server;
  auto server_binding = BindServer(&server);

  auto client_request =
      MakeMessage<FakeUnknownMethod>(0, ::fidl::MessageDynamicFlags::kStrictMethod);
  ASSERT_EQ(ZX_OK, client.write(0, &client_request, client_request.size(), nullptr, 0));

  loop().RunUntilIdle();

  auto received = ReadResult<16>::ReadFromChannel(client);
  EXPECT_EQ(ZX_ERR_PEER_CLOSED, received.status);
}

TEST_F(UnknownInteractions, UnknownFlexibleOneWayClosedProtocol) {
  auto client = TakeClientChannel();
  class Server : public ::fidl::Server<::test::UnknownInteractionsClosedProtocol> {};
  Server server;
  auto server_binding = BindServer(&server);

  auto client_request =
      MakeMessage<FakeUnknownMethod>(0, ::fidl::MessageDynamicFlags::kFlexibleMethod);
  ASSERT_EQ(ZX_OK, client.write(0, &client_request, client_request.size(), nullptr, 0));

  loop().RunUntilIdle();

  auto received = ReadResult<16>::ReadFromChannel(client);
  EXPECT_EQ(ZX_ERR_PEER_CLOSED, received.status);
}

TEST_F(UnknownInteractions, UnknownStrictTwoWayClosedProtocol) {
  auto client = TakeClientChannel();
  class Server : public ::fidl::Server<::test::UnknownInteractionsClosedProtocol> {};
  Server server;
  auto server_binding = BindServer(&server);

  auto client_request =
      MakeMessage<FakeUnknownMethod>(0xABCD, ::fidl::MessageDynamicFlags::kStrictMethod);
  ASSERT_EQ(ZX_OK, client.write(0, &client_request, client_request.size(), nullptr, 0));

  loop().RunUntilIdle();

  auto received = ReadResult<16>::ReadFromChannel(client);
  EXPECT_EQ(ZX_ERR_PEER_CLOSED, received.status);
}

TEST_F(UnknownInteractions, UnknownFlexibleTwoWayClosedProtocol) {
  auto client = TakeClientChannel();
  class Server : public ::fidl::Server<::test::UnknownInteractionsClosedProtocol> {};
  Server server;
  auto server_binding = BindServer(&server);

  auto client_request =
      MakeMessage<FakeUnknownMethod>(0xABCD, ::fidl::MessageDynamicFlags::kFlexibleMethod);
  ASSERT_EQ(ZX_OK, client.write(0, &client_request, client_request.size(), nullptr, 0));

  loop().RunUntilIdle();

  auto received = ReadResult<16>::ReadFromChannel(client);
  EXPECT_EQ(ZX_ERR_PEER_CLOSED, received.status);
}
}  // namespace
