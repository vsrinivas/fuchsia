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

namespace {
namespace test = ::test_unknown_interactions;

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

  fidl::Client<test::UnknownInteractionsProtocol> AsyncClient() {
    EXPECT_TRUE(client_end_.is_valid());
    return fidl::Client<test::UnknownInteractionsProtocol>(std::move(client_end_),
                                                           loop_->dispatcher());
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
  // Number of bytes read.
  uint32_t num_bytes;
  // Number of handles read.
  uint32_t num_handles;

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
    status = channel.read(/* flags= */ 0, buf.data(), /* handles= */ nullptr, N,
                          /* num_handles= */ 0, &num_bytes, &num_handles);
  }
};

template <uint32_t N>
struct TwoWayServerRequest : public ReadResult<N> {
  // Status from sending a reply.
  zx_status_t reply_status;

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
    reply_status = channel.write(/* flags= */ 0, reply_bytes.data(), static_cast<uint32_t>(M),
                                 /* handles= */ nullptr, /* num_handles= */ 0);
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
  auto client = AsyncClient();
  auto server = TakeServerChannel();
  auto result = client->StrictOneWay();
  EXPECT_TRUE(result.is_ok());

  auto received = ReadResult<16>::ReadFromChannel(server);
  EXPECT_EQ(ZX_OK, received.status);
  EXPECT_EQ(16u, received.num_bytes);
  EXPECT_EQ(0u, received.num_handles);

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
  EXPECT_EQ(16u, received.num_bytes);
  EXPECT_EQ(0u, received.num_handles);

  auto expected = MakeMessage<test::UnknownInteractionsProtocol::FlexibleOneWay>(
      fidl::MessageDynamicFlags::kFlexibleMethod);
  EXPECT_THAT(received.buf, ::testing::ContainerEq(expected));
}

TEST_F(UnknownInteractions, TwoWayStrictAsyncSend) {
  auto client = AsyncClient();
  auto server = TakeServerChannel();

  client->StrictTwoWay().Then([](auto& response) { EXPECT_TRUE(response.is_ok()); });

  auto received = TwoWayServerRequest<16>::ReadFromChannel(server);
  EXPECT_EQ(ZX_OK, received.status);
  EXPECT_EQ(16u, received.num_bytes);
  EXPECT_EQ(0u, received.num_handles);
  auto expected = ExcludeTxid(MakeMessage<test::UnknownInteractionsProtocol::StrictTwoWay>(
      fidl::MessageDynamicFlags::kStrictMethod));
  EXPECT_THAT(received.buf_excluding_txid(), ::testing::ContainerEq(expected));
  EXPECT_NE(0u, received.txid());

  auto server_reply = MakeMessage<test::UnknownInteractionsProtocol::StrictTwoWay>(
      fidl::MessageDynamicFlags::kStrictMethod);
  received.reply(server, server_reply);
  EXPECT_EQ(ZX_OK, received.reply_status);

  loop().RunUntilIdle();
}

TEST_F(UnknownInteractions, TwoWayStrictErrAsyncSend) {
  auto client = AsyncClient();
  auto server = TakeServerChannel();

  client->StrictTwoWayErr().Then([](auto& response) { EXPECT_TRUE(response.is_ok()); });

  auto received = TwoWayServerRequest<16>::ReadFromChannel(server);
  EXPECT_EQ(ZX_OK, received.status);
  EXPECT_EQ(16u, received.num_bytes);
  EXPECT_EQ(0u, received.num_handles);
  auto expected = ExcludeTxid(MakeMessage<test::UnknownInteractionsProtocol::StrictTwoWayErr>(
      fidl::MessageDynamicFlags::kStrictMethod));
  EXPECT_THAT(received.buf_excluding_txid(), ::testing::ContainerEq(expected));
  EXPECT_NE(0u, received.txid());

  auto server_reply = MakeMessage<test::UnknownInteractionsProtocol::StrictTwoWayErr>(
      fidl::MessageDynamicFlags::kStrictMethod, ResultUnionTag::kSuccess, 0);
  received.reply(server, server_reply);
  EXPECT_EQ(ZX_OK, received.reply_status);

  loop().RunUntilIdle();
}

