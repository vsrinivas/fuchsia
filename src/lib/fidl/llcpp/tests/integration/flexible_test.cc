// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/llcpptest.flexible.test/cpp/wire.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/wait.h>
#include <zircon/fidl.h>
#include <zircon/status.h>

#include <gtest/gtest.h>

namespace test = ::llcpptest_flexible_test;

// The only difference between StrictUnboundedXUnion and StrictBoundedXUnion is that
// StrictBoundedXUnion limits the vector payload length to 200 bytes. Therefore, by observing that
// sizeof(fidl::WireResult<test::ReceiveStrictEnvelope::GetUnboundedXUnion>) is less than 200, we
// can guarantee that the response storage is not inlined. Rather, it is allocated on the heap.
static_assert(sizeof(fidl::WireResult<test::ReceiveStrictEnvelope::GetUnboundedXUnion>) < 200,
              "Result of GetUnboundedXUnion should be stored as a pointer to heap allocation");

// GetBoundedXUnion should be inlined, because it is smaller than 512, but bigger than 200, making
// the entire ResultOf object bigger than 200. The assertion triggers when the ResultOf object size
// falls below 200, at which point we know it is physically incapable of holding a GetBoundedXUnion
// inline, so probably used heap allocation. Here we are trying to test this without plumbing extra
// flags which themselves need to be tested.
static_assert(sizeof(fidl::WireResult<test::ReceiveStrictEnvelope::GetBoundedXUnion>) > 200,
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
    auto indicator_msg_bytes = indicator_msg->CopyBytes();
    ZX_ASSERT(
        indicator_msg_bytes.size() >=
        sizeof(fidl::WireResponse<test::ReceiveFlexibleEnvelope::GetUnknownXUnionMoreHandles>));

    char real_msg_bytes[ZX_CHANNEL_MAX_MSG_BYTES] = {};
    zx_handle_disposition_t real_msg_handles[ZX_CHANNEL_MAX_MSG_HANDLES] = {};
    reinterpret_cast<fidl_message_header_t*>(&real_msg_bytes[0])->txid = txid_;
    fidl_outgoing_msg_t real_msg = {
        .type = FIDL_OUTGOING_MSG_TYPE_BYTE,
        .byte =
            {
                .bytes = &real_msg_bytes[0],
                .handles = &real_msg_handles[0],
                .num_bytes = 0u,
                .num_handles = 0u,
            },
    };

    // Determine if |indicator_msg| has a xunion or a table, by inspecting the first few bytes.
    auto maybe_vector = reinterpret_cast<const fidl_vector_t*>(indicator_msg_bytes.data() +
                                                               sizeof(fidl_message_header_t));
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
        ZX_ASSERT(real_msg.type == FIDL_OUTGOING_MSG_TYPE_BYTE);
        real_msg.byte.num_bytes = envelope_payload_offset + kUnknownBytes;
        real_msg.byte.num_handles = kUnknownHandles;
        memset(&real_msg_bytes[envelope_payload_offset], 0xAA, kUnknownBytes);
      } else {
        // The |want_more_than_4_handles_at_ordinal_4| field was set.
        // Create a message with more handles than expected
        constexpr uint32_t kUnknownBytes = 16;
        constexpr uint32_t kUnknownHandles = ZX_CHANNEL_MAX_MSG_HANDLES;
        for (uint32_t i = 0; i < kUnknownHandles; i++) {
          ZX_ASSERT(zx_event_create(0, &real_msg_handles[i].handle) == ZX_OK);
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
        ZX_ASSERT(real_msg.type == FIDL_OUTGOING_MSG_TYPE_BYTE);
        real_msg.byte.num_bytes = envelope_payload_offset + kUnknownBytes;
        real_msg.byte.num_handles = kUnknownHandles;
        memset(&real_msg_bytes[envelope_payload_offset], 0xBB, kUnknownBytes);
      }
    } else {
      // Manually craft the actual response which has an unknown ordinal
      constexpr uint32_t kBadOrdinal = 0x8badf00d;
      static_assert(kBadOrdinal !=
                    static_cast<uint32_t>(test::wire::FlexibleXUnion::Tag::kWantMoreThan30Bytes));
      static_assert(kBadOrdinal !=
                    static_cast<uint32_t>(test::wire::FlexibleXUnion::Tag::kWantMoreThan4Handles));
      auto real_response =
          reinterpret_cast<fidl_xunion_t*>(&real_msg_bytes[sizeof(fidl_message_header_t)]);
      real_response->tag = kBadOrdinal;

      auto indicator_response = reinterpret_cast<
          const fidl::WireResponse<test::ReceiveFlexibleEnvelope::GetUnknownXUnionMoreHandles>*>(
          indicator_msg_bytes.data());
      switch (indicator_response->xu.which()) {
        case test::wire::FlexibleXUnion::Tag::kWantMoreThan30Bytes: {
          // Create a message with more bytes than expected
          constexpr uint32_t kUnknownBytes = 5000;
          constexpr uint32_t kUnknownHandles = 0;
          real_response->envelope = fidl_envelope_t{
              .num_bytes = kUnknownBytes,
              .num_handles = kUnknownHandles,
              .presence = FIDL_ALLOC_PRESENT,
          };
          ZX_ASSERT(real_msg.type == FIDL_OUTGOING_MSG_TYPE_BYTE);
          real_msg.byte.num_bytes =
              sizeof(fidl_message_header_t) + sizeof(fidl_xunion_t) + kUnknownBytes;
          real_msg.byte.num_handles = kUnknownHandles;
          memset(&real_msg_bytes[sizeof(fidl_message_header_t) + sizeof(fidl_xunion_t)], 0xAA,
                 kUnknownBytes);
          break;
        }
        case test::wire::FlexibleXUnion::Tag::kWantMoreThan4Handles: {
          // Create a message with more handles than expected
          constexpr uint32_t kUnknownBytes = 16;
          constexpr uint32_t kUnknownHandles = ZX_CHANNEL_MAX_MSG_HANDLES;
          for (uint32_t i = 0; i < kUnknownHandles; i++) {
            ZX_ASSERT(zx_event_create(0, &real_msg_handles[i].handle) == ZX_OK);
          }
          real_response->envelope = fidl_envelope_t{
              .num_bytes = kUnknownBytes,
              .num_handles = kUnknownHandles,
              .presence = FIDL_ALLOC_PRESENT,
          };
          ZX_ASSERT(real_msg.type == FIDL_OUTGOING_MSG_TYPE_BYTE);
          real_msg.byte.num_bytes =
              sizeof(fidl_message_header_t) + sizeof(fidl_xunion_t) + kUnknownBytes;
          real_msg.byte.num_handles = kUnknownHandles;
          memset(&real_msg_bytes[sizeof(fidl_message_header_t) + sizeof(fidl_xunion_t)], 0xBB,
                 kUnknownBytes);
          break;
        }
        case test::wire::FlexibleXUnion::Tag::kUnknown:
          ZX_ASSERT_MSG(false, "Cannot reach here");
      }
    }
    ZX_ASSERT(real_msg.type == FIDL_OUTGOING_MSG_TYPE_BYTE);
    zx_status_t status = channel_->write_etc(0, real_msg.byte.bytes, real_msg.byte.num_bytes,
                                             real_msg.byte.handles, real_msg.byte.num_handles);
    ZX_ASSERT(status == ZX_OK);
    return ZX_OK;
  }

  RewriteTransaction(zx_txid_t txid, zx::unowned_channel channel)
      : txid_(txid), channel_(std::move(channel)) {}

 private:
  zx_txid_t txid_;
  zx::unowned_channel channel_;
};

