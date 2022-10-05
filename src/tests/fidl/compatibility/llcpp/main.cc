// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fidl.test.compatibility/cpp/wire.h>
#include <fidl/fidl.test.compatibility/cpp/wire_test_base.h>
#include <fidl/fidl.test.compatibility/cpp/wire_types.h>
#include <fidl/fidl.test.imported/cpp/wire.h>
#include <fidl/fidl.test.imported/cpp/wire_types.h>
#include <fuchsia/sys/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/fidl/cpp/interface_request.h>
#include <lib/fidl/cpp/wire/string_view.h>
#include <lib/sys/cpp/component_context.h>
#include <zircon/status.h>

#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>

constexpr const char kEchoInterfaceName[] = "fidl.test.compatibility.Echo";

using namespace fidl_test_compatibility;

class EchoClientApp {
 public:
  EchoClientApp()
      : context_(sys::ComponentContext::CreateAndServeOutgoingDirectory()),
        client_(fidl::WireSyncClient<Echo>(ConnectTo())) {}

  // Half the methods are testing the managed flavor; the other half are testing caller-allocate.

  fidl::WireResult<Echo::EchoMinimal> EchoMinimal(::fidl::StringView forward_to_server) {
    return client_->EchoMinimal(std::move(forward_to_server));
  }

  fidl::WireResult<Echo::EchoMinimalWithError> EchoMinimalWithError(
      ::fidl::StringView forward_to_server, wire::RespondWith result_variant) {
    return client_->EchoMinimalWithError(std::move(forward_to_server), result_variant);
  }

  fidl::Status EchoMinimalNoRetVal(::fidl::StringView forward_to_server,
                                   fidl::WireSyncEventHandler<Echo>& event_handler) {
    auto result = client_->EchoMinimalNoRetVal(std::move(forward_to_server));
    if (!result.ok()) {
      return result;
    }
    return client_.HandleOneEvent(event_handler);
  }

  fidl::WireResult<Echo::EchoStruct> EchoStruct(wire::Struct value,
                                                ::fidl::StringView forward_to_server) {
    return client_->EchoStruct(std::move(value), std::move(forward_to_server));
  }

  fidl::WireResult<Echo::EchoNamedStruct> EchoNamedStruct(
      fidl_test_imported::wire::SimpleStruct value, ::fidl::StringView forward_to_server) {
    return client_->EchoNamedStruct(std::move(value), std::move(forward_to_server));
  }

  fidl::WireResult<Echo::EchoNamedStructWithError> EchoNamedStructWithError(
      fidl_test_imported::wire::SimpleStruct value, uint32_t err,
      ::fidl::StringView forward_to_server, fidl_test_imported::wire::WantResponse result_variant) {
    return client_->EchoNamedStructWithError(std::move(value), err, std::move(forward_to_server),
                                             result_variant);
  }

  fidl::Status EchoNamedStructNoRetVal(fidl_test_imported::wire::SimpleStruct value,
                                       ::fidl::StringView forward_to_server,
                                       fidl::WireSyncEventHandler<Echo>& event_handler) {
    auto result = client_->EchoNamedStructNoRetVal(std::move(value), std::move(forward_to_server));
    if (!result.ok()) {
      return result;
    }
    return client_.HandleOneEvent(event_handler);
  }

  fidl::WireResult<Echo::EchoStructWithError> EchoStructWithError(
      wire::Struct value, wire::DefaultEnum err, ::fidl::StringView forward_to_server,
      wire::RespondWith result_variant) {
    return client_->EchoStructWithError(std::move(value), err, std::move(forward_to_server),
                                        result_variant);
  }

  fidl::Status EchoStructNoRetVal(wire::Struct value, ::fidl::StringView forward_to_server,
                                  fidl::WireSyncEventHandler<Echo>& event_handler) {
    auto result = client_->EchoStructNoRetVal(std::move(value), std::move(forward_to_server));
    if (!result.ok()) {
      return result;
    }
    return client_.HandleOneEvent(event_handler);
  }

  fidl::WireUnownedResult<Echo::EchoArrays> EchoArrays(::fidl::BufferSpan buffer,
                                                       wire::ArraysStruct value,
                                                       ::fidl::StringView forward_to_server) {
    return client_.buffer(buffer)->EchoArrays(std::move(value), forward_to_server);
  }

  fidl::WireResult<Echo::EchoArraysWithError> EchoArraysWithError(
      wire::ArraysStruct value, wire::DefaultEnum err, ::fidl::StringView forward_to_server,
      wire::RespondWith result_variant) {
    return client_->EchoArraysWithError(std::move(value), err, std::move(forward_to_server),
                                        result_variant);
  }

  fidl::WireResult<Echo::EchoVectors> EchoVectors(wire::VectorsStruct value,
                                                  ::fidl::StringView forward_to_server) {
    return client_->EchoVectors(std::move(value), std::move(forward_to_server));
  }

