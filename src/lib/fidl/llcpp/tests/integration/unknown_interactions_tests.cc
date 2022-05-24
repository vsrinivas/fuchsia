// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/test.unknown.interactions/cpp/wire.h>
#include <fidl/test.unknown.interactions/cpp/wire_test_base.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fidl/llcpp/client.h>
#include <lib/fidl/llcpp/server.h>
#include <lib/zx/time.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>

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

  fidl::WireSyncClient<test::UnknownInteractionsProtocol> SyncClient() {
    EXPECT_TRUE(client_end_.is_valid());
    return fidl::WireSyncClient<test::UnknownInteractionsProtocol>(std::move(client_end_));
  }

  fidl::WireClient<test::UnknownInteractionsProtocol> AsyncClient(
      fidl::WireAsyncEventHandler<test::UnknownInteractionsProtocol>* handler = nullptr) {
    EXPECT_TRUE(client_end_.is_valid());
    return fidl::WireClient<test::UnknownInteractionsProtocol>(std::move(client_end_),
                                                               loop_->dispatcher(), handler);
  }

 private:
  std::optional<async::Loop> loop_;
  fidl::ClientEnd<test::UnknownInteractionsProtocol> client_end_;
  fidl::ServerEnd<test::UnknownInteractionsProtocol> server_end_;
};

constexpr std::array<uint8_t, 4> zero_txid = {0, 0, 0, 0};

MATCHER(NonZeroTxId, "") {
  return ::testing::ExplainMatchResult(::testing::Not(::testing::ContainerEq(zero_txid)), arg,
                                       result_listener);
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
  // Construct a ReadResult by reading from a channel.
  explicit ReadResult(const zx::channel& channel) {
    status = channel.wait_one(ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED, zx::time::infinite(),
                              nullptr);
    if (status != ZX_OK)
      return;
    status = channel.read(/* flags= */ 0, buf.data(), /* handles= */ nullptr, N,
                          /* num_handles= */ 0, &num_bytes, &num_handles);
  }

  // Get the contents of the buffer excluding the transaction ID.
  std::array<uint8_t, N - 4> buf_excluding_txid() {
    std::array<uint8_t, N - 4> without_txid;
    std::copy(buf.begin() + 4, buf.end(), without_txid.begin());
    return without_txid;
  }

  // Get the transaction id portion of the buffer.
  std::array<uint8_t, 4> buf_txid() {
    std::array<uint8_t, 4> txid;
    std::copy(buf.begin(), buf.begin() + 4, txid.begin());
    return txid;
  }
};

template <uint32_t N>
struct TwoWayServerResult : public ReadResult<N> {
  // Status from sending a reply.
  zx_status_t reply_status;

  TwoWayServerResult() = delete;
  using ReadResult<N>::ReadResult;

  // Helper to send a reply to the read as a two-way message.
  // Copies the txid (first four) bytes from |buf| into |reply_bytes| and sends
  // the result on the channel, storing the status in |reply_status|.
  template <uint32_t M>
  void reply(const zx::channel& channel, std::array<uint8_t, M> reply_bytes) {
    std::copy(this->buf.begin(), this->buf.begin() + 4, reply_bytes.begin());
    reply_status = channel.write(/* flags= */ 0, reply_bytes.data(), M, /* handles= */ nullptr,
                                 /* num_handles= */ 0);
  }
};

TEST_F(UnknownInteractions, OneWayStrictSyncSend) {
  auto client = SyncClient();
  auto server = TakeServerChannel();
  auto result = client->StrictOneWay();
  EXPECT_EQ(result.status(), ZX_OK);

  ReadResult<16> received(server);
  EXPECT_EQ(received.status, ZX_OK);
  EXPECT_EQ(received.num_bytes, 16u);
  EXPECT_EQ(received.num_handles, 0u);

  std::array<uint8_t, 16> expected{
      0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x01,  //
      0xd5, 0x82, 0xb3, 0x4c, 0x50, 0x81, 0xa5, 0x1f,  //
  };
  EXPECT_THAT(received.buf, ::testing::ContainerEq(expected));
}

