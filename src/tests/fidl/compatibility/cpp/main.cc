// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fidl.test.compatibility/cpp/fidl.h>
#include <fuchsia/sys/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/fidl/cpp/interface_request.h>
#include <lib/stdcompat/optional.h>
#include <lib/sys/cpp/component_context.h>
#include <zircon/status.h>

#include <cstdlib>
#include <iostream>
#include <string>

constexpr const char kEchoInterfaceName[] = "fidl.test.compatibility.Echo";

using namespace fidl_test_compatibility;

class EventProxy : public fidl::AsyncEventHandler<Echo> {
 public:
  explicit EventProxy(::fidl::ServerBindingRef<Echo>& server_binding)
      : server_binding_(server_binding), result_(fit::ok()) {}

  void EchoMinimalEvent(fidl::Event<Echo::EchoMinimalEvent>& event) override {
    result_ = fidl::SendEvent(server_binding_)->EchoMinimalEvent();
    sync_completion_signal(&completion);
  }

  void EchoEvent(fidl::Event<Echo::EchoEvent>& event) override {
    result_ = fidl::SendEvent(server_binding_)->EchoEvent(std::move(event.value()));
    sync_completion_signal(&completion);
  }
  void OnEchoNamedEvent(fidl::Event<Echo::OnEchoNamedEvent>& event) override {
    result_ = fidl::SendEvent(server_binding_)->OnEchoNamedEvent(std::move(event.value()));
    sync_completion_signal(&completion);
  }
  void OnEchoTablePayloadEvent(fidl::Event<Echo::OnEchoTablePayloadEvent>& event) override {
    result_ = fidl::SendEvent(server_binding_)->OnEchoTablePayloadEvent(std::move(event));
    sync_completion_signal(&completion);
  }
  void OnEchoUnionPayloadEvent(fidl::Event<Echo::OnEchoUnionPayloadEvent>& event) override {
    result_ = fidl::SendEvent(server_binding_)->OnEchoUnionPayloadEvent(std::move(event));
    sync_completion_signal(&completion);
  }

  fit::result<fidl::Error> WaitForEvent() {
    sync_completion_wait(&completion, ZX_TIME_INFINITE);
    return result_;
  }

 private:
  fidl::ServerBindingRef<Echo>& server_binding_;
  fit::result<fidl::Error> result_;
  sync_completion_t completion;
};

class EchoClientApp {
 public:
  EchoClientApp()
      : context_(sys::ComponentContext::CreateAndServeOutgoingDirectory()),
        loop_(&kAsyncLoopConfigNoAttachToCurrentThread),
        client_(fidl::SharedClient<Echo>(ConnectTo(), loop_.dispatcher())) {
    loop_.StartThread();
  }
  EchoClientApp(EventProxy* event_handler)
      : context_(sys::ComponentContext::CreateAndServeOutgoingDirectory()),
        loop_(&kAsyncLoopConfigNoAttachToCurrentThread),
        client_(fidl::SharedClient<Echo>(ConnectTo(), loop_.dispatcher(), event_handler)) {
    loop_.StartThread();
  }

  ~EchoClientApp() { loop_.Quit(); }

  void EchoMinimal(::std::string forward_to_server,
                   fidl::ClientCallback<Echo::EchoMinimal> callback) {
    client_->EchoMinimal(std::move(forward_to_server)).ThenExactlyOnce(std::move(callback));
  }

  void EchoMinimalWithError(::std::string forward_to_server, RespondWith result_variant,
                            fidl::ClientCallback<Echo::EchoMinimalWithError> callback) {
    client_
        ->EchoMinimalWithError(
            EchoEchoMinimalWithErrorRequest(std::move(forward_to_server), result_variant))
        .ThenExactlyOnce(std::move(callback));
  }

  zx_status_t EchoMinimalNoRetVal(::std::string forward_to_server) {
    auto result = client_->EchoMinimalNoRetVal(std::move(forward_to_server));
    if (result.is_error()) {
      return result.error_value().status();
    }
    return ZX_OK;
  }

  void EchoStruct(Struct value, ::std::string forward_to_server,
                  fidl::ClientCallback<Echo::EchoStruct> callback) {
    client_->EchoStruct(EchoEchoStructRequest(std::move(value), std::move(forward_to_server)))
        .ThenExactlyOnce(std::move(callback));
  }

  void EchoStructWithError(Struct value, DefaultEnum err, ::std::string forward_to_server,
                           RespondWith result_variant,
                           fidl::ClientCallback<Echo::EchoStructWithError> callback) {
    client_
        ->EchoStructWithError(EchoEchoStructWithErrorRequest(
            std::move(value), err, std::move(forward_to_server), result_variant))
        .ThenExactlyOnce(std::move(callback));
  }

  zx_status_t EchoStructNoRetVal(Struct value, ::std::string forward_to_server) {
    auto result = client_->EchoStructNoRetVal(
        EchoEchoStructNoRetValRequest(std::move(value), std::move(forward_to_server)));
    if (result.is_error()) {
      return result.error_value().status();
    }
    return ZX_OK;
  }

