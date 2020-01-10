// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/wait.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fidl/cpp/message_part.h>
#include <lib/fidl/cpp/test/test_util.h>
#include <lib/fidl/txn_header.h>
#include <zircon/errors.h>
#include <zircon/fidl.h>
#include <zircon/status.h>
#include <zircon/types.h>

#include <atomic>
#include <memory>
#include <thread>
#include <vector>

#include <example/cpp/fidl.h>
#include <transformerintegration/test/cpp/fidl.h>

#include "gtest/gtest.h"

namespace test = ::transformerintegration::test;

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
    loop_ = std::make_unique<async::Loop>(&kAsyncLoopConfigAttachToCurrentThread);
  }

  virtual void TearDown() override { loop_->Shutdown(); }

  zx::channel& client_end() { return client_end_; }
  zx::channel& server_end() { return server_end_; }

  static void InitSandwich(::example::Sandwich4* sandwich) {
    sandwich->before = 0x04030201u;
    sandwich->after = 0x08070605;
    std::array<uint8_t, 32> array;
    for (size_t i = 0; i < array.size(); i++) {
      array[i] = static_cast<uint8_t>(0xa0 + i);
    }
    sandwich->the_union.set_variant(array);
  }

  async::Loop* loop() const { return loop_.get(); }

 private:
  zx::channel client_end_;
  zx::channel server_end_;
  std::unique_ptr<async::Loop> loop_;
};

TEST_F(TransformerIntegrationTest, ReadPathUnionEvent) {
  std::vector<uint8_t> response(sizeof(fidl_message_header_t) + sizeof(sandwich4_case1_v1));
  auto response_hdr = reinterpret_cast<fidl_message_header_t*>(&response[0]);
  fidl_init_txn_header(response_hdr, 0,
                       test::internal::kReceiveXunionsForUnions_UnionEvent_Ordinal);
  response_hdr->flags[0] |= FIDL_TXN_HEADER_UNION_FROM_XUNION_FLAG;
  memcpy(&response[sizeof(fidl_message_header_t)], sandwich4_case1_v1, sizeof(sandwich4_case1_v1));
  ASSERT_EQ(ZX_OK, server_end().write(0, &response[0], response.size(), nullptr, 0));

  test::ReceiveXunionsForUnionsPtr client;
  client.Bind(std::move(client_end()), loop()->dispatcher());
  client.events().UnionEvent = [this](::example::Sandwich4 sandwich) {
    EXPECT_EQ(example::UnionSize36Alignment4::Tag::kVariant, sandwich.the_union.Which());
    EXPECT_EQ(0x04030201u, sandwich.before);
    EXPECT_EQ(0x08070605u, sandwich.after);
    loop()->Quit();
  };
  ASSERT_EQ(ZX_ERR_CANCELED, loop()->Run());
}

TEST_F(TransformerIntegrationTest, ReadPathSendUnion) {
  class TestServer : public test::ReceiveXunionsForUnions {
   public:
    void SendUnion(::example::Sandwich4 sandwich, SendUnionCallback callback) override {
      reply_called_ += 1;
      EXPECT_EQ(example::UnionSize36Alignment4::Tag::kVariant, sandwich.the_union.Which());
      EXPECT_EQ(0x04030201u, sandwich.before);
      EXPECT_EQ(0x08070605u, sandwich.after);
      callback(true);
    }

    void ReceiveUnion(ReceiveUnionCallback callback) override {
      FAIL() << "Never used in the test";
    }

    int reply_called() const { return reply_called_; }

   private:
    int reply_called_ = 0;
  };
  std::vector<uint8_t> request(sizeof(fidl_message_header_t) + sizeof(sandwich4_case1_v1));
  auto request_hdr = reinterpret_cast<fidl_message_header_t*>(&request[0]);
  fidl_init_txn_header(request_hdr, 1, test::internal::kReceiveXunionsForUnions_SendUnion_Ordinal);
  request_hdr->flags[0] |= FIDL_TXN_HEADER_UNION_FROM_XUNION_FLAG;
  memcpy(&request[sizeof(fidl_message_header_t)], sandwich4_case1_v1, sizeof(sandwich4_case1_v1));
  ASSERT_EQ(ZX_OK, client_end().write(0, &request[0], request.size(), nullptr, 0));

  TestServer server_impl;
  fidl::Binding<test::ReceiveXunionsForUnions> binding(&server_impl, std::move(server_end()),
                                                       loop()->dispatcher());
  loop()->RunUntilIdle();
  ASSERT_EQ(1, server_impl.reply_called());
}

TEST_F(TransformerIntegrationTest, ReadPathReceiveUnion) {
  test::ReceiveXunionsForUnionsPtr client;
  client.Bind(std::move(client_end()), loop()->dispatcher());

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
    fidl_init_txn_header(response_hdr, 1,
                         test::internal::kReceiveXunionsForUnions_ReceiveUnion_Ordinal);
    // Set the flag indicating unions are encoded as xunions.
    response_hdr->flags[0] |= FIDL_TXN_HEADER_UNION_FROM_XUNION_FLAG;
    response_hdr->txid = request_hdr->txid;
    memcpy(&response[sizeof(fidl_message_header_t)], sandwich4_case1_v1,
           sizeof(sandwich4_case1_v1));
    ASSERT_EQ(ZX_OK, server_end().write(0, &response[0], response.size(), nullptr, 0));
  });

  std::atomic<bool> response_received = false;
  client->ReceiveUnion([this, &response_received](::example::Sandwich4 sandwich) {
    EXPECT_EQ(example::UnionSize36Alignment4::Tag::kVariant, sandwich.the_union.Which());
    EXPECT_EQ(0x04030201u, sandwich.before);
    EXPECT_EQ(0x08070605u, sandwich.after);
    response_received.store(true);
    loop()->Quit();
  });

  loop()->Run();
  server_thread.join();
  ASSERT_TRUE(response_received.load());
}

}  // namespace