class Server : fidl::WireServer<test::ReceiveFlexibleEnvelope>, private async_wait_t {
 public:
  void GetUnknownXUnionMoreBytes(GetUnknownXUnionMoreBytesRequestView request,
                                 GetUnknownXUnionMoreBytesCompleter::Sync& completer) override {
    test::wire::FlexibleXUnion xunion;
    fidl::Array<uint8_t, 30> array = {};
    xunion.set_want_more_than_30_bytes(
        fidl::ObjectView<fidl::Array<uint8_t, 30>>::FromExternal(&array));
    completer.Reply(std::move(xunion));
  }

  void GetUnknownXUnionMoreHandles(GetUnknownXUnionMoreHandlesRequestView request,
                                   GetUnknownXUnionMoreHandlesCompleter::Sync& completer) override {
    test::wire::FlexibleXUnion xunion;
    fidl::Array<zx::handle, 4> array = {};
    xunion.set_want_more_than_4_handles(
        fidl::ObjectView<fidl::Array<zx::handle, 4>>::FromExternal(&array));
    completer.Reply(std::move(xunion));
  }

  void GetUnknownTableMoreBytes(GetUnknownTableMoreBytesRequestView request,
                                GetUnknownTableMoreBytesCompleter::Sync& completer) override {
    fidl::Arena allocator;
    test::wire::FlexibleTable flexible_table(allocator);
    flexible_table.set_want_more_than_30_bytes_at_ordinal_3(allocator);
    completer.Reply(std::move(flexible_table));
  }