  void EchoNamedStruct(fidl_test_imported::SimpleStruct value, ::std::string forward_to_server,
                       fidl::ClientCallback<Echo::EchoNamedStruct> callback) {
    client_
        ->EchoNamedStruct(
            fidl_test_imported::RequestStruct(std::move(value), std::move(forward_to_server)))
        .ThenExactlyOnce(std::move(callback));
  }

  void EchoNamedStructWithError(fidl_test_imported::SimpleStruct value, uint32_t err,
                                ::std::string forward_to_server,
                                fidl_test_imported::WantResponse result_variant,
                                fidl::ClientCallback<Echo::EchoNamedStructWithError> callback) {
    client_
        ->EchoNamedStructWithError(fidl_test_imported::ErrorableRequestStruct(
            std::move(value), err, std::move(forward_to_server), result_variant))
        .ThenExactlyOnce(std::move(callback));
  }

  zx_status_t EchoNamedStructNoRetVal(fidl_test_imported::SimpleStruct value,
                                      ::std::string forward_to_server) {
    auto result = client_->EchoNamedStructNoRetVal(fidl_test_imported::EventTriggeringRequestStruct(
        std::move(value), std::move(forward_to_server)));
    if (result.is_error()) {
      return result.error_value().status();
    }
    return ZX_OK;
  }

  void EchoArrays(ArraysStruct value, ::std::string forward_to_server,
                  fidl::ClientCallback<Echo::EchoArrays> callback) {
    client_->EchoArrays(EchoEchoArraysRequest(std::move(value), forward_to_server))
        .ThenExactlyOnce(std::move(callback));
  }

  void EchoArraysWithError(ArraysStruct value, DefaultEnum err, ::std::string forward_to_server,
                           RespondWith result_variant,
                           fidl::ClientCallback<Echo::EchoArraysWithError> callback) {
    client_
        ->EchoArraysWithError(EchoEchoArraysWithErrorRequest(
            std::move(value), err, std::move(forward_to_server), result_variant))
        .ThenExactlyOnce(std::move(callback));
  }

  void EchoVectors(VectorsStruct value, ::std::string forward_to_server,
                   fidl::ClientCallback<Echo::EchoVectors> callback) {
    client_->EchoVectors(EchoEchoVectorsRequest(std::move(value), std::move(forward_to_server)))
        .ThenExactlyOnce(std::move(callback));
  }

  void EchoVectorsWithError(VectorsStruct value, DefaultEnum err, ::std::string forward_to_server,
                            RespondWith result_variant,
                            fidl::ClientCallback<Echo::EchoVectorsWithError> callback) {
    client_
        ->EchoVectorsWithError(EchoEchoVectorsWithErrorRequest(
            std::move(value), err, std::move(forward_to_server), result_variant))
        .ThenExactlyOnce(std::move(callback));
  }

  void EchoTable(AllTypesTable value, ::std::string forward_to_server,
                 fidl::ClientCallback<Echo::EchoTable> callback) {
    return client_->EchoTable(EchoEchoTableRequest(std::move(value), forward_to_server))
        .ThenExactlyOnce(std::move(callback));
  }

  void EchoTableWithError(AllTypesTable value, DefaultEnum err, ::std::string forward_to_server,
                          RespondWith result_variant,
                          fidl::ClientCallback<Echo::EchoTableWithError> callback) {
    return client_
        ->EchoTableWithError(
            EchoEchoTableWithErrorRequest(std::move(value), err, forward_to_server, result_variant))
        .ThenExactlyOnce(std::move(callback));
  }

  void EchoXunions(::std::vector<AllTypesXunion> value, ::std::string forward_to_server,
                   fidl::ClientCallback<Echo::EchoXunions> callback) {
    client_->EchoXunions(EchoEchoXunionsRequest(std::move(value), std::move(forward_to_server)))
        .ThenExactlyOnce(std::move(callback));
  }

  void EchoXunionsWithError(::std::vector<AllTypesXunion> value, DefaultEnum err,
                            ::std::string forward_to_server, RespondWith result_variant,
                            fidl::ClientCallback<Echo::EchoXunionsWithError> callback) {
    client_
        ->EchoXunionsWithError(EchoEchoXunionsWithErrorRequest(
            std::move(value), err, std::move(forward_to_server), result_variant))
        .ThenExactlyOnce(std::move(callback));
  }

  void EchoTablePayload(fidl_test_compatibility::RequestTable payload,
                        fidl::ClientCallback<Echo::EchoTablePayload> callback) {
    client_->EchoTablePayload(std::move(payload)).ThenExactlyOnce(std::move(callback));
  }

  void EchoTablePayloadWithError(
      fidl_test_compatibility::EchoEchoTablePayloadWithErrorRequest payload,
      fidl::ClientCallback<Echo::EchoTablePayloadWithError> callback) {
    client_->EchoTablePayloadWithError(std::move(payload)).ThenExactlyOnce(std::move(callback));
  }

  zx_status_t EchoTablePayloadNoRetVal(fidl_test_compatibility::RequestTable payload) {
    auto result = client_->EchoTablePayloadNoRetVal(std::move(payload));
    if (result.is_error()) {
      return result.error_value().status();
    }
    return ZX_OK;
  }

