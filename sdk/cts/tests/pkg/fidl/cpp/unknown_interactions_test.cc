// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/zx/time.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>

#include <algorithm>
#include <future>
#include <optional>

#include <test/unknown/interactions/cpp/fidl.h>
#include <test/unknown/interactions/cpp/fidl_test_base.h>
#include <zxtest/zxtest.h>

namespace {
namespace test = ::test::unknown::interactions;

class UnknownInteractionsImpl : public test::testing::UnknownInteractionsProtocol_TestBase {
  void NotImplemented_(const std::string& name) override {
    ADD_FAILURE("Method %s called unexpectedly", name.c_str());
  }
};

class UnknownInteractions : public ::zxtest::Test {
 protected:
  void SetUp() override {
    loop_.emplace(&kAsyncLoopConfigAttachToCurrentThread);

    ASSERT_EQ(zx::channel::create(0, &client_end_, &server_end_), ZX_OK);
  }

  async::Loop& loop() { return loop_.value(); }

  fidl::InterfaceRequest<test::UnknownInteractionsProtocol> TakeServerEnd() {
    EXPECT_TRUE(server_end_.is_valid());
    return fidl::InterfaceRequest<test::UnknownInteractionsProtocol>(std::move(server_end_));
  }

  zx::channel TakeServerChannel() {
    EXPECT_TRUE(server_end_.is_valid());
    return std::move(server_end_);
  }

  zx::channel TakeClientChannel() {
    EXPECT_TRUE(client_end_.is_valid());
    return std::move(client_end_);
  }

  fidl::SynchronousInterfacePtr<test::UnknownInteractionsProtocol> SyncClient() {
    EXPECT_TRUE(client_end_.is_valid());
    fidl::SynchronousInterfacePtr<test::UnknownInteractionsProtocol> client;
    client.Bind(std::move(client_end_));
    return client;
  }

  fidl::InterfacePtr<test::UnknownInteractionsProtocol> AsyncClient(
      fit::function<void(zx_status_t)> error_handler = nullptr) {
    EXPECT_TRUE(client_end_.is_valid());
    fidl::InterfacePtr<test::UnknownInteractionsProtocol> client;
    client.Bind(std::move(client_end_), loop_->dispatcher());
    if (error_handler) {
      client.set_error_handler(std::move(error_handler));
    } else {
      client.set_error_handler([](auto error) { ADD_FAILURE("Unexpected error %d", error); });
    }
    return client;
  }

