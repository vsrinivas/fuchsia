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
      : server_binding_(server_binding), result_(fitx::ok()) {}

  void EchoMinimalEvent(fidl::Event<Echo::EchoMinimalEvent>& event) override {
    result_ = fidl::SendEvent(server_binding_)->EchoMinimalEvent();
    sync_completion_signal(&completion);
  }

  void EchoEvent(fidl::Event<Echo::EchoEvent>& event) override {
    result_ = fidl::SendEvent(server_binding_)->EchoEvent(std::move(event->value()));
    sync_completion_signal(&completion);
  }
  void OnEchoNamedEvent(fidl::Event<Echo::OnEchoNamedEvent>& event) override {
    result_ = fidl::SendEvent(server_binding_)->OnEchoNamedEvent(std::move(event->value()));
    sync_completion_signal(&completion);
  }

  fitx::result<fidl::Error> WaitForEvent() {
    sync_completion_wait(&completion, ZX_TIME_INFINITE);
    return result_;
  }

 private:
  fidl::ServerBindingRef<Echo>& server_binding_;
  fitx::result<fidl::Error> result_;
  sync_completion_t completion;
};

class EchoClientApp {
 public:
  EchoClientApp(::std::string server_url)
      : context_(sys::ComponentContext::CreateAndServeOutgoingDirectory()),
        loop_(&kAsyncLoopConfigNoAttachToCurrentThread),
        client_(fidl::SharedClient<Echo>(ConnectTo(server_url), loop_.dispatcher())) {
    loop_.StartThread();
  }
  EchoClientApp(::std::string server_url, EventProxy* event_handler)
      : context_(sys::ComponentContext::CreateAndServeOutgoingDirectory()),
        loop_(&kAsyncLoopConfigNoAttachToCurrentThread),
        client_(
            fidl::SharedClient<Echo>(ConnectTo(server_url), loop_.dispatcher(), event_handler)) {
    loop_.StartThread();
  }

  ~EchoClientApp() { loop_.Quit(); }

  void EchoMinimal(::std::string forward_to_server,
                   fidl::ClientCallback<Echo::EchoMinimal> callback) {
    client_->EchoMinimal(std::move(forward_to_server), std::move(callback));
  }

  void EchoMinimalWithError(::std::string forward_to_server, RespondWith result_variant,
                            fidl::ClientCallback<Echo::EchoMinimalWithError> callback) {
    client_->EchoMinimalWithError(
        EchoEchoMinimalWithErrorRequest(std::move(forward_to_server), result_variant),
        std::move(callback));
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
    client_->EchoStruct(EchoEchoStructRequest(std::move(value), std::move(forward_to_server)),
                        std::move(callback));
  }

  void EchoStructWithError(Struct value, default_enum err, ::std::string forward_to_server,
                           RespondWith result_variant,
                           fidl::ClientCallback<Echo::EchoStructWithError> callback) {
    client_->EchoStructWithError(
        EchoEchoStructWithErrorRequest(std::move(value), err, std::move(forward_to_server),
                                       result_variant),
        std::move(callback));
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
    client_->EchoNamedStruct(
        fidl_test_imported::RequestStruct(std::move(value), std::move(forward_to_server)),
        std::move(callback));
  }

  void EchoNamedStructWithError(fidl_test_imported::SimpleStruct value, uint32_t err,
                                ::std::string forward_to_server,
                                fidl_test_imported::WantResponse result_variant,
                                fidl::ClientCallback<Echo::EchoNamedStructWithError> callback) {
    client_->EchoNamedStructWithError(
        fidl_test_imported::ErrorableRequestStruct(std::move(value), err,
                                                   std::move(forward_to_server), result_variant),
        std::move(callback));
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
    client_->EchoArrays(EchoEchoArraysRequest(std::move(value), forward_to_server),
                        std::move(callback));
  }