  void EchoTableRequestComposed(
      ::fidl_test_imported::ComposedEchoTableRequestComposedRequest payload,
      fidl::ClientCallback<Echo::EchoTableRequestComposed> callback) {
    client_->EchoTableRequestComposed(std::move(payload)).ThenExactlyOnce(std::move(callback));
  }

  void EchoUnionPayload(fidl_test_compatibility::RequestUnion payload,
                        fidl::ClientCallback<Echo::EchoUnionPayload> callback) {
    return client_->EchoUnionPayload(std::move(payload)).ThenExactlyOnce(std::move(callback));
  }

  void EchoUnionPayloadWithError(
      fidl_test_compatibility::EchoEchoUnionPayloadWithErrorRequest payload,
      fidl::ClientCallback<Echo::EchoUnionPayloadWithError> callback) {
    return client_->EchoUnionPayloadWithError(std::move(payload))
        .ThenExactlyOnce(std::move(callback));
  }

  void EchoUnionResponseWithErrorComposed(
      int64_t value, bool want_absolute_value, ::std::string forward_to_server, uint32_t result_err,
      fidl_test_imported::WantResponse result_variant,
      fidl::ClientCallback<Echo::EchoUnionResponseWithErrorComposed> callback) {
    client_
        ->EchoUnionResponseWithErrorComposed(
            fidl_test_imported::ComposedEchoUnionResponseWithErrorComposedRequest(
                value, want_absolute_value, std::move(forward_to_server), result_err,
                std::move(result_variant)))
        .ThenExactlyOnce(std::move(callback));
  }

  zx_status_t EchoUnionPayloadNoRetVal(fidl_test_compatibility::RequestUnion payload) {
    auto result = client_->EchoUnionPayloadNoRetVal(std::move(payload));
    if (result.is_error()) {
      return result.error_value().status();
    }
    return ZX_OK;
  }

  EchoClientApp(const EchoClientApp&) = delete;
  EchoClientApp& operator=(const EchoClientApp&) = delete;

 private:
  // Called once upon construction to connect to the server.
  ::fidl::ClientEnd<Echo> ConnectTo() {
    auto context = sys::ComponentContext::Create();
    auto echo_ends = ::fidl::CreateEndpoints<fidl_test_compatibility::Echo>();
    ZX_ASSERT(echo_ends.is_ok());
    ZX_ASSERT(context->svc()->Connect(kEchoInterfaceName, echo_ends->server.TakeChannel()) ==
              ZX_OK);

    return std::move(echo_ends->client);
  }

  std::unique_ptr<sys::ComponentContext> context_;
  async::Loop loop_;
  fidl::SharedClient<Echo> client_;
};

class EchoConnection final : public fidl::Server<Echo> {
 public:
  EchoConnection() = default;

  void EchoMinimal(EchoMinimalRequest& request, EchoMinimalCompleter::Sync& completer) override {
    if (request.forward_to_server().empty()) {
      completer.Reply();
    } else {
      std::shared_ptr<EchoClientApp> app = std::make_shared<EchoClientApp>();
      app->EchoMinimal("", [completer = completer.ToAsync(), extend_lifetime = app](
                               fidl::Result<Echo::EchoMinimal>& result) mutable {
        ZX_ASSERT_MSG(result.is_ok(), "Forwarding failed: %s",
                      result.error_value().FormatDescription().c_str());
        completer.Reply();
      });
    }
  }

  void EchoMinimalWithError(EchoMinimalWithErrorRequest& request,
                            EchoMinimalWithErrorCompleter::Sync& completer) override {
    if (request.forward_to_server().empty()) {
      if (request.result_variant() == wire::RespondWith::kErr) {
        completer.Reply(fit::error(0u));
      } else {
        completer.Reply(fit::ok());
      }
    } else {
      std::shared_ptr<EchoClientApp> app = std::make_shared<EchoClientApp>();
      app->EchoMinimalWithError(
          "", request.result_variant(),
          [completer = completer.ToAsync(),
           extend_lifetime = app](fidl::Result<Echo::EchoMinimalWithError>& result) mutable {
            ZX_ASSERT_MSG(result.is_ok() || result.error_value().is_domain_error(),
                          "Forwarding failed: %s",
                          result.error_value().FormatDescription().c_str());
            if (result.is_ok()) {
              completer.Reply(fit::ok());
            } else {
              completer.Reply(fit::error(result.error_value().domain_error()));
            }
          });
    }
  }

  void EchoMinimalNoRetVal(EchoMinimalNoRetValRequest& request,
                           EchoMinimalNoRetValCompleter::Sync&) override {
    if (request.forward_to_server().empty()) {
      auto result = fidl::SendEvent(server_binding_.value())->EchoMinimalEvent();
      ZX_ASSERT_MSG(result.is_ok(), "Replying with event failed: %s",
                    result.error_value().FormatDescription().c_str());
    } else {
      EventProxy event_handler(server_binding_.value());
      EchoClientApp app(&event_handler);
      zx_status_t status = app.EchoMinimalNoRetVal("");
      ZX_ASSERT_MSG(status == ZX_OK, "Replying with event failed direct: %s",
                    zx_status_get_string(status));
      auto result = event_handler.WaitForEvent();
      ZX_ASSERT_MSG(result.is_ok(), "Replying with event failed indirect: %s",
                    result.error_value().FormatDescription().c_str());
    }
  }

