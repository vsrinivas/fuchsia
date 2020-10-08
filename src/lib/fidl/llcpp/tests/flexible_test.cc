// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/wait.h>
#include <zircon/fidl.h>
#include <zircon/status.h>

#include <gtest/gtest.h>
#include <llcpptest/flexible/test/llcpp/fidl.h>

namespace test = ::llcpp::llcpptest::flexible::test;

// The only difference between StrictUnboundedXUnion and StrictBoundedXUnion is that
// StrictBoundedXUnion limits the vector payload length to 200 bytes. Therefore, by observing that
// sizeof(test::ReceiveStrictEnvelope::ResultOf::GetUnboundedXUnion) is less than 200, we can
// guarantee that the response storage is not inlined. Rather, it is allocated on the heap.
static_assert(sizeof(test::ReceiveStrictEnvelope::ResultOf::GetUnboundedXUnion) < 200,
              "Result of GetUnboundedXUnion should be stored as a pointer to heap allocation");

// GetBoundedXUnion should be inlined, because it is smaller than 512, but bigger than 200, making
// the entire ResultOf object bigger than 200. The assertion triggers when the ResultOf object size
// falls below 200, at which point we know it is physically incapable of holding a GetBoundedXUnion
// inline, so probably used heap allocation. Here we are trying to test this without plumbing extra
// flags which themselves need to be tested.
static_assert(sizeof(test::ReceiveStrictEnvelope::ResultOf::GetBoundedXUnion) > 200,
              "Result of GetBoundedXUnion should be inlined");

// Implement a special server that returns xunion/tables with unknown ordinals.
// This is impossible to do when using the bindings normally. Here we use a normal server to
// set a tag in the response xunion corresponding to the FIDL call, and intercept and rewrite
// the xunion to an unknown ordinal using a special fidl::Transaction implementation.
namespace {

class RewriteTransaction : public fidl::Transaction {
 public:
  std::unique_ptr<Transaction> TakeOwnership() override {
    ZX_ASSERT_MSG(false, "Never called");
    return {};
  }

  void Close(zx_status_t epitaph) override {
    ZX_ASSERT_MSG(false, "Transaction::Close called with epitaph %d", epitaph);
  }