  void EchoArraysWithError(ArraysStruct value, default_enum err, ::std::string forward_to_server,
                           RespondWith result_variant,
                           fidl::ClientCallback<Echo::EchoArraysWithError> callback) {
    client_->EchoArraysWithError(
        EchoEchoArraysWithErrorRequest(std::move(value), err, std::move(forward_to_server),
                                       result_variant),
        std::move(callback));
  }

  void EchoVectors(VectorsStruct value, ::std::string forward_to_server,
                   fidl::ClientCallback<Echo::EchoVectors> callback) {
    client_->EchoVectors(EchoEchoVectorsRequest(std::move(value), std::move(forward_to_server)),
                         std::move(callback));
  }

  void EchoVectorsWithError(VectorsStruct value, default_enum err, ::std::string forward_to_server,
                            RespondWith result_variant,
                            fidl::ClientCallback<Echo::EchoVectorsWithError> callback) {
    client_->EchoVectorsWithError(
        EchoEchoVectorsWithErrorRequest(std::move(value), err, std::move(forward_to_server),
                                        result_variant),
        std::move(callback));
  }

  void EchoTable(AllTypesTable value, ::std::string forward_to_server,
                 fidl::ClientCallback<Echo::EchoTable> callback) {
    return client_->EchoTable(EchoEchoTableRequest(std::move(value), forward_to_server),
                              std::move(callback));
  }

  void EchoTableWithError(AllTypesTable value, default_enum err, ::std::string forward_to_server,
                          RespondWith result_variant,
                          fidl::ClientCallback<Echo::EchoTableWithError> callback) {
    return client_->EchoTableWithError(
        EchoEchoTableWithErrorRequest(std::move(value), err, forward_to_server, result_variant),
        std::move(callback));
  }

  void EchoXunions(::std::vector<AllTypesXunion> value, ::std::string forward_to_server,
                   fidl::ClientCallback<Echo::EchoXunions> callback) {
    client_->EchoXunions(EchoEchoXunionsRequest(std::move(value), std::move(forward_to_server)),
                         std::move(callback));
  }

  void EchoXunionsWithError(::std::vector<AllTypesXunion> value, default_enum err,
                            ::std::string forward_to_server, RespondWith result_variant,
                            fidl::ClientCallback<Echo::EchoXunionsWithError> callback) {
    client_->EchoXunionsWithError(
        EchoEchoXunionsWithErrorRequest(std::move(value), err, std::move(forward_to_server),
                                        result_variant),
        std::move(callback));
  }

  EchoClientApp(const EchoClientApp&) = delete;
  EchoClientApp& operator=(const EchoClientApp&) = delete;

 private:
  // Called once upon construction to launch and connect to the server.
  ::fidl::ClientEnd<Echo> ConnectTo(::std::string server_url) {
    fuchsia::sys::LaunchInfo launch_info;
    launch_info.url = std::string(server_url.data(), server_url.size());
    echo_provider_ = sys::ServiceDirectory::CreateWithRequest(&launch_info.directory_request);

    fuchsia::sys::LauncherPtr launcher;
    context_->svc()->Connect(launcher.NewRequest());
    launcher->CreateComponent(std::move(launch_info), controller_.NewRequest());

    auto echo_ends = ::fidl::CreateEndpoints<Echo>();
    ZX_ASSERT(echo_ends.is_ok());
    ZX_ASSERT(echo_provider_->Connect(kEchoInterfaceName, echo_ends->server.TakeChannel()) ==
              ZX_OK);

    return std::move(echo_ends->client);
  }

  std::unique_ptr<sys::ComponentContext> context_;
  std::shared_ptr<sys::ServiceDirectory> echo_provider_;
  fuchsia::sys::ComponentControllerPtr controller_;
  async::Loop loop_;
  fidl::SharedClient<Echo> client_;
};

class EchoConnection final : public fidl::Server<Echo> {
 public:
  EchoConnection() = default;