TEST_F(UnknownInteractions, TwoWayFlexibleAsyncSend) {
  auto client = AsyncClient();
  auto server = TakeServerChannel();

  client->FlexibleTwoWay().Then([](auto& response) { EXPECT_TRUE(response.is_ok()); });

  auto received = TwoWayServerRequest<16>::ReadFromChannel(server);
  EXPECT_EQ(ZX_OK, received.status);
  EXPECT_EQ(16u, received.num_bytes);
  EXPECT_EQ(0u, received.num_handles);
  auto expected = ExcludeTxid(MakeMessage<test::UnknownInteractionsProtocol::FlexibleTwoWay>(
      fidl::MessageDynamicFlags::kFlexibleMethod));
  EXPECT_THAT(received.buf_excluding_txid(), ::testing::ContainerEq(expected));
  EXPECT_NE(0u, received.txid());

  auto server_reply = MakeMessage<test::UnknownInteractionsProtocol::FlexibleTwoWay>(
      fidl::MessageDynamicFlags::kFlexibleMethod, ResultUnionTag::kSuccess, 0);
  received.reply(server, server_reply);
  EXPECT_EQ(ZX_OK, received.reply_status);

  loop().RunUntilIdle();
}

TEST_F(UnknownInteractions, TwoWayFlexibleAsyncSendUnknownResponse) {
  auto client = AsyncClient();
  auto server = TakeServerChannel();

  client->FlexibleTwoWay().Then([](auto& response) {
    ASSERT_TRUE(response.is_error());
    EXPECT_EQ(ZX_ERR_NOT_SUPPORTED, response.error_value().status());
    EXPECT_EQ(fidl::Reason::kUnknownInteraction, response.error_value().reason());
  });

  auto received = TwoWayServerRequest<16>::ReadFromChannel(server);
  EXPECT_EQ(ZX_OK, received.status);
  EXPECT_EQ(16u, received.num_bytes);
  EXPECT_EQ(0u, received.num_handles);
  auto expected = ExcludeTxid(MakeMessage<test::UnknownInteractionsProtocol::FlexibleTwoWay>(
      fidl::MessageDynamicFlags::kFlexibleMethod));
  EXPECT_THAT(received.buf_excluding_txid(), ::testing::ContainerEq(expected));
  EXPECT_NE(0u, received.txid());

  auto server_reply = MakeMessage<test::UnknownInteractionsProtocol::FlexibleTwoWay>(
      fidl::MessageDynamicFlags::kFlexibleMethod, ResultUnionTag::kTransportError,
      ZX_ERR_NOT_SUPPORTED);
  received.reply(server, server_reply);
  EXPECT_EQ(ZX_OK, received.reply_status);

  loop().RunUntilIdle();
}

TEST_F(UnknownInteractions, TwoWayFlexibleAsyncSendOtherTransportError) {
  auto client = AsyncClient();
  auto server = TakeServerChannel();

  client->FlexibleTwoWay().Then([](auto& response) {
    ASSERT_TRUE(response.is_error());
    EXPECT_EQ(ZX_ERR_INTERNAL, response.error_value().status());
    EXPECT_EQ(fidl::Reason::kDecodeError, response.error_value().reason());
  });

  auto received = TwoWayServerRequest<16>::ReadFromChannel(server);
  EXPECT_EQ(ZX_OK, received.status);
  EXPECT_EQ(16u, received.num_bytes);
  EXPECT_EQ(0u, received.num_handles);
  EXPECT_EQ(0u, received.num_handles);
  auto expected = ExcludeTxid(MakeMessage<test::UnknownInteractionsProtocol::FlexibleTwoWay>(
      fidl::MessageDynamicFlags::kFlexibleMethod));
  EXPECT_THAT(received.buf_excluding_txid(), ::testing::ContainerEq(expected));
  EXPECT_NE(0u, received.txid());

  auto server_reply = MakeMessage<test::UnknownInteractionsProtocol::FlexibleTwoWay>(
      fidl::MessageDynamicFlags::kFlexibleMethod, ResultUnionTag::kTransportError,
      ZX_ERR_ACCESS_DENIED);
  received.reply(server, server_reply);
  EXPECT_EQ(ZX_OK, received.reply_status);

  loop().RunUntilIdle();
}