  fidl::WireResult<Echo::EchoVectorsWithError> EchoVectorsWithError(
      wire::VectorsStruct value, wire::DefaultEnum err, ::fidl::StringView forward_to_server,
      wire::RespondWith result_variant) {
    return client_->EchoVectorsWithError(std::move(value), err, std::move(forward_to_server),
                                         result_variant);
  }

  fidl::WireUnownedResult<Echo::EchoTable> EchoTable(::fidl::BufferSpan buffer,
                                                     wire::AllTypesTable value,
                                                     ::fidl::StringView forward_to_server) {
    return client_.buffer(buffer)->EchoTable(value, forward_to_server);
  }

  fidl::WireUnownedResult<Echo::EchoTableWithError> EchoTableWithError(
      ::fidl::BufferSpan buffer, wire::AllTypesTable value, wire::DefaultEnum err,
      ::fidl::StringView forward_to_server, wire::RespondWith result_variant) {
    return client_.buffer(buffer)->EchoTableWithError(value, err, forward_to_server,
                                                      result_variant);
  }

  fidl::WireResult<Echo::EchoXunions> EchoXunions(::fidl::VectorView<wire::AllTypesXunion> value,
                                                  ::fidl::StringView forward_to_server) {
    return client_->EchoXunions(std::move(value), std::move(forward_to_server));
  }

  fidl::WireResult<Echo::EchoXunionsWithError> EchoXunionsWithError(
      ::fidl::VectorView<wire::AllTypesXunion> value, wire::DefaultEnum err,
      ::fidl::StringView forward_to_server, wire::RespondWith result_variant) {
    return client_->EchoXunionsWithError(std::move(value), err, std::move(forward_to_server),
                                         result_variant);
  }

  fidl::WireResult<Echo::EchoTablePayload> EchoTablePayload(
      fidl_test_compatibility::wire::RequestTable payload) {
    return client_->EchoTablePayload(std::move(payload));
  }

  fidl::WireResult<Echo::EchoTablePayloadWithError> EchoTablePayloadWithError(
      fidl_test_compatibility::wire::EchoEchoTablePayloadWithErrorRequest payload) {
    return client_->EchoTablePayloadWithError(std::move(payload));
  }

  fidl::Status EchoTablePayloadNoRetVal(fidl_test_compatibility::wire::RequestTable payload,
                                        fidl::WireSyncEventHandler<Echo>& event_handler) {
    auto result = client_->EchoTablePayloadNoRetVal(std::move(payload));
    if (!result.ok()) {
      return result;
    }
    return client_.HandleOneEvent(event_handler);
  }

  fidl::WireResult<Echo::EchoTableRequestComposed> EchoTableRequestComposed(
      ::fidl_test_imported::wire::ComposedEchoTableRequestComposedRequest payload) {
    return client_->EchoTableRequestComposed(std::move(payload));
  }

  fidl::WireResult<Echo::EchoUnionPayload> EchoUnionPayload(
      fidl_test_compatibility::wire::RequestUnion payload) {
    return client_->EchoUnionPayload(std::move(payload));
  }

  fidl::WireResult<Echo::EchoUnionPayloadWithError> EchoUnionPayloadWithError(
      fidl_test_compatibility::wire::EchoEchoUnionPayloadWithErrorRequest payload) {
    return client_->EchoUnionPayloadWithError(std::move(payload));
  }

  fidl::Status EchoUnionPayloadNoRetVal(fidl_test_compatibility::wire::RequestUnion payload,
                                        fidl::WireSyncEventHandler<Echo>& event_handler) {
    auto result = client_->EchoUnionPayloadNoRetVal(std::move(payload));
    if (!result.ok()) {
      return result;
    }
    return client_.HandleOneEvent(event_handler);
  }

  fidl::WireResult<Echo::EchoUnionResponseWithErrorComposed> EchoUnionResponseWithErrorComposed(
      int64_t value, bool want_absolute_value, ::fidl::StringView forward_to_server,
      uint32_t result_err, fidl_test_imported::wire::WantResponse result_variant) {
    return client_->EchoUnionResponseWithErrorComposed(
        value, want_absolute_value, forward_to_server, result_err, result_variant);
  }

  EchoClientApp(const EchoClientApp&) = delete;
  EchoClientApp& operator=(const EchoClientApp&) = delete;

 private:
  // Called once upon construction to connect to the server.
  ::fidl::ClientEnd<fidl_test_compatibility::Echo> ConnectTo() {
    auto context = sys::ComponentContext::Create();
    auto echo_ends = ::fidl::CreateEndpoints<fidl_test_compatibility::Echo>();
    ZX_ASSERT(echo_ends.is_ok());
    ZX_ASSERT(context->svc()->Connect(kEchoInterfaceName, echo_ends->server.TakeChannel()) ==
              ZX_OK);

    return std::move(echo_ends->client);
  }