 private:
  std::optional<async::Loop> loop_;
  zx::channel client_end_;
  zx::channel server_end_;
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
std::array<uint8_t, sizeof(fidl_message_header_t) + sizeof(fidl_xunion_v2_t)> MakeMessage(
    uint64_t ordinal, zx_txid_t txid, fidl::MessageDynamicFlags dynamic_flags,
    ResultUnionTag result_union_tag, InlineValue inline_value) {
  fidl_message_header_t header{
      .txid = txid,
      .at_rest_flags = {FIDL_MESSAGE_HEADER_AT_REST_FLAGS_0_USE_VERSION_V2, 0},
      .dynamic_flags =
          static_cast<std::underlying_type_t<fidl::MessageDynamicFlags>>(dynamic_flags),
      .magic_number = kFidlWireFormatMagicNumberInitial,
      .ordinal = ordinal,
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
std::array<uint8_t, sizeof(fidl_message_header_t) + sizeof(fidl_xunion_v2_t)> MakeMessage(
    uint64_t ordinal, fidl::MessageDynamicFlags dynamic_flags, ResultUnionTag result_union_tag,
    InlineValue inline_value) {
  return MakeMessage(ordinal, 0, dynamic_flags, result_union_tag, inline_value);
}

// Make an array representing a message with just a transaction header.
std::array<uint8_t, sizeof(fidl_message_header_t)> MakeMessage(
    uint64_t ordinal, zx_txid_t txid, fidl::MessageDynamicFlags dynamic_flags) {
  fidl_message_header_t header{
      .txid = txid,
      .at_rest_flags = {FIDL_MESSAGE_HEADER_AT_REST_FLAGS_0_USE_VERSION_V2, 0},
      .dynamic_flags =
          static_cast<std::underlying_type_t<fidl::MessageDynamicFlags>>(dynamic_flags),
      .magic_number = kFidlWireFormatMagicNumberInitial,
      .ordinal = ordinal};
  std::array<uint8_t, sizeof(fidl_message_header_t)> result;
  std::memcpy(result.data(), &header, sizeof(fidl_message_header_t));
  return result;
}

// Make an array representing a message with just a transaction header.
// This version is for tests which don't care about the txid or want a zero
// txid (e.g. one way interactions).
std::array<uint8_t, sizeof(fidl_message_header_t)> MakeMessage(
    uint64_t ordinal, fidl::MessageDynamicFlags dynamic_flags) {
  return MakeMessage(ordinal, 0, dynamic_flags);
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
  client->StrictOneWay();

  auto received = ReadResult<16>::ReadFromChannel(server);
  EXPECT_EQ(ZX_OK, received.status);

  auto expected = MakeMessage(test::internal::kUnknownInteractionsProtocol_StrictOneWay_Ordinal,
                              fidl::MessageDynamicFlags::kStrictMethod);
  EXPECT_EQ(expected, received.buf);
}

TEST_F(UnknownInteractions, OneWayFlexibleAsyncSend) {
  auto client = AsyncClient();
  auto server = TakeServerChannel();
  client->FlexibleOneWay();

  auto received = ReadResult<16>::ReadFromChannel(server);
  EXPECT_EQ(ZX_OK, received.status);

  auto expected = MakeMessage(test::internal::kUnknownInteractionsProtocol_FlexibleOneWay_Ordinal,
                              fidl::MessageDynamicFlags::kFlexibleMethod);
  EXPECT_EQ(expected, received.buf);
}

///////////////////////////////////////////////////////////////////////////////
//// Two-Way Methods - Async Client
///////////////////////////////////////////////////////////////////////////////

TEST_F(UnknownInteractions, TwoWayStrictAsyncSend) {
  auto client = AsyncClient();
  auto server = TakeServerChannel();

  client->StrictTwoWay([]() {});

  auto received = TwoWayServerRequest<16>::ReadFromChannel(server);
  EXPECT_EQ(ZX_OK, received.status);
  auto expected =
      ExcludeTxid(MakeMessage(test::internal::kUnknownInteractionsProtocol_StrictTwoWay_Ordinal,
                              fidl::MessageDynamicFlags::kStrictMethod));
  EXPECT_EQ(expected, received.buf_excluding_txid());
  EXPECT_NE(0u, received.txid());

  auto server_reply = MakeMessage(test::internal::kUnknownInteractionsProtocol_StrictTwoWay_Ordinal,
                                  fidl::MessageDynamicFlags::kStrictMethod);
  received.reply(server, server_reply);

  loop().RunUntilIdle();
}

TEST_F(UnknownInteractions, TwoWayStrictErrAsyncSend) {
  auto client = AsyncClient();
  auto server = TakeServerChannel();

  client->StrictTwoWayErr([](auto response) { EXPECT_TRUE(response.is_response()); });

  auto received = TwoWayServerRequest<16>::ReadFromChannel(server);
  EXPECT_EQ(ZX_OK, received.status);
  auto expected =
      ExcludeTxid(MakeMessage(test::internal::kUnknownInteractionsProtocol_StrictTwoWayErr_Ordinal,
                              fidl::MessageDynamicFlags::kStrictMethod));
  EXPECT_EQ(expected, received.buf_excluding_txid());
  EXPECT_NE(0u, received.txid());

  auto server_reply =
      MakeMessage(test::internal::kUnknownInteractionsProtocol_StrictTwoWayErr_Ordinal,
                  fidl::MessageDynamicFlags::kStrictMethod, ResultUnionTag::kSuccess, 0);
  received.reply(server, server_reply);

  loop().RunUntilIdle();
}

TEST_F(UnknownInteractions, TwoWayFlexibleAsyncSend) {
  auto client = AsyncClient();
  auto server = TakeServerChannel();

  client->FlexibleTwoWay([](auto response) { EXPECT_TRUE(response.is_response()); });

  auto received = TwoWayServerRequest<16>::ReadFromChannel(server);
  EXPECT_EQ(ZX_OK, received.status);
  auto expected =
      ExcludeTxid(MakeMessage(test::internal::kUnknownInteractionsProtocol_FlexibleTwoWay_Ordinal,
                              fidl::MessageDynamicFlags::kFlexibleMethod));
  EXPECT_EQ(expected, received.buf_excluding_txid());
  EXPECT_NE(0u, received.txid());

  auto server_reply =
      MakeMessage(test::internal::kUnknownInteractionsProtocol_FlexibleTwoWay_Ordinal,
                  fidl::MessageDynamicFlags::kFlexibleMethod, ResultUnionTag::kSuccess, 0);
  received.reply(server, server_reply);

  loop().RunUntilIdle();
}

TEST_F(UnknownInteractions, TwoWayFlexibleAsyncSendUnknownResponse) {
  auto client = AsyncClient();
  auto server = TakeServerChannel();

  client->FlexibleTwoWay([](auto response) {
    ASSERT_TRUE(response.is_transport_err());
    EXPECT_EQ(::fidl::TransportErr::kUnknownMethod, response.transport_err());
  });

  auto received = TwoWayServerRequest<16>::ReadFromChannel(server);
  EXPECT_EQ(ZX_OK, received.status);
  auto expected =
      ExcludeTxid(MakeMessage(test::internal::kUnknownInteractionsProtocol_FlexibleTwoWay_Ordinal,
                              fidl::MessageDynamicFlags::kFlexibleMethod));
  EXPECT_EQ(expected, received.buf_excluding_txid());
  EXPECT_NE(0u, received.txid());

  auto server_reply =
      MakeMessage(test::internal::kUnknownInteractionsProtocol_FlexibleTwoWay_Ordinal,
                  fidl::MessageDynamicFlags::kFlexibleMethod, ResultUnionTag::kTransportError,
                  ZX_ERR_NOT_SUPPORTED);
  received.reply(server, server_reply);

  loop().RunUntilIdle();
}

TEST_F(UnknownInteractions, TwoWayFlexibleAsyncSendOtherTransportError) {
  auto error = std::make_shared<zx_status_t>(ZX_OK);
  auto client = AsyncClient([error](auto status) { *error = status; });
  auto server = TakeServerChannel();

  client->FlexibleTwoWay(
      [](auto response) { ADD_FAILURE("Response should have been a decoding error"); });

  auto received = TwoWayServerRequest<16>::ReadFromChannel(server);
  EXPECT_EQ(ZX_OK, received.status);
  auto expected =
      ExcludeTxid(MakeMessage(test::internal::kUnknownInteractionsProtocol_FlexibleTwoWay_Ordinal,
                              fidl::MessageDynamicFlags::kFlexibleMethod));
  EXPECT_EQ(expected, received.buf_excluding_txid());
  EXPECT_NE(0u, received.txid());

  auto server_reply =
      MakeMessage(test::internal::kUnknownInteractionsProtocol_FlexibleTwoWay_Ordinal,
                  fidl::MessageDynamicFlags::kFlexibleMethod, ResultUnionTag::kTransportError,
                  ZX_ERR_INVALID_ARGS);
  received.reply(server, server_reply);

  loop().RunUntilIdle();

  ASSERT_EQ(ZX_ERR_INVALID_ARGS, *error);
}

TEST_F(UnknownInteractions, TwoWayFlexibleAsyncSendOkTransportError) {
  auto error = std::make_shared<zx_status_t>(ZX_OK);
  auto client = AsyncClient([error](auto status) { *error = status; });
  auto server = TakeServerChannel();

  client->FlexibleTwoWay(
      [](auto response) { ADD_FAILURE("Response should have been a decoding error"); });

  auto received = TwoWayServerRequest<16>::ReadFromChannel(server);
  EXPECT_EQ(ZX_OK, received.status);
  auto expected =
      ExcludeTxid(MakeMessage(test::internal::kUnknownInteractionsProtocol_FlexibleTwoWay_Ordinal,
                              fidl::MessageDynamicFlags::kFlexibleMethod));
  EXPECT_EQ(expected, received.buf_excluding_txid());
  EXPECT_NE(0u, received.txid());

  auto server_reply = MakeMessage(
      test::internal::kUnknownInteractionsProtocol_FlexibleTwoWay_Ordinal,
      fidl::MessageDynamicFlags::kFlexibleMethod, ResultUnionTag::kTransportError, ZX_OK);
  received.reply(server, server_reply);

  loop().RunUntilIdle();

  ASSERT_EQ(ZX_ERR_INVALID_ARGS, *error);
}

TEST_F(UnknownInteractions, TwoWayFlexibleAsyncSendErrorVariant) {
  auto error = std::make_shared<zx_status_t>(ZX_OK);
  auto client = AsyncClient([error](auto status) { *error = status; });
  auto server = TakeServerChannel();

  client->FlexibleTwoWay(
      [](auto response) { ADD_FAILURE("Response should have been a decoding error"); });

  auto received = TwoWayServerRequest<16>::ReadFromChannel(server);
  EXPECT_EQ(ZX_OK, received.status);
  auto expected =
      ExcludeTxid(MakeMessage(test::internal::kUnknownInteractionsProtocol_FlexibleTwoWay_Ordinal,
                              fidl::MessageDynamicFlags::kFlexibleMethod));
  EXPECT_EQ(expected, received.buf_excluding_txid());
  EXPECT_NE(0u, received.txid());

  auto server_reply = MakeMessage(
      test::internal::kUnknownInteractionsProtocol_FlexibleTwoWay_Ordinal,
      fidl::MessageDynamicFlags::kFlexibleMethod, ResultUnionTag::kApplicationError, 0x100);
  received.reply(server, server_reply);

  loop().RunUntilIdle();

  ASSERT_EQ(ZX_ERR_INVALID_ARGS, *error);
}

TEST_F(UnknownInteractions, TwoWayFlexibleFieldsAsyncSend) {
  auto client = AsyncClient();
  auto server = TakeServerChannel();

  client->FlexibleTwoWayFields([](auto response) {
    ASSERT_TRUE(response.is_response());
    EXPECT_EQ(32, response.response().some_field);
  });

  auto received = TwoWayServerRequest<16>::ReadFromChannel(server);
  EXPECT_EQ(ZX_OK, received.status);
  auto expected = ExcludeTxid(
      MakeMessage(test::internal::kUnknownInteractionsProtocol_FlexibleTwoWayFields_Ordinal,
                  fidl::MessageDynamicFlags::kFlexibleMethod));
  EXPECT_EQ(expected, received.buf_excluding_txid());
  EXPECT_NE(0u, received.txid());

  auto server_reply =
      MakeMessage(test::internal::kUnknownInteractionsProtocol_FlexibleTwoWayFields_Ordinal,
                  fidl::MessageDynamicFlags::kFlexibleMethod, ResultUnionTag::kSuccess, 32);
  received.reply(server, server_reply);

  loop().RunUntilIdle();
}

TEST_F(UnknownInteractions, TwoWayFlexibleFieldsAsyncSendUnknownResponse) {
  auto client = AsyncClient();
  auto server = TakeServerChannel();

  client->FlexibleTwoWayFields([](auto response) {
    ASSERT_TRUE(response.is_transport_err());
    EXPECT_EQ(::fidl::TransportErr::kUnknownMethod, response.transport_err());
  });

  auto received = TwoWayServerRequest<16>::ReadFromChannel(server);
  EXPECT_EQ(ZX_OK, received.status);
  auto expected = ExcludeTxid(
      MakeMessage(test::internal::kUnknownInteractionsProtocol_FlexibleTwoWayFields_Ordinal,
                  fidl::MessageDynamicFlags::kFlexibleMethod));
  EXPECT_EQ(expected, received.buf_excluding_txid());
  EXPECT_NE(0u, received.txid());

  auto server_reply =
      MakeMessage(test::internal::kUnknownInteractionsProtocol_FlexibleTwoWayFields_Ordinal,
                  fidl::MessageDynamicFlags::kFlexibleMethod, ResultUnionTag::kTransportError,
                  ZX_ERR_NOT_SUPPORTED);
  received.reply(server, server_reply);

  loop().RunUntilIdle();
}

TEST_F(UnknownInteractions, TwoWayFlexibleErrAsyncSend) {
  auto client = AsyncClient();
  auto server = TakeServerChannel();

  client->FlexibleTwoWayErr([](auto response) { EXPECT_TRUE(response.is_response()); });

  auto received = TwoWayServerRequest<16>::ReadFromChannel(server);
  EXPECT_EQ(ZX_OK, received.status);
  auto expected = ExcludeTxid(
      MakeMessage(test::internal::kUnknownInteractionsProtocol_FlexibleTwoWayErr_Ordinal,
                  fidl::MessageDynamicFlags::kFlexibleMethod));
  EXPECT_EQ(expected, received.buf_excluding_txid());
  EXPECT_NE(0u, received.txid());

  auto server_reply =
      MakeMessage(test::internal::kUnknownInteractionsProtocol_FlexibleTwoWayErr_Ordinal,
                  fidl::MessageDynamicFlags::kFlexibleMethod, ResultUnionTag::kSuccess, 0);
  received.reply(server, server_reply);

  loop().RunUntilIdle();
}

TEST_F(UnknownInteractions, TwoWayFlexibleErrAsyncSendUnknownResponse) {
  auto client = AsyncClient();
  auto server = TakeServerChannel();

  client->FlexibleTwoWayErr([](auto response) {
    ASSERT_TRUE(response.is_transport_err());
    EXPECT_EQ(::fidl::TransportErr::kUnknownMethod, response.transport_err());
  });

  auto received = TwoWayServerRequest<16>::ReadFromChannel(server);
  EXPECT_EQ(ZX_OK, received.status);
  auto expected = ExcludeTxid(
      MakeMessage(test::internal::kUnknownInteractionsProtocol_FlexibleTwoWayErr_Ordinal,
                  fidl::MessageDynamicFlags::kFlexibleMethod));
  EXPECT_EQ(expected, received.buf_excluding_txid());
  EXPECT_NE(0u, received.txid());

  auto server_reply =
      MakeMessage(test::internal::kUnknownInteractionsProtocol_FlexibleTwoWayErr_Ordinal,
                  fidl::MessageDynamicFlags::kFlexibleMethod, ResultUnionTag::kTransportError,
                  ZX_ERR_NOT_SUPPORTED);
  received.reply(server, server_reply);

  loop().RunUntilIdle();
}

TEST_F(UnknownInteractions, TwoWayFlexibleErrAsyncSendOtherTransportError) {
  auto error = std::make_shared<zx_status_t>(ZX_OK);
  auto client = AsyncClient([error](auto status) { *error = status; });
  auto server = TakeServerChannel();

  client->FlexibleTwoWayErr(
      [](auto response) { ADD_FAILURE("Response should have been a decoding error"); });

  auto received = TwoWayServerRequest<16>::ReadFromChannel(server);
  EXPECT_EQ(ZX_OK, received.status);
  auto expected = ExcludeTxid(
      MakeMessage(test::internal::kUnknownInteractionsProtocol_FlexibleTwoWayErr_Ordinal,
                  fidl::MessageDynamicFlags::kFlexibleMethod));
  EXPECT_EQ(expected, received.buf_excluding_txid());
  EXPECT_NE(0u, received.txid());

  auto server_reply =
      MakeMessage(test::internal::kUnknownInteractionsProtocol_FlexibleTwoWayErr_Ordinal,
                  fidl::MessageDynamicFlags::kFlexibleMethod, ResultUnionTag::kTransportError,
                  ZX_ERR_ACCESS_DENIED);
  received.reply(server, server_reply);

  loop().RunUntilIdle();

  ASSERT_EQ(ZX_ERR_INVALID_ARGS, *error);
}

TEST_F(UnknownInteractions, TwoWayFlexibleErrAsyncSendErrorVariant) {
  auto client = AsyncClient();
  auto server = TakeServerChannel();

  client->FlexibleTwoWayErr([](auto response) {
    ASSERT_TRUE(response.is_err());
    EXPECT_EQ(0x100, response.err());
  });

  auto received = TwoWayServerRequest<16>::ReadFromChannel(server);
  EXPECT_EQ(ZX_OK, received.status);
  auto expected = ExcludeTxid(
      MakeMessage(test::internal::kUnknownInteractionsProtocol_FlexibleTwoWayErr_Ordinal,
                  fidl::MessageDynamicFlags::kFlexibleMethod));
  EXPECT_EQ(expected, received.buf_excluding_txid());
  EXPECT_NE(0u, received.txid());

  auto server_reply = MakeMessage(
      test::internal::kUnknownInteractionsProtocol_FlexibleTwoWayErr_Ordinal,
      fidl::MessageDynamicFlags::kFlexibleMethod, ResultUnionTag::kApplicationError, 0x100);
  received.reply(server, server_reply);

  loop().RunUntilIdle();
}

TEST_F(UnknownInteractions, TwoWayFlexibleFieldsErrAsyncSend) {
  auto client = AsyncClient();
  auto server = TakeServerChannel();

  client->FlexibleTwoWayFieldsErr([](auto response) {
    ASSERT_TRUE(response.is_response());
    EXPECT_EQ(32, response.response().some_field);
  });

  auto received = TwoWayServerRequest<16>::ReadFromChannel(server);
  EXPECT_EQ(ZX_OK, received.status);
  auto expected = ExcludeTxid(
      MakeMessage(test::internal::kUnknownInteractionsProtocol_FlexibleTwoWayFieldsErr_Ordinal,
                  fidl::MessageDynamicFlags::kFlexibleMethod));
  EXPECT_EQ(expected, received.buf_excluding_txid());
  EXPECT_NE(0u, received.txid());

  auto server_reply =
      MakeMessage(test::internal::kUnknownInteractionsProtocol_FlexibleTwoWayFieldsErr_Ordinal,
                  fidl::MessageDynamicFlags::kFlexibleMethod, ResultUnionTag::kSuccess, 32);
  received.reply(server, server_reply);

  loop().RunUntilIdle();
}

TEST_F(UnknownInteractions, TwoWayFlexibleFieldsErrAsyncSendUnknownResponse) {
  auto client = AsyncClient();
  auto server = TakeServerChannel();

  client->FlexibleTwoWayFieldsErr([](auto response) {
    ASSERT_TRUE(response.is_transport_err());
    EXPECT_EQ(::fidl::TransportErr::kUnknownMethod, response.transport_err());
  });

  auto received = TwoWayServerRequest<16>::ReadFromChannel(server);
  EXPECT_EQ(ZX_OK, received.status);
  auto expected = ExcludeTxid(
      MakeMessage(test::internal::kUnknownInteractionsProtocol_FlexibleTwoWayFieldsErr_Ordinal,
                  fidl::MessageDynamicFlags::kFlexibleMethod));
  EXPECT_EQ(expected, received.buf_excluding_txid());
  EXPECT_NE(0u, received.txid());

  auto server_reply =
      MakeMessage(test::internal::kUnknownInteractionsProtocol_FlexibleTwoWayFieldsErr_Ordinal,
                  fidl::MessageDynamicFlags::kFlexibleMethod, ResultUnionTag::kTransportError,
                  ZX_ERR_NOT_SUPPORTED);
  received.reply(server, server_reply);

  loop().RunUntilIdle();
}

TEST_F(UnknownInteractions, TwoWayFlexibleFieldsErrAsyncSendErrorVariant) {
  auto client = AsyncClient();
  auto server = TakeServerChannel();

  client->FlexibleTwoWayFieldsErr([](auto response) {
    ASSERT_TRUE(response.is_err());
    EXPECT_EQ(0x100, response.err());
  });

  auto received = TwoWayServerRequest<16>::ReadFromChannel(server);
  EXPECT_EQ(ZX_OK, received.status);
  auto expected = ExcludeTxid(
      MakeMessage(test::internal::kUnknownInteractionsProtocol_FlexibleTwoWayFieldsErr_Ordinal,
                  fidl::MessageDynamicFlags::kFlexibleMethod));
  EXPECT_EQ(expected, received.buf_excluding_txid());
  EXPECT_NE(0u, received.txid());

  auto server_reply = MakeMessage(
      test::internal::kUnknownInteractionsProtocol_FlexibleTwoWayFieldsErr_Ordinal,
      fidl::MessageDynamicFlags::kFlexibleMethod, ResultUnionTag::kApplicationError, 0x100);
  received.reply(server, server_reply);

  loop().RunUntilIdle();
}

///////////////////////////////////////////////////////////////////////////////
//// One-Way Methods - Sync Client
///////////////////////////////////////////////////////////////////////////////

TEST_F(UnknownInteractions, OneWayStrictSyncSend) {
  auto client = SyncClient();
  auto server = TakeServerChannel();
  EXPECT_EQ(ZX_OK, client->StrictOneWay());

  auto received = ReadResult<16>::ReadFromChannel(server);
  EXPECT_EQ(ZX_OK, received.status);

  auto expected = MakeMessage(test::internal::kUnknownInteractionsProtocol_StrictOneWay_Ordinal,
                              fidl::MessageDynamicFlags::kStrictMethod);
  EXPECT_EQ(expected, received.buf);
}

TEST_F(UnknownInteractions, OneWayFlexibleSyncSend) {
  auto client = SyncClient();
  auto server = TakeServerChannel();
  EXPECT_EQ(ZX_OK, client->FlexibleOneWay());

  auto received = ReadResult<16>::ReadFromChannel(server);
  EXPECT_EQ(ZX_OK, received.status);

  auto expected = MakeMessage(test::internal::kUnknownInteractionsProtocol_FlexibleOneWay_Ordinal,
                              fidl::MessageDynamicFlags::kFlexibleMethod);
  EXPECT_EQ(expected, received.buf);
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
  auto expected =
      ExcludeTxid(MakeMessage(test::internal::kUnknownInteractionsProtocol_StrictTwoWay_Ordinal,
                              fidl::MessageDynamicFlags::kStrictMethod));
  EXPECT_EQ(expected, received.buf_excluding_txid());
  EXPECT_NE(0u, received.txid());

  auto server_reply = MakeMessage(test::internal::kUnknownInteractionsProtocol_StrictTwoWay_Ordinal,
                                  fidl::MessageDynamicFlags::kStrictMethod);
  received.reply(server, server_reply);

  auto response = response_fut.get();
  EXPECT_EQ(ZX_OK, response);
}

TEST_F(UnknownInteractions, TwoWayStrictErrSyncSend) {
  auto client = SyncClient();
  auto server = TakeServerChannel();

  auto response_fut = std::async([&client]() {
    test::UnknownInteractionsProtocol_StrictTwoWayErr_Result out_result;
    auto status_code = client->StrictTwoWayErr(&out_result);
    return std::make_tuple(status_code, std::move(out_result));
  });

  auto received = TwoWayServerRequest<16>::ReadFromChannel(server);
  EXPECT_EQ(ZX_OK, received.status);
  auto expected =
      ExcludeTxid(MakeMessage(test::internal::kUnknownInteractionsProtocol_StrictTwoWayErr_Ordinal,
                              fidl::MessageDynamicFlags::kStrictMethod));
  EXPECT_EQ(expected, received.buf_excluding_txid());
  EXPECT_NE(0u, received.txid());

  auto server_reply =
      MakeMessage(test::internal::kUnknownInteractionsProtocol_StrictTwoWayErr_Ordinal,
                  fidl::MessageDynamicFlags::kStrictMethod, ResultUnionTag::kSuccess, 0);
  received.reply(server, server_reply);

  auto response = response_fut.get();
  ASSERT_EQ(ZX_OK, std::get<0>(response));
  EXPECT_TRUE(std::get<1>(response).is_response());
}

TEST_F(UnknownInteractions, TwoWayFlexibleSyncSend) {
  auto client = SyncClient();
  auto server = TakeServerChannel();

  auto response_fut = std::async([&client]() {
    test::UnknownInteractionsProtocol_FlexibleTwoWay_Result out_result;
    auto status_code = client->FlexibleTwoWay(&out_result);
    return std::make_tuple(status_code, std::move(out_result));
  });

  auto received = TwoWayServerRequest<16>::ReadFromChannel(server);
  EXPECT_EQ(ZX_OK, received.status);
  auto expected =
      ExcludeTxid(MakeMessage(test::internal::kUnknownInteractionsProtocol_FlexibleTwoWay_Ordinal,
                              fidl::MessageDynamicFlags::kFlexibleMethod));
  EXPECT_EQ(expected, received.buf_excluding_txid());
  EXPECT_NE(0u, received.txid());

  auto server_reply =
      MakeMessage(test::internal::kUnknownInteractionsProtocol_FlexibleTwoWay_Ordinal,
                  fidl::MessageDynamicFlags::kFlexibleMethod, ResultUnionTag::kSuccess, 0);
  received.reply(server, server_reply);

  auto response = response_fut.get();
  ASSERT_EQ(ZX_OK, std::get<0>(response));
  EXPECT_TRUE(std::get<1>(response).is_response());
}

TEST_F(UnknownInteractions, TwoWayFlexibleSyncSendUnknownResponse) {
  auto client = SyncClient();
  auto server = TakeServerChannel();

  auto response_fut = std::async([&client]() {
    test::UnknownInteractionsProtocol_FlexibleTwoWay_Result out_result;
    auto status_code = client->FlexibleTwoWay(&out_result);
    return std::make_tuple(status_code, std::move(out_result));
  });

  auto received = TwoWayServerRequest<16>::ReadFromChannel(server);
  EXPECT_EQ(ZX_OK, received.status);
  auto expected =
      ExcludeTxid(MakeMessage(test::internal::kUnknownInteractionsProtocol_FlexibleTwoWay_Ordinal,
                              fidl::MessageDynamicFlags::kFlexibleMethod));
  EXPECT_EQ(expected, received.buf_excluding_txid());
  EXPECT_NE(0u, received.txid());

  auto server_reply =
      MakeMessage(test::internal::kUnknownInteractionsProtocol_FlexibleTwoWay_Ordinal,
                  fidl::MessageDynamicFlags::kFlexibleMethod, ResultUnionTag::kTransportError,
                  ZX_ERR_NOT_SUPPORTED);
  received.reply(server, server_reply);

  auto response = response_fut.get();
  ASSERT_EQ(ZX_OK, std::get<0>(response));
  ASSERT_TRUE(std::get<1>(response).is_transport_err());
  EXPECT_EQ(::fidl::TransportErr::kUnknownMethod, std::get<1>(response).transport_err());
}

TEST_F(UnknownInteractions, TwoWayFlexibleSyncSendOtherTransportError) {
  auto client = SyncClient();
  auto server = TakeServerChannel();

  auto response_fut = std::async([&client]() {
    test::UnknownInteractionsProtocol_FlexibleTwoWay_Result out_result;
    auto status_code = client->FlexibleTwoWay(&out_result);
    return std::make_tuple(status_code, std::move(out_result));
  });

  auto received = TwoWayServerRequest<16>::ReadFromChannel(server);
  EXPECT_EQ(ZX_OK, received.status);
  auto expected =
      ExcludeTxid(MakeMessage(test::internal::kUnknownInteractionsProtocol_FlexibleTwoWay_Ordinal,
                              fidl::MessageDynamicFlags::kFlexibleMethod));
  EXPECT_EQ(expected, received.buf_excluding_txid());
  EXPECT_NE(0u, received.txid());

  auto server_reply =
      MakeMessage(test::internal::kUnknownInteractionsProtocol_FlexibleTwoWay_Ordinal,
                  fidl::MessageDynamicFlags::kFlexibleMethod, ResultUnionTag::kTransportError,
                  ZX_ERR_ACCESS_DENIED);
  received.reply(server, server_reply);

  auto response = response_fut.get();
  EXPECT_EQ(ZX_ERR_INVALID_ARGS, std::get<0>(response));
}

TEST_F(UnknownInteractions, TwoWayFlexibleSyncSendOkTransportError) {
  auto client = SyncClient();
  auto server = TakeServerChannel();

  auto response_fut = std::async([&client]() {
    test::UnknownInteractionsProtocol_FlexibleTwoWay_Result out_result;
    auto status_code = client->FlexibleTwoWay(&out_result);
    return std::make_tuple(status_code, std::move(out_result));
  });

  auto received = TwoWayServerRequest<16>::ReadFromChannel(server);
  EXPECT_EQ(ZX_OK, received.status);
  auto expected =
      ExcludeTxid(MakeMessage(test::internal::kUnknownInteractionsProtocol_FlexibleTwoWay_Ordinal,
                              fidl::MessageDynamicFlags::kFlexibleMethod));
  EXPECT_EQ(expected, received.buf_excluding_txid());
  EXPECT_NE(0u, received.txid());

  auto server_reply = MakeMessage(
      test::internal::kUnknownInteractionsProtocol_FlexibleTwoWay_Ordinal,
      fidl::MessageDynamicFlags::kFlexibleMethod, ResultUnionTag::kTransportError, ZX_OK);
  received.reply(server, server_reply);

  auto response = response_fut.get();
  EXPECT_EQ(ZX_ERR_INVALID_ARGS, std::get<0>(response));
}

TEST_F(UnknownInteractions, TwoWayFlexibleSyncSendErrorVariant) {
  auto client = SyncClient();
  auto server = TakeServerChannel();

  auto response_fut = std::async([&client]() {
    test::UnknownInteractionsProtocol_FlexibleTwoWay_Result out_result;
    auto status_code = client->FlexibleTwoWay(&out_result);
    return std::make_tuple(status_code, std::move(out_result));
  });

  auto received = TwoWayServerRequest<16>::ReadFromChannel(server);
  EXPECT_EQ(ZX_OK, received.status);
  auto expected =
      ExcludeTxid(MakeMessage(test::internal::kUnknownInteractionsProtocol_FlexibleTwoWay_Ordinal,
                              fidl::MessageDynamicFlags::kFlexibleMethod));
  EXPECT_EQ(expected, received.buf_excluding_txid());
  EXPECT_NE(0u, received.txid());

  auto server_reply = MakeMessage(
      test::internal::kUnknownInteractionsProtocol_FlexibleTwoWay_Ordinal,
      fidl::MessageDynamicFlags::kFlexibleMethod, ResultUnionTag::kApplicationError, 0x100);
  received.reply(server, server_reply);

  auto response = response_fut.get();
  EXPECT_EQ(ZX_ERR_INVALID_ARGS, std::get<0>(response));
}

TEST_F(UnknownInteractions, TwoWayFlexibleFieldsSyncSend) {
  auto client = SyncClient();
  auto server = TakeServerChannel();

  auto response_fut = std::async([&client]() {
    test::UnknownInteractionsProtocol_FlexibleTwoWayFields_Result out_result;
    auto status_code = client->FlexibleTwoWayFields(&out_result);
    return std::make_tuple(status_code, std::move(out_result));
  });

  auto received = TwoWayServerRequest<16>::ReadFromChannel(server);
  EXPECT_EQ(ZX_OK, received.status);
  auto expected = ExcludeTxid(
      MakeMessage(test::internal::kUnknownInteractionsProtocol_FlexibleTwoWayFields_Ordinal,
                  fidl::MessageDynamicFlags::kFlexibleMethod));
  EXPECT_EQ(expected, received.buf_excluding_txid());
  EXPECT_NE(0u, received.txid());

  auto server_reply =
      MakeMessage(test::internal::kUnknownInteractionsProtocol_FlexibleTwoWayFields_Ordinal,
                  fidl::MessageDynamicFlags::kFlexibleMethod, ResultUnionTag::kSuccess, 32);
  received.reply(server, server_reply);

  auto response = response_fut.get();
  ASSERT_EQ(ZX_OK, std::get<0>(response));
  ASSERT_TRUE(std::get<1>(response).is_response());
  EXPECT_EQ(32, std::get<1>(response).response().some_field);
}

TEST_F(UnknownInteractions, TwoWayFlexibleFieldsSyncSendUnknownResponse) {
  auto client = SyncClient();
  auto server = TakeServerChannel();

  auto response_fut = std::async([&client]() {
    test::UnknownInteractionsProtocol_FlexibleTwoWayFields_Result out_result;
    auto status_code = client->FlexibleTwoWayFields(&out_result);
    return std::make_tuple(status_code, std::move(out_result));
  });

  auto received = TwoWayServerRequest<16>::ReadFromChannel(server);
  EXPECT_EQ(ZX_OK, received.status);
  auto expected = ExcludeTxid(
      MakeMessage(test::internal::kUnknownInteractionsProtocol_FlexibleTwoWayFields_Ordinal,
                  fidl::MessageDynamicFlags::kFlexibleMethod));
  EXPECT_EQ(expected, received.buf_excluding_txid());
  EXPECT_NE(0u, received.txid());

  auto server_reply =
      MakeMessage(test::internal::kUnknownInteractionsProtocol_FlexibleTwoWayFields_Ordinal,
                  fidl::MessageDynamicFlags::kFlexibleMethod, ResultUnionTag::kTransportError,
                  ZX_ERR_NOT_SUPPORTED);
  received.reply(server, server_reply);

  auto response = response_fut.get();
  ASSERT_EQ(ZX_OK, std::get<0>(response));
  ASSERT_TRUE(std::get<1>(response).is_transport_err());
  EXPECT_EQ(::fidl::TransportErr::kUnknownMethod, std::get<1>(response).transport_err());
}

TEST_F(UnknownInteractions, TwoWayFlexibleErrSyncSend) {
  auto client = SyncClient();
  auto server = TakeServerChannel();

  auto response_fut = std::async([&client]() {
    test::UnknownInteractionsProtocol_FlexibleTwoWayErr_Result out_result;
    auto status_code = client->FlexibleTwoWayErr(&out_result);
    return std::make_tuple(status_code, std::move(out_result));
  });

  auto received = TwoWayServerRequest<16>::ReadFromChannel(server);
  EXPECT_EQ(ZX_OK, received.status);
  auto expected = ExcludeTxid(
      MakeMessage(test::internal::kUnknownInteractionsProtocol_FlexibleTwoWayErr_Ordinal,
                  fidl::MessageDynamicFlags::kFlexibleMethod));
  EXPECT_EQ(expected, received.buf_excluding_txid());
  EXPECT_NE(0u, received.txid());

  auto server_reply =
      MakeMessage(test::internal::kUnknownInteractionsProtocol_FlexibleTwoWayErr_Ordinal,
                  fidl::MessageDynamicFlags::kFlexibleMethod, ResultUnionTag::kSuccess, 0);
  received.reply(server, server_reply);

  auto response = response_fut.get();
  ASSERT_EQ(ZX_OK, std::get<0>(response));
  EXPECT_TRUE(std::get<1>(response).is_response());
}

TEST_F(UnknownInteractions, TwoWayFlexibleErrSyncSendUnknownResponse) {
  auto client = SyncClient();
  auto server = TakeServerChannel();

  auto response_fut = std::async([&client]() {
    test::UnknownInteractionsProtocol_FlexibleTwoWayErr_Result out_result;
    auto status_code = client->FlexibleTwoWayErr(&out_result);
    return std::make_tuple(status_code, std::move(out_result));
  });

  auto received = TwoWayServerRequest<16>::ReadFromChannel(server);
  EXPECT_EQ(ZX_OK, received.status);
  auto expected = ExcludeTxid(
      MakeMessage(test::internal::kUnknownInteractionsProtocol_FlexibleTwoWayErr_Ordinal,
                  fidl::MessageDynamicFlags::kFlexibleMethod));
  EXPECT_EQ(expected, received.buf_excluding_txid());
  EXPECT_NE(0u, received.txid());

  auto server_reply =
      MakeMessage(test::internal::kUnknownInteractionsProtocol_FlexibleTwoWayErr_Ordinal,
                  fidl::MessageDynamicFlags::kFlexibleMethod, ResultUnionTag::kTransportError,
                  ZX_ERR_NOT_SUPPORTED);
  received.reply(server, server_reply);

  auto response = response_fut.get();
  ASSERT_EQ(ZX_OK, std::get<0>(response));
  ASSERT_TRUE(std::get<1>(response).is_transport_err());
  EXPECT_EQ(::fidl::TransportErr::kUnknownMethod, std::get<1>(response).transport_err());
}

TEST_F(UnknownInteractions, TwoWayFlexibleErrSyncSendOtherTransportError) {
  auto client = SyncClient();
  auto server = TakeServerChannel();

  auto response_fut = std::async([&client]() {
    test::UnknownInteractionsProtocol_FlexibleTwoWayErr_Result out_result;
    auto status_code = client->FlexibleTwoWayErr(&out_result);
    return std::make_tuple(status_code, std::move(out_result));
  });

  auto received = TwoWayServerRequest<16>::ReadFromChannel(server);
  EXPECT_EQ(ZX_OK, received.status);
  auto expected = ExcludeTxid(
      MakeMessage(test::internal::kUnknownInteractionsProtocol_FlexibleTwoWayErr_Ordinal,
                  fidl::MessageDynamicFlags::kFlexibleMethod));
  EXPECT_EQ(expected, received.buf_excluding_txid());
  EXPECT_NE(0u, received.txid());

  auto server_reply =
      MakeMessage(test::internal::kUnknownInteractionsProtocol_FlexibleTwoWayErr_Ordinal,
                  fidl::MessageDynamicFlags::kFlexibleMethod, ResultUnionTag::kTransportError,
                  ZX_ERR_ACCESS_DENIED);
  received.reply(server, server_reply);

  auto response = response_fut.get();
  EXPECT_EQ(ZX_ERR_INVALID_ARGS, std::get<0>(response));
}

TEST_F(UnknownInteractions, TwoWayFlexibleErrSyncSendErrorVariant) {
  auto client = SyncClient();
  auto server = TakeServerChannel();

  auto response_fut = std::async([&client]() {
    test::UnknownInteractionsProtocol_FlexibleTwoWayErr_Result out_result;
    auto status_code = client->FlexibleTwoWayErr(&out_result);
    return std::make_tuple(status_code, std::move(out_result));
  });

  auto received = TwoWayServerRequest<16>::ReadFromChannel(server);
  EXPECT_EQ(ZX_OK, received.status);
  auto expected = ExcludeTxid(
      MakeMessage(test::internal::kUnknownInteractionsProtocol_FlexibleTwoWayErr_Ordinal,
                  fidl::MessageDynamicFlags::kFlexibleMethod));
  EXPECT_EQ(expected, received.buf_excluding_txid());
  EXPECT_NE(0u, received.txid());

  auto server_reply = MakeMessage(
      test::internal::kUnknownInteractionsProtocol_FlexibleTwoWayErr_Ordinal,
      fidl::MessageDynamicFlags::kFlexibleMethod, ResultUnionTag::kApplicationError, 0x100);
  received.reply(server, server_reply);

  auto response = response_fut.get();
  ASSERT_EQ(ZX_OK, std::get<0>(response));
  ASSERT_TRUE(std::get<1>(response).is_err());
  EXPECT_EQ(0x100, std::get<1>(response).err());
}

TEST_F(UnknownInteractions, TwoWayFlexibleFieldsErrSyncSend) {
  auto client = SyncClient();
  auto server = TakeServerChannel();

  auto response_fut = std::async([&client]() {
    test::UnknownInteractionsProtocol_FlexibleTwoWayFieldsErr_Result out_result;
    auto status_code = client->FlexibleTwoWayFieldsErr(&out_result);
    return std::make_tuple(status_code, std::move(out_result));
  });

  auto received = TwoWayServerRequest<16>::ReadFromChannel(server);
  EXPECT_EQ(ZX_OK, received.status);
  auto expected = ExcludeTxid(
      MakeMessage(test::internal::kUnknownInteractionsProtocol_FlexibleTwoWayFieldsErr_Ordinal,
                  fidl::MessageDynamicFlags::kFlexibleMethod));
  EXPECT_EQ(expected, received.buf_excluding_txid());
  EXPECT_NE(0u, received.txid());

  auto server_reply =
      MakeMessage(test::internal::kUnknownInteractionsProtocol_FlexibleTwoWayFieldsErr_Ordinal,
                  fidl::MessageDynamicFlags::kFlexibleMethod, ResultUnionTag::kSuccess, 32);
  received.reply(server, server_reply);

  auto response = response_fut.get();
  ASSERT_EQ(ZX_OK, std::get<0>(response));
  ASSERT_TRUE(std::get<1>(response).is_response());
  EXPECT_EQ(32, std::get<1>(response).response().some_field);
}

TEST_F(UnknownInteractions, TwoWayFlexibleFieldsErrSyncSendUnknownResponse) {
  auto client = SyncClient();
  auto server = TakeServerChannel();

  auto response_fut = std::async([&client]() {
    test::UnknownInteractionsProtocol_FlexibleTwoWayFieldsErr_Result out_result;
    auto status_code = client->FlexibleTwoWayFieldsErr(&out_result);
    return std::make_tuple(status_code, std::move(out_result));
  });

  auto received = TwoWayServerRequest<16>::ReadFromChannel(server);
  EXPECT_EQ(ZX_OK, received.status);
  auto expected = ExcludeTxid(
      MakeMessage(test::internal::kUnknownInteractionsProtocol_FlexibleTwoWayFieldsErr_Ordinal,
                  fidl::MessageDynamicFlags::kFlexibleMethod));
  EXPECT_EQ(expected, received.buf_excluding_txid());
  EXPECT_NE(0u, received.txid());

  auto server_reply =
      MakeMessage(test::internal::kUnknownInteractionsProtocol_FlexibleTwoWayFieldsErr_Ordinal,
                  fidl::MessageDynamicFlags::kFlexibleMethod, ResultUnionTag::kTransportError,
                  ZX_ERR_NOT_SUPPORTED);
  received.reply(server, server_reply);

  auto response = response_fut.get();
  ASSERT_EQ(ZX_OK, std::get<0>(response));
  ASSERT_TRUE(std::get<1>(response).is_transport_err());
  EXPECT_EQ(::fidl::TransportErr::kUnknownMethod, std::get<1>(response).transport_err());
}

TEST_F(UnknownInteractions, TwoWayFlexibleFieldsErrSyncSendErrorVariant) {
  auto client = SyncClient();
  auto server = TakeServerChannel();

  auto response_fut = std::async([&client]() {
    test::UnknownInteractionsProtocol_FlexibleTwoWayFieldsErr_Result out_result;
    auto status_code = client->FlexibleTwoWayFieldsErr(&out_result);
    return std::make_tuple(status_code, std::move(out_result));
  });

  auto received = TwoWayServerRequest<16>::ReadFromChannel(server);
  EXPECT_EQ(ZX_OK, received.status);
  auto expected = ExcludeTxid(
      MakeMessage(test::internal::kUnknownInteractionsProtocol_FlexibleTwoWayFieldsErr_Ordinal,
                  fidl::MessageDynamicFlags::kFlexibleMethod));
  EXPECT_EQ(expected, received.buf_excluding_txid());
  EXPECT_NE(0u, received.txid());

  auto server_reply = MakeMessage(
      test::internal::kUnknownInteractionsProtocol_FlexibleTwoWayFieldsErr_Ordinal,
      fidl::MessageDynamicFlags::kFlexibleMethod, ResultUnionTag::kApplicationError, 0x100);
  received.reply(server, server_reply);

  auto response = response_fut.get();
  ASSERT_EQ(ZX_OK, std::get<0>(response));
  ASSERT_TRUE(std::get<1>(response).is_err());
  EXPECT_EQ(0x100, std::get<1>(response).err());
}
}  // namespace