  void EchoMinimal(EchoMinimalRequest& request, EchoMinimalCompleter::Sync& completer) override {
    if (request->forward_to_server().empty()) {
      completer.Reply();
    } else {
      std::shared_ptr<EchoClientApp> app =
          std::make_shared<EchoClientApp>(request->forward_to_server());
      app->EchoMinimal(
          "", [completer = completer.ToAsync(), extend_lifetime = app](
                  fitx::result<fidl::Error, fidl::Response<Echo::EchoMinimal>>& result) mutable {
            ZX_ASSERT_MSG(result.is_ok(), "Forwarding failed: %s",
                          result.error_value().FormatDescription().c_str());
            completer.Reply();
          });
    }
  }

  void EchoMinimalWithError(EchoMinimalWithErrorRequest& request,
                            EchoMinimalWithErrorCompleter::Sync& completer) override {
    if (request->forward_to_server().empty()) {
      if (request->result_variant() == wire::RespondWith::kErr) {
        completer.Reply(Echo_EchoMinimalWithError_Result::WithErr(0u));
      } else {
        completer.Reply(Echo_EchoMinimalWithError_Result::WithResponse({}));
      }
    } else {
      std::shared_ptr<EchoClientApp> app =
          std::make_shared<EchoClientApp>(request->forward_to_server());
      app->EchoMinimalWithError(
          "", request->result_variant(),
          [completer = completer.ToAsync(), extend_lifetime = app](
              fitx::result<fidl::Error, fidl::Response<Echo::EchoMinimalWithError>>&
                  result) mutable {
            ZX_ASSERT_MSG(result.is_ok(), "Forwarding failed: %s",
                          result.error_value().FormatDescription().c_str());
            completer.Reply(std::move(result->result()));
          });
    }
  }

  void EchoMinimalNoRetVal(EchoMinimalNoRetValRequest& request,
                           EchoMinimalNoRetValCompleter::Sync&) override {
    if (request->forward_to_server().empty()) {
      auto result = fidl::SendEvent(server_binding_.value())->EchoMinimalEvent();
      ZX_ASSERT_MSG(result.is_ok(), "Replying with event failed: %s",
                    result.error_value().FormatDescription().c_str());
    } else {
      EventProxy event_handler(server_binding_.value());
      EchoClientApp app(request->forward_to_server(), &event_handler);
      zx_status_t status = app.EchoMinimalNoRetVal("");
      ZX_ASSERT_MSG(status == ZX_OK, "Replying with event failed direct: %s",
                    zx_status_get_string(status));
      auto result = event_handler.WaitForEvent();
      ZX_ASSERT_MSG(result.is_ok(), "Replying with event failed indirect: %s",
                    result.error_value().FormatDescription().c_str());
    }
  }

  void EchoStruct(EchoStructRequest& request, EchoStructCompleter::Sync& completer) override {
    if (request->forward_to_server().empty()) {
      completer.Reply(std::move(request->value()));
    } else {
      std::shared_ptr<EchoClientApp> app =
          std::make_shared<EchoClientApp>(request->forward_to_server());
      app->EchoStruct(
          std::move(request->value()), "",
          [completer = completer.ToAsync(), extend_lifetime = app](
              fitx::result<fidl::Error, fidl::Response<Echo::EchoStruct>>& result) mutable {
            ZX_ASSERT_MSG(result.is_ok(), "Forwarding failed: %s",
                          result.error_value().FormatDescription().c_str());
            completer.Reply(std::move(result->value()));
          });
    }
  }

  void EchoStructWithError(EchoStructWithErrorRequest& request,
                           EchoStructWithErrorCompleter::Sync& completer) override {
    if (request->forward_to_server().empty()) {
      if (request->result_variant() == wire::RespondWith::kErr) {
        completer.Reply(Echo_EchoStructWithError_Result::WithErr(request->result_err()));
      } else {
        completer.Reply(Echo_EchoStructWithError_Result::WithResponse(std::move(request->value())));
      }
    } else {
      std::shared_ptr<EchoClientApp> app =
          std::make_shared<EchoClientApp>(request->forward_to_server());
      app->EchoStructWithError(
          std::move(request->value()), request->result_err(), "", request->result_variant(),
          [completer = completer.ToAsync(), extend_lifetime = app](
              fitx::result<fidl::Error, fidl::Response<Echo::EchoStructWithError>>&
                  result) mutable {
            ZX_ASSERT_MSG(result.is_ok(), "Forwarding failed: %s",
                          result.error_value().FormatDescription().c_str());
            completer.Reply(std::move(result->result()));
          });
    }
  }

