// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/wait.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fidl/cpp/message_part.h>
#include <lib/fidl/cpp/test/test_util.h>
#include <lib/fidl/runtime_flag.h>
#include <lib/fidl/txn_header.h>
#include <zircon/errors.h>
#include <zircon/fidl.h>
#include <zircon/status.h>
#include <zircon/types.h>

#include <atomic>
#include <thread>
#include <vector>
#include <memory>

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

// The old wire-format version of |example/Sandwich4|.
// This excerpt of bytes is taken directly from zircon/system/utest/fidl/transformer_tests.cc.
constexpr uint8_t sandwich4_case1_old[] = {
    0x01, 0x02, 0x03, 0x04,  // Sandwich4.before

    0x03, 0x00, 0x00, 0x00,  // UnionSize36Alignment4.tag, i.e. Sandwich4.the_union
    0xa0, 0xa1, 0xa2, 0xa3,  // UnionSize36Alignment4.data
    0xa4, 0xa5, 0xa6, 0xa7,  // UnionSize36Alignment4.data [cont.]
    0xa8, 0xa9, 0xaa, 0xab,  // UnionSize36Alignment4.data [cont.]
    0xac, 0xad, 0xae, 0xaf,  // UnionSize36Alignment4.data [cont.]
    0xb0, 0xb1, 0xb2, 0xb3,  // UnionSize36Alignment4.data [cont.]
    0xb4, 0xb5, 0xb6, 0xb7,  // UnionSize36Alignment4.data [cont.]
    0xb8, 0xb9, 0xba, 0xbb,  // UnionSize36Alignment4.data [cont.]
    0xbc, 0xbd, 0xbe, 0xbf,  // UnionSize36Alignment4.data [cont.]

    0x05, 0x06, 0x07, 0x08,  // Sandwich4.after

    0x00, 0x00, 0x00, 0x00,  // padding for top-level struct
};