TEST_F(UnknownInteractions, OneWayFlexibleSyncSend) {
  auto client = SyncClient();
  auto server = TakeServerChannel();
  auto result = client->FlexibleOneWay();
  EXPECT_EQ(result.status(), ZX_OK);

  ReadResult<16> received(server);
  EXPECT_EQ(received.status, ZX_OK);
  EXPECT_EQ(received.num_bytes, 16u);
  EXPECT_EQ(received.num_handles, 0u);

  std::array<uint8_t, 16> expected{
      0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x80, 0x01,  //
      0xfc, 0x90, 0xbb, 0xe2, 0x7a, 0x27, 0x93, 0x27,  //
  };
  EXPECT_THAT(received.buf, ::testing::ContainerEq(expected));
}

TEST_F(UnknownInteractions, TwoWayStrictSyncSend) {
  auto client = SyncClient();
  auto server = std::async(std::launch::async, [server = TakeServerChannel()]() {
    TwoWayServerResult<16> result(server);
    result.reply<16>(server, {
                                 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x01,  //
                                 0xdc, 0xb0, 0x55, 0x70, 0x95, 0x6f, 0xba, 0x73,  //
                             });
    return result;
  });

  auto result = client->StrictTwoWay();
  EXPECT_EQ(result.status(), ZX_OK);

  auto received = server.get();
  EXPECT_EQ(received.status, ZX_OK);
  EXPECT_EQ(received.num_bytes, 16u);
  EXPECT_EQ(received.num_handles, 0u);
  EXPECT_EQ(received.reply_status, ZX_OK);

  std::array<uint8_t, 12> expected{
      0x02, 0x00, 0x00, 0x01,                          //
      0xdc, 0xb0, 0x55, 0x70, 0x95, 0x6f, 0xba, 0x73,  //
  };
  EXPECT_THAT(received.buf_excluding_txid(), ::testing::ContainerEq(expected));
  EXPECT_THAT(received.buf_txid(), NonZeroTxId());
}

TEST_F(UnknownInteractions, TwoWayStrictErrSyncSend) {
  auto client = SyncClient();
  auto server = std::async(std::launch::async, [server = TakeServerChannel()]() {
    TwoWayServerResult<16> result(server);
    result.reply<32>(server, {
                                 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x01,  //
                                 0xbb, 0x58, 0xe0, 0x08, 0x4e, 0xeb, 0x9b, 0x2e,  //
                                 // Result union with success envelope to satisfy client side:
                                 // ordinal  ---------------------------------|
                                 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  //
                                 // inline value -----|  nhandles |  flags ---|
                                 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00,  //

                             });
    return result;
  });

  auto result = client->StrictTwoWayErr();
  EXPECT_EQ(result.status(), ZX_OK);

  auto received = server.get();
  EXPECT_EQ(received.status, ZX_OK);
  EXPECT_EQ(received.num_bytes, 16u);
  EXPECT_EQ(received.num_handles, 0u);
  EXPECT_EQ(received.reply_status, ZX_OK);

  std::array<uint8_t, 12> expected{
      0x02, 0x00, 0x00, 0x01,                          //
      0xbb, 0x58, 0xe0, 0x08, 0x4e, 0xeb, 0x9b, 0x2e,  //
  };
  EXPECT_THAT(received.buf_excluding_txid(), ::testing::ContainerEq(expected));
  EXPECT_THAT(received.buf_txid(), NonZeroTxId());
}