  void EchoStructNoRetVal(EchoStructNoRetValRequest& request,
                          EchoStructNoRetValCompleter::Sync&) override {
    if (request->forward_to_server().empty()) {
      fitx::result<fidl::Error> result =
          fidl::SendEvent(server_binding_.value())->EchoEvent(std::move(request->value()));
      ZX_ASSERT_MSG(result.is_ok(), "Replying with event failed: %s",
                    result.error_value().FormatDescription().c_str());
    } else {
      EventProxy event_handler(server_binding_.value());
      EchoClientApp app(request->forward_to_server(), &event_handler);
      zx_status_t status = app.EchoStructNoRetVal(std::move(request->value()), "");
      ZX_ASSERT_MSG(status == ZX_OK, "Replying with event failed direct: %s",
                    zx_status_get_string(status));
      auto result = event_handler.WaitForEvent();
      ZX_ASSERT_MSG(result.is_ok(), "Replying with event failed indirect: %s",
                    result.error_value().FormatDescription().c_str());
    }
  }

  void EchoNamedStruct(EchoNamedStructRequest& request,
                       EchoNamedStructCompleter::Sync& completer) override {
    if (request->forward_to_server().empty()) {
      completer.Reply(std::move(request->value()));
    } else {
      std::shared_ptr<EchoClientApp> app =
          std::make_shared<EchoClientApp>(request->forward_to_server());
      app->EchoNamedStruct(
          std::move(request->value()), "",
          [completer = completer.ToAsync(), extend_lifetime = app](
              fitx::result<fidl::Error, fidl::Response<Echo::EchoNamedStruct>>& result) mutable {
            ZX_ASSERT_MSG(result.is_ok(), "Forwarding failed: %s",
                          result.error_value().FormatDescription().c_str());
            completer.Reply(std::move(result->value()));
          });
    }
  }

  void EchoNamedStructWithError(EchoNamedStructWithErrorRequest& request,
                                EchoNamedStructWithErrorCompleter::Sync& completer) override {
    if (request->forward_to_server().empty()) {
      if (request->result_variant() == fidl_test_imported::WantResponse::kErr) {
        completer.Reply(Echo_EchoNamedStructWithError_Result::WithErr(request->result_err()));
      } else {
        completer.Reply(
            Echo_EchoNamedStructWithError_Result::WithResponse(std::move(request->value())));
      }
    } else {
      std::shared_ptr<EchoClientApp> app =
          std::make_shared<EchoClientApp>(request->forward_to_server());
      app->EchoNamedStructWithError(
          std::move(request->value()), request->result_err(), "", request->result_variant(),
          [completer = completer.ToAsync(), extend_lifetime = app](
              fitx::result<fidl::Error, fidl::Response<Echo::EchoNamedStructWithError>>&
                  result) mutable {
            ZX_ASSERT_MSG(result.is_ok(), "Forwarding failed: %s",
                          result.error_value().FormatDescription().c_str());
            completer.Reply(std::move(result->result()));
          });
    }
  }

  void EchoNamedStructNoRetVal(EchoNamedStructNoRetValRequest& request,
                               EchoNamedStructNoRetValCompleter::Sync&) override {
    if (request->forward_to_server().empty()) {
      fitx::result<fidl::Error> result =
          fidl::SendEvent(server_binding_.value())->OnEchoNamedEvent(std::move(request->value()));
      ZX_ASSERT_MSG(result.is_ok(), "Replying with event failed: %s",
                    result.error_value().FormatDescription().c_str());
    } else {
      EventProxy event_handler(server_binding_.value());
      EchoClientApp app(request->forward_to_server(), &event_handler);
      zx_status_t status = app.EchoNamedStructNoRetVal(std::move(request->value()), "");
      ZX_ASSERT_MSG(status == ZX_OK, "Replying with event failed direct: %s",
                    zx_status_get_string(status));
      auto result = event_handler.WaitForEvent();
      ZX_ASSERT_MSG(result.is_ok(), "Replying with event failed indirect: %s",
                    result.error_value().FormatDescription().c_str());
    }
  }

