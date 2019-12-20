// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/wait.h>
#include <lib/fidl-async/cpp/bind.h>
#include <lib/fidl/cpp/message_part.h>
#include <lib/fidl/llcpp/array.h>
#include <lib/fidl/llcpp/sync_call.h>
#include <lib/fidl/runtime_flag.h>
#include <lib/fidl/txn_header.h>
#include <zircon/errors.h>
#include <zircon/fidl.h>
#include <zircon/status.h>
#include <zircon/types.h>

#include <thread>
#include <vector>

#include <example/llcpp/fidl.h>
#include <transformerintegration/test/llcpp/fidl.h>

#include "garnet/public/lib/fidl/llcpp/test_utils.h"
#include "gtest/gtest.h"

namespace test = ::llcpp::transformerintegration::test;

namespace {

// The v1 version of |example/Sandwich4|.
// This excerpt of bytes is taken directly from zircon/system/utest/fidl/transformer_tests.cc.
constexpr uint8_t sandwich4_case1_v1[] = {
    0x01, 0x02, 0x03, 0x04,  // Sandwich4.before
    0x00, 0x00, 0x00, 0x00,  // Sandwich4.before (padding)

    0x04, 0x00, 0x00, 0x00,  // UnionSize36Alignment4.tag, i.e. Sandwich4.the_union
    0x00, 0x00, 0x00, 0x00,  // UnionSize36Alignment4.tag (padding)
    0x20, 0x00, 0x00, 0x00,  // UnionSize36Alignment4.env.num_bytes
    0x00, 0x00, 0x00, 0x00,  // UnionSize36Alignment4.env.num_handle
    0xff, 0xff, 0xff, 0xff,  // UnionSize36Alignment4.env.presence
    0xff, 0xff, 0xff, 0xff,  // UnionSize36Alignment4.env.presence [cont.]

    0x05, 0x06, 0x07, 0x08,  // Sandwich4.after
    0x00, 0x00, 0x00, 0x00,  // Sandwich4.after (padding)

    0xa0, 0xa1, 0xa2, 0xa3,  // UnionSize36Alignment4.data, i.e. Sandwich4.the_union.data
    0xa4, 0xa5, 0xa6, 0xa7,  // UnionSize36Alignment4.data [cont.]
    0xa8, 0xa9, 0xaa, 0xab,  // UnionSize36Alignment4.data [cont.]
    0xac, 0xad, 0xae, 0xaf,  // UnionSize36Alignment4.data [cont.]
    0xb0, 0xb1, 0xb2, 0xb3,  // UnionSize36Alignment4.data [cont.]
    0xb4, 0xb5, 0xb6, 0xb7,  // UnionSize36Alignment4.data [cont.]
    0xb8, 0xb9, 0xba, 0xbb,  // UnionSize36Alignment4.data [cont.]
    0xbc, 0xbd, 0xbe, 0xbf,  // UnionSize36Alignment4.data [cont.]
};

class TransformerIntegrationTest : public ::testing::Test {
 protected:
  virtual void SetUp() override {
    ASSERT_EQ(zx::channel::create(0, &client_end_, &server_end_), ZX_OK);
  }

  virtual void TearDown() override {}

  test::ReceiveXunionsForUnions::SyncClient TakeClient() {
    EXPECT_TRUE(client_end_.is_valid());
    return test::ReceiveXunionsForUnions::SyncClient(std::move(client_end_));
  }

  zx::channel& client_end() { return client_end_; }
  zx::channel& server_end() { return server_end_; }

  static void InitSandwich(::llcpp::example::Sandwich4* sandwich) {
    sandwich->before = 0x04030201u;
    sandwich->after = 0x08070605;
    fidl::Array<uint8_t, 32> array;
    for (size_t i = 0; i < array.size(); i++) {
      array[i] = static_cast<uint8_t>(0xa0 + i);
    }
    sandwich->the_union.set_variant(&array);
  }