  std::unique_ptr<sys::ComponentContext> context_;
  fidl::WireSyncClient<Echo> client_;
};

class EchoSyncEventHandlerTestStub : public fidl::testing::WireSyncEventHandlerTestBase<Echo> {
 public:
  void NotImplemented_(const std::string& name) final { ZX_PANIC("Unexpected %s", name.c_str()); }
};

class EchoConnection final : public fidl::WireServer<Echo> {
 public:
  EchoConnection() = default;

  void EchoMinimal(EchoMinimalRequestView request, EchoMinimalCompleter::Sync& completer) override {
    if (request->forward_to_server.empty()) {
      completer.Reply();
    } else {
      EchoClientApp app;
      auto result = app.EchoMinimal("");
      ZX_ASSERT_MSG(result.status() == ZX_OK, "Forwarding failed: %s",
                    result.FormatDescription().c_str());
      completer.Reply();
    }
  }

  void EchoMinimalWithError(EchoMinimalWithErrorRequestView request,
                            EchoMinimalWithErrorCompleter::Sync& completer) override {
    if (request->forward_to_server.empty()) {
      if (request->result_variant == wire::RespondWith::kErr) {
        completer.ReplyError(0u);
      } else {
        completer.ReplySuccess();
      }
    } else {
      EchoClientApp app;
      auto result = app.EchoMinimalWithError("", request->result_variant);
      ZX_ASSERT_MSG(result.status() == ZX_OK, "Forwarding failed: %s",
                    result.FormatDescription().c_str());
      if (result.value().is_error()) {
        completer.ReplyError(result.value().error_value());
      } else {
        completer.ReplySuccess();
      }
    }
  }

  void EchoMinimalNoRetVal(EchoMinimalNoRetValRequestView request,
                           EchoMinimalNoRetValCompleter::Sync&) override {
    if (request->forward_to_server.empty()) {
      fidl::Status result = fidl::WireSendEvent(server_binding_.value())->EchoMinimalEvent();
      ZX_ASSERT_MSG(result.ok(), "Replying with event failed: %s",
                    result.FormatDescription().c_str());
    } else {
      class EventHandler : public EchoSyncEventHandlerTestStub {
       public:
        explicit EventHandler(EchoConnection* connection) : connection_(connection) {}

        fidl::Status result() const { return result_; }

        void EchoMinimalEvent(fidl::WireEvent<Echo::EchoMinimalEvent>* event) override {
          result_ = fidl::WireSendEvent(connection_->server_binding_.value())->EchoMinimalEvent();
        }

       private:
        EchoConnection* const connection_;
        fidl::Status result_ = fidl::Status::Ok();
      };

      EchoClientApp app;
      EventHandler event_handler(this);
      fidl::Status status = app.EchoMinimalNoRetVal("", event_handler);
      ZX_ASSERT_MSG(status.ok(), "Replying with event failed direct: %s",
                    status.FormatDescription().c_str());
      ZX_ASSERT_MSG(event_handler.result().ok(), "Replying with event failed indirect: %s",
                    event_handler.result().FormatDescription().c_str());
    }
  }

  void EchoStruct(EchoStructRequestView request, EchoStructCompleter::Sync& completer) override {
    if (request->forward_to_server.empty()) {
      completer.Reply(std::move(request->value));
    } else {
      EchoClientApp app;
      auto result = app.EchoStruct(std::move(request->value), "");
      ZX_ASSERT_MSG(result.status() == ZX_OK, "Forwarding failed: %s",
                    result.FormatDescription().c_str());
      completer.Reply(std::move(result->value));
    }
  }

  void EchoStructWithError(EchoStructWithErrorRequestView request,
                           EchoStructWithErrorCompleter::Sync& completer) override {
    if (request->forward_to_server.empty()) {
      if (request->result_variant == wire::RespondWith::kErr) {
        completer.ReplyError(request->result_err);
      } else {
        completer.ReplySuccess(std::move(request->value));
      }
    } else {
      EchoClientApp app;
      auto result = app.EchoStructWithError(std::move(request->value), request->result_err, "",
                                            request->result_variant);
      ZX_ASSERT_MSG(result.status() == ZX_OK, "Forwarding failed: %s",
                    result.FormatDescription().c_str());
      if (result.value().is_error()) {
        completer.ReplyError(result.value().error_value());
      } else {
        completer.ReplySuccess(std::move(result.value().value()->value));
      }
    }
  }