  zx_status_t Reply(fidl::OutgoingMessage* indicator_msg) override {
    ZX_ASSERT(txid_ != 0);
    ZX_ASSERT(indicator_msg->byte_actual() >=
              sizeof(test::ReceiveFlexibleEnvelope::GetUnknownXUnionMoreHandlesResponse));

    char real_msg_bytes[ZX_CHANNEL_MAX_MSG_BYTES] = {};
    zx_handle_t real_msg_handles[ZX_CHANNEL_MAX_MSG_HANDLES] = {};
    reinterpret_cast<fidl_message_header_t*>(&real_msg_bytes[0])->txid = txid_;
    fidl_msg_t real_msg = {
        .bytes = &real_msg_bytes[0],
        .handles = &real_msg_handles[0],
        .num_bytes = 0u,
        .num_handles = 0u,
    };

    // Determine if |indicator_msg| has a xunion or a table, by inspecting the first few bytes.
    auto maybe_vector =
        reinterpret_cast<fidl_vector_t*>(indicator_msg->bytes() + sizeof(fidl_message_header_t));
    if ((maybe_vector->count == 1 || maybe_vector->count == 2) &&
        reinterpret_cast<uintptr_t>(maybe_vector->data) == FIDL_ALLOC_PRESENT) {
      // Table
      // Manually craft the actual response which has an unknown ordinal
      auto real_response =
          reinterpret_cast<fidl_table_t*>(&real_msg_bytes[sizeof(fidl_message_header_t)]);
      real_response->envelopes.data = reinterpret_cast<void*>(FIDL_ALLOC_PRESENT);

      if (maybe_vector->count == 1) {
        // The |want_more_than_30_bytes_at_ordinal_3| field was set.
        // Create a message with more bytes than expected
        constexpr uint32_t kUnknownBytes = 5000;
        constexpr uint32_t kUnknownHandles = 0;
        real_response->envelopes.count = 3;
        const auto envelope_header_offset =
            sizeof(fidl_message_header_t) + sizeof(fidl_table_t) + sizeof(fidl_envelope_t) * 2;
        const auto envelope_payload_offset = envelope_header_offset + sizeof(fidl_envelope_t);
        auto envelope = reinterpret_cast<fidl_envelope_t*>(&real_msg_bytes[envelope_header_offset]);
        *envelope = fidl_envelope_t{
            .num_bytes = kUnknownBytes,
            .num_handles = kUnknownHandles,
            .presence = FIDL_ALLOC_PRESENT,
        };
        real_msg.num_bytes = envelope_payload_offset + kUnknownBytes;
        real_msg.num_handles = kUnknownHandles;
        memset(&real_msg_bytes[envelope_payload_offset], 0xAA, kUnknownBytes);
      } else {
        // The |want_more_than_4_handles_at_ordinal_4| field was set.
        // Create a message with more handles than expected
        constexpr uint32_t kUnknownBytes = 16;
        constexpr uint32_t kUnknownHandles = ZX_CHANNEL_MAX_MSG_HANDLES;
        for (uint32_t i = 0; i < kUnknownHandles; i++) {
          ZX_ASSERT(zx_event_create(0, &real_msg_handles[i]) == ZX_OK);
        }
        real_response->envelopes.count = 4;
        const auto envelope_header_offset =
            sizeof(fidl_message_header_t) + sizeof(fidl_table_t) + sizeof(fidl_envelope_t) * 3;
        const auto envelope_payload_offset = envelope_header_offset + sizeof(fidl_envelope_t);
        auto envelope = reinterpret_cast<fidl_envelope_t*>(&real_msg_bytes[envelope_header_offset]);
        *envelope = fidl_envelope_t{
            .num_bytes = kUnknownBytes,
            .num_handles = kUnknownHandles,
            .presence = FIDL_ALLOC_PRESENT,
        };
        real_msg.num_bytes = envelope_payload_offset + kUnknownBytes;
        real_msg.num_handles = kUnknownHandles;
        memset(&real_msg_bytes[envelope_payload_offset], 0xBB, kUnknownBytes);
      }
    } else {
      // Manually craft the actual response which has an unknown ordinal
      constexpr uint32_t kBadOrdinal = 0x8badf00d;
      static_assert(kBadOrdinal !=
                    static_cast<uint32_t>(test::FlexibleXUnion::Tag::kWantMoreThan30Bytes));
      static_assert(kBadOrdinal !=
                    static_cast<uint32_t>(test::FlexibleXUnion::Tag::kWantMoreThan4Handles));
      auto real_response =
          reinterpret_cast<fidl_xunion_t*>(&real_msg_bytes[sizeof(fidl_message_header_t)]);
      real_response->tag = kBadOrdinal;

      auto indicator_response =
          reinterpret_cast<test::ReceiveFlexibleEnvelope::GetUnknownXUnionMoreHandlesResponse*>(
              indicator_msg->bytes());
      switch (indicator_response->xu.which()) {
        case test::FlexibleXUnion::Tag::kWantMoreThan30Bytes: {
          // Create a message with more bytes than expected
          constexpr uint32_t kUnknownBytes = 5000;
          constexpr uint32_t kUnknownHandles = 0;
          real_response->envelope = fidl_envelope_t{
              .num_bytes = kUnknownBytes,
              .num_handles = kUnknownHandles,
              .presence = FIDL_ALLOC_PRESENT,
          };
          real_msg.num_bytes =
              sizeof(fidl_message_header_t) + sizeof(fidl_xunion_t) + kUnknownBytes;
          real_msg.num_handles = kUnknownHandles;
          memset(&real_msg_bytes[sizeof(fidl_message_header_t) + sizeof(fidl_xunion_t)], 0xAA,
                 kUnknownBytes);
          break;
        }
        case test::FlexibleXUnion::Tag::kWantMoreThan4Handles: {
          // Create a message with more handles than expected
          constexpr uint32_t kUnknownBytes = 16;
          constexpr uint32_t kUnknownHandles = ZX_CHANNEL_MAX_MSG_HANDLES;
          for (uint32_t i = 0; i < kUnknownHandles; i++) {
            ZX_ASSERT(zx_event_create(0, &real_msg_handles[i]) == ZX_OK);
          }
          real_response->envelope = fidl_envelope_t{
              .num_bytes = kUnknownBytes,
              .num_handles = kUnknownHandles,
              .presence = FIDL_ALLOC_PRESENT,
          };
          real_msg.num_bytes =
              sizeof(fidl_message_header_t) + sizeof(fidl_xunion_t) + kUnknownBytes;
          real_msg.num_handles = kUnknownHandles;
          memset(&real_msg_bytes[sizeof(fidl_message_header_t) + sizeof(fidl_xunion_t)], 0xBB,
                 kUnknownBytes);
          break;
        }
        case test::FlexibleXUnion::Tag::kUnknown:
          ZX_ASSERT_MSG(false, "Cannot reach here");
      }
    }
    zx_status_t status = channel_->write(0, real_msg.bytes, real_msg.num_bytes, real_msg.handles,
                                         real_msg.num_handles);
    ZX_ASSERT(status == ZX_OK);
    return ZX_OK;
  }