  void EchoStruct(EchoStructRequest& request, EchoStructCompleter::Sync& completer) override {
    if (request.forward_to_server().empty()) {
      completer.Reply(std::move(request.value()));
    } else {
      std::shared_ptr<EchoClientApp> app = std::make_shared<EchoClientApp>();
      app->EchoStruct(std::move(request.value()), "",
                      [completer = completer.ToAsync(),
                       extend_lifetime = app](fidl::Result<Echo::EchoStruct>& result) mutable {
                        ZX_ASSERT_MSG(result.is_ok(), "Forwarding failed: %s",
                                      result.error_value().FormatDescription().c_str());
                        completer.Reply(std::move(result->value()));
                      });
    }
  }

  void EchoStructWithError(EchoStructWithErrorRequest& request,
                           EchoStructWithErrorCompleter::Sync& completer) override {
    if (request.forward_to_server().empty()) {
      if (request.result_variant() == wire::RespondWith::kErr) {
        completer.Reply(fit::error(request.result_err()));
      } else {
        completer.Reply(fit::ok(std::move(request.value())));
      }
    } else {
      std::shared_ptr<EchoClientApp> app = std::make_shared<EchoClientApp>();
      app->EchoStructWithError(
          std::move(request.value()), request.result_err(), "", request.result_variant(),
          [completer = completer.ToAsync(),
           extend_lifetime = app](fidl::Result<Echo::EchoStructWithError>& result) mutable {
            ZX_ASSERT_MSG(result.is_ok() || result.error_value().is_domain_error(),
                          "Forwarding failed: %s",
                          result.error_value().FormatDescription().c_str());
            if (result.is_ok()) {
              completer.Reply(fit::ok(std::move(result.value())));
            } else {
              completer.Reply(fit::error(result.error_value().domain_error()));
            }
          });
    }
  }

  void EchoStructNoRetVal(EchoStructNoRetValRequest& request,
                          EchoStructNoRetValCompleter::Sync&) override {
    if (request.forward_to_server().empty()) {
      fit::result<fidl::Error> result =
          fidl::SendEvent(server_binding_.value())->EchoEvent(std::move(request.value()));
      ZX_ASSERT_MSG(result.is_ok(), "Replying with event failed: %s",
                    result.error_value().FormatDescription().c_str());
    } else {
      EventProxy event_handler(server_binding_.value());
      EchoClientApp app(&event_handler);
      zx_status_t status = app.EchoStructNoRetVal(std::move(request.value()), "");
      ZX_ASSERT_MSG(status == ZX_OK, "Replying with event failed direct: %s",
                    zx_status_get_string(status));
      auto result = event_handler.WaitForEvent();
      ZX_ASSERT_MSG(result.is_ok(), "Replying with event failed indirect: %s",
                    result.error_value().FormatDescription().c_str());
    }
  }

  void EchoNamedStruct(EchoNamedStructRequest& request,
                       EchoNamedStructCompleter::Sync& completer) override {
    if (request.forward_to_server().empty()) {
      completer.Reply(std::move(request.value()));
    } else {
      std::shared_ptr<EchoClientApp> app = std::make_shared<EchoClientApp>();
      app->EchoNamedStruct(std::move(request.value()), "",
                           [completer = completer.ToAsync(), extend_lifetime = app](
                               fidl::Result<Echo::EchoNamedStruct>& result) mutable {
                             ZX_ASSERT_MSG(result.is_ok(), "Forwarding failed: %s",
                                           result.error_value().FormatDescription().c_str());
                             completer.Reply(std::move(result->value()));
                           });
    }
  }

  void EchoNamedStructWithError(EchoNamedStructWithErrorRequest& request,
                                EchoNamedStructWithErrorCompleter::Sync& completer) override {
    if (request.forward_to_server().empty()) {
      if (request.result_variant() == fidl_test_imported::WantResponse::kErr) {
        completer.Reply(fit::error(request.result_err()));
      } else {
        completer.Reply(fit::ok(std::move(request.value())));
      }
    } else {
      std::shared_ptr<EchoClientApp> app = std::make_shared<EchoClientApp>();
      app->EchoNamedStructWithError(
          std::move(request.value()), request.result_err(), "", request.result_variant(),
          [completer = completer.ToAsync(),
           extend_lifetime = app](fidl::Result<Echo::EchoNamedStructWithError>& result) mutable {
            ZX_ASSERT_MSG(result.is_ok() || result.error_value().is_domain_error(),
                          "Forwarding failed: %s",
                          result.error_value().FormatDescription().c_str());
            if (result.is_ok()) {
              completer.Reply(fit::ok(std::move(result.value())));
            } else {
              completer.Reply(fit::error(result.error_value().domain_error()));
            }
          });
    }
  }