TEST_F(UnknownInteractions, TwoWayFlexibleAsyncSendOkTransportError) {
  auto client = AsyncClient();
  auto server = TakeServerChannel();

  client->FlexibleTwoWay().Then([](auto& response) {
    ASSERT_TRUE(response.is_error());
    EXPECT_EQ(ZX_ERR_INTERNAL, response.error_value().status());
    EXPECT_EQ(fidl::Reason::kDecodeError, response.error_value().reason());
  });

  auto received = TwoWayServerRequest<16>::ReadFromChannel(server);
  EXPECT_EQ(ZX_OK, received.status);
  EXPECT_EQ(16u, received.num_bytes);
  EXPECT_EQ(0u, received.num_handles);
  EXPECT_EQ(0u, received.num_handles);
  auto expected = ExcludeTxid(MakeMessage<test::UnknownInteractionsProtocol::FlexibleTwoWay>(
      fidl::MessageDynamicFlags::kFlexibleMethod));
  EXPECT_THAT(received.buf_excluding_txid(), ::testing::ContainerEq(expected));
  EXPECT_NE(0u, received.txid());

  auto server_reply = MakeMessage<test::UnknownInteractionsProtocol::FlexibleTwoWay>(
      fidl::MessageDynamicFlags::kFlexibleMethod, ResultUnionTag::kTransportError, ZX_OK);
  received.reply(server, server_reply);
  EXPECT_EQ(ZX_OK, received.reply_status);

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
  EXPECT_EQ(16u, received.num_bytes);
  EXPECT_EQ(0u, received.num_handles);
  auto expected = ExcludeTxid(MakeMessage<test::UnknownInteractionsProtocol::FlexibleTwoWay>(
      fidl::MessageDynamicFlags::kFlexibleMethod));
  EXPECT_THAT(received.buf_excluding_txid(), ::testing::ContainerEq(expected));
  EXPECT_NE(0u, received.txid());

  auto server_reply = MakeMessage<test::UnknownInteractionsProtocol::FlexibleTwoWay>(
      fidl::MessageDynamicFlags::kFlexibleMethod, ResultUnionTag::kApplicationError, 0x100);
  received.reply(server, server_reply);
  EXPECT_EQ(ZX_OK, received.reply_status);

  loop().RunUntilIdle();
}

TEST_F(UnknownInteractions, TwoWayFlexibleErrAsyncSend) {
  auto client = AsyncClient();
  auto server = TakeServerChannel();

  client->FlexibleTwoWayErr().Then([](auto& response) { EXPECT_TRUE(response.is_ok()); });

  auto received = TwoWayServerRequest<16>::ReadFromChannel(server);
  EXPECT_EQ(ZX_OK, received.status);
  EXPECT_EQ(16u, received.num_bytes);
  EXPECT_EQ(0u, received.num_handles);
  auto expected = ExcludeTxid(MakeMessage<test::UnknownInteractionsProtocol::FlexibleTwoWayErr>(
      fidl::MessageDynamicFlags::kFlexibleMethod));
  EXPECT_THAT(received.buf_excluding_txid(), ::testing::ContainerEq(expected));
  EXPECT_NE(0u, received.txid());

  auto server_reply = MakeMessage<test::UnknownInteractionsProtocol::FlexibleTwoWayErr>(
      fidl::MessageDynamicFlags::kFlexibleMethod, ResultUnionTag::kSuccess, 0);
  received.reply(server, server_reply);
  EXPECT_EQ(ZX_OK, received.reply_status);

  loop().RunUntilIdle();
}

