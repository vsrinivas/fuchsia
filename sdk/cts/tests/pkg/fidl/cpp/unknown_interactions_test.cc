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

  fidl::InterfacePtr<test::UnknownInteractionsProtocol> AsyncClient() {
    EXPECT_TRUE(client_end_.is_valid());
    fidl::InterfacePtr<test::UnknownInteractionsProtocol> client;
    client.Bind(std::move(client_end_), loop_->dispatcher());
    return client;
  }

 private:
  std::optional<async::Loop> loop_;
  zx::channel client_end_;
  zx::channel server_end_;
};

constexpr std::array<uint8_t, 4> zero_txid = {0, 0, 0, 0};

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
  EXPECT_EQ(client->StrictOneWay(), ZX_OK);

  ReadResult<16> received(server);
  EXPECT_EQ(received.status, ZX_OK);
  EXPECT_EQ(received.num_bytes, 16u);
  EXPECT_EQ(received.num_handles, 0u);

  std::array<uint8_t, 16> expected{
      0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x01,  //
      0xd5, 0x82, 0xb3, 0x4c, 0x50, 0x81, 0xa5, 0x1f,  //
  };
  EXPECT_EQ(received.buf, expected);
}

TEST_F(UnknownInteractions, OneWayFlexibleSyncSend) {
  auto client = SyncClient();
  auto server = TakeServerChannel();
  EXPECT_EQ(client->FlexibleOneWay(), ZX_OK);

  ReadResult<16> received(server);
  EXPECT_EQ(received.status, ZX_OK);
  EXPECT_EQ(received.num_bytes, 16u);
  EXPECT_EQ(received.num_handles, 0u);

  std::array<uint8_t, 16> expected{
      0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x80, 0x01,  //
      0xfc, 0x90, 0xbb, 0xe2, 0x7a, 0x27, 0x93, 0x27,  //
  };
  EXPECT_EQ(received.buf, expected);
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

  EXPECT_EQ(client->StrictTwoWay(), ZX_OK);

  auto received = server.get();
  EXPECT_EQ(received.status, ZX_OK);
  EXPECT_EQ(received.num_bytes, 16u);
  EXPECT_EQ(received.num_handles, 0u);
  EXPECT_EQ(received.reply_status, ZX_OK);

  std::array<uint8_t, 12> expected{
      0x02, 0x00, 0x00, 0x01,                          //
      0xdc, 0xb0, 0x55, 0x70, 0x95, 0x6f, 0xba, 0x73,  //
  };
  EXPECT_EQ(received.buf_excluding_txid(), expected);
  EXPECT_NE(received.buf_txid(), zero_txid);
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

  test::UnknownInteractionsProtocol_StrictTwoWayErr_Result result;
  EXPECT_EQ(client->StrictTwoWayErr(&result), ZX_OK);

  auto received = server.get();
  EXPECT_EQ(received.status, ZX_OK);
  EXPECT_EQ(received.num_bytes, 16u);
  EXPECT_EQ(received.num_handles, 0u);
  EXPECT_EQ(received.reply_status, ZX_OK);

  std::array<uint8_t, 12> expected{
      0x02, 0x00, 0x00, 0x01,                          //
      0xbb, 0x58, 0xe0, 0x08, 0x4e, 0xeb, 0x9b, 0x2e,  //
  };
  EXPECT_EQ(received.buf_excluding_txid(), expected);
  EXPECT_NE(received.buf_txid(), zero_txid);
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

  test::UnknownInteractionsProtocol_FlexibleTwoWay_Result result;
  EXPECT_EQ(client->FlexibleTwoWay(&result), ZX_OK);

  auto received = server.get();
  EXPECT_EQ(received.status, ZX_OK);
  EXPECT_EQ(received.num_bytes, 16u);
  EXPECT_EQ(received.num_handles, 0u);
  EXPECT_EQ(received.reply_status, ZX_OK);

  std::array<uint8_t, 12> expected{
      0x02, 0x00, 0x80, 0x01,                          //
      0x9d, 0x60, 0x95, 0x03, 0x7a, 0x51, 0x33, 0x1f,  //

  };
  EXPECT_EQ(received.buf_excluding_txid(), expected);
  EXPECT_NE(received.buf_txid(), zero_txid);
}

TEST_F(UnknownInteractions, OneWayStrictAsyncSend) {
  auto client = AsyncClient();
  auto server = TakeServerChannel();
  client.set_error_handler(
      [](zx_status_t status) { ADD_FAILURE("Got an error status: %d", status); });
  client->StrictOneWay();

  ReadResult<16> received(server);
  EXPECT_EQ(received.status, ZX_OK);
  EXPECT_EQ(received.num_bytes, 16u);
  EXPECT_EQ(received.num_handles, 0u);

  std::array<uint8_t, 16> expected{
      0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x01,  //
      0xd5, 0x82, 0xb3, 0x4c, 0x50, 0x81, 0xa5, 0x1f,  //
  };
  EXPECT_EQ(received.buf, expected);
}

TEST_F(UnknownInteractions, OneWayFlexibleAsyncSend) {
  auto client = AsyncClient();
  auto server = TakeServerChannel();
  client.set_error_handler(
      [](zx_status_t status) { ADD_FAILURE("Got an error status: %d", status); });
  client->FlexibleOneWay();

  ReadResult<16> received(server);
  EXPECT_EQ(received.status, ZX_OK);
  EXPECT_EQ(received.num_bytes, 16u);
  EXPECT_EQ(received.num_handles, 0u);

  std::array<uint8_t, 16> expected{
      0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x80, 0x01,  //
      0xfc, 0x90, 0xbb, 0xe2, 0x7a, 0x27, 0x93, 0x27,  //
  };
  EXPECT_EQ(received.buf, expected);
}