/// V1 wire-format for two |example.Sandwich4| fields. See |MultiArgReceiveXunions.TwoUnion|.
/// Their inline parts will be next to each other, then followed by the out-of-line parts.
constexpr uint8_t two_sandwich4_case1_v1[] = {
    0x01, 0x02, 0x03, 0x04,  // (1st) Sandwich4.before
    0x00, 0x00, 0x00, 0x00,  // (1st) Sandwich4.before (padding)

    0x04, 0x00, 0x00, 0x00,  // (1st) UnionSize36Alignment4.tag, i.e. Sandwich4.the_union
    0x00, 0x00, 0x00, 0x00,  // (1st) UnionSize36Alignment4.tag (padding)
    0x20, 0x00, 0x00, 0x00,  // (1st) UnionSize36Alignment4.env.num_bytes
    0x00, 0x00, 0x00, 0x00,  // (1st) UnionSize36Alignment4.env.num_handle
    0xff, 0xff, 0xff, 0xff,  // (1st) UnionSize36Alignment4.env.presence
    0xff, 0xff, 0xff, 0xff,  // (1st) UnionSize36Alignment4.env.presence [cont.]

    0x05, 0x06, 0x07, 0x08,  // (1st) Sandwich4.after
    0x00, 0x00, 0x00, 0x00,  // (1st) Sandwich4.after (padding)

    0x01, 0x02, 0x03, 0x04,  // (2nd) Sandwich4.before
    0x00, 0x00, 0x00, 0x00,  // (2nd) Sandwich4.before (padding)

    0x04, 0x00, 0x00, 0x00,  // (2nd) UnionSize36Alignment4.tag, i.e. Sandwich4.the_union
    0x00, 0x00, 0x00, 0x00,  // (2nd) UnionSize36Alignment4.tag (padding)
    0x20, 0x00, 0x00, 0x00,  // (2nd) UnionSize36Alignment4.env.num_bytes
    0x00, 0x00, 0x00, 0x00,  // (2nd) UnionSize36Alignment4.env.num_handle
    0xff, 0xff, 0xff, 0xff,  // (2nd) UnionSize36Alignment4.env.presence
    0xff, 0xff, 0xff, 0xff,  // (2nd) UnionSize36Alignment4.env.presence [cont.]

    0x05, 0x06, 0x07, 0x08,  // (2nd) Sandwich4.after
    0x00, 0x00, 0x00, 0x00,  // (2nd) Sandwich4.after (padding)

    0xa0, 0xa1, 0xa2, 0xa3,  // (1st) UnionSize36Alignment4.data, i.e. Sandwich4.the_union.data
    0xa4, 0xa5, 0xa6, 0xa7,  // (1st) UnionSize36Alignment4.data [cont.]
    0xa8, 0xa9, 0xaa, 0xab,  // (1st) UnionSize36Alignment4.data [cont.]
    0xac, 0xad, 0xae, 0xaf,  // (1st) UnionSize36Alignment4.data [cont.]
    0xb0, 0xb1, 0xb2, 0xb3,  // (1st) UnionSize36Alignment4.data [cont.]
    0xb4, 0xb5, 0xb6, 0xb7,  // (1st) UnionSize36Alignment4.data [cont.]
    0xb8, 0xb9, 0xba, 0xbb,  // (1st) UnionSize36Alignment4.data [cont.]
    0xbc, 0xbd, 0xbe, 0xbf,  // (1st) UnionSize36Alignment4.data [cont.]

    0xa0, 0xa1, 0xa2, 0xa3,  // (2nd) UnionSize36Alignment4.data, i.e. Sandwich4.the_union.data
    0xa4, 0xa5, 0xa6, 0xa7,  // (2nd) UnionSize36Alignment4.data [cont.]
    0xa8, 0xa9, 0xaa, 0xab,  // (2nd) UnionSize36Alignment4.data [cont.]
    0xac, 0xad, 0xae, 0xaf,  // (2nd) UnionSize36Alignment4.data [cont.]
    0xb0, 0xb1, 0xb2, 0xb3,  // (2nd) UnionSize36Alignment4.data [cont.]
    0xb4, 0xb5, 0xb6, 0xb7,  // (2nd) UnionSize36Alignment4.data [cont.]
    0xb8, 0xb9, 0xba, 0xbb,  // (2nd) UnionSize36Alignment4.data [cont.]
    0xbc, 0xbd, 0xbe, 0xbf,  // (2nd) UnionSize36Alignment4.data [cont.]
};

class TransformerIntegrationTest : public ::testing::Test {
 protected:
  virtual void SetUp() override {
    ASSERT_EQ(zx::channel::create(0, &client_end_, &server_end_), ZX_OK);
    loop_ = std::make_unique<async::Loop>(&kAsyncLoopConfigAttachToCurrentThread);
  }