TEST_F(UnknownInteractions, TwoWayFlexibleErrAsyncSendUnknownResponse) {
  auto client = AsyncClient();
  auto server = TakeServerChannel();

  client->FlexibleTwoWayErr().Then([](auto& response) {
    ASSERT_TRUE(response.is_error());
    ASSERT_TRUE(response.error_value().is_transport_error());
    EXPECT_EQ(ZX_ERR_NOT_SUPPORTED, response.error_value().transport_error().status());
    EXPECT_EQ(fidl::Reason::kUnknownInteraction, response.error_value().transport_error().reason());
  });

  auto received = TwoWayServerRequest<16>::ReadFromChannel(server);
  EXPECT_EQ(ZX_OK, received.status);
  EXPECT_EQ(16u, received.num_bytes);
  EXPECT_EQ(0u, received.num_handles);
  auto expected = ExcludeTxid(MakeMessage<test::UnknownInteractionsProtocol::FlexibleTwoWayErr>(
      fidl::MessageDynamicFlags::kFlexibleMethod));
  EXPECT_THAT(received.buf_excluding_txid(), ::testing::ContainerEq(expected));
  EXPECT_NE(0u, received.txid());

  auto server_reply = MakeMessage<test::UnknownInteractionsProtocol::FlexibleTwoWayErr>(
      fidl::MessageDynamicFlags::kFlexibleMethod, ResultUnionTag::kTransportError,
      ZX_ERR_NOT_SUPPORTED);
  received.reply(server, server_reply);
  EXPECT_EQ(ZX_OK, received.reply_status);

  loop().RunUntilIdle();
}

TEST_F(UnknownInteractions, TwoWayFlexibleErrAsyncSendOtherTransportError) {
  auto client = AsyncClient();
  auto server = TakeServerChannel();

  client->FlexibleTwoWayErr().Then([](auto& response) {
    ASSERT_TRUE(response.is_error());
    ASSERT_TRUE(response.error_value().is_transport_error());
    EXPECT_EQ(ZX_ERR_INTERNAL, response.error_value().transport_error().status());
    EXPECT_EQ(fidl::Reason::kDecodeError, response.error_value().transport_error().reason());
  });

  auto received = TwoWayServerRequest<16>::ReadFromChannel(server);
  EXPECT_EQ(ZX_OK, received.status);
  EXPECT_EQ(16u, received.num_bytes);
  EXPECT_EQ(0u, received.num_handles);
  auto expected = ExcludeTxid(MakeMessage<test::UnknownInteractionsProtocol::FlexibleTwoWayErr>(
      fidl::MessageDynamicFlags::kFlexibleMethod));
  EXPECT_THAT(received.buf_excluding_txid(), ::testing::ContainerEq(expected));
  EXPECT_NE(0u, received.txid());

  auto server_reply = MakeMessage<test::UnknownInteractionsProtocol::FlexibleTwoWayErr>(
      fidl::MessageDynamicFlags::kFlexibleMethod, ResultUnionTag::kTransportError,
      ZX_ERR_ACCESS_DENIED);
  received.reply(server, server_reply);
  EXPECT_EQ(ZX_OK, received.reply_status);

  loop().RunUntilIdle();
}

TEST_F(UnknownInteractions, TwoWayFlexibleErrAsyncSendErrorVariant) {
  auto client = AsyncClient();
  auto server = TakeServerChannel();

  client->FlexibleTwoWayErr().Then([](auto& response) {
    ASSERT_TRUE(response.is_error());
    ASSERT_TRUE(response.error_value().is_application_error());
    EXPECT_EQ(0x100, response.error_value().application_error());
  });

  auto received = TwoWayServerRequest<16>::ReadFromChannel(server);
  EXPECT_EQ(ZX_OK, received.status);
  EXPECT_EQ(16u, received.num_bytes);
  EXPECT_EQ(0u, received.num_handles);
  auto expected = ExcludeTxid(MakeMessage<test::UnknownInteractionsProtocol::FlexibleTwoWayErr>(
      fidl::MessageDynamicFlags::kFlexibleMethod));
  EXPECT_THAT(received.buf_excluding_txid(), ::testing::ContainerEq(expected));
  EXPECT_NE(0u, received.txid());

  auto server_reply = MakeMessage<test::UnknownInteractionsProtocol::FlexibleTwoWayErr>(
      fidl::MessageDynamicFlags::kFlexibleMethod, ResultUnionTag::kApplicationError, 0x100);
  received.reply(server, server_reply);
  EXPECT_EQ(ZX_OK, received.reply_status);

  loop().RunUntilIdle();
}

