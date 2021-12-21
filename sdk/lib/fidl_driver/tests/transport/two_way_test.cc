// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// clang-format off
// The MakeAnyTransport overloads need to be defined before including
// message.h, which uses them.
#include <lib/fidl_driver/cpp/transport.h>
// clang-format on

#include <lib/fdf/arena.h>
#include <lib/fdf/cpp/dispatcher.h>
#include <lib/fdf/internal.h>
#include <lib/fidl/llcpp/client_base.h>
#include <lib/fidl/llcpp/internal/transport.h>
#include <lib/fidl/llcpp/message.h>
#include <lib/fidl/llcpp/server.h>
#include <lib/fit/defer.h>
#include <zircon/errors.h>

#include <memory>

#include <zxtest/zxtest.h>

constexpr uint32_t kRequestPayload = 1234;
constexpr uint32_t kResponsePayload = 5678;

constexpr uint64_t kTwoWayTxid = 100;
constexpr uint64_t kTwoWayOrdinal = 200;

const struct FidlCodedStruct CodingTableStruct = {
    .tag = kFidlTypeStruct,
    .contains_envelope = kFidlContainsEnvelope_DoesNotContainEnvelope,
    .element_count = 0u,
    .size_v1 = 24u,
    .size_v2 = 24u,
    .elements = nullptr,
    .name = "integration/TwoWayMessage"};

struct TwoWayRequest {
  static constexpr const fidl_type_t* Type = &CodingTableStruct;
  static constexpr uint32_t MaxNumHandles = 0;
  static constexpr uint32_t PrimarySize = 24;
  static constexpr uint32_t PrimarySizeV1 = 24;
  static constexpr uint32_t MaxOutOfLineV1 = 0;
  static constexpr ::fidl::internal::TransactionalMessageKind MessageKind =
      ::fidl::internal::TransactionalMessageKind::kRequest;
  static constexpr bool HasFlexibleEnvelope = false;
  fidl_message_header_t header;
  alignas(8) uint32_t payload;
};

struct TwoWayResponse {
  static constexpr const fidl_type_t* Type = &CodingTableStruct;
  static constexpr uint32_t MaxNumHandles = 0;
  static constexpr uint32_t PrimarySize = 24;
  static constexpr uint32_t PrimarySizeV1 = 24;
  static constexpr uint32_t MaxOutOfLineV1 = 0;
  static constexpr ::fidl::internal::TransactionalMessageKind MessageKind =
      ::fidl::internal::TransactionalMessageKind::kResponse;
  static constexpr bool HasFlexibleEnvelope = false;
  fidl_message_header_t header;
  alignas(8) uint32_t payload;
};

template <>
struct fidl::IsFidlType<TwoWayRequest> : public std::true_type {};
template <>
struct fidl::IsFidlType<TwoWayResponse> : public std::true_type {};

template <>
struct fidl::IsFidlMessage<TwoWayRequest> : public std::true_type {};
template <>
struct fidl::IsFidlMessage<TwoWayResponse> : public std::true_type {};

class TestClient : public fidl::internal::ClientBase {
 public:
  void Bind(std::shared_ptr<TestClient> client, fdf::Channel handle,
            async_dispatcher_t* dispatcher) {
    fidl::internal::ClientBase::Bind(
        client, fidl::internal::MakeAnyTransport(std::move(handle)), dispatcher, nullptr,
        fidl::AnyTeardownObserver::Noop(),
        fidl::internal::ThreadingPolicy::kCreateAndTeardownFromAnyThread);
  }

  void TwoWay(
      TwoWayRequest request, fidl::internal::OutgoingTransportContext* outgoing_transport_context,
      fit::callback<void(TwoWayResponse, fidl::internal::IncomingTransportContext*)> callback) {
    class TwoWayResponseContext : public fidl::internal::ResponseContext {
     public:
      TwoWayResponseContext(
          fit::callback<void(TwoWayResponse, fidl::internal::IncomingTransportContext*)> callback)
          : fidl::internal::ResponseContext(kTwoWayOrdinal), callback(std::move(callback)) {}

     private:
      cpp17::optional<fidl::UnbindInfo> OnRawResult(
          ::fidl::IncomingMessage&& result,
          fidl::internal::IncomingTransportContext* incoming_transport_context) override {
        if (result.status() == ZX_ERR_CANCELED) {
          delete this;
          return cpp17::nullopt;
        }
        ZX_ASSERT_MSG(result.ok(), "%s", result.lossy_description());
        fidl::DecodedMessage<TwoWayResponse, fidl::internal::DriverTransport> decoded(
            std::move(result));
        callback(*decoded.PrimaryObject(), incoming_transport_context);
        delete this;
        return cpp17::nullopt;
      }
      fit::callback<void(TwoWayResponse, fidl::internal::IncomingTransportContext*)> callback;
    };
    auto* context = new TwoWayResponseContext(std::move(callback));
    fidl::OwnedEncodedMessage<TwoWayRequest, fidl::internal::DriverTransport> encoded(&request);
    fidl::internal::ClientBase::SendTwoWay(
        encoded.GetOutgoingMessage(), context,
        fidl::WriteOptions{.outgoing_transport_context = outgoing_transport_context});
  }

