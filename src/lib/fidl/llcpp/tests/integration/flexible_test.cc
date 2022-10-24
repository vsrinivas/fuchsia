// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/llcpptest.flexible.test/cpp/wire.h>
#include <fidl/llcpptest.flexible.test/cpp/wire_test_base.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/wait.h>
#include <lib/zx/event.h>
#include <zircon/fidl.h>
#include <zircon/status.h>

#include <gtest/gtest.h>

namespace test = ::llcpptest_flexible_test;

// These tests verify that the messaging APIs allocate a bespoke amount of memory
// depending on the shapes of types in the methods in the protocol, but also
// anticipate future bytes/handles size additions to flexible types, and allocate
// the transport maximum in those cases.
namespace {

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
class RewriteTransaction : public fidl::Transaction {
 public:
  std::unique_ptr<Transaction> TakeOwnership() override {
    ZX_ASSERT_MSG(false, "Never called");
    return {};
  }

  void Close(zx_status_t epitaph) override {
    ZX_ASSERT_MSG(false, "Transaction::Close called with epitaph %d", epitaph);
  }

  zx_status_t Reply(fidl::OutgoingMessage* indicator_msg,
                    fidl::WriteOptions write_options) override {
    ZX_ASSERT(txid_ != 0);
    auto indicator_msg_bytes = indicator_msg->CopyBytes();

    char real_msg_bytes[ZX_CHANNEL_MAX_MSG_BYTES] = {};
    zx_handle_t real_msg_handles[ZX_CHANNEL_MAX_MSG_HANDLES] = {};
    fidl_channel_handle_metadata_t real_msg_handle_metadata[ZX_CHANNEL_MAX_MSG_HANDLES] = {};
    // Copy from original header to get magic, flags, and ordinals.
    ZX_ASSERT(indicator_msg_bytes.size() >= sizeof(fidl_message_header_t));
    memcpy(real_msg_bytes, indicator_msg_bytes.data(), sizeof(fidl_message_header_t));
    fidl_message_header_t* header = reinterpret_cast<fidl_message_header_t*>(real_msg_bytes);
    header->txid = txid_;
    fidl_outgoing_msg_t real_msg = {
        .type = FIDL_OUTGOING_MSG_TYPE_BYTE,
        .byte =
            {
                .bytes = real_msg_bytes,
                .handles = real_msg_handles,
                .handle_metadata =
                    reinterpret_cast<fidl_handle_metadata_t*>(real_msg_handle_metadata),
                .num_bytes = 0u,
                .num_handles = 0u,
            },
    };

    ZX_ASSERT(indicator_msg_bytes.size() >=
              sizeof(fidl::internal::TransactionalResponse<
                     test::ReceiveFlexibleEnvelope::GetUnknownXUnionMoreHandles>));
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
            sizeof(fidl_message_header_t) + sizeof(fidl_table_t) + sizeof(fidl_envelope_v2_t) * 2;
        const auto envelope_payload_offset = envelope_header_offset + sizeof(fidl_envelope_v2_t);
        auto envelope =
            reinterpret_cast<fidl_envelope_v2_t*>(&real_msg_bytes[envelope_header_offset]);
        *envelope = fidl_envelope_v2_t{
            .num_bytes = kUnknownBytes,
            .num_handles = kUnknownHandles,
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
          ZX_ASSERT(zx_event_create(0, &real_msg_handles[i]) == ZX_OK);
        }
        real_response->envelopes.count = 4;
        const auto envelope_header_offset =
            sizeof(fidl_message_header_t) + sizeof(fidl_table_t) + sizeof(fidl_envelope_v2_t) * 3;
        const auto envelope_payload_offset = envelope_header_offset + sizeof(fidl_envelope_v2_t);
        auto envelope =
            reinterpret_cast<fidl_envelope_v2_t*>(&real_msg_bytes[envelope_header_offset]);
        *envelope = fidl_envelope_v2_t{
            .num_bytes = kUnknownBytes,
            .num_handles = kUnknownHandles,
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
          reinterpret_cast<fidl_xunion_v2_t*>(&real_msg_bytes[sizeof(fidl_message_header_t)]);
      real_response->tag = kBadOrdinal;

      auto indicator_response = reinterpret_cast<const fidl::internal::TransactionalResponse<
          test::ReceiveFlexibleEnvelope::GetUnknownXUnionMoreHandles>*>(indicator_msg_bytes.data());
      switch (indicator_response->body.xu.Which()) {
        case test::wire::FlexibleXUnion::Tag::kWantMoreThan30Bytes: {
          // Create a message with more bytes than expected
          constexpr uint32_t kUnknownBytes = 5000;
          constexpr uint32_t kUnknownHandles = 0;
          real_response->envelope = fidl_envelope_v2_t{
              .num_bytes = kUnknownBytes,
              .num_handles = kUnknownHandles,
          };
          ZX_ASSERT(real_msg.type == FIDL_OUTGOING_MSG_TYPE_BYTE);
          real_msg.byte.num_bytes =
              sizeof(fidl_message_header_t) + sizeof(fidl_xunion_v2_t) + kUnknownBytes;
          real_msg.byte.num_handles = kUnknownHandles;
          memset(&real_msg_bytes[sizeof(fidl_message_header_t) + sizeof(fidl_xunion_v2_t)], 0xAA,
                 kUnknownBytes);
          break;
        }
        case test::wire::FlexibleXUnion::Tag::kWantMoreThan4Handles: {
          // Create a message with more handles than expected
          constexpr uint32_t kUnknownBytes = 16;
          constexpr uint32_t kUnknownHandles = ZX_CHANNEL_MAX_MSG_HANDLES;
          for (uint32_t i = 0; i < kUnknownHandles; i++) {
            ZX_ASSERT(zx_event_create(0, &real_msg_handles[i]) == ZX_OK);
          }
          real_response->envelope = fidl_envelope_v2_t{
              .num_bytes = kUnknownBytes,
              .num_handles = kUnknownHandles,
          };
          ZX_ASSERT(real_msg.type == FIDL_OUTGOING_MSG_TYPE_BYTE);
          real_msg.byte.num_bytes =
              sizeof(fidl_message_header_t) + sizeof(fidl_xunion_v2_t) + kUnknownBytes;
          real_msg.byte.num_handles = kUnknownHandles;
          memset(&real_msg_bytes[sizeof(fidl_message_header_t) + sizeof(fidl_xunion_v2_t)], 0xBB,
                 kUnknownBytes);
          break;
        }
        default:
          ZX_ASSERT_MSG(false, "Cannot reach here");
      }
    }
    ZX_ASSERT(real_msg.type == FIDL_OUTGOING_MSG_TYPE_BYTE);
    zx_handle_disposition_t handle_dispositions[ZX_CHANNEL_MAX_MSG_HANDLES];
    fidl_channel_handle_metadata_t* metadata =
        reinterpret_cast<fidl_channel_handle_metadata_t*>(real_msg.byte.handle_metadata);
    for (uint32_t i = 0; i < real_msg.byte.num_handles; i++) {
      handle_dispositions[i] = {
          .operation = ZX_HANDLE_OP_MOVE,
          .handle = real_msg.byte.handles[i],
          .type = metadata[i].obj_type,
          .rights = metadata[i].rights,
          .result = ZX_OK,
      };
    }
    zx_status_t status = channel_->write_etc(0, real_msg.byte.bytes, real_msg.byte.num_bytes,
                                             handle_dispositions, real_msg.byte.num_handles);
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
  void GetUnknownXUnionMoreBytes(GetUnknownXUnionMoreBytesCompleter::Sync& completer) override {
    test::wire::FlexibleXUnion xunion;
    fidl::Array<uint8_t, 30> array = {};
    completer.Reply(test::wire::FlexibleXUnion::WithWantMoreThan30Bytes(
        fidl::ObjectView<fidl::Array<uint8_t, 30>>::FromExternal(&array)));
  }

  void GetUnknownXUnionMoreHandles(GetUnknownXUnionMoreHandlesCompleter::Sync& completer) override {
    test::wire::FlexibleXUnion xunion;
    fidl::Array<zx::handle, 4> array = {};
    completer.Reply(test::wire::FlexibleXUnion::WithWantMoreThan4Handles(
        fidl::ObjectView<fidl::Array<zx::handle, 4>>::FromExternal(&array)));
  }

  void GetUnknownTableMoreBytes(GetUnknownTableMoreBytesCompleter::Sync& completer) override {
    fidl::Arena allocator;
    auto flexible_table = test::wire::FlexibleTable::Builder(allocator)
                              .want_more_than_30_bytes_at_ordinal_3({})
                              .Build();
    completer.Reply(std::move(flexible_table));
  }

  void GetUnknownTableMoreHandles(GetUnknownTableMoreHandlesCompleter::Sync& completer) override {
    fidl::Arena allocator;
    auto flexible_table = test::wire::FlexibleTable::Builder(allocator)
                              .want_more_than_4_handles_at_ordinal_4({})
                              .Build();
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
        fidl::IncomingHeaderAndMessage msg =
            fidl::MessageRead(zx::unowned_channel(async_wait_t::object),
                              fidl::ChannelMessageStorageView{
                                  .bytes = fidl::BufferSpan(bytes_->data(), bytes_->size()),
                                  .handles = handles_->data(),
                                  .handle_metadata = handle_metadata_->data(),
                                  .handle_capacity = static_cast<uint32_t>(handles_->size()),
                              });
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
  std::unique_ptr<std::array<zx_handle_t, ZX_CHANNEL_MAX_MSG_HANDLES>> handles_ =
      std::make_unique<std::array<zx_handle_t, ZX_CHANNEL_MAX_MSG_HANDLES>>();
  std::unique_ptr<std::array<fidl_channel_handle_metadata_t, ZX_CHANNEL_MAX_MSG_HANDLES>>
      handle_metadata_ = std::make_unique<
          std::array<fidl_channel_handle_metadata_t, ZX_CHANNEL_MAX_MSG_HANDLES>>();
};

class FlexibleEnvelopeTest : public ::testing::Test {
 protected:
  virtual void SetUp() {
    loop_ = std::make_unique<async::Loop>(&kAsyncLoopConfigAttachToCurrentThread);
    ASSERT_EQ(loop_->StartThread("test_llcpp_flexible_envelope_server"), ZX_OK);
    zx::result server_end = fidl::CreateEndpoints(&client_end_);
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
                  fidl::internal::TransactionalResponse<
                      test::ReceiveFlexibleEnvelope::GetUnknownXUnionMoreBytes>,
                  fidl::MessageDirection::kReceiving>() == ZX_CHANNEL_MAX_MSG_BYTES,
              "Cannot assume any limit on byte size apart from the channel limit");

TEST_F(FlexibleEnvelopeTest, ReceiveUnknownVariantWithMoreBytes) {
  auto client = TakeClient();
  auto result = client->GetUnknownXUnionMoreBytes();
  EXPECT_TRUE(result.ok());
  ASSERT_EQ(result.status(), ZX_OK) << zx_status_get_string(result.status());
  ASSERT_TRUE(result.value().xu.IsUnknown());
}

static_assert(fidl::internal::ClampedHandleCount<
                  fidl::internal::TransactionalResponse<
                      test::ReceiveFlexibleEnvelope::GetUnknownXUnionMoreHandles>,
                  fidl::MessageDirection::kReceiving>() == ZX_CHANNEL_MAX_MSG_HANDLES,
              "Cannot assume any limit on handle count apart from the channel limit");

TEST_F(FlexibleEnvelopeTest, ReceiveUnknownVariantWithMoreHandles) {
  auto client = TakeClient();
  auto result = client->GetUnknownXUnionMoreHandles();
  EXPECT_TRUE(result.ok());
  ASSERT_EQ(result.status(), ZX_OK) << zx_status_get_string(result.status());
  ASSERT_TRUE(result.value().xu.IsUnknown());
}

static_assert(
    fidl::internal::ClampedMessageSize<fidl::internal::TransactionalResponse<
                                           test::ReceiveFlexibleEnvelope::GetUnknownTableMoreBytes>,
                                       fidl::MessageDirection::kReceiving>() ==
        ZX_CHANNEL_MAX_MSG_BYTES,
    "Cannot assume any limit on byte size apart from the channel limit");

TEST_F(FlexibleEnvelopeTest, ReceiveUnknownTableFieldWithMoreBytes) {
  auto client = TakeClient();
  auto result = client->GetUnknownTableMoreBytes();
  EXPECT_TRUE(result.ok());
  ASSERT_EQ(result.status(), ZX_OK) << zx_status_get_string(result.status());
  EXPECT_FALSE(result.value().t.has_want_more_than_30_bytes_at_ordinal_3());
  EXPECT_FALSE(result.value().t.has_want_more_than_4_handles_at_ordinal_4());
}

static_assert(fidl::internal::ClampedHandleCount<
                  fidl::internal::TransactionalResponse<
                      test::ReceiveFlexibleEnvelope::GetUnknownTableMoreHandles>,
                  fidl::MessageDirection::kReceiving>() == ZX_CHANNEL_MAX_MSG_HANDLES,
              "Cannot assume any limit on handle count apart from the channel limit");

TEST_F(FlexibleEnvelopeTest, ReceiveUnknownTableFieldWithMoreHandles) {
  auto client = TakeClient();
  auto result = client->GetUnknownTableMoreHandles();
  EXPECT_TRUE(result.ok());
  ASSERT_EQ(result.status(), ZX_OK) << zx_status_get_string(result.status());
  EXPECT_FALSE(result.value().t.has_want_more_than_30_bytes_at_ordinal_3());
  EXPECT_FALSE(result.value().t.has_want_more_than_4_handles_at_ordinal_4());
}

// Test receiving an event with a flexible envelope that's larger than the types
// described by the FIDL schema.
class FlexibleEnvelopeEventTest : public ::testing::Test {
 public:
  void SetUp() final {
    zx::result endpoints = fidl::CreateEndpoints<test::ReceiveFlexibleEnvelope>();
    ASSERT_TRUE(endpoints.is_ok());
    client_end_ = std::move(endpoints->client);
    server_end_ = std::move(endpoints->server);
  }

  const fidl::ClientEnd<test::ReceiveFlexibleEnvelope>& client_end() const { return client_end_; }
  const fidl::ServerEnd<test::ReceiveFlexibleEnvelope>& server_end() const { return server_end_; }

  static constexpr uint32_t kBadOrdinal = 0x8badf00d;
  static_assert(kBadOrdinal !=
                static_cast<uint32_t>(test::wire::FlexibleXUnion::Tag::kWantMoreThan30Bytes));
  static_assert(kBadOrdinal !=
                static_cast<uint32_t>(test::wire::FlexibleXUnion::Tag::kWantMoreThan4Handles));

 private:
  fidl::ClientEnd<test::ReceiveFlexibleEnvelope> client_end_;
  fidl::ServerEnd<test::ReceiveFlexibleEnvelope> server_end_;
};

struct MessageStorage {
  template <typename FidlType>
  void Init() {
    FidlType value;
    static_assert(fidl::IsFidlTransactionalMessage<FidlType>::value);
    memcpy(bytes_, static_cast<void*>(&value), sizeof(value));
  }

  template <typename T>
  T* Build() {
    T* ptr = reinterpret_cast<T*>(&bytes_[num_bytes_]);
    num_bytes_ += FIDL_ALIGN(sizeof(T));
    return ptr;
  }

  void AddGarbage(uint32_t count) {
    memset(&bytes_[num_bytes_], 0xAA, count);
    num_bytes_ += FIDL_ALIGN(count);
  }

  void AddHandles(uint32_t count) {
    for (uint32_t i = 0; i < count; i++) {
      zx::event e;
      ZX_ASSERT(ZX_OK == zx::event::create(0, &e));
      handles_[num_handles_] = e.release();
      num_handles_++;
    }
  }

  zx_status_t Write(const zx::channel& channel) {
    return channel.write(0, bytes_, num_bytes_, handles_, num_handles_);
  }

 private:
  uint8_t bytes_[ZX_CHANNEL_MAX_MSG_BYTES] = {};
  zx_handle_t handles_[ZX_CHANNEL_MAX_MSG_HANDLES] = {};
  uint32_t num_bytes_ = sizeof(fidl_message_header_t);
  uint32_t num_handles_ = 0;
};

static_assert(
    fidl::internal::ClampedMessageSize<
        fidl::internal::TransactionalEvent<test::ReceiveFlexibleEnvelope::OnUnknownXUnionMoreBytes>,
        fidl::MessageDirection::kReceiving>() == ZX_CHANNEL_MAX_MSG_BYTES,
    "Cannot assume any limit on byte size apart from the channel limit");

TEST_F(FlexibleEnvelopeEventTest, ReceiveUnknownXUnionFieldWithMoreBytes) {
  MessageStorage storage;
  storage.Init<fidl::internal::TransactionalEvent<
      test::ReceiveFlexibleEnvelope::OnUnknownXUnionMoreBytes>>();

  // Manually craft a xunion response with an unknown ordinal that is larger
  // than expected.
  auto* real_response = storage.Build<fidl_xunion_v2_t>();
  real_response->tag = kBadOrdinal;
  constexpr uint32_t kUnknownBytes = 5000;
  constexpr uint32_t kUnknownHandles = 0;
  real_response->envelope = fidl_envelope_v2_t{
      .num_bytes = kUnknownBytes,
      .num_handles = kUnknownHandles,
  };
  storage.AddGarbage(kUnknownBytes);

  ASSERT_EQ(ZX_OK, storage.Write(server_end().channel()));

  class EventHandler
      : public fidl::testing::WireSyncEventHandlerTestBase<test::ReceiveFlexibleEnvelope> {
   public:
    void NotImplemented_(const std::string& name) final { ADD_FAILURE() << "Unexpected " << name; }

    void OnUnknownXUnionMoreBytes(
        fidl::WireEvent<test::ReceiveFlexibleEnvelope::OnUnknownXUnionMoreBytes>* event) final {
      EXPECT_FALSE(event->is_want_more_than_30_bytes());
      EXPECT_FALSE(event->is_want_more_than_4_handles());
      EXPECT_TRUE(event->IsUnknown());
      called = true;
    }

    bool called = false;
  };
  EventHandler event_handler;
  fidl::Status status = event_handler.HandleOneEvent(client_end());
  EXPECT_TRUE(status.ok()) << status;
  EXPECT_TRUE(event_handler.called);
}

static_assert(fidl::internal::ClampedHandleCount<
                  fidl::internal::TransactionalEvent<
                      test::ReceiveFlexibleEnvelope::OnUnknownXUnionMoreHandles>,
                  fidl::MessageDirection::kReceiving>() == ZX_CHANNEL_MAX_MSG_HANDLES,
              "Cannot assume any limit on handle count apart from the channel limit");

TEST_F(FlexibleEnvelopeEventTest, ReceiveUnknownXUnionFieldWithMoreHandles) {
  MessageStorage storage;
  storage.Init<fidl::internal::TransactionalEvent<
      test::ReceiveFlexibleEnvelope::OnUnknownXUnionMoreHandles>>();

  // Manually craft a xunion response with an unknown ordinal has more handles
  // than expected.
  auto* real_response = storage.Build<fidl_xunion_v2_t>();
  real_response->tag = kBadOrdinal;
  constexpr uint32_t kUnknownBytes = 16;
  constexpr uint32_t kUnknownHandles = ZX_CHANNEL_MAX_MSG_HANDLES;
  real_response->envelope = fidl_envelope_v2_t{
      .num_bytes = kUnknownBytes,
      .num_handles = kUnknownHandles,
  };
  storage.AddGarbage(kUnknownBytes);
  storage.AddHandles(kUnknownHandles);

  ASSERT_EQ(ZX_OK, storage.Write(server_end().channel()));

  class EventHandler
      : public fidl::testing::WireSyncEventHandlerTestBase<test::ReceiveFlexibleEnvelope> {
   public:
    void NotImplemented_(const std::string& name) final { ADD_FAILURE() << "Unexpected " << name; }

    void OnUnknownXUnionMoreHandles(
        fidl::WireEvent<test::ReceiveFlexibleEnvelope::OnUnknownXUnionMoreHandles>* event) final {
      EXPECT_FALSE(event->is_want_more_than_30_bytes());
      EXPECT_FALSE(event->is_want_more_than_4_handles());
      EXPECT_TRUE(event->IsUnknown());
      called = true;
    }

    bool called = false;
  };
  EventHandler event_handler;
  fidl::Status status = event_handler.HandleOneEvent(client_end());
  EXPECT_TRUE(status.ok()) << status;
  EXPECT_TRUE(event_handler.called);
}

static_assert(
    fidl::internal::ClampedMessageSize<
        fidl::internal::TransactionalEvent<test::ReceiveFlexibleEnvelope::OnUnknownTableMoreBytes>,
        fidl::MessageDirection::kReceiving>() == ZX_CHANNEL_MAX_MSG_BYTES,
    "Cannot assume any limit on byte size apart from the channel limit");

TEST_F(FlexibleEnvelopeEventTest, ReceiveUnknownTableFieldWithMoreBytes) {
  MessageStorage storage;
  storage.Init<
      fidl::internal::TransactionalEvent<test::ReceiveFlexibleEnvelope::OnUnknownTableMoreBytes>>();

  // Manually craft a table response with an unknown ordinal that is larger
  // than expected.
  auto* real_response = storage.Build<fidl_table_t>();
  real_response->envelopes.count = 4;
  auto* envelopes = storage.Build<fidl_envelope_v2_t[4]>();
  real_response->envelopes.data = reinterpret_cast<void*>(FIDL_ALLOC_PRESENT);  // NOLINT
  (*envelopes)[0] = fidl_envelope_v2_t{};
  (*envelopes)[1] = fidl_envelope_v2_t{};
  (*envelopes)[2] = fidl_envelope_v2_t{};
  constexpr uint32_t kUnknownBytes = 5000;
  constexpr uint32_t kUnknownHandles = 0;
  (*envelopes)[3] = fidl_envelope_v2_t{
      .num_bytes = kUnknownBytes,
      .num_handles = kUnknownHandles,
  };
  storage.AddGarbage(kUnknownBytes);

  ASSERT_EQ(ZX_OK, storage.Write(server_end().channel()));

  class EventHandler
      : public fidl::testing::WireSyncEventHandlerTestBase<test::ReceiveFlexibleEnvelope> {
   public:
    void NotImplemented_(const std::string& name) final { ADD_FAILURE() << "Unexpected " << name; }

    void OnUnknownTableMoreBytes(
        fidl::WireEvent<test::ReceiveFlexibleEnvelope::OnUnknownTableMoreBytes>* event) final {
      EXPECT_FALSE(event->has_want_more_than_30_bytes_at_ordinal_3());
      EXPECT_FALSE(event->has_want_more_than_4_handles_at_ordinal_4());
      EXPECT_TRUE(event->HasUnknownData());
      called = true;
    }

    bool called = false;
  };
  EventHandler event_handler;
  fidl::Status status = event_handler.HandleOneEvent(client_end());
  EXPECT_TRUE(status.ok()) << status;
  EXPECT_TRUE(event_handler.called);
}

static_assert(fidl::internal::ClampedMessageSize<
                  fidl::internal::TransactionalEvent<
                      test::ReceiveFlexibleEnvelope::OnUnknownTableMoreHandles>,
                  fidl::MessageDirection::kReceiving>() == ZX_CHANNEL_MAX_MSG_BYTES,
              "Cannot assume any limit on handle count apart from the channel limit");

TEST_F(FlexibleEnvelopeEventTest, ReceiveUnknownTableFieldWithMoreHandles) {
  MessageStorage storage;
  storage.Init<fidl::internal::TransactionalEvent<
      test::ReceiveFlexibleEnvelope::OnUnknownTableMoreHandles>>();

  // Manually craft a table response with an unknown ordinal that has more
  // handles than expected.
  auto* real_response = storage.Build<fidl_table_t>();
  real_response->envelopes.count = 4;
  auto* envelopes = storage.Build<fidl_envelope_v2_t[4]>();
  real_response->envelopes.data = reinterpret_cast<void*>(FIDL_ALLOC_PRESENT);  // NOLINT
  (*envelopes)[0] = fidl_envelope_v2_t{};
  (*envelopes)[1] = fidl_envelope_v2_t{};
  (*envelopes)[2] = fidl_envelope_v2_t{};
  constexpr uint32_t kUnknownBytes = 16;
  constexpr uint32_t kUnknownHandles = ZX_CHANNEL_MAX_MSG_HANDLES;
  (*envelopes)[3] = fidl_envelope_v2_t{
      .num_bytes = kUnknownBytes,
      .num_handles = kUnknownHandles,
  };
  storage.AddGarbage(kUnknownBytes);
  storage.AddHandles(kUnknownHandles);

  ASSERT_EQ(ZX_OK, storage.Write(server_end().channel()));

  class EventHandler
      : public fidl::testing::WireSyncEventHandlerTestBase<test::ReceiveFlexibleEnvelope> {
   public:
    void NotImplemented_(const std::string& name) final { ADD_FAILURE() << "Unexpected " << name; }

    void OnUnknownTableMoreHandles(
        fidl::WireEvent<test::ReceiveFlexibleEnvelope::OnUnknownTableMoreHandles>* event) final {
      EXPECT_FALSE(event->has_want_more_than_30_bytes_at_ordinal_3());
      EXPECT_FALSE(event->has_want_more_than_4_handles_at_ordinal_4());
      EXPECT_TRUE(event->HasUnknownData());
      called = true;
    }

    bool called = false;
  };
  EventHandler event_handler;
  fidl::Status status = event_handler.HandleOneEvent(client_end());
  EXPECT_TRUE(status.ok()) << status;
  EXPECT_TRUE(event_handler.called);
}

}  // namespace