  void EchoStructNoRetVal(EchoStructNoRetValRequestView request,
                          EchoStructNoRetValCompleter::Sync&) override {
    if (request->forward_to_server.empty()) {
      fidl::Status result =
          fidl::WireSendEvent(server_binding_.value())->EchoEvent(std::move(request->value));
      ZX_ASSERT_MSG(result.ok(), "Replying with event failed: %s",
                    result.FormatDescription().c_str());
    } else {
      class EventHandler : public EchoSyncEventHandlerTestStub {
       public:
        explicit EventHandler(EchoConnection* connection) : connection_(connection) {}

        fidl::Status result() const { return result_; }

        void EchoEvent(fidl::WireEvent<Echo::EchoEvent>* event) override {
          result_ = fidl::WireSendEvent(connection_->server_binding_.value())
                        ->EchoEvent(std::move(event->value));
        }

       private:
        EchoConnection* const connection_;
        fidl::Status result_ = fidl::Status::Ok();
      };

      EchoClientApp app;
      EventHandler event_handler(this);
      fidl::Status status = app.EchoStructNoRetVal(std::move(request->value), "", event_handler);
      ZX_ASSERT_MSG(status.ok(), "Replying with event failed direct: %s",
                    status.FormatDescription().c_str());
      ZX_ASSERT_MSG(event_handler.result().ok(), "Replying with event failed indirect: %s",
                    event_handler.result().FormatDescription().c_str());
    }
  }

  void EchoArrays(EchoArraysRequestView request, EchoArraysCompleter::Sync& completer) override {
    if (request->forward_to_server.empty()) {
      completer.Reply(std::move(request->value));
    } else {
      fidl::SyncClientBuffer<Echo::EchoArrays> buffer;
      EchoClientApp app;
      auto result = app.EchoArrays(buffer.view(), std::move(request->value), "");
      ZX_ASSERT_MSG(result.status() == ZX_OK, "Forwarding failed: %s",
                    result.FormatDescription().c_str());
      completer.Reply(std::move(result->value));
    }
  }

  void EchoArraysWithError(EchoArraysWithErrorRequestView request,
                           EchoArraysWithErrorCompleter::Sync& completer) override {
    if (request->forward_to_server.empty()) {
      if (request->result_variant == wire::RespondWith::kErr) {
        completer.ReplyError(request->result_err);
      } else {
        completer.ReplySuccess(std::move(request->value));
      }
    } else {
      EchoClientApp app;
      auto result = app.EchoArraysWithError(std::move(request->value), request->result_err, "",
                                            request->result_variant);
      ZX_ASSERT_MSG(result.status() == ZX_OK, "Forwarding failed: %s: %s",
                    zx_status_get_string(result.status()), result.FormatDescription().c_str());
      if (result.value().is_error()) {
        completer.ReplyError(result.value().error_value());
      } else {
        completer.ReplySuccess(std::move(result.value().value()->value));
      }
    }
  }

  void EchoVectors(EchoVectorsRequestView request, EchoVectorsCompleter::Sync& completer) override {
    if (request->forward_to_server.empty()) {
      completer.Reply(std::move(request->value));
    } else {
      EchoClientApp app;
      auto result = app.EchoVectors(std::move(request->value), "");
      ZX_ASSERT_MSG(result.status() == ZX_OK, "Forwarding failed: %s: %s",
                    zx_status_get_string(result.status()), result.FormatDescription().c_str());
      completer.Reply(std::move(result->value));
    }
  }

  void EchoVectorsWithError(EchoVectorsWithErrorRequestView request,
                            EchoVectorsWithErrorCompleter::Sync& completer) override {
    if (request->forward_to_server.empty()) {
      if (request->result_variant == wire::RespondWith::kErr) {
        completer.ReplyError(request->result_err);
      } else {
        completer.ReplySuccess(std::move(request->value));
      }
    } else {
      EchoClientApp app;
      auto result = app.EchoVectorsWithError(std::move(request->value), request->result_err, "",
                                             request->result_variant);
      ZX_ASSERT_MSG(result.status() == ZX_OK, "Forwarding failed: %s: %s",
                    zx_status_get_string(result.status()), result.FormatDescription().c_str());
      if (result.value().is_error()) {
        completer.ReplyError(result.value().error_value());
      } else {
        completer.ReplySuccess(std::move(result.value().value()->value));
      }
    }
  }

  void EchoTable(EchoTableRequestView request, EchoTableCompleter::Sync& completer) override {
    if (request->forward_to_server.empty()) {
      completer.Reply(request->value);
    } else {
      fidl::SyncClientBuffer<Echo::EchoTable> buffer;
      EchoClientApp app;
      auto result = app.EchoTable(buffer.view(), request->value, "");
      ZX_ASSERT_MSG(result.status() == ZX_OK, "Forwarding failed: %s: %s",
                    zx_status_get_string(result.status()), result.FormatDescription().c_str());
      completer.Reply(result->value);
    }
  }