TEST_F(UnknownInteractions, OneWayStrictSyncSend) {
  auto client = SyncClient();
  auto server = TakeServerChannel();
  auto result = client->StrictOneWay();
  EXPECT_TRUE(result.is_ok());

  auto received = ReadResult<16>::ReadFromChannel(server);
  EXPECT_EQ(ZX_OK, received.status);
  EXPECT_EQ(16u, received.num_bytes);
  EXPECT_EQ(0u, received.num_handles);

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
  EXPECT_EQ(16u, received.num_bytes);
  EXPECT_EQ(0u, received.num_handles);

  auto expected = MakeMessage<test::UnknownInteractionsProtocol::FlexibleOneWay>(
      fidl::MessageDynamicFlags::kFlexibleMethod);
  EXPECT_THAT(received.buf, ::testing::ContainerEq(expected));
}

TEST_F(UnknownInteractions, TwoWayStrictSyncSend) {
  auto client = SyncClient();
  auto server = TakeServerChannel();

  auto response_fut = std::async([&client]() { return client->StrictTwoWay(); });

  auto received = TwoWayServerRequest<16>::ReadFromChannel(server);
  EXPECT_EQ(ZX_OK, received.status);
  EXPECT_EQ(16u, received.num_bytes);
  EXPECT_EQ(0u, received.num_handles);
  auto expected = ExcludeTxid(MakeMessage<test::UnknownInteractionsProtocol::StrictTwoWay>(
      fidl::MessageDynamicFlags::kStrictMethod));
  EXPECT_THAT(received.buf_excluding_txid(), ::testing::ContainerEq(expected));
  EXPECT_NE(0u, received.txid());

  auto server_reply = MakeMessage<test::UnknownInteractionsProtocol::StrictTwoWay>(
      fidl::MessageDynamicFlags::kStrictMethod);
  received.reply(server, server_reply);
  EXPECT_EQ(ZX_OK, received.reply_status);

  auto response = response_fut.get();
  EXPECT_TRUE(response.is_ok());
}

TEST_F(UnknownInteractions, TwoWayStrictErrSyncSend) {
  auto client = SyncClient();
  auto server = TakeServerChannel();

  auto response_fut = std::async([&client]() { return client->StrictTwoWayErr(); });

  auto received = TwoWayServerRequest<16>::ReadFromChannel(server);
  EXPECT_EQ(ZX_OK, received.status);
  EXPECT_EQ(16u, received.num_bytes);
  EXPECT_EQ(0u, received.num_handles);
  auto expected = ExcludeTxid(MakeMessage<test::UnknownInteractionsProtocol::StrictTwoWayErr>(
      fidl::MessageDynamicFlags::kStrictMethod));
  EXPECT_THAT(received.buf_excluding_txid(), ::testing::ContainerEq(expected));
  EXPECT_NE(0u, received.txid());

  auto server_reply = MakeMessage<test::UnknownInteractionsProtocol::StrictTwoWayErr>(
      fidl::MessageDynamicFlags::kStrictMethod, ResultUnionTag::kSuccess, 0);
  received.reply(server, server_reply);

  EXPECT_EQ(ZX_OK, received.reply_status);

  auto response = response_fut.get();
  EXPECT_TRUE(response.is_ok());
}

TEST_F(UnknownInteractions, TwoWayFlexibleSyncSend) {
  auto client = SyncClient();
  auto server = TakeServerChannel();

  auto response_fut = std::async([&client]() { return client->FlexibleTwoWay(); });

  auto received = TwoWayServerRequest<16>::ReadFromChannel(server);
  EXPECT_EQ(ZX_OK, received.status);
  EXPECT_EQ(16u, received.num_bytes);
  EXPECT_EQ(0u, received.num_handles);
  auto expected = ExcludeTxid(MakeMessage<test::UnknownInteractionsProtocol::FlexibleTwoWay>(
      fidl::MessageDynamicFlags::kFlexibleMethod));
  EXPECT_THAT(received.buf_excluding_txid(), ::testing::ContainerEq(expected));
  EXPECT_NE(0u, received.txid());

  auto server_reply = MakeMessage<test::UnknownInteractionsProtocol::FlexibleTwoWay>(
      fidl::MessageDynamicFlags::kFlexibleMethod, ResultUnionTag::kSuccess, 0);
  received.reply(server, server_reply);
  EXPECT_EQ(ZX_OK, received.reply_status);

  auto response = response_fut.get();
  EXPECT_TRUE(response.is_ok());
}