  virtual void TearDown() override {
    loop_->Shutdown();
  }

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
  fidl_init_txn_header(request_hdr, 1,
                       test::internal::kReceiveXunionsForUnions_SendUnion_Ordinal);
  request_hdr->flags[0] |= FIDL_TXN_HEADER_UNION_FROM_XUNION_FLAG;
  memcpy(&request[sizeof(fidl_message_header_t)], sandwich4_case1_v1,
         sizeof(sandwich4_case1_v1));
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
  client->ReceiveUnion([this, &response_received] (::example::Sandwich4 sandwich) {
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

class ScopedToggleWriteXunion {
 public:
  explicit ScopedToggleWriteXunion(bool enabled = true) {
    original_flag_ = fidl_global_get_should_write_union_as_xunion();
    fidl_global_set_should_write_union_as_xunion(enabled);
  }
  ~ScopedToggleWriteXunion() {
    fidl_global_set_should_write_union_as_xunion(original_flag_);
  }

 private:
  ScopedToggleWriteXunion(const ScopedToggleWriteXunion&) = delete;
  ScopedToggleWriteXunion& operator=(const ScopedToggleWriteXunion&) = delete;
  ScopedToggleWriteXunion(ScopedToggleWriteXunion&&) = delete;
  ScopedToggleWriteXunion& operator=(ScopedToggleWriteXunion&&) = delete;

  bool original_flag_;
};

TEST_F(TransformerIntegrationTest, WritePathUnionEvent) {
  loop()->StartThread();
  class TestServer : public test::ReceiveXunionsForUnions {
   public:
    void SendUnion(::example::Sandwich4 sandwich, SendUnionCallback callback) override {
      FAIL() << "Never used in the test";
    }
    void ReceiveUnion(ReceiveUnionCallback callback) override {
      FAIL() << "Never used in the test";
    }
  };

  TestServer server_impl;
  fidl::Binding<test::ReceiveXunionsForUnions> binding(&server_impl, std::move(server_end()),
                                                        loop()->dispatcher());

  // Send the event from the server end; we expect to read out an event in v1 wire-format.
  {
    ScopedToggleWriteXunion toggle(true);
    ::example::Sandwich4 sandwich;
    InitSandwich(&sandwich);
    binding.events().UnionEvent(std::move(sandwich));
    std::vector<uint8_t> response_buf(ZX_CHANNEL_MAX_MSG_BYTES);
    uint32_t actual_bytes = 0;
    ASSERT_EQ(ZX_OK, client_end().read(0, &response_buf[0], nullptr, response_buf.size(), 0,
                                       &actual_bytes, nullptr));

    ASSERT_GE(actual_bytes, sizeof(fidl_message_header_t));
    EXPECT_TRUE(fidl::test::util::cmp_payload(
        &response_buf[sizeof(fidl_message_header_t)], actual_bytes - sizeof(fidl_message_header_t),
        sandwich4_case1_v1, sizeof(sandwich4_case1_v1)));
    EXPECT_TRUE(fidl_should_decode_union_from_xunion(
        reinterpret_cast<fidl_message_header_t*>(&response_buf[0])));
  }

  // Send the event from the server end; we expect to read out an event in the old wire-format.
  {
    ScopedToggleWriteXunion toggle(false);
    ::example::Sandwich4 sandwich;
    InitSandwich(&sandwich);
    binding.events().UnionEvent(std::move(sandwich));
    std::vector<uint8_t> response_buf(ZX_CHANNEL_MAX_MSG_BYTES);
    uint32_t actual_bytes = 0;
    ASSERT_EQ(ZX_OK, client_end().read(0, &response_buf[0], nullptr, response_buf.size(), 0,
                                       &actual_bytes, nullptr));

    ASSERT_GE(actual_bytes, sizeof(fidl_message_header_t));
    EXPECT_TRUE(fidl::test::util::cmp_payload(
        &response_buf[sizeof(fidl_message_header_t)], actual_bytes - sizeof(fidl_message_header_t),
        sandwich4_case1_old, sizeof(sandwich4_case1_old)));
    EXPECT_FALSE(fidl_should_decode_union_from_xunion(
        reinterpret_cast<fidl_message_header_t*>(&response_buf[0])));
  }
}

TEST_F(TransformerIntegrationTest, WritePathSendUnion) {
  test::ReceiveXunionsForUnionsPtr client;
  client.Bind(std::move(client_end()), loop()->dispatcher());

  auto with_common_test_body = [&, this](auto&& run_asserts) {
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

      run_asserts(request_buffer, actual_bytes);

      auto request_hdr = reinterpret_cast<fidl_message_header_t*>(&request_buffer[0]);
      // |SendUnionResponse| is a header plus a boolean.
      uint8_t response[sizeof(fidl_message_header_t) + FIDL_ALIGN(1)] = {};
      auto response_hdr = reinterpret_cast<fidl_message_header_t*>(response);
      // Set the |success| field to true.
      response[sizeof(fidl_message_header_t)] = 1;
      fidl_init_txn_header(response_hdr, request_hdr->txid,
                           test::internal::kReceiveXunionsForUnions_SendUnion_Ordinal);
      ASSERT_EQ(ZX_OK, server_end().write(0, response, sizeof(response), nullptr, 0));
    });

    std::atomic<bool> response_received = false;
    ::example::Sandwich4 sandwich;
    TransformerIntegrationTest::InitSandwich(&sandwich);
    client->SendUnion(std::move(sandwich), [this, &response_received] (bool success) {
      EXPECT_TRUE(success);
      response_received.store(true);
      loop()->Quit();
    });

    loop()->Run();
    server_thread.join();
    loop()->ResetQuit();
    EXPECT_TRUE(response_received.load());
  };

  // Send the request from the client end; we expect to read out the request in v1 wire-format.
  {
    ScopedToggleWriteXunion toggle(true);

    with_common_test_body([&](const std::vector<uint8_t>& request_buffer, uint32_t actual_bytes) {
      ASSERT_TRUE(fidl::test::util::cmp_payload(&request_buffer[sizeof(fidl_message_header_t)],
                                                actual_bytes - sizeof(fidl_message_header_t),
                                                sandwich4_case1_v1, sizeof(sandwich4_case1_v1)));
      ASSERT_TRUE(fidl_should_decode_union_from_xunion(
          reinterpret_cast<const fidl_message_header_t*>(&request_buffer[0])));
    });
  }

  // Send the request from the client end; we expect to read out the request in old wire-format.
  {
    ScopedToggleWriteXunion toggle(false);

    with_common_test_body([&](const std::vector<uint8_t>& request_buffer, uint32_t actual_bytes) {
      ASSERT_TRUE(fidl::test::util::cmp_payload(&request_buffer[sizeof(fidl_message_header_t)],
                                                actual_bytes - sizeof(fidl_message_header_t),
                                                sandwich4_case1_old, sizeof(sandwich4_case1_old)));
      ASSERT_FALSE(fidl_should_decode_union_from_xunion(
          reinterpret_cast<const fidl_message_header_t*>(&request_buffer[0])));
    });
  }
}

TEST_F(TransformerIntegrationTest, WritePathReceiveUnion) {
  class Server : public test::ReceiveXunionsForUnions {
   public:
    void SendUnion(::example::Sandwich4 sandwich, SendUnionCallback callback) override {
      FAIL() << "Never used in the test";
    }
    void ReceiveUnion(ReceiveUnionCallback callback) override {
      ::example::Sandwich4 sandwich;
      TransformerIntegrationTest::InitSandwich(&sandwich);
      callback(std::move(sandwich));
    }
  };

  loop()->StartThread("transformer-integration-test-server-thread");
  Server server;
  fidl::Binding<test::ReceiveXunionsForUnions> binding(&server, std::move(server_end()),
                                                       loop()->dispatcher());

  auto with_common_test_body = [&](auto&& run_asserts) {
    // Manually craft the request, since we need to manually validate the response bytes.
    // |ReceiveUnionRequest| has no arguments.
    uint8_t request[sizeof(fidl_message_header_t)] = {};
    auto request_hdr = reinterpret_cast<fidl_message_header_t*>(request);
    fidl_init_txn_header(request_hdr, 1,
                         test::internal::kReceiveXunionsForUnions_ReceiveUnion_Ordinal);
    ASSERT_EQ(ZX_OK, client_end().write(0, request, sizeof(request), nullptr, 0));

    zx_signals_t observed;
    ASSERT_EQ(ZX_OK, client_end().wait_one(ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED,
                                           zx::time::infinite(), &observed));
    if ((observed & ZX_CHANNEL_READABLE) == 0) {
      FAIL() << "Failed to observe a readable channel signal";
    }
    std::vector<uint8_t> response_buffer(ZX_CHANNEL_MAX_MSG_BYTES);
    zx_handle_t handles[ZX_CHANNEL_MAX_MSG_HANDLES];
    uint32_t actual_bytes = 0u;
    uint32_t actual_handles = 0u;
    ASSERT_EQ(ZX_OK, client_end().read(0, &response_buffer[0], handles, response_buffer.size(),
                                       ZX_CHANNEL_MAX_MSG_HANDLES, &actual_bytes, &actual_handles));
    ASSERT_GE(actual_bytes, sizeof(fidl_message_header_t));
    ASSERT_EQ(actual_handles, 0u);

    run_asserts(response_buffer, actual_bytes);
  };

  {
    ScopedToggleWriteXunion toggle(true);

    with_common_test_body([&](const std::vector<uint8_t>& response_buffer, uint32_t actual_bytes) {
      ASSERT_TRUE(fidl::test::util::cmp_payload(&response_buffer[sizeof(fidl_message_header_t)],
                                                actual_bytes - sizeof(fidl_message_header_t),
                                                sandwich4_case1_v1, sizeof(sandwich4_case1_v1)));
      ASSERT_TRUE(fidl_should_decode_union_from_xunion(
          reinterpret_cast<const fidl_message_header_t*>(&response_buffer[0])));
    });
  }

  {
    ScopedToggleWriteXunion toggle(false);

    with_common_test_body([&](const std::vector<uint8_t>& response_buffer, uint32_t actual_bytes) {
      ASSERT_TRUE(fidl::test::util::cmp_payload(&response_buffer[sizeof(fidl_message_header_t)],
                                                actual_bytes - sizeof(fidl_message_header_t),
                                                sandwich4_case1_old, sizeof(sandwich4_case1_old)));
      ASSERT_FALSE(fidl_should_decode_union_from_xunion(
          reinterpret_cast<const fidl_message_header_t*>(&response_buffer[0])));
    });
  }
}

TEST_F(TransformerIntegrationTest, MultiArgXunion) {
  ScopedToggleWriteXunion toggle(true);

  class TestServer : public test::MultiArgReceiveXunions {
   public:
    void TwoUnion(TwoUnionCallback callback) override {
      reply_called_ += 1;
      ::example::Sandwich4 sandwichA;
      InitSandwich(&sandwichA);
      ::example::Sandwich4 sandwichB;
      InitSandwich(&sandwichB);
      callback(std::move(sandwichA), std::move(sandwichB));
    }

    int reply_called() const { return reply_called_; }

   private:
    int reply_called_ = 0;
  };

  std::vector<uint8_t> request(sizeof(fidl_message_header_t));
  auto request_hdr = reinterpret_cast<fidl_message_header_t*>(&request[0]);
  fidl_init_txn_header(request_hdr, 1,
                       test::internal::kMultiArgReceiveXunions_TwoUnion_Ordinal);
  ASSERT_EQ(ZX_OK, client_end().write(0, &request[0], request.size(), nullptr, 0));

  TestServer server_impl;
  fidl::Binding<test::MultiArgReceiveXunions> binding(&server_impl, std::move(server_end()),
                                                      loop()->dispatcher());
  loop()->RunUntilIdle();
  ASSERT_EQ(1, server_impl.reply_called());

  // Read out the encoded response, which should be 2 |example.Sandwich4|s.
  std::vector<uint8_t> response_buf(ZX_CHANNEL_MAX_MSG_BYTES);
  uint32_t actual_bytes = 0;
  ASSERT_EQ(ZX_OK, client_end().read(0, &response_buf[0], nullptr, response_buf.size(), 0,
                                     &actual_bytes, nullptr));
  ASSERT_GE(actual_bytes, sizeof(fidl_message_header_t));
  EXPECT_TRUE(fidl::test::util::cmp_payload(
      &response_buf[sizeof(fidl_message_header_t)], actual_bytes - sizeof(fidl_message_header_t),
      two_sandwich4_case1_v1, sizeof(two_sandwich4_case1_v1)));
  EXPECT_TRUE(fidl_should_decode_union_from_xunion(
      reinterpret_cast<fidl_message_header_t*>(&response_buf[0])));
}

}  // namespace