  void EchoTableWithError(EchoTableWithErrorRequestView request,
                          EchoTableWithErrorCompleter::Sync& completer) override {
    if (request->forward_to_server.empty()) {
      if (request->result_variant == wire::RespondWith::kErr) {
        completer.ReplyError(request->result_err);
      } else {
        completer.ReplySuccess(request->value);
      }
    } else {
      fidl::SyncClientBuffer<Echo::EchoTableWithError> buffer;
      EchoClientApp app;
      auto result = app.EchoTableWithError(buffer.view(), request->value, request->result_err, "",
                                           request->result_variant);
      ZX_ASSERT_MSG(result.status() == ZX_OK, "Forwarding failed: %s: %s",
                    zx_status_get_string(result.status()), result.FormatDescription().c_str());
      if (result.value().is_error()) {
        completer.ReplyError(result.value().error_value());
      } else {
        completer.ReplySuccess(std::move(result.value().value()->value));
      }
    }
  }

  void EchoXunions(EchoXunionsRequestView request, EchoXunionsCompleter::Sync& completer) override {
    if (request->forward_to_server.empty()) {
      completer.Reply(std::move(request->value));
    } else {
      EchoClientApp app;
      auto result = app.EchoXunions(std::move(request->value), "");
      ZX_ASSERT_MSG(result.status() == ZX_OK, "Forwarding failed: %s: %s",
                    zx_status_get_string(result.status()), result.FormatDescription().c_str());
      completer.Reply(std::move(result->value));
    }
  }

  void EchoXunionsWithError(EchoXunionsWithErrorRequestView request,
                            EchoXunionsWithErrorCompleter::Sync& completer) override {
    if (request->forward_to_server.empty()) {
      if (request->result_variant == wire::RespondWith::kErr) {
        completer.ReplyError(request->result_err);
      } else {
        completer.ReplySuccess(std::move(request->value));
      }
    } else {
      EchoClientApp app;
      auto result = app.EchoXunionsWithError(std::move(request->value), request->result_err, "",
                                             request->result_variant);
      ZX_ASSERT_MSG(result.status() == ZX_OK, "Forwarding failed: %s: %s",
                    zx_status_get_string(result.status()), result.FormatDescription().c_str());
      if (result.value().is_error()) {
        completer.ReplyError(result.value().error_value());
      } else {
        completer.ReplySuccess(std::move(result.value().value()->value));
      }
    }
  }

  void EchoNamedStruct(EchoNamedStructRequestView request,
                       EchoNamedStructCompleter::Sync& completer) override {
    if (request->forward_to_server.empty()) {
      completer.Reply(std::move(request->value));
    } else {
      EchoClientApp app;
      auto result = app.EchoNamedStruct(std::move(request->value), "");
      ZX_ASSERT_MSG(result.status() == ZX_OK, "Forwarding failed: %s",
                    result.FormatDescription().c_str());
      completer.Reply(std::move(result->value));
    }
  }

  void EchoNamedStructWithError(EchoNamedStructWithErrorRequestView request,
                                EchoNamedStructWithErrorCompleter::Sync& completer) override {
    if (request->forward_to_server.empty()) {
      if (request->result_variant == fidl_test_imported::wire::WantResponse::kErr) {
        completer.ReplyError(request->result_err);
      } else {
        completer.ReplySuccess(std::move(request->value));
      }
    } else {
      EchoClientApp app;
      auto result = app.EchoNamedStructWithError(std::move(request->value), request->result_err, "",
                                                 request->result_variant);
      ZX_ASSERT_MSG(result.status() == ZX_OK, "Forwarding failed: %s",
                    result.FormatDescription().c_str());
      if (result.value().is_error()) {
        completer.ReplyError(result.value().error_value());
      } else {
        completer.ReplySuccess(std::move(result.value().value()->value));
      }
    }
  }

  void EchoNamedStructNoRetVal(EchoNamedStructNoRetValRequestView request,
                               EchoNamedStructNoRetValCompleter::Sync&) override {
    if (request->forward_to_server.empty()) {
      fidl::Status result =
          fidl::WireSendEvent(server_binding_.value())->OnEchoNamedEvent(std::move(request->value));
      ZX_ASSERT_MSG(result.ok(), "Replying with event failed: %s",
                    result.FormatDescription().c_str());
    } else {
      class EventHandler : public EchoSyncEventHandlerTestStub {
       public:
        explicit EventHandler(EchoConnection* connection) : connection_(connection) {}

        fidl::Status result() const { return result_; }

        void OnEchoNamedEvent(fidl::WireEvent<Echo::OnEchoNamedEvent>* event) override {
          result_ = fidl::WireSendEvent(connection_->server_binding_.value())
                        ->OnEchoNamedEvent(std::move(event->value));
        }

       private:
        EchoConnection* const connection_;
        fidl::Status result_ = fidl::Status::Ok();
      };

      EchoClientApp app;
      EventHandler event_handler(this);
      fidl::Status status = app.EchoNamedStructNoRetVal(request->value, "", event_handler);
      ZX_ASSERT_MSG(status.ok(), "Replying with event failed direct: %s",
                    status.FormatDescription().c_str());
      ZX_ASSERT_MSG(event_handler.result().ok(), "Replying with event failed indirect: %s",
                    event_handler.result().FormatDescription().c_str());
    }
  }