TEST_F(UnknownInteractions, TwoWayFlexibleSyncSend) {
  auto client = SyncClient();
  auto server = std::async(std::launch::async, [server = TakeServerChannel()]() {
    TwoWayServerResult<16> result(server);
    result.reply<32>(server, {
                                 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x80, 0x01,  //
                                 0x9d, 0x60, 0x95, 0x03, 0x7a, 0x51, 0x33, 0x1f,  //
                                 // Result union with success envelope to satisfy client side:
                                 // ordinal  ---------------------------------|
                                 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  //
                                 // inline value -----|  nhandles |  flags ---|
                                 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00,  //

                             });
    return result;
  });

  auto result = client->FlexibleTwoWay();
  EXPECT_EQ(result.status(), ZX_OK);

  auto received = server.get();
  EXPECT_EQ(received.status, ZX_OK);
  EXPECT_EQ(received.num_bytes, 16u);
  EXPECT_EQ(received.num_handles, 0u);
  EXPECT_EQ(received.reply_status, ZX_OK);

  std::array<uint8_t, 12> expected{
      0x02, 0x00, 0x80, 0x01,                          //
      0x9d, 0x60, 0x95, 0x03, 0x7a, 0x51, 0x33, 0x1f,  //

  };
  EXPECT_THAT(received.buf_excluding_txid(), ::testing::ContainerEq(expected));
  EXPECT_THAT(received.buf_txid(), NonZeroTxId());
}

TEST_F(UnknownInteractions, OneWayStrictAsyncSend) {
  auto client = AsyncClient();
  auto server = TakeServerChannel();
  auto result = client->StrictOneWay();
  EXPECT_EQ(result.status(), ZX_OK);

  ReadResult<16> received(server);
  EXPECT_EQ(received.status, ZX_OK);
  EXPECT_EQ(received.num_bytes, 16u);
  EXPECT_EQ(received.num_handles, 0u);

  std::array<uint8_t, 16> expected{
      0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x01,  //
      0xd5, 0x82, 0xb3, 0x4c, 0x50, 0x81, 0xa5, 0x1f,  //
  };
  EXPECT_THAT(received.buf, ::testing::ContainerEq(expected));
}

TEST_F(UnknownInteractions, OneWayFlexibleAsyncSend) {
  auto client = AsyncClient();
  auto server = TakeServerChannel();
  auto result = client->FlexibleOneWay();
  EXPECT_EQ(result.status(), ZX_OK);

  ReadResult<16> received(server);
  EXPECT_EQ(received.status, ZX_OK);
  EXPECT_EQ(received.num_bytes, 16u);
  EXPECT_EQ(received.num_handles, 0u);

  std::array<uint8_t, 16> expected{
      0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x80, 0x01,  //
      0xfc, 0x90, 0xbb, 0xe2, 0x7a, 0x27, 0x93, 0x27,  //
  };
  EXPECT_THAT(received.buf, ::testing::ContainerEq(expected));
}

TEST_F(UnknownInteractions, TwoWayStrictAsyncSend) {
  auto client = AsyncClient();
  auto server = TakeServerChannel();

  client->StrictTwoWay().Then([](auto& response) { EXPECT_TRUE(response.ok()); });

  TwoWayServerResult<16> received(server);
  EXPECT_EQ(received.status, ZX_OK);
  EXPECT_EQ(received.num_bytes, 16u);
  EXPECT_EQ(received.num_handles, 0u);
  std::array<uint8_t, 12> expected{
      0x02, 0x00, 0x00, 0x01,                          //
      0xdc, 0xb0, 0x55, 0x70, 0x95, 0x6f, 0xba, 0x73,  //
  };
  EXPECT_THAT(received.buf_excluding_txid(), ::testing::ContainerEq(expected));
  EXPECT_THAT(received.buf_txid(), NonZeroTxId());

  received.reply<16>(server, {
                                 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x01,  //
                                 0xdc, 0xb0, 0x55, 0x70, 0x95, 0x6f, 0xba, 0x73,  //
                             });
  EXPECT_EQ(received.reply_status, ZX_OK);

  loop().RunUntilIdle();
}