 private:
  std::optional<::fidl::UnbindInfo> DispatchEvent(
      ::fidl::IncomingMessage& msg, ::fidl::internal::AsyncEventHandler* maybe_event_handler,
      fidl::internal::IncomingTransportContext* incoming_transport_context) override {
    ZX_PANIC("unexpected event");
  }
};

struct ProtocolMarker {};

class TestServer : public fidl::internal::IncomingMessageDispatcher {
 public:
  using _EnclosingProtocol = ProtocolMarker;
  using _Transport = fidl::internal::DriverTransport;

 private:
  void dispatch_message(::fidl::IncomingMessage&& msg, ::fidl::Transaction* txn,
                        fidl::internal::IncomingTransportContext* transport_context) override {
    ZX_ASSERT(msg.ok());
    fidl::DecodedMessage<TwoWayRequest, fidl::internal::DriverTransport> decoded(std::move(msg));
    ZX_ASSERT(decoded.PrimaryObject()->payload == kRequestPayload);

    TwoWayResponse response{.payload = kResponsePayload};
    fidl_init_txn_header(&response.header, kTwoWayTxid, kTwoWayOrdinal);
    fidl::OwnedEncodedMessage<TwoWayResponse, fidl::internal::DriverTransport> encoded(&response);
    fidl::WriteOptions write_options = {
        // Reuse the same arena for the response.
        .outgoing_transport_context =
            reinterpret_cast<fidl::internal::OutgoingTransportContext*>(transport_context),
    };
    txn->Reply(&encoded.GetOutgoingMessage(), write_options);
  }
};

template <>
class fidl::internal::WireWeakEventSender<ProtocolMarker> {
 public:
  explicit WireWeakEventSender(std::weak_ptr<fidl::internal::AsyncServerBinding> binding)
      : inner_(std::move(binding)) {}
  fidl::internal::WeakEventSenderInner inner_;
};

TEST(DriverTransport, TwoWayAsync) {
  void* driver = reinterpret_cast<void*>(uintptr_t(1));
  fdf_internal_push_driver(driver);
  auto deferred = fit::defer([]() { fdf_internal_pop_driver(); });

  auto dispatcher = fdf::Dispatcher::Create(FDF_DISPATCHER_OPTION_UNSYNCHRONIZED);
  ASSERT_OK(dispatcher.status_value());

  auto channels = fdf::ChannelPair::Create(0);
  ASSERT_OK(channels.status_value());

  fidl::ServerEnd<ProtocolMarker, fidl::internal::DriverTransport> server_end(
      std::move(channels->end0));
  fidl::OnUnboundFn<TestServer> on_unbound =
      [](TestServer*, fidl::UnbindInfo info,
         fidl::ServerEnd<ProtocolMarker, fidl::internal::DriverTransport> server_end) {
        // TODO(bprosnitz) Ensure unbound is called after using generated code.
        ZX_PANIC("unbound isn't called");
      };
  auto server = std::make_shared<TestServer>();
  fidl::BindServer(dispatcher->async_dispatcher(), std::move(server_end), server,
                   std::move(on_unbound));

  auto client = std::make_shared<TestClient>();
  client->Bind(client, std::move(channels->end1), dispatcher->async_dispatcher());
  TwoWayRequest request{.payload = kRequestPayload};
  fidl_init_txn_header(&request.header, kTwoWayTxid, kTwoWayOrdinal);
  fdf_arena_t* arena;
  ASSERT_OK(fdf_arena_create(0, nullptr, 0, &arena));
  sync_completion_t done;
  client->TwoWay(request, reinterpret_cast<fidl::internal::OutgoingTransportContext*>(arena),
                 [&done](TwoWayResponse response,
                         fidl::internal::IncomingTransportContext* incoming_transport_context) {
                   ASSERT_EQ(kResponsePayload, response.payload);
                   fdf_arena_destroy(reinterpret_cast<fdf_arena_t*>(incoming_transport_context));
                   sync_completion_signal(&done);
                 });

  ASSERT_OK(sync_completion_wait(&done, ZX_TIME_INFINITE));
}
