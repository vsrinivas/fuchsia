// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fidl.test.compatibility/cpp/wire.h>
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

class EchoClientApp {
 public:
  EchoClientApp(::fidl::StringView server_url)
      : context_(sys::ComponentContext::CreateAndServeOutgoingDirectory()),
        client_(fidl::WireSyncClient<Echo>(ConnectTo(server_url))) {}

  // Half the methods are testing the managed flavor; the other half are testing caller-allocate.

  fidl::WireResult<Echo::EchoMinimal> EchoMinimal(::fidl::StringView forward_to_server) {
    return client_.EchoMinimal(std::move(forward_to_server));
  }

  fidl::WireResult<Echo::EchoMinimalWithError> EchoMinimalWithError(
      ::fidl::StringView forward_to_server, wire::RespondWith result_variant) {
    return client_.EchoMinimalWithError(std::move(forward_to_server), result_variant);
  }

  zx_status_t EchoMinimalNoRetVal(::fidl::StringView forward_to_server,
                                  fidl::WireSyncEventHandler<Echo>& event_handler) {
    auto result = client_.EchoMinimalNoRetVal(std::move(forward_to_server));
    if (result.status() != ZX_OK) {
      return result.status();
    }
    return client_.HandleOneEvent(event_handler).status();
  }

  fidl::WireResult<Echo::EchoStruct> EchoStruct(wire::Struct value,
                                                ::fidl::StringView forward_to_server) {
    return client_.EchoStruct(std::move(value), std::move(forward_to_server));
  }

  fidl::WireResult<Echo::EchoStructWithError> EchoStructWithError(
      wire::Struct value, wire::DefaultEnum err, ::fidl::StringView forward_to_server,
      wire::RespondWith result_variant) {
    return client_.EchoStructWithError(std::move(value), err, std::move(forward_to_server),
                                       result_variant);
  }

  zx_status_t EchoStructNoRetVal(wire::Struct value, ::fidl::StringView forward_to_server,
                                 fidl::WireSyncEventHandler<Echo>& event_handler) {
    auto result = client_.EchoStructNoRetVal(std::move(value), std::move(forward_to_server));
    if (result.status() != ZX_OK) {
      return result.status();
    }
    return client_.HandleOneEvent(event_handler).status();
  }

  fidl::WireUnownedResult<Echo::EchoArrays> EchoArrays(::fidl::BufferSpan request_buffer,
                                                       wire::ArraysStruct value,
                                                       ::fidl::StringView forward_to_server,
                                                       ::fidl::BufferSpan response_buffer) {
    return client_.EchoArrays(request_buffer, std::move(value), std::move(forward_to_server),
                              response_buffer);
  }

  fidl::WireResult<Echo::EchoArraysWithError> EchoArraysWithError(
      wire::ArraysStruct value, wire::DefaultEnum err, ::fidl::StringView forward_to_server,
      wire::RespondWith result_variant) {
    return client_.EchoArraysWithError(std::move(value), err, std::move(forward_to_server),
                                       result_variant);
  }

  fidl::WireResult<Echo::EchoVectors> EchoVectors(wire::VectorsStruct value,
                                                  ::fidl::StringView forward_to_server) {
    return client_.EchoVectors(std::move(value), std::move(forward_to_server));
  }

  fidl::WireResult<Echo::EchoVectorsWithError> EchoVectorsWithError(
      wire::VectorsStruct value, wire::DefaultEnum err, ::fidl::StringView forward_to_server,
      wire::RespondWith result_variant) {
    return client_.EchoVectorsWithError(std::move(value), err, std::move(forward_to_server),
                                        result_variant);
  }

  fidl::WireUnownedResult<Echo::EchoTable> EchoTable(::fidl::BufferSpan request_buffer,
                                                     wire::AllTypesTable value,
                                                     ::fidl::StringView forward_to_server,
                                                     ::fidl::BufferSpan response_buffer) {
    return client_.EchoTable(request_buffer, std::move(value), std::move(forward_to_server),
                             response_buffer);
  }