  void EchoNamedStructNoRetVal(EchoNamedStructNoRetValRequest& request,
                               EchoNamedStructNoRetValCompleter::Sync&) override {
    if (request.forward_to_server().empty()) {
      fit::result<fidl::Error> result =
          fidl::SendEvent(server_binding_.value())->OnEchoNamedEvent(std::move(request.value()));
      ZX_ASSERT_MSG(result.is_ok(), "Replying with event failed: %s",
                    result.error_value().FormatDescription().c_str());
    } else {
      EventProxy event_handler(server_binding_.value());
      EchoClientApp app(&event_handler);
      zx_status_t status = app.EchoNamedStructNoRetVal(std::move(request.value()), "");
      ZX_ASSERT_MSG(status == ZX_OK, "Replying with event failed direct: %s",
                    zx_status_get_string(status));
      auto result = event_handler.WaitForEvent();
      ZX_ASSERT_MSG(result.is_ok(), "Replying with event failed indirect: %s",
                    result.error_value().FormatDescription().c_str());
    }
  }

  void EchoArrays(EchoArraysRequest& request, EchoArraysCompleter::Sync& completer) override {
    if (request.forward_to_server().empty()) {
      completer.Reply(std::move(request.value()));
    } else {
      std::shared_ptr<EchoClientApp> app = std::make_shared<EchoClientApp>();
      app->EchoArrays(std::move(request.value()), "",
                      [completer = completer.ToAsync(),
                       extend_lifetime = app](fidl::Result<Echo::EchoArrays>& result) mutable {
                        ZX_ASSERT_MSG(result.is_ok(), "Forwarding failed: %s",
                                      result.error_value().FormatDescription().c_str());
                        completer.Reply(std::move(result->value()));
                      });
    }
  }

  void EchoArraysWithError(EchoArraysWithErrorRequest& request,
                           EchoArraysWithErrorCompleter::Sync& completer) override {
    if (request.forward_to_server().empty()) {
      if (request.result_variant() == wire::RespondWith::kErr) {
        completer.Reply(fit::error(request.result_err()));
      } else {
        completer.Reply(fit::ok(std::move(request.value())));
      }
    } else {
      std::shared_ptr<EchoClientApp> app = std::make_shared<EchoClientApp>();
      app->EchoArraysWithError(
          std::move(request.value()), request.result_err(), "", request.result_variant(),
          [completer = completer.ToAsync(),
           extend_lifetime = app](fidl::Result<Echo::EchoArraysWithError>& result) mutable {
            ZX_ASSERT_MSG(result.is_ok() || result.error_value().is_domain_error(),
                          "Forwarding failed: %s",
                          result.error_value().FormatDescription().c_str());
            if (result.is_ok()) {
              completer.Reply(fit::ok(std::move(result.value())));
            } else {
              completer.Reply(fit::error(result.error_value().domain_error()));
            }
          });
    }
  }

  void EchoVectors(EchoVectorsRequest& request, EchoVectorsCompleter::Sync& completer) override {
    if (request.forward_to_server().empty()) {
      completer.Reply(std::move(request.value()));
    } else {
      std::shared_ptr<EchoClientApp> app = std::make_shared<EchoClientApp>();
      app->EchoVectors(std::move(request.value()), "",
                       [completer = completer.ToAsync(),
                        extend_lifetime = app](fidl::Result<Echo::EchoVectors>& result) mutable {
                         ZX_ASSERT_MSG(result.is_ok(), "Forwarding failed: %s",
                                       result.error_value().FormatDescription().c_str());
                         completer.Reply(std::move(result->value()));
                       });
    }
  }

  void EchoVectorsWithError(EchoVectorsWithErrorRequest& request,
                            EchoVectorsWithErrorCompleter::Sync& completer) override {
    if (request.forward_to_server().empty()) {
      if (request.result_variant() == wire::RespondWith::kErr) {
        completer.Reply(fit::error(request.result_err()));
      } else {
        completer.Reply(fit::ok(std::move(request.value())));
      }
    } else {
      std::shared_ptr<EchoClientApp> app = std::make_shared<EchoClientApp>();
      app->EchoVectorsWithError(
          std::move(request.value()), request.result_err(), "", request.result_variant(),
          [completer = completer.ToAsync(),
           extend_lifetime = app](fidl::Result<Echo::EchoVectorsWithError>& result) mutable {
            ZX_ASSERT_MSG(result.is_ok() || result.error_value().is_domain_error(),
                          "Forwarding failed: %s",
                          result.error_value().FormatDescription().c_str());
            if (result.is_ok()) {
              completer.Reply(fit::ok(std::move(result.value())));
            } else {
              completer.Reply(fit::error(result.error_value().domain_error()));
            }
          });
    }
  }

  void EchoTable(EchoTableRequest& request, EchoTableCompleter::Sync& completer) override {
    if (request.forward_to_server().empty()) {
      completer.Reply(std::move(request.value()));
    } else {
      std::shared_ptr<EchoClientApp> app = std::make_shared<EchoClientApp>();
      app->EchoTable(std::move(request.value()), "",
                     [completer = completer.ToAsync(),
                      extend_lifetime = app](fidl::Result<Echo::EchoTable>& result) mutable {
                       ZX_ASSERT_MSG(result.is_ok(), "Forwarding failed: %s",
                                     result.error_value().FormatDescription().c_str());
                       completer.Reply(std::move(result->value()));
                     });
    }
  }