  void EchoTablePayload(EchoTablePayloadRequestView request,
                        EchoTablePayloadCompleter::Sync& completer) override {
    if (!request->has_forward_to_server()) {
      fidl::Arena allocator;
      completer.Reply(fidl_test_compatibility::wire::ResponseTable::Builder(allocator)
                          .value(request->value())
                          .Build());
    } else {
      EchoClientApp app;
      fidl::Arena allocator;
      ;
      auto result =
          app.EchoTablePayload(fidl_test_compatibility::wire::RequestTable::Builder(allocator)
                                   .value(request->value())
                                   .Build());
      ZX_ASSERT_MSG(result.status() == ZX_OK, "Forwarding failed: %s",
                    result.FormatDescription().c_str());
      completer.Reply(std::move(*result.Unwrap()));
    }
  }

  void EchoTablePayloadWithError(EchoTablePayloadWithErrorRequestView request,
                                 EchoTablePayloadWithErrorCompleter::Sync& completer) override {
    if (!request->has_forward_to_server()) {
      if (request->result_variant() == wire::RespondWith::kErr) {
        completer.ReplyError(request->result_err());
      } else {
        fidl::Arena allocator;

        wire::EchoEchoTablePayloadWithErrorResult res =
            wire::EchoEchoTablePayloadWithErrorResult::WithResponse(
                allocator, wire::ResponseTable::Builder(allocator).value(request->value()).Build());
        completer.Reply(::fit::ok(&res.response()));
      }
    } else {
      EchoClientApp app;
      fidl::Arena allocator;
      auto builder = wire::EchoEchoTablePayloadWithErrorRequest::Builder(allocator);
      builder.result_variant(request->result_variant());
      if (request->result_variant() == wire::RespondWith::kErr) {
        builder.result_err(request->result_err());
      } else {
        builder.value(request->value());
      }

      auto result = app.EchoTablePayloadWithError(builder.Build());
      ZX_ASSERT_MSG(result.status() == ZX_OK, "Forwarding failed: %s",
                    result.FormatDescription().c_str());
      if (result.value().is_error()) {
        completer.ReplyError(result.value().error_value());
      } else {
        completer.ReplySuccess(*result.value().value());
      }
    }
  }

  void EchoTablePayloadNoRetVal(EchoTablePayloadNoRetValRequestView request,
                                EchoTablePayloadNoRetValCompleter::Sync&) override {
    if (!request->has_forward_to_server()) {
      fidl::Arena allocator;
      fidl::Status result =
          fidl::WireSendEvent(server_binding_.value())
              ->OnEchoTablePayloadEvent(
                  wire::ResponseTable::Builder(allocator).value(request->value()).Build());
      ZX_ASSERT_MSG(result.ok(), "Replying with event failed: %s",
                    result.FormatDescription().c_str());
    } else {
      class EventHandler : public EchoSyncEventHandlerTestStub {
       public:
        explicit EventHandler(EchoConnection* connection) : connection_(connection) {}

        fidl::Status result() const { return result_; }

        void OnEchoTablePayloadEvent(
            fidl::WireEvent<Echo::OnEchoTablePayloadEvent>* event) override {
          result_ = fidl::WireSendEvent(connection_->server_binding_.value())
                        ->OnEchoTablePayloadEvent(*event);
        }

       private:
        EchoConnection* const connection_;
        fidl::Status result_ = fidl::Status::Ok();
      };

      EchoClientApp app;
      EventHandler event_handler(this);
      fidl::Arena allocator;
      fidl::Status status = app.EchoTablePayloadNoRetVal(
          wire::RequestTable::Builder(allocator).value(request->value()).Build(), event_handler);
      ZX_ASSERT_MSG(status.ok(), "Replying with event failed direct: %s",
                    status.FormatDescription().c_str());
      ZX_ASSERT_MSG(event_handler.result().ok(), "Replying with event failed indirect: %s",
                    event_handler.result().FormatDescription().c_str());
    }
  }