  void EchoArrays(EchoArraysRequest& request, EchoArraysCompleter::Sync& completer) override {
    if (request->forward_to_server().empty()) {
      completer.Reply(std::move(request->value()));
    } else {
      std::shared_ptr<EchoClientApp> app =
          std::make_shared<EchoClientApp>(request->forward_to_server());
      app->EchoArrays(
          std::move(request->value()), "",
          [completer = completer.ToAsync(), extend_lifetime = app](
              fitx::result<fidl::Error, fidl::Response<Echo::EchoArrays>>& result) mutable {
            ZX_ASSERT_MSG(result.is_ok(), "Forwarding failed: %s",
                          result.error_value().FormatDescription().c_str());
            completer.Reply(std::move(result->value()));
          });
    }
  }

  void EchoArraysWithError(EchoArraysWithErrorRequest& request,
                           EchoArraysWithErrorCompleter::Sync& completer) override {
    if (request->forward_to_server().empty()) {
      if (request->result_variant() == wire::RespondWith::kErr) {
        completer.Reply(Echo_EchoArraysWithError_Result::WithErr(request->result_err()));
      } else {
        completer.Reply(Echo_EchoArraysWithError_Result::WithResponse(std::move(request->value())));
      }
    } else {
      std::shared_ptr<EchoClientApp> app =
          std::make_shared<EchoClientApp>(request->forward_to_server());
      app->EchoArraysWithError(
          std::move(request->value()), request->result_err(), "", request->result_variant(),
          [completer = completer.ToAsync(), extend_lifetime = app](
              fitx::result<fidl::Error, fidl::Response<Echo::EchoArraysWithError>>&
                  result) mutable {
            ZX_ASSERT_MSG(result.is_ok(), "Forwarding failed: %s",
                          result.error_value().FormatDescription().c_str());
            completer.Reply(std::move(result->result()));
          });
    }
  }

  void EchoVectors(EchoVectorsRequest& request, EchoVectorsCompleter::Sync& completer) override {
    if (request->forward_to_server().empty()) {
      completer.Reply(std::move(request->value()));
    } else {
      std::shared_ptr<EchoClientApp> app =
          std::make_shared<EchoClientApp>(request->forward_to_server());
      app->EchoVectors(
          std::move(request->value()), "",
          [completer = completer.ToAsync(), extend_lifetime = app](
              fitx::result<fidl::Error, fidl::Response<Echo::EchoVectors>>& result) mutable {
            ZX_ASSERT_MSG(result.is_ok(), "Forwarding failed: %s",
                          result.error_value().FormatDescription().c_str());
            completer.Reply(std::move(result->value()));
          });
    }
  }

  void EchoVectorsWithError(EchoVectorsWithErrorRequest& request,
                            EchoVectorsWithErrorCompleter::Sync& completer) override {
    if (request->forward_to_server().empty()) {
      if (request->result_variant() == wire::RespondWith::kErr) {
        completer.Reply(Echo_EchoVectorsWithError_Result::WithErr(request->result_err()));
      } else {
        completer.Reply(
            Echo_EchoVectorsWithError_Result::WithResponse(std::move(request->value())));
      }
    } else {
      std::shared_ptr<EchoClientApp> app =
          std::make_shared<EchoClientApp>(request->forward_to_server());
      app->EchoVectorsWithError(
          std::move(request->value()), request->result_err(), "", request->result_variant(),
          [completer = completer.ToAsync(), extend_lifetime = app](
              fitx::result<fidl::Error, fidl::Response<Echo::EchoVectorsWithError>>&
                  result) mutable {
            ZX_ASSERT_MSG(result.is_ok(), "Forwarding failed: %s",
                          result.error_value().FormatDescription().c_str());
            completer.Reply(std::move(result->result()));
          });
    }
  }