  void EchoTableWithError(EchoTableWithErrorRequest& request,
                          EchoTableWithErrorCompleter::Sync& completer) override {
    if (request.forward_to_server().empty()) {
      if (request.result_variant() == wire::RespondWith::kErr) {
        completer.Reply(fit::error(request.result_err()));
      } else {
        completer.Reply(fit::ok(std::move(request.value())));
      }
    } else {
      std::shared_ptr<EchoClientApp> app = std::make_shared<EchoClientApp>();
      app->EchoTableWithError(
          std::move(request.value()), request.result_err(), "", request.result_variant(),
          [completer = completer.ToAsync(),
           extend_lifetime = app](fidl::Result<Echo::EchoTableWithError>& result) mutable {
            ZX_ASSERT_MSG(result.is_ok() || result.error_value().is_domain_error(),
                          "Forwarding failed: %s",
                          result.error_value().FormatDescription().c_str());
            if (result.is_ok()) {
              completer.Reply(fit::ok(std::move(result.value())));
            } else {
              completer.Reply(fit::error(result.error_value().domain_error()));
            }
          });
    }
  }

  void EchoXunions(EchoXunionsRequest& request, EchoXunionsCompleter::Sync& completer) override {
    if (request.forward_to_server().empty()) {
      completer.Reply(std::move(request.value()));
    } else {
      std::shared_ptr<EchoClientApp> app = std::make_shared<EchoClientApp>();
      app->EchoXunions(std::move(request.value()), "",
                       [completer = completer.ToAsync(),
                        extend_lifetime = app](fidl::Result<Echo::EchoXunions>& result) mutable {
                         ZX_ASSERT_MSG(result.is_ok(), "Forwarding failed: %s",
                                       result.error_value().FormatDescription().c_str());
                         completer.Reply(std::move(result->value()));
                       });
    }
  }

  void EchoXunionsWithError(EchoXunionsWithErrorRequest& request,
                            EchoXunionsWithErrorCompleter::Sync& completer) override {
    if (request.forward_to_server().empty()) {
      if (request.result_variant() == wire::RespondWith::kErr) {
        completer.Reply(fit::error(request.result_err()));
      } else {
        completer.Reply(fit::ok(std::move(request.value())));
      }
    } else {
      std::shared_ptr<EchoClientApp> app = std::make_shared<EchoClientApp>();
      app->EchoXunionsWithError(
          std::move(request.value()), request.result_err(), "", request.result_variant(),
          [completer = completer.ToAsync(),
           extend_lifetime = app](fidl::Result<Echo::EchoXunionsWithError>& result) mutable {
            ZX_ASSERT_MSG(result.is_ok() || result.error_value().is_domain_error(),
                          "Forwarding failed: %s",
                          result.error_value().FormatDescription().c_str());
            if (result.is_ok()) {
              completer.Reply(fit::ok(std::move(result.value())));
            } else {
              completer.Reply(fit::error(result.error_value().domain_error()));
            }
          });
    }
  }

  void EchoTablePayload(EchoTablePayloadRequest& request,
                        EchoTablePayloadCompleter::Sync& completer) override {
    if (!request.forward_to_server().has_value()) {
      ::fidl_test_compatibility::ResponseTable resp{{.value = request.value()}};
      completer.Reply(std::move(resp));
    } else {
      std::shared_ptr<EchoClientApp> app = std::make_shared<EchoClientApp>();
      fidl_test_compatibility::RequestTable req{{.value = request.value()}};
      app->EchoTablePayload(std::move(req),
                            [completer = completer.ToAsync(), extend_lifetime = app](
                                fidl::Result<Echo::EchoTablePayload>& result) mutable {
                              ZX_ASSERT_MSG(result.is_ok(), "Forwarding failed: %s",
                                            result.error_value().FormatDescription().c_str());
                              completer.Reply(std::move(result.value()));
                            });
    }
  }

  void EchoTablePayloadWithError(EchoTablePayloadWithErrorRequest& request,
                                 EchoTablePayloadWithErrorCompleter::Sync& completer) override {
    if (!request.forward_to_server().has_value()) {
      if (request.result_variant() == wire::RespondWith::kErr) {
        completer.Reply(fit::error(request.result_err().value()));
      } else {
        ::fidl_test_compatibility::ResponseTable resp{{.value = request.value()}};
        completer.Reply(fit::ok(std::move(resp)));
      }
    } else {
      std::shared_ptr<EchoClientApp> app = std::make_shared<EchoClientApp>();
      EchoEchoTablePayloadWithErrorRequest req{{
          .value = request.value(),
          .result_err = request.result_err(),
          .result_variant = request.result_variant(),
      }};

      app->EchoTablePayloadWithError(
          std::move(req), [completer = completer.ToAsync(), extend_lifetime = app](
                              fidl::Result<Echo::EchoTablePayloadWithError>& result) mutable {
            ZX_ASSERT_MSG(result.is_ok() || result.error_value().is_domain_error(),
                          "Forwarding failed: %s",
                          result.error_value().FormatDescription().c_str());
            if (result.is_ok()) {
              completer.Reply(fit::ok(std::move(result.value())));
            } else {
              completer.Reply(fit::error(result.error_value().domain_error()));
            }
          });
    }
  }

