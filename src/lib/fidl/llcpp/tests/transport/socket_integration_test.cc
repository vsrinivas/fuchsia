// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// clang-format off
#include "transport_socket.h" // Needs to be included first.
// clang-format on

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fidl/llcpp/client_base.h>
#include <lib/fidl/llcpp/message.h>
#include <lib/fidl/llcpp/server.h>
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
  void Bind(std::shared_ptr<TestClient> client, zx::socket handle, async_dispatcher_t* dispatcher) {
    fidl::internal::ClientBase::Bind(
        client, fidl::internal::MakeAnyTransport(std::move(handle)), dispatcher, nullptr,
        fidl::AnyTeardownObserver::Noop(),
        fidl::internal::ThreadingPolicy::kCreateAndTeardownFromAnyThread);
  }

  void TwoWay(TwoWayRequest request, fit::callback<void(TwoWayResponse)> callback) {
    class TwoWayResponseContext : public fidl::internal::ResponseContext {
     public:
      TwoWayResponseContext(fit::callback<void(TwoWayResponse)> callback)
          : fidl::internal::ResponseContext(kTwoWayOrdinal), callback(std::move(callback)) {}

     private:
      cpp17::optional<fidl::UnbindInfo> OnRawResult(
          ::fidl::IncomingMessage&& result,
          fidl::internal::IncomingTransportContext* incoming_transport_context) override {
        ZX_ASSERT(result.ok());
        fidl::DecodedMessage<TwoWayResponse, fidl::internal::SocketTransport> decoded(
            std::move(result));
        callback(*decoded.PrimaryObject());
        delete this;
        return cpp17::nullopt;
      }
      fit::callback<void(TwoWayResponse)> callback;
    };
    auto* context = new TwoWayResponseContext(std::move(callback));
    fidl::OwnedEncodedMessage<TwoWayRequest, fidl::internal::SocketTransport> encoded(&request);
    fidl::internal::ClientBase::SendTwoWay(encoded.GetOutgoingMessage(), context);
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
  using _Transport = fidl::internal::SocketTransport;

 private:
  void dispatch_message(::fidl::IncomingMessage&& msg, ::fidl::Transaction* txn,
                        fidl::internal::IncomingTransportContext* transport_context) override {
    ZX_ASSERT(msg.ok());
    fidl::DecodedMessage<TwoWayRequest, fidl::internal::SocketTransport> decoded(std::move(msg));
    ZX_ASSERT(decoded.PrimaryObject()->payload == kRequestPayload);

    TwoWayResponse response{.payload = kResponsePayload};
    fidl_init_txn_header(&response.header, kTwoWayTxid, kTwoWayOrdinal);
    fidl::OwnedEncodedMessage<TwoWayResponse, fidl::internal::SocketTransport> encoded(&response);
    txn->Reply(&encoded.GetOutgoingMessage());
  }
};

template <>
class fidl::internal::WireWeakEventSender<ProtocolMarker> {
 public:
  explicit WireWeakEventSender(std::weak_ptr<fidl::internal::AsyncServerBinding> binding)
      : inner_(std::move(binding)) {}
  fidl::internal::WeakEventSenderInner inner_;
};

TEST(TransportIntegration, TwoWayAsync) {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);

  zx::socket s1, s2;
  ASSERT_OK(zx::socket::create(0, &s1, &s2));

  fidl::ServerEnd<ProtocolMarker, fidl::internal::SocketTransport> server_end(std::move(s1));
  fidl::OnUnboundFn<TestServer> on_unbound =
      [](TestServer*, fidl::UnbindInfo info,
         fidl::ServerEnd<ProtocolMarker, fidl::internal::SocketTransport> server_end) {
        EXPECT_EQ(fidl::Reason::kDispatcherError, info.reason());
        EXPECT_EQ(ZX_ERR_CANCELED, info.status());
        EXPECT_TRUE(server_end);
      };
  fidl::BindServer(loop.dispatcher(), std::move(server_end), std::make_shared<TestServer>(),
                   std::move(on_unbound));

  auto client = std::make_shared<TestClient>();
  client->Bind(client, std::move(s2), loop.dispatcher());
  TwoWayRequest request{.payload = kRequestPayload};
  fidl_init_txn_header(&request.header, kTwoWayTxid, kTwoWayOrdinal);
  client->TwoWay(request,
                 [](TwoWayResponse response) { ASSERT_EQ(kResponsePayload, response.payload); });

  loop.RunUntilIdle();
}