TEST_F(UnknownInteractions, TwoWayFlexibleSyncSendUnknownResponse) {
  auto client = SyncClient();
  auto server = TakeServerChannel();

  auto response_fut = std::async([&client]() { return client->FlexibleTwoWay(); });

  auto received = TwoWayServerRequest<16>::ReadFromChannel(server);
  EXPECT_EQ(ZX_OK, received.status);
  EXPECT_EQ(16u, received.num_bytes);
  EXPECT_EQ(0u, received.num_handles);
  auto expected = ExcludeTxid(MakeMessage<test::UnknownInteractionsProtocol::FlexibleTwoWay>(
      fidl::MessageDynamicFlags::kFlexibleMethod));
  EXPECT_THAT(received.buf_excluding_txid(), ::testing::ContainerEq(expected));
  EXPECT_NE(0u, received.txid());

  auto server_reply = MakeMessage<test::UnknownInteractionsProtocol::FlexibleTwoWay>(
      fidl::MessageDynamicFlags::kFlexibleMethod, ResultUnionTag::kTransportError,
      ZX_ERR_NOT_SUPPORTED);
  received.reply(server, server_reply);
  EXPECT_EQ(ZX_OK, received.reply_status);

  auto response = response_fut.get();
  ASSERT_TRUE(response.is_error());
  EXPECT_EQ(ZX_ERR_NOT_SUPPORTED, response.error_value().status());
  EXPECT_EQ(fidl::Reason::kUnknownInteraction, response.error_value().reason());
}

TEST_F(UnknownInteractions, TwoWayFlexibleSyncSendOtherTransportError) {
  auto client = SyncClient();
  auto server = TakeServerChannel();

  auto response_fut = std::async([&client]() { return client->FlexibleTwoWay(); });

  auto received = TwoWayServerRequest<16>::ReadFromChannel(server);
  EXPECT_EQ(ZX_OK, received.status);
  EXPECT_EQ(16u, received.num_bytes);
  EXPECT_EQ(0u, received.num_handles);
  auto expected = ExcludeTxid(MakeMessage<test::UnknownInteractionsProtocol::FlexibleTwoWay>(
      fidl::MessageDynamicFlags::kFlexibleMethod));
  EXPECT_THAT(received.buf_excluding_txid(), ::testing::ContainerEq(expected));
  EXPECT_NE(0u, received.txid());

  auto server_reply = MakeMessage<test::UnknownInteractionsProtocol::FlexibleTwoWay>(
      fidl::MessageDynamicFlags::kFlexibleMethod, ResultUnionTag::kTransportError,
      ZX_ERR_ACCESS_DENIED);
  received.reply(server, server_reply);
  EXPECT_EQ(ZX_OK, received.reply_status);

  auto response = response_fut.get();
  ASSERT_TRUE(response.is_error());
  EXPECT_EQ(ZX_ERR_INTERNAL, response.error_value().status());
  EXPECT_EQ(fidl::Reason::kDecodeError, response.error_value().reason());
}

TEST_F(UnknownInteractions, TwoWayFlexibleSyncSendOkTransportError) {
  auto client = SyncClient();
  auto server = TakeServerChannel();

  auto response_fut = std::async([&client]() { return client->FlexibleTwoWay(); });

  auto received = TwoWayServerRequest<16>::ReadFromChannel(server);
  EXPECT_EQ(ZX_OK, received.status);
  EXPECT_EQ(16u, received.num_bytes);
  EXPECT_EQ(0u, received.num_handles);
  auto expected = ExcludeTxid(MakeMessage<test::UnknownInteractionsProtocol::FlexibleTwoWay>(
      fidl::MessageDynamicFlags::kFlexibleMethod));
  EXPECT_THAT(received.buf_excluding_txid(), ::testing::ContainerEq(expected));
  EXPECT_NE(0u, received.txid());

  auto server_reply = MakeMessage<test::UnknownInteractionsProtocol::FlexibleTwoWay>(
      fidl::MessageDynamicFlags::kFlexibleMethod, ResultUnionTag::kTransportError, ZX_OK);
  received.reply(server, server_reply);
  EXPECT_EQ(ZX_OK, received.reply_status);

  auto response = response_fut.get();
  ASSERT_TRUE(response.is_error());
  EXPECT_EQ(ZX_ERR_INTERNAL, response.error_value().status());
  EXPECT_EQ(fidl::Reason::kDecodeError, response.error_value().reason());
}