TEST_F(UnknownInteractions, TwoWayStrictAsyncSend) {
  auto client = AsyncClient();
  auto server = TakeServerChannel();
  client.set_error_handler(
      [](zx_status_t status) { ADD_FAILURE("Got an error status: %d", status); });

  bool response_received = false;
  client->StrictTwoWay([&response_received]() { response_received = true; });

  TwoWayServerResult<16> received(server);
  EXPECT_EQ(received.status, ZX_OK);
  EXPECT_EQ(received.num_bytes, 16u);
  EXPECT_EQ(received.num_handles, 0u);
  std::array<uint8_t, 12> expected{
      0x02, 0x00, 0x00, 0x01,                          //
      0xdc, 0xb0, 0x55, 0x70, 0x95, 0x6f, 0xba, 0x73,  //
  };
  EXPECT_EQ(received.buf_excluding_txid(), expected);
  EXPECT_NE(received.buf_txid(), zero_txid);

  received.reply<16>(server, {
                                 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x01,  //
                                 0xdc, 0xb0, 0x55, 0x70, 0x95, 0x6f, 0xba, 0x73,  //
                             });
  EXPECT_EQ(received.reply_status, ZX_OK);

  loop().RunUntilIdle();
  EXPECT_TRUE(response_received);
}

TEST_F(UnknownInteractions, TwoWayStrictErrAsyncSend) {
  auto client = AsyncClient();
  auto server = TakeServerChannel();
  client.set_error_handler(
      [](zx_status_t status) { ADD_FAILURE("Got an error status: %d", status); });

  bool response_received = false;
  client->StrictTwoWayErr([&response_received](auto response) {
    response_received = true;
    EXPECT_TRUE(response.is_response());
  });

  TwoWayServerResult<16> received(server);
  EXPECT_EQ(received.status, ZX_OK);
  EXPECT_EQ(received.num_bytes, 16u);
  EXPECT_EQ(received.num_handles, 0u);
  std::array<uint8_t, 12> expected{
      0x02, 0x00, 0x00, 0x01,                          //
      0xbb, 0x58, 0xe0, 0x08, 0x4e, 0xeb, 0x9b, 0x2e,  //
  };
  EXPECT_EQ(received.buf_excluding_txid(), expected);
  EXPECT_NE(received.buf_txid(), zero_txid);

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
  EXPECT_TRUE(response_received);
}

TEST_F(UnknownInteractions, TwoWayFlexibleAsyncSend) {
  auto client = AsyncClient();
  auto server = TakeServerChannel();
  client.set_error_handler(
      [](zx_status_t status) { ADD_FAILURE("Got an error status: %d", status); });

  bool response_received = false;
  client->FlexibleTwoWay([&response_received](auto response) { response_received = true; });

  TwoWayServerResult<16> received(server);
  EXPECT_EQ(received.status, ZX_OK);
  EXPECT_EQ(received.num_bytes, 16u);
  EXPECT_EQ(received.num_handles, 0u);
  std::array<uint8_t, 12> expected{
      0x02, 0x00, 0x80, 0x01,                          //
      0x9d, 0x60, 0x95, 0x03, 0x7a, 0x51, 0x33, 0x1f,  //

  };
  EXPECT_EQ(received.buf_excluding_txid(), expected);
  EXPECT_NE(received.buf_txid(), zero_txid);

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
  EXPECT_TRUE(response_received);
}

TEST_F(UnknownInteractions, SendStrictEvent) {
  auto client = TakeClientChannel();

  UnknownInteractionsImpl impl;
  fidl::Binding<test::UnknownInteractionsProtocol> binding(&impl);
  auto& event_sender = binding.events();
  binding.set_error_handler(
      [](zx_status_t status) { ADD_FAILURE("Got an error status: %d", status); });
  binding.Bind(TakeServerEnd());

  event_sender.StrictEvent();

  ReadResult<16> received(client);
  EXPECT_EQ(received.status, ZX_OK);
  EXPECT_EQ(received.num_bytes, 16u);
  EXPECT_EQ(received.num_handles, 0u);

  std::array<uint8_t, 16> expected{
      0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x01,  //
      0x38, 0x27, 0xa3, 0x91, 0x98, 0x41, 0x4b, 0x58,  //

  };
  EXPECT_EQ(received.buf, expected);
}

TEST_F(UnknownInteractions, SendFlexibleEvent) {
  auto client = TakeClientChannel();

  UnknownInteractionsImpl impl;
  fidl::Binding<test::UnknownInteractionsProtocol> binding(&impl);
  auto& event_sender = binding.events();
  binding.set_error_handler(
      [](zx_status_t status) { ADD_FAILURE("Got an error status: %d", status); });
  binding.Bind(TakeServerEnd());

  event_sender.FlexibleEvent();

  ReadResult<16> received(client);
  EXPECT_EQ(received.status, ZX_OK);
  EXPECT_EQ(received.num_bytes, 16u);
  EXPECT_EQ(received.num_handles, 0u);

  std::array<uint8_t, 16> expected{
      0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x80, 0x01,  //
      0x6c, 0x2c, 0x80, 0x0b, 0x8e, 0x1a, 0x7a, 0x31,  //
  };
  EXPECT_EQ(received.buf, expected);
}
}  // namespace