  RewriteTransaction(zx_txid_t txid, zx::unowned_channel channel)
      : txid_(txid), channel_(std::move(channel)) {}

 private:
  zx_txid_t txid_;
  zx::unowned_channel channel_;
};

class Server : test::ReceiveFlexibleEnvelope::Interface, private async_wait_t {
 public:
  void GetUnknownXUnionMoreBytes(GetUnknownXUnionMoreBytesCompleter::Sync& completer) override {
    test::FlexibleXUnion xunion;
    fidl::aligned<fidl::Array<uint8_t, 30>> array = {};
    xunion.set_want_more_than_30_bytes(fidl::unowned_ptr(&array));
    completer.Reply(std::move(xunion));
  }

  void GetUnknownXUnionMoreHandles(GetUnknownXUnionMoreHandlesCompleter::Sync& completer) override {
    test::FlexibleXUnion xunion;
    fidl::Array<zx::handle, 4> array = {};
    xunion.set_want_more_than_4_handles(fidl::unowned_ptr(&array));
    completer.Reply(std::move(xunion));
  }

  void GetUnknownTableMoreBytes(GetUnknownTableMoreBytesCompleter::Sync& completer) override {
    fidl::aligned<fidl::Array<uint8_t, 30>> array = {};
    auto table_builder =
        test::FlexibleTable::UnownedBuilder().set_want_more_than_30_bytes_at_ordinal_3(
            fidl::unowned_ptr(&array));
    completer.Reply(table_builder.build());
  }

  void GetUnknownTableMoreHandles(GetUnknownTableMoreHandlesCompleter::Sync& completer) override {
    fidl::aligned<fidl::Array<zx::handle, 4>> array = {};
    auto table_builder =
        test::FlexibleTable::UnownedBuilder().set_want_more_than_4_handles_at_ordinal_4(
            fidl::unowned_ptr(&array));
    completer.Reply(table_builder.build());
  }

  Server(async_dispatcher_t* dispatcher, zx::channel channel)
      : async_wait_t({
            .state = ASYNC_STATE_INIT,
            .handler = &MessageHandler,
            .object = channel.release(),
            .trigger = ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED,
            .options = 0,
        }),
        dispatcher_(dispatcher) {
    async_begin_wait(dispatcher_, this);
  }

  ~Server() {
    async_cancel_wait(dispatcher_, this);
    zx_handle_close(async_wait_t::object);
  }

  void HandleMessage(async_dispatcher_t* dispatcher, zx_status_t status,
                     const zx_packet_signal_t* signal) {
    if (status != ZX_OK) {
      return;
    }
    if (signal->observed & ZX_CHANNEL_READABLE) {
      for (uint64_t i = 0; i < signal->count; i++) {
        fidl_msg_t msg = {
            .bytes = &bytes_[0],
            .handles = &handles_[0],
            .num_bytes = 0u,
            .num_handles = 0u,
        };
        status = zx_channel_read(async_wait_t::object, 0, msg.bytes, msg.handles,
                                 ZX_CHANNEL_MAX_MSG_BYTES, ZX_CHANNEL_MAX_MSG_HANDLES,
                                 &msg.num_bytes, &msg.num_handles);
        if (status != ZX_OK || msg.num_bytes < sizeof(fidl_message_header_t)) {
          return;
        }

        auto hdr = reinterpret_cast<fidl_message_header_t*>(msg.bytes);
        RewriteTransaction txn(hdr->txid, zx::unowned_channel(async_wait_t::object));
        test::ReceiveFlexibleEnvelope::Dispatch(this, &msg, &txn);
      }

      // Will only get here if every single message was handled synchronously and successfully.
      async_begin_wait(dispatcher_, this);
    } else {
      ZX_ASSERT(signal->observed & ZX_CHANNEL_PEER_CLOSED);
    }
  }

  // Implement the function required by |async_wait_t|.
  static void MessageHandler(async_dispatcher_t* dispatcher, async_wait_t* wait, zx_status_t status,
                             const zx_packet_signal_t* signal) {
    static_cast<Server*>(wait)->HandleMessage(dispatcher, status, signal);
  }

 private:
  async_dispatcher_t* dispatcher_;
  std::unique_ptr<char[]> bytes_ = std::make_unique<char[]>(ZX_CHANNEL_MAX_MSG_BYTES);
  std::unique_ptr<zx_handle_t[]> handles_ =
      std::make_unique<zx_handle_t[]>(ZX_CHANNEL_MAX_MSG_HANDLES);
};

}  // namespace