  void EchoTableRequestComposed(EchoTableRequestComposedRequestView request,
                                EchoTableRequestComposedCompleter::Sync& completer) override {
    if (!request->has_forward_to_server()) {
      completer.Reply(
          std::move(fidl_test_imported::wire::SimpleStruct{.f1 = true, .f2 = request->value()}));
    } else {
      EchoClientApp app;
      fidl::Arena allocator;

      auto result = app.EchoTableRequestComposed(
          fidl_test_imported::wire::ComposedEchoTableRequestComposedRequest::Builder(allocator)
              .value(request->value())
              .Build());
      ZX_ASSERT_MSG(result.status() == ZX_OK, "Forwarding failed: %s",
                    result.FormatDescription().c_str());
      completer.Reply(std::move(result->value));
    }
  }

  void EchoUnionPayload(EchoUnionPayloadRequestView request,
                        EchoUnionPayloadCompleter::Sync& completer) override {
    fidl::StringView& forward_to_server = request->is_signed_()
                                              ? request->signed_().forward_to_server
                                              : request->unsigned_().forward_to_server;
    if (forward_to_server.empty()) {
      fidl::Arena allocator;
      fidl_test_compatibility::wire::ResponseUnion resp;
      if (request->is_signed_()) {
        resp = fidl_test_compatibility::wire::ResponseUnion::WithSigned_(allocator,
                                                                         request->signed_().value);
      } else {
        resp = fidl_test_compatibility::wire::ResponseUnion::WithUnsigned_(
            allocator, request->unsigned_().value);
      }
      completer.Reply(std::move(resp));
    } else {
      EchoClientApp app;
      fidl::Arena allocator;
      fidl_test_compatibility::wire::RequestUnion req;

      if (request->is_signed_()) {
        req =
            fidl_test_compatibility::wire::RequestUnion::WithSigned_(allocator, request->signed_());
        req.signed_().forward_to_server = "";
      } else {
        req = fidl_test_compatibility::wire::RequestUnion::WithUnsigned_(allocator,
                                                                         request->unsigned_());
        req.unsigned_().forward_to_server = "";
      }
      auto result = app.EchoUnionPayload(req);
      ZX_ASSERT_MSG(result.status() == ZX_OK, "Forwarding failed: %s",
                    result.FormatDescription().c_str());
      completer.Reply(std::move(*result.Unwrap()));
    }
  }

  void EchoUnionPayloadWithError(EchoUnionPayloadWithErrorRequestView request,
                                 EchoUnionPayloadWithErrorCompleter::Sync& completer) override {
    fidl::StringView& forward_to_server = request->is_signed_()
                                              ? request->signed_().forward_to_server
                                              : request->unsigned_().forward_to_server;
    wire::RespondWith& result_variant = request->is_signed_() ? request->signed_().result_variant
                                                              : request->unsigned_().result_variant;
    if (forward_to_server.empty()) {
      if (result_variant == wire::RespondWith::kErr) {
        completer.ReplyError(request->is_signed_() ? request->signed_().result_err
                                                   : request->unsigned_().result_err);
      } else {
        fidl::Arena allocator;
        fidl_test_compatibility::wire::ResponseUnion resp;
        if (request->is_signed_()) {
          resp = fidl_test_compatibility::wire::ResponseUnion::WithSigned_(
              allocator, request->signed_().value);
        } else {
          resp = fidl_test_compatibility::wire::ResponseUnion::WithUnsigned_(
              allocator, request->unsigned_().value);
        }
        completer.ReplySuccess(resp);
      }
    } else {
      EchoClientApp app;
      fidl::Arena allocator;
      fidl_test_compatibility::wire::EchoEchoUnionPayloadWithErrorRequest req;

      if (request->is_signed_()) {
        req = fidl_test_compatibility::wire::EchoEchoUnionPayloadWithErrorRequest::WithSigned_(
            allocator, request->signed_());
        req.signed_().forward_to_server = "";
      } else {
        req = fidl_test_compatibility::wire::EchoEchoUnionPayloadWithErrorRequest::WithUnsigned_(
            allocator, request->unsigned_());
        req.unsigned_().forward_to_server = "";
      }
      auto result = app.EchoUnionPayloadWithError(req);
      ZX_ASSERT_MSG(result.status() == ZX_OK, "Forwarding failed: %s",
                    result.FormatDescription().c_str());
      if (result.value().is_error()) {
        completer.ReplyError(result.value().error_value());
      } else {
        completer.ReplySuccess(*result.value().value());
      }
    }
  }