  fidl::WireUnownedResult<Echo::EchoTableWithError> EchoTableWithError(
      ::fidl::BufferSpan request_buffer, wire::AllTypesTable value, wire::DefaultEnum err,
      ::fidl::StringView forward_to_server, wire::RespondWith result_variant,
      ::fidl::BufferSpan response_buffer) {
    return client_.EchoTableWithError(request_buffer, std::move(value), err,
                                      std::move(forward_to_server), result_variant,
                                      response_buffer);
  }

  fidl::WireResult<Echo::EchoXunions> EchoXunions(::fidl::VectorView<wire::AllTypesXunion> value,
                                                  ::fidl::StringView forward_to_server) {
    return client_.EchoXunions(std::move(value), std::move(forward_to_server));
  }

  fidl::WireResult<Echo::EchoXunionsWithError> EchoXunionsWithError(
      ::fidl::VectorView<wire::AllTypesXunion> value, wire::DefaultEnum err,
      ::fidl::StringView forward_to_server, wire::RespondWith result_variant) {
    return client_.EchoXunionsWithError(std::move(value), err, std::move(forward_to_server),
                                        result_variant);
  }

  EchoClientApp(const EchoClientApp&) = delete;
  EchoClientApp& operator=(const EchoClientApp&) = delete;

 private:
  // Called once upon construction to launch and connect to the server.
  ::fidl::ClientEnd<fidl_test_compatibility::Echo> ConnectTo(::fidl::StringView server_url) {
    fuchsia::sys::LaunchInfo launch_info;
    launch_info.url = std::string(server_url.data(), server_url.size());
    echo_provider_ = sys::ServiceDirectory::CreateWithRequest(&launch_info.directory_request);

    fuchsia::sys::LauncherPtr launcher;
    context_->svc()->Connect(launcher.NewRequest());
    launcher->CreateComponent(std::move(launch_info), controller_.NewRequest());

    auto echo_ends = ::fidl::CreateEndpoints<fidl_test_compatibility::Echo>();
    ZX_ASSERT(echo_ends.is_ok());
    ZX_ASSERT(echo_provider_->Connect(kEchoInterfaceName, echo_ends->server.TakeChannel()) ==
              ZX_OK);

    return std::move(echo_ends->client);
  }

  std::unique_ptr<sys::ComponentContext> context_;
  std::shared_ptr<sys::ServiceDirectory> echo_provider_;
  fuchsia::sys::ComponentControllerPtr controller_;
  fidl::WireSyncClient<Echo> client_;
};

class EchoConnection final : public fidl::WireServer<Echo> {
 public:
  EchoConnection() = default;

  void EchoMinimal(EchoMinimalRequestView request, EchoMinimalCompleter::Sync& completer) override {
    if (request->forward_to_server.empty()) {
      completer.Reply();
    } else {
      EchoClientApp app(request->forward_to_server);
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
      EchoClientApp app(request->forward_to_server);
      auto result = app.EchoMinimalWithError("", request->result_variant);
      ZX_ASSERT_MSG(result.status() == ZX_OK, "Forwarding failed: %s",
                    result.FormatDescription().c_str());
      completer.Reply(std::move(result->result));
    }
  }

  void EchoMinimalNoRetVal(EchoMinimalNoRetValRequestView request,
                           EchoMinimalNoRetValCompleter::Sync&) override {
    if (request->forward_to_server.empty()) {
      fidl::Result result = server_binding_.value()->EchoMinimalEvent();
      ZX_ASSERT_MSG(result.ok(), "Replying with event failed: %s",
                    result.FormatDescription().c_str());
    } else {
      class EventHandler : public fidl::WireSyncEventHandler<Echo> {
       public:
        explicit EventHandler(EchoConnection* connection) : connection_(connection) {}

        fidl::Result result() const { return result_; }

        void EchoMinimalEvent(fidl::WireResponse<Echo::EchoMinimalEvent>* event) override {
          result_ = connection_->server_binding_.value()->EchoMinimalEvent();
        }

        zx_status_t Unknown() override {
          ZX_PANIC("Received unexpected event");
          return ZX_ERR_INVALID_ARGS;
        }

       private:
        EchoConnection* const connection_;
        fidl::Result result_ = fidl::Result::Ok();
      };

      EchoClientApp app(request->forward_to_server);
      EventHandler event_handler(this);
      zx_status_t status = app.EchoMinimalNoRetVal("", event_handler);
      ZX_ASSERT_MSG(status == ZX_OK, "Replying with event failed direct: %s",
                    zx_status_get_string(status));
      ZX_ASSERT_MSG(event_handler.result().ok(), "Replying with event failed indirect: %s",
                    event_handler.result().FormatDescription().c_str());
    }
  }

