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

class TransformerIntegrationTest : public ::testing::Test {
 protected:
  virtual void SetUp() override {
    ASSERT_EQ(zx::channel::create(0, &client_end_, &server_end_), ZX_OK);
  }

  virtual void TearDown() override {
  }

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
    sandwich->the_union.set_variant(array);
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
      .union_event = [&](llcpp::example::Sandwich4 sandwich) {
        EXPECT_EQ(llcpp::example::UnionSize36Alignment4::Tag::kVariant, sandwich.the_union.which());
        EXPECT_EQ(0x04030201u, sandwich.before);
        EXPECT_EQ(0x08070605u, sandwich.after);
        return ZX_OK;
      },
      .unknown = [&] {
        return ZX_ERR_NOT_SUPPORTED;
      },
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
  // Send the event from the server end; we expect to read out an event in v1 wire-format.
  {
    ScopedToggleWriteXunion toggle(true);
    ::llcpp::example::Sandwich4 sandwich;
    InitSandwich(&sandwich);
    test::ReceiveXunionsForUnions::SendUnionEventEvent(zx::unowned_channel(server_end()),
                                                       std::move(sandwich));
    std::vector<uint8_t> response_buf(512);
    uint32_t actual_bytes = 0;
    ASSERT_EQ(ZX_OK, client_end().read(0, &response_buf[0], nullptr, response_buf.size(), 0,
                                       &actual_bytes, nullptr));

    ASSERT_GE(actual_bytes, sizeof(fidl_message_header_t));
    EXPECT_TRUE(llcpp_conformance_utils::ComparePayload(
        &response_buf[sizeof(fidl_message_header_t)], actual_bytes - sizeof(fidl_message_header_t),
        sandwich4_case1_v1, sizeof(sandwich4_case1_v1)));
    EXPECT_TRUE(fidl_should_decode_union_from_xunion(
        reinterpret_cast<fidl_message_header_t*>(&response_buf[0])));
  }

  // Send the event from the server end; we expect to read out an event in the old wire-format.
  {
    ScopedToggleWriteXunion toggle(false);
    ::llcpp::example::Sandwich4 sandwich;
    InitSandwich(&sandwich);
    test::ReceiveXunionsForUnions::SendUnionEventEvent(zx::unowned_channel(server_end()),
                                                       std::move(sandwich));
    std::vector<uint8_t> response_buf(512);
    uint32_t actual_bytes = 0;
    ASSERT_EQ(ZX_OK, client_end().read(0, &response_buf[0], nullptr, response_buf.size(), 0,
                                       &actual_bytes, nullptr));

    ASSERT_GE(actual_bytes, sizeof(fidl_message_header_t));
    EXPECT_TRUE(llcpp_conformance_utils::ComparePayload(
        &response_buf[sizeof(fidl_message_header_t)], actual_bytes - sizeof(fidl_message_header_t),
        sandwich4_case1_old, sizeof(sandwich4_case1_old)));
    EXPECT_FALSE(fidl_should_decode_union_from_xunion(
        reinterpret_cast<fidl_message_header_t*>(&response_buf[0])));
  }
}