  void GetUnknownTableMoreHandles(GetUnknownTableMoreHandlesRequestView request,
                                  GetUnknownTableMoreHandlesCompleter::Sync& completer) override {
    fidl::Arena allocator;
    test::wire::FlexibleTable flexible_table(allocator);
    flexible_table.set_want_more_than_4_handles_at_ordinal_4(allocator);
    completer.Reply(std::move(flexible_table));
  }

  Server(async_dispatcher_t* dispatcher, fidl::ServerEnd<test::ReceiveFlexibleEnvelope> channel)
      : async_wait_t({
            .state = ASYNC_STATE_INIT,
            .handler = &MessageHandler,
            .object = channel.TakeChannel().release(),
            .trigger = ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED,
            .options = 0,
        }),
        dispatcher_(dispatcher) {
    async_begin_wait(dispatcher_, this);
  }

  ~Server() override {
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
        fidl::IncomingMessage msg = fidl::ChannelReadEtc(
            async_wait_t::object, 0, fidl::BufferSpan(bytes_->data(), bytes_->size()),
            cpp20::span(*handles_));
        if (!msg.ok()) {
          return;
        }

        auto hdr = msg.header();
        RewriteTransaction txn(hdr->txid, zx::unowned_channel(async_wait_t::object));
        fidl::WireDispatch<test::ReceiveFlexibleEnvelope>(this, std::move(msg), &txn);
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
  std::unique_ptr<std::array<uint8_t, ZX_CHANNEL_MAX_MSG_BYTES>> bytes_ =
      std::make_unique<std::array<uint8_t, ZX_CHANNEL_MAX_MSG_BYTES>>();
  std::unique_ptr<std::array<zx_handle_info_t, ZX_CHANNEL_MAX_MSG_HANDLES>> handles_ =
      std::make_unique<std::array<zx_handle_info_t, ZX_CHANNEL_MAX_MSG_HANDLES>>();
};

}  // namespace

class FlexibleEnvelopeTest : public ::testing::Test {
 protected:
  virtual void SetUp() {
    loop_ = std::make_unique<async::Loop>(&kAsyncLoopConfigAttachToCurrentThread);
    ASSERT_EQ(loop_->StartThread("test_llcpp_flexible_envelope_server"), ZX_OK);
    zx::status server_end = fidl::CreateEndpoints(&client_end_);
    ASSERT_EQ(server_end.status_value(), ZX_OK);
    server_ = std::make_unique<Server>(loop_->dispatcher(), std::move(*server_end));
  }

  virtual void TearDown() {
    loop_->Quit();
    loop_->JoinThreads();
  }