  void EchoStruct(EchoStructRequestView request, EchoStructCompleter::Sync& completer) override {
    if (request->forward_to_server.empty()) {
      completer.Reply(std::move(request->value));
    } else {
      EchoClientApp app(request->forward_to_server);
      auto result = app.EchoStruct(std::move(request->value), "");
      ZX_ASSERT_MSG(result.status() == ZX_OK, "Forwarding failed: %s",
                    result.FormatDescription().c_str());
      completer.Reply(std::move(result.Unwrap()->value));
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
      EchoClientApp app(request->forward_to_server);
      auto result = app.EchoStructWithError(std::move(request->value), request->result_err, "",
                                            request->result_variant);
      ZX_ASSERT_MSG(result.status() == ZX_OK, "Forwarding failed: %s",
                    result.FormatDescription().c_str());
      completer.Reply(std::move(result->result));
    }
  }

  void EchoStructNoRetVal(EchoStructNoRetValRequestView request,
                          EchoStructNoRetValCompleter::Sync&) override {
    if (request->forward_to_server.empty()) {
      fidl::Result result = server_binding_.value()->EchoEvent(std::move(request->value));
      ZX_ASSERT_MSG(result.ok(), "Replying with event failed: %s",
                    result.FormatDescription().c_str());
    } else {
      class EventHandler : public fidl::WireSyncEventHandler<Echo> {
       public:
        explicit EventHandler(EchoConnection* connection) : connection_(connection) {}

        fidl::Result result() const { return result_; }

        void EchoEvent(fidl::WireResponse<Echo::EchoEvent>* event) override {
          result_ = connection_->server_binding_.value()->EchoEvent(std::move(event->value));
        }

        zx_status_t Unknown() override {
          ZX_PANIC("Received unexpected event");
          return ZX_ERR_INVALID_ARGS;
        }

       private:
        EchoConnection* const connection_;
        fidl::Result result_ = fidl::Result::Ok();
      };

      EchoClientApp app(request->forward_to_server);
      EventHandler event_handler(this);
      zx_status_t status = app.EchoStructNoRetVal(std::move(request->value), "", event_handler);
      ZX_ASSERT_MSG(status == ZX_OK, "Replying with event failed direct: %s",
                    zx_status_get_string(status));
      ZX_ASSERT_MSG(event_handler.result().ok(), "Replying with event failed indirect: %s",
                    event_handler.result().FormatDescription().c_str());
    }
  }

  void EchoArrays(EchoArraysRequestView request, EchoArraysCompleter::Sync& completer) override {
    if (request->forward_to_server.empty()) {
      completer.Reply(std::move(request->value));
    } else {
      std::vector<uint8_t> request_buffer(ZX_CHANNEL_MAX_MSG_BYTES);
      std::vector<uint8_t> response_buffer(ZX_CHANNEL_MAX_MSG_BYTES);
      EchoClientApp app(request->forward_to_server);
      auto result = app.EchoArrays(
          ::fidl::BufferSpan(&request_buffer[0], static_cast<uint32_t>(request_buffer.size())),
          std::move(request->value), "",
          ::fidl::BufferSpan(&response_buffer[0], static_cast<uint32_t>(response_buffer.size())));
      ZX_ASSERT_MSG(result.status() == ZX_OK, "Forwarding failed: %s",
                    result.FormatDescription().c_str());
      completer.Reply(std::move(result.Unwrap()->value));
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
      EchoClientApp app(request->forward_to_server);
      auto result = app.EchoArraysWithError(std::move(request->value), request->result_err, "",
                                            request->result_variant);
      ZX_ASSERT_MSG(result.status() == ZX_OK, "Forwarding failed: %s: %s",
                    zx_status_get_string(result.status()), result.FormatDescription().c_str());
      completer.Reply(std::move(result->result));
    }
  }