class FlexibleEnvelopeTest : public ::testing::Test {
 protected:
  virtual void SetUp() {
    loop_ = std::make_unique<async::Loop>(&kAsyncLoopConfigAttachToCurrentThread);
    ASSERT_EQ(loop_->StartThread("test_llcpp_flexible_envelope_server"), ZX_OK);
    ASSERT_EQ(zx::channel::create(0, &client_end_, &server_end_), ZX_OK);
    server_ = std::make_unique<Server>(loop_->dispatcher(), std::move(server_end_));
  }

  virtual void TearDown() {
    loop_->Quit();
    loop_->JoinThreads();
  }

  test::ReceiveFlexibleEnvelope::SyncClient TakeClient() {
    EXPECT_TRUE(client_end_.is_valid());
    return test::ReceiveFlexibleEnvelope::SyncClient(std::move(client_end_));
  }

 private:
  std::unique_ptr<async::Loop> loop_;
  std::unique_ptr<Server> server_;
  zx::channel client_end_;
  zx::channel server_end_;
};

static_assert(fidl::internal::ClampedMessageSize<
                  test::ReceiveFlexibleEnvelope::GetUnknownXUnionMoreBytesResponse,
                  fidl::MessageDirection::kReceiving>() == ZX_CHANNEL_MAX_MSG_BYTES,
              "Cannot assume any limit on byte size apart from the channel limit");

TEST_F(FlexibleEnvelopeTest, ReceiveUnknownVariantWithMoreBytes) {
  auto client = TakeClient();
  auto result = client.GetUnknownXUnionMoreBytes();
  EXPECT_TRUE(result.ok());
  EXPECT_EQ(result.error(), nullptr) << result.error();
  ASSERT_EQ(result.status(), ZX_OK) << zx_status_get_string(result.status());
  ASSERT_EQ(result.value().xu.which(), test::FlexibleXUnion::Tag::kUnknown);
}

static_assert(fidl::internal::ClampedHandleCount<
                  test::ReceiveFlexibleEnvelope::GetUnknownXUnionMoreHandlesResponse,
                  fidl::MessageDirection::kReceiving>() == ZX_CHANNEL_MAX_MSG_HANDLES,
              "Cannot assume any limit on handle count apart from the channel limit");

TEST_F(FlexibleEnvelopeTest, ReceiveUnknownVariantWithMoreHandles) {
  auto client = TakeClient();
  auto result = client.GetUnknownXUnionMoreHandles();
  EXPECT_TRUE(result.ok());
  EXPECT_EQ(result.error(), nullptr) << result.error();
  ASSERT_EQ(result.status(), ZX_OK) << zx_status_get_string(result.status());
  ASSERT_EQ(result.value().xu.which(), test::FlexibleXUnion::Tag::kUnknown);
}

static_assert(fidl::internal::ClampedMessageSize<
                  test::ReceiveFlexibleEnvelope::GetUnknownTableMoreBytesResponse,
                  fidl::MessageDirection::kReceiving>() == ZX_CHANNEL_MAX_MSG_BYTES,
              "Cannot assume any limit on byte size apart from the channel limit");

TEST_F(FlexibleEnvelopeTest, ReceiveUnknownTableFieldWithMoreBytes) {
  auto client = TakeClient();
  auto result = client.GetUnknownTableMoreBytes();
  EXPECT_TRUE(result.ok());
  EXPECT_EQ(result.error(), nullptr) << result.error();
  ASSERT_EQ(result.status(), ZX_OK) << zx_status_get_string(result.status());
  EXPECT_FALSE(result.value().t.has_want_more_than_30_bytes_at_ordinal_3());
  EXPECT_FALSE(result.value().t.has_want_more_than_4_handles_at_ordinal_4());
}

static_assert(fidl::internal::ClampedHandleCount<
                  test::ReceiveFlexibleEnvelope::GetUnknownTableMoreHandlesResponse,
                  fidl::MessageDirection::kReceiving>() == ZX_CHANNEL_MAX_MSG_HANDLES,
              "Cannot assume any limit on handle count apart from the channel limit");

TEST_F(FlexibleEnvelopeTest, ReceiveUnknownTableFieldWithMoreHandles) {
  auto client = TakeClient();
  auto result = client.GetUnknownTableMoreHandles();
  EXPECT_TRUE(result.ok());
  EXPECT_EQ(result.error(), nullptr) << result.error();
  ASSERT_EQ(result.status(), ZX_OK) << zx_status_get_string(result.status());
  EXPECT_FALSE(result.value().t.has_want_more_than_30_bytes_at_ordinal_3());
  EXPECT_FALSE(result.value().t.has_want_more_than_4_handles_at_ordinal_4());
}