  fidl::WireSyncClient<test::ReceiveFlexibleEnvelope> TakeClient() {
    EXPECT_TRUE(client_end_.is_valid());
    return fidl::WireSyncClient<test::ReceiveFlexibleEnvelope>(std::move(client_end_));
  }

 private:
  std::unique_ptr<async::Loop> loop_;
  std::unique_ptr<Server> server_;
  fidl::ClientEnd<test::ReceiveFlexibleEnvelope> client_end_;
};

static_assert(fidl::internal::ClampedMessageSize<
                  fidl::WireResponse<test::ReceiveFlexibleEnvelope::GetUnknownXUnionMoreBytes>,
                  fidl::MessageDirection::kReceiving>() == ZX_CHANNEL_MAX_MSG_BYTES,
              "Cannot assume any limit on byte size apart from the channel limit");

TEST_F(FlexibleEnvelopeTest, ReceiveUnknownVariantWithMoreBytes) {
  auto client = TakeClient();
  auto result = client.GetUnknownXUnionMoreBytes();
  EXPECT_TRUE(result.ok());
  ASSERT_EQ(result.status(), ZX_OK) << zx_status_get_string(result.status());
  ASSERT_EQ(result.value().xu.which(), test::wire::FlexibleXUnion::Tag::kUnknown);
}

static_assert(fidl::internal::ClampedHandleCount<
                  fidl::WireResponse<test::ReceiveFlexibleEnvelope::GetUnknownXUnionMoreHandles>,
                  fidl::MessageDirection::kReceiving>() == ZX_CHANNEL_MAX_MSG_HANDLES,
              "Cannot assume any limit on handle count apart from the channel limit");

TEST_F(FlexibleEnvelopeTest, ReceiveUnknownVariantWithMoreHandles) {
  auto client = TakeClient();
  auto result = client.GetUnknownXUnionMoreHandles();
  EXPECT_TRUE(result.ok());
  ASSERT_EQ(result.status(), ZX_OK) << zx_status_get_string(result.status());
  ASSERT_EQ(result.value().xu.which(), test::wire::FlexibleXUnion::Tag::kUnknown);
}

static_assert(fidl::internal::ClampedMessageSize<
                  fidl::WireResponse<test::ReceiveFlexibleEnvelope::GetUnknownTableMoreBytes>,
                  fidl::MessageDirection::kReceiving>() == ZX_CHANNEL_MAX_MSG_BYTES,
              "Cannot assume any limit on byte size apart from the channel limit");

TEST_F(FlexibleEnvelopeTest, ReceiveUnknownTableFieldWithMoreBytes) {
  auto client = TakeClient();
  auto result = client.GetUnknownTableMoreBytes();
  EXPECT_TRUE(result.ok());
  ASSERT_EQ(result.status(), ZX_OK) << zx_status_get_string(result.status());
  EXPECT_FALSE(result.value().t.has_want_more_than_30_bytes_at_ordinal_3());
  EXPECT_FALSE(result.value().t.has_want_more_than_4_handles_at_ordinal_4());
}

static_assert(fidl::internal::ClampedHandleCount<
                  fidl::WireResponse<test::ReceiveFlexibleEnvelope::GetUnknownTableMoreHandles>,
                  fidl::MessageDirection::kReceiving>() == ZX_CHANNEL_MAX_MSG_HANDLES,
              "Cannot assume any limit on handle count apart from the channel limit");

TEST_F(FlexibleEnvelopeTest, ReceiveUnknownTableFieldWithMoreHandles) {
  auto client = TakeClient();
  auto result = client.GetUnknownTableMoreHandles();
  EXPECT_TRUE(result.ok());
  ASSERT_EQ(result.status(), ZX_OK) << zx_status_get_string(result.status());
  EXPECT_FALSE(result.value().t.has_want_more_than_30_bytes_at_ordinal_3());
  EXPECT_FALSE(result.value().t.has_want_more_than_4_handles_at_ordinal_4());
}