 private:
  zx::channel client_end_;
  zx::channel server_end_;
};

TEST_F(TransformerIntegrationTest, ReadPathUnionEvent) {
  auto client = TakeClient();

  // Send the event from the server end
  std::vector<uint8_t> response(sizeof(fidl_message_header_t) + sizeof(sandwich4_case1_v1));
  auto response_hdr = reinterpret_cast<fidl_message_header_t*>(&response[0]);
  fidl::DecodedMessage<test::ReceiveXunionsForUnions::UnionEventResponse> msg(
      fidl::BytePart(&response[0], response.size(), response.size()));
  test::ReceiveXunionsForUnions::SetTransactionHeaderFor::UnionEventResponse(msg);
  msg.Release();
  // Set the flag indicating unions are encoded as xunions.
  response_hdr->flags[0] |= FIDL_TXN_HEADER_UNION_FROM_XUNION_FLAG;
  memcpy(&response[sizeof(fidl_message_header_t)], sandwich4_case1_v1, sizeof(sandwich4_case1_v1));
  ASSERT_EQ(ZX_OK, server_end().write(0, &response[0], response.size(), nullptr, 0));

  // Test reading it from the client end
  zx_status_t status = client.HandleEvents({
      .union_event =
          [&](llcpp::example::Sandwich4 sandwich) {
            EXPECT_EQ(llcpp::example::UnionSize36Alignment4::Tag::kVariant,
                      sandwich.the_union.which());
            EXPECT_EQ(0x04030201u, sandwich.before);
            EXPECT_EQ(0x08070605u, sandwich.after);
            return ZX_OK;
          },
      .unknown = [&] { return ZX_ERR_NOT_SUPPORTED; },
  });
  EXPECT_EQ(ZX_OK, status);
}

TEST_F(TransformerIntegrationTest, ReadPathSendUnion) {
  class TestServer : public test::ReceiveXunionsForUnions::Interface {
   public:
    void SendUnion(llcpp::example::Sandwich4 sandwich, SendUnionCompleter::Sync completer) final {
      EXPECT_EQ(llcpp::example::UnionSize36Alignment4::Tag::kVariant, sandwich.the_union.which());
      EXPECT_EQ(0x04030201u, sandwich.before);
      EXPECT_EQ(0x08070605u, sandwich.after);
      completer.Reply(true);
    }

    void ReceiveUnion(ReceiveUnionCompleter::Sync completer) final {
      completer.Close(ZX_ERR_INVALID_ARGS);
      FAIL() << "Never used in this test";
    }
  };

  class TestTransaction : public fidl::Transaction {
   public:
    std::unique_ptr<Transaction> TakeOwnership() final { ZX_PANIC("Never used in this test"); }
    void Close(zx_status_t) final { FAIL() << "Never used in this test"; }

    void Reply(fidl::Message message) final {
      auto response = reinterpret_cast<test::ReceiveXunionsForUnions::SendUnionResponse*>(
          message.bytes().begin());
      EXPECT_TRUE(response->success);
      reply_called_ += 1;
    }

    int reply_called() const { return reply_called_; }

   private:
    int reply_called_ = 0;
  };

  TestServer server;
  TestTransaction txn;
  std::vector<uint8_t> fake_request(sizeof(fidl_message_header_t) + sizeof(sandwich4_case1_v1));
  auto fake_request_hdr = reinterpret_cast<fidl_message_header_t*>(&fake_request[0]);
  fidl::DecodedMessage<test::ReceiveXunionsForUnions::SendUnionRequest> decoded_msg(
      fidl::BytePart(&fake_request[0], fake_request.size(), fake_request.size()));
  test::ReceiveXunionsForUnions::SetTransactionHeaderFor::SendUnionRequest(decoded_msg);
  decoded_msg.Release();
  // Set the flag indicating unions are encoded as xunions.
  fake_request_hdr->flags[0] |= FIDL_TXN_HEADER_UNION_FROM_XUNION_FLAG;
  fake_request_hdr->txid = 1;
  memcpy(&fake_request[sizeof(fidl_message_header_t)], sandwich4_case1_v1,
         sizeof(sandwich4_case1_v1));
  zx_handle_t handles[1] = {};
  fidl_msg_t msg = {
      .bytes = &fake_request[0],
      .handles = handles,
      .num_bytes = static_cast<uint32_t>(fake_request.size()),
      .num_handles = 0,
  };
  bool handled = test::ReceiveXunionsForUnions::TryDispatch(&server, &msg, &txn);
  EXPECT_TRUE(handled);
  EXPECT_EQ(1, txn.reply_called());
}

TEST_F(TransformerIntegrationTest, ReadPathReceiveUnion) {
  auto client = TakeClient();

  // Send the method response from the server end, on another thread
  std::thread server_thread([&, this] {
    // Wait for request
    zx_signals_t observed;
    zx_status_t status = server_end().wait_one(ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED,
                                               zx::time::infinite(), &observed);
    if ((observed & ZX_CHANNEL_READABLE) == 0) {
      FAIL() << "Failed to observe a readable channel signal";
    }
    ASSERT_EQ(ZX_OK, status);
    std::vector<uint8_t> request_buffer(ZX_CHANNEL_MAX_MSG_BYTES);
    zx_handle_t handles[ZX_CHANNEL_MAX_MSG_HANDLES];
    uint32_t actual_bytes = 0u;
    uint32_t actual_handles = 0u;
    status = server_end().read(0, &request_buffer[0], handles, request_buffer.size(),
                               ZX_CHANNEL_MAX_MSG_HANDLES, &actual_bytes, &actual_handles);
    ASSERT_EQ(ZX_OK, status);
    ASSERT_GE(actual_bytes, sizeof(fidl_message_header_t));
    ASSERT_EQ(actual_handles, 0u);
    auto request_hdr = reinterpret_cast<fidl_message_header_t*>(&request_buffer[0]);
    // Send response with xunion
    std::vector<uint8_t> response(sizeof(fidl_message_header_t) + sizeof(sandwich4_case1_v1));
    auto response_hdr = reinterpret_cast<fidl_message_header_t*>(&response[0]);
    fidl::DecodedMessage<test::ReceiveXunionsForUnions::ReceiveUnionResponse> msg(
        fidl::BytePart(&response[0], response.size(), response.size()));
    test::ReceiveXunionsForUnions::SetTransactionHeaderFor::ReceiveUnionResponse(msg);
    msg.Release();
    // Set the flag indicating unions are encoded as xunions.
    response_hdr->flags[0] |= FIDL_TXN_HEADER_UNION_FROM_XUNION_FLAG;
    response_hdr->txid = request_hdr->txid;
    memcpy(&response[sizeof(fidl_message_header_t)], sandwich4_case1_v1,
           sizeof(sandwich4_case1_v1));
    ASSERT_EQ(ZX_OK, server_end().write(0, &response[0], response.size(), nullptr, 0));
  });

  auto result = client.ReceiveUnion();
  ASSERT_EQ(ZX_OK, result.status());
  auto& sandwich = result.value().sandwich;
  EXPECT_EQ(llcpp::example::UnionSize36Alignment4::Tag::kVariant, sandwich.the_union.which());
  EXPECT_EQ(0x04030201u, sandwich.before);
  EXPECT_EQ(0x08070605u, sandwich.after);

  server_thread.join();
}

}  // namespace