TEST_F(TransformerIntegrationTest, WritePathSendUnion) {
  auto client = TakeClient();

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
      fidl::Buffer<test::ReceiveXunionsForUnions::SendUnionResponse> response_buffer;
      fidl::BytePart bytes = response_buffer.view();
      bytes.set_actual(bytes.capacity());
      memset(bytes.begin(), 0, bytes.capacity());
      uint8_t* response = bytes.data();
      auto response_hdr = reinterpret_cast<fidl_message_header_t*>(response);
      fidl::DecodedMessage<test::ReceiveXunionsForUnions::SendUnionResponse> msg(std::move(bytes));
      test::ReceiveXunionsForUnions::SetTransactionHeaderFor::SendUnionResponse(msg);
      msg.message()->success = true;
      bytes = msg.Release();
      response_hdr->txid = request_hdr->txid;
      ASSERT_EQ(ZX_OK, server_end().write(0, bytes.data(), bytes.size(), nullptr, 0));
    });

    ::llcpp::example::Sandwich4 sandwich;
    TransformerIntegrationTest::InitSandwich(&sandwich);
    auto result = client.SendUnion(std::move(sandwich));
    ASSERT_EQ(ZX_OK, result.status());
    ASSERT_TRUE(result->success);

    server_thread.join();
  };

  // Send the request from the client end; we expect to read out the request in v1 wire-format.
  {
    ScopedToggleWriteXunion toggle(true);

    with_common_test_body([&](const std::vector<uint8_t>& request_buffer, uint32_t actual_bytes) {
      ASSERT_TRUE(
          llcpp_conformance_utils::ComparePayload(&request_buffer[sizeof(fidl_message_header_t)],
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
      ASSERT_TRUE(llcpp_conformance_utils::ComparePayload(
          &request_buffer[sizeof(fidl_message_header_t)],
          actual_bytes - sizeof(fidl_message_header_t), sandwich4_case1_old,
          sizeof(sandwich4_case1_old)));
      ASSERT_FALSE(fidl_should_decode_union_from_xunion(
          reinterpret_cast<const fidl_message_header_t*>(&request_buffer[0])));
    });
  }
}

TEST_F(TransformerIntegrationTest, WritePathReceiveUnion) {
  class Server : public test::ReceiveXunionsForUnions::Interface {
   public:
    void SendUnion(::llcpp::example::Sandwich4 sandwich,
                   SendUnionCompleter::Sync completer) override {
      ZX_PANIC("Never called");
    }
    void ReceiveUnion(ReceiveUnionCompleter::Sync completer) override {
      ::llcpp::example::Sandwich4 sandwich;
      TransformerIntegrationTest::InitSandwich(&sandwich);
      completer.Reply(std::move(sandwich));
    }
  };

  Server server;
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  ASSERT_EQ(ZX_OK, fidl::Bind(loop.dispatcher(), std::move(server_end()), &server));
  loop.StartThread("transformer-integration-test-server-thread");

  auto with_common_test_body = [&](auto&& run_asserts) {
    // Manually craft the request, since we need to manually validate the response bytes.
    fidl::Buffer<test::ReceiveXunionsForUnions::ReceiveUnionRequest> request_buffer;
    fidl::BytePart bytes = request_buffer.view();
    bytes.set_actual(bytes.capacity());
    memset(bytes.begin(), 0, bytes.capacity());
    fidl::DecodedMessage<test::ReceiveXunionsForUnions::ReceiveUnionRequest> msg(std::move(bytes));
    test::ReceiveXunionsForUnions::SetTransactionHeaderFor::ReceiveUnionRequest(msg);
    msg.message()->_hdr.txid = 1;
    bytes = msg.Release();
    ASSERT_EQ(ZX_OK, client_end().write(0, bytes.data(), bytes.size(), nullptr, 0));

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
      ASSERT_TRUE(
          llcpp_conformance_utils::ComparePayload(&response_buffer[sizeof(fidl_message_header_t)],
                                                  actual_bytes - sizeof(fidl_message_header_t),
                                                  sandwich4_case1_v1, sizeof(sandwich4_case1_v1)));
      ASSERT_TRUE(fidl_should_decode_union_from_xunion(
          reinterpret_cast<const fidl_message_header_t*>(&response_buffer[0])));
    });
  }

  {
    ScopedToggleWriteXunion toggle(false);

    with_common_test_body([&](const std::vector<uint8_t>& response_buffer, uint32_t actual_bytes) {
      ASSERT_TRUE(llcpp_conformance_utils::ComparePayload(
          &response_buffer[sizeof(fidl_message_header_t)],
          actual_bytes - sizeof(fidl_message_header_t), sandwich4_case1_old,
          sizeof(sandwich4_case1_old)));
      ASSERT_FALSE(fidl_should_decode_union_from_xunion(
          reinterpret_cast<const fidl_message_header_t*>(&response_buffer[0])));
    });
  }
}

}  // namespace