  void EchoUnionPayloadNoRetVal(EchoUnionPayloadNoRetValRequestView request,
                                EchoUnionPayloadNoRetValCompleter::Sync&) override {
    fidl::StringView& forward_to_server = request->is_signed_()
                                              ? request->signed_().forward_to_server
                                              : request->unsigned_().forward_to_server;
    if (forward_to_server.empty()) {
      fidl::Arena allocator;
      fidl_test_compatibility::wire::ResponseUnion resp;
      if (request->is_signed_()) {
        resp = fidl_test_compatibility::wire::ResponseUnion::WithSigned_(allocator,
                                                                         request->signed_().value);
      } else {
        resp = fidl_test_compatibility::wire::ResponseUnion::WithUnsigned_(
            allocator, request->unsigned_().value);
      }
      fidl::Status result =
          fidl::WireSendEvent(server_binding_.value())->OnEchoUnionPayloadEvent(std::move(resp));
      ZX_ASSERT_MSG(result.ok(), "Replying with event failed: %s",
                    result.FormatDescription().c_str());
    } else {
      class EventHandler : public EchoSyncEventHandlerTestStub {
       public:
        explicit EventHandler(EchoConnection* connection) : connection_(connection) {}

        fidl::Status result() const { return result_; }
        void OnEchoUnionPayloadEvent(
            fidl::WireEvent<Echo::OnEchoUnionPayloadEvent>* event) override {
          result_ = fidl::WireSendEvent(connection_->server_binding_.value())
                        ->OnEchoUnionPayloadEvent(*event);
        }

       private:
        EchoConnection* const connection_;
        fidl::Status result_ = fidl::Status::Ok();
      };

      EchoClientApp app;
      EventHandler event_handler(this);
      fidl::Arena allocator;
      fidl_test_compatibility::wire::RequestUnion req;

      if (request->is_signed_()) {
        req =
            fidl_test_compatibility::wire::RequestUnion::WithSigned_(allocator, request->signed_());
        req.signed_().forward_to_server = "";
      } else {
        req = fidl_test_compatibility::wire::RequestUnion::WithUnsigned_(allocator,
                                                                         request->unsigned_());
        req.unsigned_().forward_to_server = "";
      }
      fidl::Status status = app.EchoUnionPayloadNoRetVal(req, event_handler);
      ZX_ASSERT_MSG(status.ok(), "Replying with event failed direct: %s",
                    status.FormatDescription().c_str());
      ZX_ASSERT_MSG(event_handler.result().ok(), "Replying with event failed indirect: %s",
                    event_handler.result().FormatDescription().c_str());
    }
  }

  void EchoUnionResponseWithErrorComposed(
      EchoUnionResponseWithErrorComposedRequestView request,
      EchoUnionResponseWithErrorComposedCompleter::Sync& completer) override {
    if (request->forward_to_server.empty()) {
      if (request->result_variant == fidl_test_imported::wire::WantResponse::kErr) {
        completer.ReplyError(request->result_err);
        return;
      }
      fidl::Arena allocator;
      fidl_test_imported::wire::ComposedEchoUnionResponseWithErrorComposedResponse resp;
      if (request->want_absolute_value) {
        auto obj_view =
            fidl::ObjectView<uint64_t>(allocator, static_cast<uint64_t>(std::abs(request->value)));
        resp = fidl_test_imported::wire::ComposedEchoUnionResponseWithErrorComposedResponse::
            WithUnsigned_(std::move(obj_view));
      } else {
        auto obj_view = fidl::ObjectView<int64_t>(allocator, request->value);
        resp = fidl_test_imported::wire::ComposedEchoUnionResponseWithErrorComposedResponse::
            WithSigned_(std::move(obj_view));
      }
      completer.ReplySuccess(resp);
    } else {
      EchoClientApp app;
      auto result =
          app.EchoUnionResponseWithErrorComposed(request->value, request->want_absolute_value, "",
                                                 request->result_err, request->result_variant);
      ZX_ASSERT_MSG(result.status() == ZX_OK, "Forwarding failed: %s",
                    result.FormatDescription().c_str());
      if (result.value().is_error()) {
        completer.ReplyError(result.value().error_value());
      } else {
        completer.ReplySuccess(*result.value().value());
      }
    }
  }

  void set_server_binding(::fidl::ServerBindingRef<Echo> binding) {
    server_binding_.emplace(binding);
  }

 private:
  std::optional<::fidl::ServerBindingRef<Echo>> server_binding_;
};

int main(int argc, const char** argv) {
  // The FIDL support lib requires async_get_default_dispatcher() to return
  // non-null.
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  auto context = sys::ComponentContext::CreateAndServeOutgoingDirectory();
  std::vector<std::unique_ptr<EchoConnection>> connections;

  context->outgoing()->AddPublicService(
      std::make_unique<vfs::Service>([&](zx::channel request, async_dispatcher_t* dispatcher) {
        auto conn = std::make_unique<EchoConnection>();
        auto binding = ::fidl::BindServer(
            dispatcher, ::fidl::ServerEnd<fidl_test_compatibility::Echo>(std::move(request)),
            conn.get());
        conn->set_server_binding(std::move(binding));
        connections.push_back(std::move(conn));
      }),
      kEchoInterfaceName);

  loop.Run();
  return EXIT_SUCCESS;
}