  void EchoTablePayloadNoRetVal(EchoTablePayloadNoRetValRequest& request,
                                EchoTablePayloadNoRetValCompleter::Sync&) override {
    if (!request.forward_to_server().has_value()) {
      ::fidl_test_compatibility::ResponseTable resp{{.value = request.value()}};
      fit::result<fidl::Error> result =
          fidl::SendEvent(server_binding_.value())->OnEchoTablePayloadEvent(std::move(resp));
      ZX_ASSERT_MSG(result.is_ok(), "Replying with event failed: %s",
                    result.error_value().FormatDescription().c_str());
    } else {
      EventProxy event_handler(server_binding_.value());
      EchoClientApp app(&event_handler);
      ::fidl_test_compatibility::RequestTable req{{.value = request.value()}};
      zx_status_t status = app.EchoTablePayloadNoRetVal(std::move(req));
      ZX_ASSERT_MSG(status == ZX_OK, "Replying with event failed direct: %s",
                    zx_status_get_string(status));
      auto result = event_handler.WaitForEvent();
      ZX_ASSERT_MSG(result.is_ok(), "Replying with event failed indirect: %s",
                    result.error_value().FormatDescription().c_str());
    }
  }

  void EchoTableRequestComposed(EchoTableRequestComposedRequest& request,
                                EchoTableRequestComposedCompleter::Sync& completer) override {
    if (!request.forward_to_server().has_value()) {
      fidl_test_imported::SimpleStruct resp(true, request.value().value());
      completer.Reply(std::move(resp));
    } else {
      std::shared_ptr<EchoClientApp> app = std::make_shared<EchoClientApp>();
      fidl_test_imported::ComposedEchoTableRequestComposedRequest req{{.value = request.value()}};
      app->EchoTableRequestComposed(
          std::move(req), [completer = completer.ToAsync(), extend_lifetime = app](
                              fidl::Result<Echo::EchoTableRequestComposed>& result) mutable {
            ZX_ASSERT_MSG(result.is_ok(), "Forwarding failed: %s",
                          result.error_value().FormatDescription().c_str());
            completer.Reply(std::move(result.value()));
          });
    }
  }

  void EchoUnionPayload(EchoUnionPayloadRequest& request,
                        EchoUnionPayloadCompleter::Sync& completer) override {
    const std::string& forward_to_server = request.signed_()
                                               ? request.signed_()->forward_to_server()
                                               : request.unsigned_()->forward_to_server();
    if (forward_to_server.empty()) {
      if (request.Which() == RequestUnion::Tag::kSigned) {
        completer.Reply(ResponseUnion::WithSigned_(request.signed_()->value()));
      } else {
        completer.Reply(ResponseUnion::WithUnsigned_(request.unsigned_()->value()));
      }
    } else {
      std::shared_ptr<EchoClientApp> app = std::make_shared<EchoClientApp>();
      std::optional<RequestUnion> req;
      if (request.Which() == RequestUnion::Tag::kSigned) {
        req = RequestUnion::WithSigned_(
            ::fidl_test_compatibility::Signed(request.signed_()->value(), ""));
      } else {
        req = RequestUnion::WithUnsigned_(
            ::fidl_test_compatibility::Unsigned(request.unsigned_()->value(), ""));
      }
      app->EchoUnionPayload(std::move(*req),
                            [completer = completer.ToAsync(), extend_lifetime = app](
                                fidl::Result<Echo::EchoUnionPayload>& result) mutable {
                              ZX_ASSERT_MSG(result.is_ok(), "Forwarding failed: %s",
                                            result.error_value().FormatDescription().c_str());
                              completer.Reply(std::move(result.value()));
                            });
    }
  }

  void EchoUnionPayloadWithError(EchoUnionPayloadWithErrorRequest& request,
                                 EchoUnionPayloadWithErrorCompleter::Sync& completer) override {
    const std::string& forward_to_server = request.signed_()
                                               ? request.signed_()->forward_to_server()
                                               : request.unsigned_()->forward_to_server();
    if (forward_to_server.empty()) {
      RespondWith result_variant = request.signed_() ? request.signed_()->result_variant()
                                                     : request.unsigned_()->result_variant();
      if (result_variant == wire::RespondWith::kErr) {
        DefaultEnum err =
            request.signed_() ? request.signed_()->result_err() : request.unsigned_()->result_err();
        completer.Reply(fit::error(err));
      } else {
        if (request.Which() == EchoEchoUnionPayloadWithErrorRequest::Tag::kSigned) {
          completer.Reply(fit::ok(ResponseUnion::WithSigned_(request.signed_()->value())));
        } else {
          completer.Reply(fit::ok(ResponseUnion::WithUnsigned_(request.unsigned_()->value())));
        }
      }
    } else {
      std::shared_ptr<EchoClientApp> app = std::make_shared<EchoClientApp>();
      std::optional<EchoEchoUnionPayloadWithErrorRequest> req;
      if (request.Which() == EchoEchoUnionPayloadWithErrorRequest::Tag::kSigned) {
        auto variant = request.signed_();
        req = EchoEchoUnionPayloadWithErrorRequest::WithSigned_(
            ::fidl_test_compatibility::SignedErrorable(variant->value(), "", variant->result_err(),
                                                       variant->result_variant()));
      } else {
        auto variant = request.unsigned_();
        req = EchoEchoUnionPayloadWithErrorRequest::WithUnsigned_(
            ::fidl_test_compatibility::UnsignedErrorable(
                variant->value(), "", variant->result_err(), variant->result_variant()));
      }

      app->EchoUnionPayloadWithError(
          std::move(*req), [completer = completer.ToAsync(), extend_lifetime = app](
                               fidl::Result<Echo::EchoUnionPayloadWithError>& result) mutable {
            ZX_ASSERT_MSG(result.is_ok() || result.error_value().is_domain_error(),
                          "Forwarding failed: %s",
                          result.error_value().FormatDescription().c_str());
            if (result.is_ok()) {
              completer.Reply(fit::ok(std::move(result.value())));
            } else {
              completer.Reply(fit::error(result.error_value().domain_error()));
            }
          });
    }
  }