TEST_F(UnknownInteractions, TwoWayFlexibleSyncSendErrorVariant) {
  auto client = SyncClient();
  auto server = TakeServerChannel();

  auto response_fut = std::async([&client]() { return client->FlexibleTwoWay(); });

  auto received = TwoWayServerRequest<16>::ReadFromChannel(server);
  EXPECT_EQ(ZX_OK, received.status);
  EXPECT_EQ(16u, received.num_bytes);
  EXPECT_EQ(0u, received.num_handles);
  auto expected = ExcludeTxid(MakeMessage<test::UnknownInteractionsProtocol::FlexibleTwoWay>(
      fidl::MessageDynamicFlags::kFlexibleMethod));
  EXPECT_THAT(received.buf_excluding_txid(), ::testing::ContainerEq(expected));
  EXPECT_NE(0u, received.txid());

  auto server_reply = MakeMessage<test::UnknownInteractionsProtocol::FlexibleTwoWay>(
      fidl::MessageDynamicFlags::kFlexibleMethod, ResultUnionTag::kApplicationError, 0x100);
  received.reply(server, server_reply);
  EXPECT_EQ(ZX_OK, received.reply_status);

  auto response = response_fut.get();
  ASSERT_TRUE(response.is_error());
  EXPECT_EQ(fidl::Reason::kDecodeError, response.error_value().reason());
}

TEST_F(UnknownInteractions, TwoWayFlexibleErrSyncSend) {
  auto client = SyncClient();
  auto server = TakeServerChannel();

  auto response_fut = std::async([&client]() { return client->FlexibleTwoWayErr(); });

  auto received = TwoWayServerRequest<16>::ReadFromChannel(server);
  EXPECT_EQ(ZX_OK, received.status);
  EXPECT_EQ(16u, received.num_bytes);
  EXPECT_EQ(0u, received.num_handles);
  auto expected = ExcludeTxid(MakeMessage<test::UnknownInteractionsProtocol::FlexibleTwoWayErr>(
      fidl::MessageDynamicFlags::kFlexibleMethod));
  EXPECT_THAT(received.buf_excluding_txid(), ::testing::ContainerEq(expected));
  EXPECT_NE(0u, received.txid());

  auto server_reply = MakeMessage<test::UnknownInteractionsProtocol::FlexibleTwoWayErr>(
      fidl::MessageDynamicFlags::kFlexibleMethod, ResultUnionTag::kSuccess, 0);
  received.reply(server, server_reply);
  EXPECT_EQ(ZX_OK, received.reply_status);

  auto response = response_fut.get();
  ASSERT_TRUE(response.is_ok());
}

TEST_F(UnknownInteractions, TwoWayFlexibleErrSyncSendUnknownResponse) {
  auto client = SyncClient();
  auto server = TakeServerChannel();

  auto response_fut = std::async([&client]() { return client->FlexibleTwoWayErr(); });

  auto received = TwoWayServerRequest<16>::ReadFromChannel(server);
  EXPECT_EQ(ZX_OK, received.status);
  EXPECT_EQ(16u, received.num_bytes);
  EXPECT_EQ(0u, received.num_handles);
  auto expected = ExcludeTxid(MakeMessage<test::UnknownInteractionsProtocol::FlexibleTwoWayErr>(
      fidl::MessageDynamicFlags::kFlexibleMethod));
  EXPECT_THAT(received.buf_excluding_txid(), ::testing::ContainerEq(expected));
  EXPECT_NE(0u, received.txid());

  auto server_reply = MakeMessage<test::UnknownInteractionsProtocol::FlexibleTwoWayErr>(
      fidl::MessageDynamicFlags::kFlexibleMethod, ResultUnionTag::kTransportError,
      ZX_ERR_NOT_SUPPORTED);
  received.reply(server, server_reply);
  EXPECT_EQ(ZX_OK, received.reply_status);

  auto response = response_fut.get();
  ASSERT_TRUE(response.is_error());
  ASSERT_TRUE(response.error_value().is_transport_error());
  EXPECT_EQ(ZX_ERR_NOT_SUPPORTED, response.error_value().transport_error().status());
  EXPECT_EQ(fidl::Reason::kUnknownInteraction, response.error_value().transport_error().reason());
}