  void EchoVectors(EchoVectorsRequestView request, EchoVectorsCompleter::Sync& completer) override {
    if (request->forward_to_server.empty()) {
      completer.Reply(std::move(request->value));
    } else {
      EchoClientApp app(request->forward_to_server);
      wire::VectorsStruct out_value;
      auto result = app.EchoVectors(std::move(request->value), "");
      ZX_ASSERT_MSG(result.status() == ZX_OK, "Forwarding failed: %s: %s",
                    zx_status_get_string(result.status()), result.FormatDescription().c_str());
      completer.Reply(std::move(result.Unwrap()->value));
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
      EchoClientApp app(request->forward_to_server);
      auto result = app.EchoVectorsWithError(std::move(request->value), request->result_err, "",
                                             request->result_variant);
      ZX_ASSERT_MSG(result.status() == ZX_OK, "Forwarding failed: %s: %s",
                    zx_status_get_string(result.status()), result.FormatDescription().c_str());
      completer.Reply(std::move(result->result));
    }
  }

  void EchoTable(EchoTableRequestView request, EchoTableCompleter::Sync& completer) override {
    if (request->forward_to_server.empty()) {
      completer.Reply(std::move(request->value));
    } else {
      std::vector<uint8_t> request_buffer(ZX_CHANNEL_MAX_MSG_BYTES);
      std::vector<uint8_t> response_buffer(ZX_CHANNEL_MAX_MSG_BYTES);
      EchoClientApp app(request->forward_to_server);
      auto result = app.EchoTable(
          ::fidl::BufferSpan(&request_buffer[0], static_cast<uint32_t>(request_buffer.size())),
          std::move(request->value), "",
          ::fidl::BufferSpan(&response_buffer[0], static_cast<uint32_t>(response_buffer.size())));
      ZX_ASSERT_MSG(result.status() == ZX_OK, "Forwarding failed: %s: %s",
                    zx_status_get_string(result.status()), result.FormatDescription().c_str());
      completer.Reply(std::move(result.Unwrap()->value));
    }
  }

  void EchoTableWithError(EchoTableWithErrorRequestView request,
                          EchoTableWithErrorCompleter::Sync& completer) override {
    if (request->forward_to_server.empty()) {
      if (request->result_variant == wire::RespondWith::kErr) {
        completer.ReplyError(request->result_err);
      } else {
        completer.ReplySuccess(std::move(request->value));
      }
    } else {
      std::vector<uint8_t> request_buffer(ZX_CHANNEL_MAX_MSG_BYTES);
      std::vector<uint8_t> response_buffer(ZX_CHANNEL_MAX_MSG_BYTES);
      EchoClientApp app(request->forward_to_server);
      auto result = app.EchoTableWithError(
          ::fidl::BufferSpan(&request_buffer[0], static_cast<uint32_t>(request_buffer.size())),
          std::move(request->value), request->result_err, "", request->result_variant,
          ::fidl::BufferSpan(&response_buffer[0], static_cast<uint32_t>(response_buffer.size())));
      ZX_ASSERT_MSG(result.status() == ZX_OK, "Forwarding failed: %s: %s",
                    zx_status_get_string(result.status()), result.FormatDescription().c_str());
      completer.Reply(std::move(result->result));
    }
  }

  void EchoXunions(EchoXunionsRequestView request, EchoXunionsCompleter::Sync& completer) override {
    if (request->forward_to_server.empty()) {
      completer.Reply(std::move(request->value));
    } else {
      EchoClientApp app(request->forward_to_server);
      auto result = app.EchoXunions(std::move(request->value), "");
      ZX_ASSERT_MSG(result.status() == ZX_OK, "Forwarding failed: %s: %s",
                    zx_status_get_string(result.status()), result.FormatDescription().c_str());
      completer.Reply(std::move(result.Unwrap()->value));
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
      EchoClientApp app(request->forward_to_server);
      auto result = app.EchoXunionsWithError(std::move(request->value), request->result_err, "",
                                             request->result_variant);
      ZX_ASSERT_MSG(result.status() == ZX_OK, "Forwarding failed: %s: %s",
                    zx_status_get_string(result.status()), result.FormatDescription().c_str());
      completer.Reply(std::move(result->result));
    }
  }

  void set_server_binding(::fidl::ServerBindingRef<Echo> binding) {
    server_binding_.emplace(binding);
  }

 private:
  cpp17::optional<::fidl::ServerBindingRef<Echo>> server_binding_;
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