  void EchoUnionPayloadNoRetVal(EchoUnionPayloadNoRetValRequest& request,
                                EchoUnionPayloadNoRetValCompleter::Sync&) override {
    const std::string& forward_to_server = request.signed_()
                                               ? request.signed_()->forward_to_server()
                                               : request.unsigned_()->forward_to_server();
    if (forward_to_server.empty()) {
      ResponseUnion resp = request.Which() == RequestUnion::Tag::kSigned
                               ? ResponseUnion::WithSigned_(request.signed_()->value())
                               : ResponseUnion::WithUnsigned_(request.unsigned_()->value());
      fit::result<fidl::Error> result =
          fidl::SendEvent(server_binding_.value())->OnEchoUnionPayloadEvent(std::move(resp));
      ZX_ASSERT_MSG(result.is_ok(), "Replying with event failed: %s",
                    result.error_value().FormatDescription().c_str());
    } else {
      EventProxy event_handler(server_binding_.value());
      EchoClientApp app(&event_handler);
      std::optional<RequestUnion> req;
      if (request.Which() == RequestUnion::Tag::kSigned) {
        req = RequestUnion::WithSigned_(
            ::fidl_test_compatibility::Signed(request.signed_()->value(), ""));
      } else {
        req = RequestUnion::WithUnsigned_(
            ::fidl_test_compatibility::Unsigned(request.unsigned_()->value(), ""));
      }

      zx_status_t status = app.EchoUnionPayloadNoRetVal(std::move(*req));
      ZX_ASSERT_MSG(status == ZX_OK, "Replying with event failed direct: %s",
                    zx_status_get_string(status));
      auto result = event_handler.WaitForEvent();
      ZX_ASSERT_MSG(result.is_ok(), "Replying with event failed indirect: %s",
                    result.error_value().FormatDescription().c_str());
    }
  }

  void EchoUnionResponseWithErrorComposed(
      EchoUnionResponseWithErrorComposedRequest& request,
      EchoUnionResponseWithErrorComposedCompleter::Sync& completer) override {
    if (request.forward_to_server().empty()) {
      if (request.result_variant() == fidl_test_imported::WantResponse::kErr) {
        completer.Reply(fit::error(request.result_err()));
        return;
      }

      if (request.want_absolute_value()) {
        completer.Reply(fit::ok(
            ::fidl_test_imported::ComposedEchoUnionResponseWithErrorComposedResponse::WithUnsigned_(
                static_cast<uint64_t>(std::abs(request.value())))));
      } else {
        completer.Reply(fit::ok(
            ::fidl_test_imported::ComposedEchoUnionResponseWithErrorComposedResponse::WithSigned_(
                request.value())));
      }
    } else {
      std::shared_ptr<EchoClientApp> app = std::make_shared<EchoClientApp>();
      request.forward_to_server() = "";
      fidl_test_imported::ComposedEchoTableRequestComposedRequest req{{.value = request.value()}};
      app->EchoUnionResponseWithErrorComposed(
          request.value(), request.want_absolute_value(), "", request.result_err(),
          request.result_variant(),
          [completer = completer.ToAsync(), extend_lifetime = app](
              fidl::Result<Echo::EchoUnionResponseWithErrorComposed>& result) mutable {
            ZX_ASSERT_MSG(result.is_ok() || result.error_value().is_domain_error(),
                          "Forwarding failed: %s",
                          result.error_value().FormatDescription().c_str());
            if (result.is_ok()) {
              completer.Reply(fit::ok(std::move(result.value())));
            } else {
              completer.Reply(fit::error(result.error_value().domain_error()));
            }
          });
    }
  }

  void set_server_binding(::fidl::ServerBindingRef<Echo> binding) {
    server_binding_.emplace(binding);
  }

 private:
  cpp17::optional<::fidl::ServerBindingRef<Echo>> server_binding_;
};

int main(int argc, const char** argv) {
  // The FIDL support lib requires a default async dispatcher.
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  auto context = sys::ComponentContext::CreateAndServeOutgoingDirectory();

  context->outgoing()->AddPublicService(
      std::make_unique<vfs::Service>([&](zx::channel request, async_dispatcher_t* dispatcher) {
        auto conn = std::make_unique<EchoConnection>();
        EchoConnection* conn_raw = conn.get();
        auto binding = ::fidl::BindServer(dispatcher, ::fidl::ServerEnd<Echo>(std::move(request)),
                                          std::move(conn));
        conn_raw->set_server_binding(std::move(binding));
      }),
      kEchoInterfaceName);

  loop.Run();
  return EXIT_SUCCESS;
}