  void EchoTable(EchoTableRequest& request, EchoTableCompleter::Sync& completer) override {
    if (request->forward_to_server().empty()) {
      completer.Reply(std::move(request->value()));
    } else {
      std::shared_ptr<EchoClientApp> app =
          std::make_shared<EchoClientApp>(request->forward_to_server());
      app->EchoTable(
          std::move(request->value()), "",
          [completer = completer.ToAsync(), extend_lifetime = app](
              fitx::result<fidl::Error, fidl::Response<Echo::EchoTable>>& result) mutable {
            ZX_ASSERT_MSG(result.is_ok(), "Forwarding failed: %s",
                          result.error_value().FormatDescription().c_str());
            completer.Reply(std::move(result->value()));
          });
    }
  }

  void EchoTableWithError(EchoTableWithErrorRequest& request,
                          EchoTableWithErrorCompleter::Sync& completer) override {
    if (request->forward_to_server().empty()) {
      if (request->result_variant() == wire::RespondWith::kErr) {
        completer.Reply(Echo_EchoTableWithError_Result::WithErr(request->result_err()));
      } else {
        completer.Reply(Echo_EchoTableWithError_Result::WithResponse(std::move(request->value())));
      }
    } else {
      std::shared_ptr<EchoClientApp> app =
          std::make_shared<EchoClientApp>(request->forward_to_server());
      app->EchoTableWithError(
          std::move(request->value()), request->result_err(), "", request->result_variant(),
          [completer = completer.ToAsync(), extend_lifetime = app](
              fitx::result<fidl::Error, fidl::Response<Echo::EchoTableWithError>>& result) mutable {
            ZX_ASSERT_MSG(result.is_ok(), "Forwarding failed: %s",
                          result.error_value().FormatDescription().c_str());
            completer.Reply(std::move(result->result()));
          });
    }
  }

  void EchoXunions(EchoXunionsRequest& request, EchoXunionsCompleter::Sync& completer) override {
    if (request->forward_to_server().empty()) {
      completer.Reply(std::move(request->value()));
    } else {
      std::shared_ptr<EchoClientApp> app =
          std::make_shared<EchoClientApp>(request->forward_to_server());
      app->EchoXunions(
          std::move(request->value()), "",
          [completer = completer.ToAsync(), extend_lifetime = app](
              fitx::result<fidl::Error, fidl::Response<Echo::EchoXunions>>& result) mutable {
            ZX_ASSERT_MSG(result.is_ok(), "Forwarding failed: %s",
                          result.error_value().FormatDescription().c_str());
            completer.Reply(std::move(result->value()));
          });
    }
  }

  void EchoXunionsWithError(EchoXunionsWithErrorRequest& request,
                            EchoXunionsWithErrorCompleter::Sync& completer) override {
    if (request->forward_to_server().empty()) {
      if (request->result_variant() == wire::RespondWith::kErr) {
        completer.Reply(Echo_EchoXunionsWithError_Result::WithErr(request->result_err()));
      } else {
        completer.Reply(
            Echo_EchoXunionsWithError_Result::WithResponse(std::move(request->value())));
      }
    } else {
      std::shared_ptr<EchoClientApp> app =
          std::make_shared<EchoClientApp>(request->forward_to_server());
      app->EchoXunionsWithError(
          std::move(request->value()), request->result_err(), "", request->result_variant(),
          [completer = completer.ToAsync(), extend_lifetime = app](
              fitx::result<fidl::Error, fidl::Response<Echo::EchoXunionsWithError>>&
                  result) mutable {
            ZX_ASSERT_MSG(result.is_ok(), "Forwarding failed: %s",
                          result.error_value().FormatDescription().c_str());
            completer.Reply(std::move(result->result()));
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
