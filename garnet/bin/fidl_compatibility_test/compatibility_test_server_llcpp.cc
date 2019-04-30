// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/test/compatibility/llcpp/fidl.h>
#include <fuchsia/sys/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/fidl-async/cpp/bind.h>
#include <lib/fidl/cpp/interface_request.h>
#include <lib/sys/cpp/component_context.h>
#include <zircon/status.h>

#include <cstdlib>
#include <iostream>
#include <string>

constexpr const char kEchoInterfaceName[] = "fidl.test.compatibility.Echo";

namespace fidl {
namespace test {
namespace compatibility {

class EchoClientApp {
 public:
  EchoClientApp(::fidl::StringView server_url)
      : context_(sys::ComponentContext::Create()),
        client_(Echo::SyncClient(ConnectTo(server_url))) {}

  ::fidl::DecodeResult<Echo::EchoStructResponse> EchoStruct(
      ::fidl::BytePart request_buffer, Struct value,
      ::fidl::StringView forward_to_server, ::fidl::BytePart response_buffer) {
    Echo::EchoStructRequest request = {};
    request.value = std::move(value);
    request.forward_to_server = forward_to_server;
    auto linearize_result =
        ::fidl::Linearize(&request, std::move(request_buffer));
    if (linearize_result.status != ZX_OK) {
      return ::fidl::DecodeResult<Echo::EchoStructResponse>(
          linearize_result.status, linearize_result.error);
    }
    return client_.EchoStruct(std::move(linearize_result.message),
                              std::move(response_buffer));
  }

  ::fidl::DecodeResult<Echo::EchoEventResponse> EchoStructNoRetVal(
      Struct value, ::fidl::StringView forward_to_server,
      ::fidl::BytePart response_buffer) {
    auto status =
        client_.EchoStructNoRetVal(std::move(value), forward_to_server);
    if (status != ZX_OK) {
      return ::fidl::DecodeResult<Echo::EchoEventResponse>(
          status, "Failed to send echo event");
    }
    // Wait for event
    ZX_ASSERT(
        client_end_->wait_one(ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED,
                              zx::time::infinite(), nullptr) == ZX_OK);
    ::fidl::EncodedMessage<Echo::EchoEventResponse> encoded_msg;
    encoded_msg.Initialize(
        [&](::fidl::BytePart* out_bytes, ::fidl::HandlePart* handles) {
          *out_bytes = std::move(response_buffer);
          uint32_t actual_bytes = 0;
          uint32_t actual_handles = 0;
          ZX_ASSERT(client_end_->read(0, out_bytes->data(), handles->data(),
                                      out_bytes->capacity(),
                                      handles->capacity(),
                                      &actual_bytes, &actual_handles) == ZX_OK);
          // TODO(FIDL-350): Hard-coding the event ordinal due to no event
          // support in llcpp; refactor once that lands.
          constexpr uint32_t kEchoStructEventOrdinal = 849359397u;
          ZX_ASSERT(actual_bytes > sizeof(fidl_message_header_t));
          ZX_ASSERT(
              reinterpret_cast<fidl_message_header_t*>(
                  out_bytes->data())->ordinal ==
              kEchoStructEventOrdinal);
          out_bytes->set_actual(actual_bytes);
          handles->set_actual(actual_handles);
        });
    // Decode the event
    return ::fidl::Decode(std::move(encoded_msg));
  }

  EchoClientApp(const EchoClientApp&) = delete;
  EchoClientApp& operator=(const EchoClientApp&) = delete;

 private:
  // Called once upon construction to launch and connect to the server.
  zx::channel ConnectTo(::fidl::StringView server_url) {
    fuchsia::sys::LaunchInfo launch_info;
    launch_info.url = std::string(server_url.data(), server_url.size());
    echo_provider_ = sys::ServiceDirectory::CreateWithRequest(
        &launch_info.directory_request);

    fuchsia::sys::LauncherPtr launcher;
    context_->svc()->Connect(launcher.NewRequest());
    launcher->CreateComponent(std::move(launch_info), controller_.NewRequest());

    zx::channel server_end, client_end;
    ZX_ASSERT(zx::channel::create(0, &client_end, &server_end) == ZX_OK);
    ZX_ASSERT(echo_provider_->Connect(kEchoInterfaceName,
                                      std::move(server_end)) == ZX_OK);

    client_end_ = zx::unowned_channel(client_end);
    return client_end;
  }