TEST_F(UnknownInteractions, TwoWayFlexibleErrSyncSendOtherTransportError) {
  auto client = SyncClient();
  auto server = TakeServerChannel();

  auto response_fut = std::async([&client]() { return client->FlexibleTwoWayErr(); });

  auto received = TwoWayServerRequest<16>::ReadFromChannel(server);
  EXPECT_EQ(ZX_OK, received.status);
  EXPECT_EQ(16u, received.num_bytes);
  EXPECT_EQ(0u, received.num_handles);
  auto expected = ExcludeTxid(MakeMessage<test::UnknownInteractionsProtocol::FlexibleTwoWayErr>(
      fidl::MessageDynamicFlags::kFlexibleMethod));
  EXPECT_THAT(received.buf_excluding_txid(), ::testing::ContainerEq(expected));
  EXPECT_NE(0u, received.txid());

  auto server_reply = MakeMessage<test::UnknownInteractionsProtocol::FlexibleTwoWayErr>(
      fidl::MessageDynamicFlags::kFlexibleMethod, ResultUnionTag::kTransportError,
      ZX_ERR_ACCESS_DENIED);
  received.reply(server, server_reply);
  EXPECT_EQ(ZX_OK, received.reply_status);

  auto response = response_fut.get();
  ASSERT_TRUE(response.is_error());
  ASSERT_TRUE(response.error_value().is_transport_error());
  EXPECT_EQ(ZX_ERR_INTERNAL, response.error_value().transport_error().status());
  EXPECT_EQ(fidl::Reason::kDecodeError, response.error_value().transport_error().reason());
}

TEST_F(UnknownInteractions, TwoWayFlexibleErrSyncSendErrorVariant) {
  auto client = SyncClient();
  auto server = TakeServerChannel();

  auto response_fut = std::async([&client]() { return client->FlexibleTwoWayErr(); });

  auto received = TwoWayServerRequest<16>::ReadFromChannel(server);
  EXPECT_EQ(ZX_OK, received.status);
  EXPECT_EQ(16u, received.num_bytes);
  EXPECT_EQ(0u, received.num_handles);
  auto expected = ExcludeTxid(MakeMessage<test::UnknownInteractionsProtocol::FlexibleTwoWayErr>(
      fidl::MessageDynamicFlags::kFlexibleMethod));
  EXPECT_THAT(received.buf_excluding_txid(), ::testing::ContainerEq(expected));
  EXPECT_NE(0u, received.txid());

  auto server_reply = MakeMessage<test::UnknownInteractionsProtocol::FlexibleTwoWayErr>(
      fidl::MessageDynamicFlags::kFlexibleMethod, ResultUnionTag::kApplicationError, 0x100);
  received.reply(server, server_reply);
  EXPECT_EQ(ZX_OK, received.reply_status);

  auto response = response_fut.get();
  ASSERT_TRUE(response.is_error());
  ASSERT_TRUE(response.error_value().is_application_error());
  EXPECT_EQ(0x100, response.error_value().application_error());
}

TEST_F(UnknownInteractions, SendStrictEvent) {
  auto client = TakeClientChannel();
  auto server = TakeServerEnd();

  EXPECT_TRUE(fidl::SendEvent(server)->StrictEvent().is_ok());

  auto received = ReadResult<16>::ReadFromChannel(client);
  EXPECT_EQ(ZX_OK, received.status);
  EXPECT_EQ(16u, received.num_bytes);
  EXPECT_EQ(0u, received.num_handles);

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
  EXPECT_EQ(16u, received.num_bytes);
  EXPECT_EQ(0u, received.num_handles);

  auto expected = MakeMessage<test::UnknownInteractionsProtocol::FlexibleEvent>(
      fidl::MessageDynamicFlags::kFlexibleMethod);
  EXPECT_THAT(received.buf, ::testing::ContainerEq(expected));
}
}  // namespace