TEST_F(UnknownInteractions, TwoWayStrictErrAsyncSend) {
  auto client = AsyncClient();
  auto server = TakeServerChannel();

  client->StrictTwoWayErr().Then([](auto& response) {
    ASSERT_TRUE(response.ok());
    EXPECT_TRUE(response.value_NEW().is_ok());
  });

  TwoWayServerResult<16> received(server);
  EXPECT_EQ(received.status, ZX_OK);
  EXPECT_EQ(received.num_bytes, 16u);
  EXPECT_EQ(received.num_handles, 0u);
  std::array<uint8_t, 12> expected{
      0x02, 0x00, 0x00, 0x01,                          //
      0xbb, 0x58, 0xe0, 0x08, 0x4e, 0xeb, 0x9b, 0x2e,  //
  };
  EXPECT_THAT(received.buf_excluding_txid(), ::testing::ContainerEq(expected));
  EXPECT_THAT(received.buf_txid(), NonZeroTxId());

  received.reply<32>(server, {
                                 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x01,  //
                                 0xbb, 0x58, 0xe0, 0x08, 0x4e, 0xeb, 0x9b, 0x2e,  //
                                 // Result union with success envelope to satisfy client side:
                                 // ordinal  ---------------------------------|
                                 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  //
                                 // inline value -----|  nhandles |  flags ---|
                                 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00,  //

                             });
  EXPECT_EQ(received.reply_status, ZX_OK);

  loop().RunUntilIdle();
}

TEST_F(UnknownInteractions, TwoWayFlexibleAsyncSend) {
  auto client = AsyncClient();
  auto server = TakeServerChannel();

  client->FlexibleTwoWay().Then([](auto& response) { EXPECT_TRUE(response.ok()); });

  TwoWayServerResult<16> received(server);
  EXPECT_EQ(received.status, ZX_OK);
  EXPECT_EQ(received.num_bytes, 16u);
  EXPECT_EQ(received.num_handles, 0u);
  std::array<uint8_t, 12> expected{
      0x02, 0x00, 0x80, 0x01,                          //
      0x9d, 0x60, 0x95, 0x03, 0x7a, 0x51, 0x33, 0x1f,  //

  };
  EXPECT_THAT(received.buf_excluding_txid(), ::testing::ContainerEq(expected));
  EXPECT_THAT(received.buf_txid(), NonZeroTxId());

  received.reply<32>(server, {
                                 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x80, 0x01,  //
                                 0x9d, 0x60, 0x95, 0x03, 0x7a, 0x51, 0x33, 0x1f,  //
                                 // Result union with success envelope to satisfy client side:
                                 // ordinal  ---------------------------------|
                                 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  //
                                 // inline value -----|  nhandles |  flags ---|
                                 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00,  //

                             });
  EXPECT_EQ(received.reply_status, ZX_OK);

  loop().RunUntilIdle();
}

TEST_F(UnknownInteractions, SendStrictEvent) {
  auto client = TakeClientChannel();
  auto server = TakeServerEnd();

  EXPECT_TRUE(fidl::WireSendEvent(server)->StrictEvent().ok());

  ReadResult<16> received(client);
  EXPECT_EQ(received.status, ZX_OK);
  EXPECT_EQ(received.num_bytes, 16u);
  EXPECT_EQ(received.num_handles, 0u);

  std::array<uint8_t, 16> expected{
      0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x01,  //
      0x38, 0x27, 0xa3, 0x91, 0x98, 0x41, 0x4b, 0x58,  //

  };
  EXPECT_THAT(received.buf, ::testing::ContainerEq(expected));
}

TEST_F(UnknownInteractions, SendFlexibleEvent) {
  auto client = TakeClientChannel();
  auto server = TakeServerEnd();

  EXPECT_TRUE(fidl::WireSendEvent(server)->FlexibleEvent().ok());

  ReadResult<16> received(client);
  EXPECT_EQ(received.status, ZX_OK);
  EXPECT_EQ(received.num_bytes, 16u);
  EXPECT_EQ(received.num_handles, 0u);

  std::array<uint8_t, 16> expected{
      0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x80, 0x01,  //
      0x6c, 0x2c, 0x80, 0x0b, 0x8e, 0x1a, 0x7a, 0x31,  //
  };
  EXPECT_THAT(received.buf, ::testing::ContainerEq(expected));
}
}  // namespace