  std::unique_ptr<sys::ComponentContext> context_;
  std::shared_ptr<sys::ServiceDirectory> echo_provider_;
  fuchsia::sys::ComponentControllerPtr controller_;
  // Used to monitor events
  zx::unowned_channel client_end_;
  Echo::SyncClient client_;
};

class EchoConnection final : public Echo::Interface {
 public:
  explicit EchoConnection(zx::unowned_channel channel) : channel_(channel) {}

  void EchoStruct(Struct value, ::fidl::StringView forward_to_server,
                  EchoStructCompleter::Sync completer) override {
    if (forward_to_server.empty()) {
      completer.Reply(std::move(value));
    } else {
      std::vector<uint8_t> request_buffer(ZX_CHANNEL_MAX_MSG_BYTES);
      std::vector<uint8_t> response_buffer(ZX_CHANNEL_MAX_MSG_BYTES);
      EchoClientApp app(forward_to_server);
      auto result = app.EchoStruct(
          ::fidl::BytePart(&request_buffer[0],
                           static_cast<uint32_t>(request_buffer.size())),
          std::move(value), ::fidl::StringView{0, ""},
          ::fidl::BytePart(&response_buffer[0],
                           static_cast<uint32_t>(response_buffer.size())));
      ZX_ASSERT_MSG(result.status == ZX_OK,
                    "Forwarding failed: %s",
                    result.error);
      completer.Reply(std::move(result.message.message()->value));
    }
  }

  void EchoStructNoRetVal(
      Struct value, ::fidl::StringView forward_to_server,
      EchoStructNoRetValCompleter::Sync completer) override {
    if (forward_to_server.empty()) {
      auto status = Echo::SendEchoEventEvent(zx::unowned_channel(channel_),
                                             std::move(value));
      ZX_ASSERT_MSG(status == ZX_OK,
                    "Replying with event failed: %s",
                    zx_status_get_string(status));
    } else {
      std::vector<uint8_t> response_buffer(ZX_CHANNEL_MAX_MSG_BYTES);
      EchoClientApp app(forward_to_server);
      auto result = app.EchoStructNoRetVal(
          std::move(value), ::fidl::StringView{0, ""},
          ::fidl::BytePart(&response_buffer[0],
                           static_cast<uint32_t>(response_buffer.size())));
      ZX_ASSERT_MSG(result.status == ZX_OK,
                    "Forwarding failed: %s",
                    result.error);
      auto status =
          Echo::SendEchoEventEvent(zx::unowned_channel(channel_),
                                   std::move(result.message.message()->value));
      ZX_ASSERT_MSG(status == ZX_OK,
                    "Replying with event failed: %s",
                    zx_status_get_string(status));
    }
  }

 private:
  zx::unowned_channel channel_;
};

}  // namespace compatibility
}  // namespace test
}  // namespace fidl

int main(int argc, const char** argv) {
  // The FIDL support lib requires async_get_default_dispatcher() to return
  // non-null.
  async::Loop loop(&kAsyncLoopConfigAttachToThread);
  auto context = sys::ComponentContext::Create();
  std::vector<std::unique_ptr<fidl::test::compatibility::EchoConnection>>
      connections;

  context->outgoing()->AddPublicService(
      std::make_unique<vfs::Service>([&](zx::channel request,
                                         async_dispatcher_t* dispatcher) {
        auto conn = std::make_unique<fidl::test::compatibility::EchoConnection>(
            zx::unowned_channel(request));
        ZX_ASSERT(fidl::Bind(dispatcher, std::move(request), conn.get()) ==
                  ZX_OK);
        connections.push_back(std::move(conn));
      }),
      kEchoInterfaceName);

  loop.Run();
  return EXIT_SUCCESS;
}
