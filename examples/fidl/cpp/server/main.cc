// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// [START fidl_includes]
#include <fidl/fuchsia.examples/cpp/fidl.h>
// [END fidl_includes]

// [START includes]
#include <lib/async-loop/cpp/loop.h>
#include <lib/sys/component/cpp/outgoing_directory.h>
#include <lib/syslog/cpp/macros.h>
// [END includes]

// [START server]
class EchoImpl : public fidl::Server<fuchsia_examples::Echo> {
 public:
  // [START bind_server]
  // Bind a new implementation to a channel. The implementation deletes itself
  // when the connection tears down.
  static void BindSelfManagedServer(async_dispatcher_t* dispatcher,
                                    fidl::ServerEnd<fuchsia_examples::Echo> server_end) {
    std::unique_ptr impl = std::make_unique<EchoImpl>();
    EchoImpl* impl_ptr = impl.get();
    fidl::ServerBindingRef binding_ref = fidl::BindServer(
        dispatcher, std::move(server_end), std::move(impl), std::mem_fn(&EchoImpl::OnUnbound));
    impl_ptr->binding_ref_.emplace(std::move(binding_ref));
  }
  // [END bind_server]

  // Called when the connection is torn down, shortly before the implementation is destroyed.
  void OnUnbound(fidl::UnbindInfo info, fidl::ServerEnd<fuchsia_examples::Echo> server_end) {
    if (info.is_peer_closed()) {
      FX_LOGS(INFO) << "Client disconnected";
    } else if (!info.is_user_initiated()) {
      FX_LOGS(ERROR) << "Server error: " << info;
    }
  }

  void EchoString(EchoStringRequest& request, EchoStringCompleter::Sync& completer) override {
    completer.Reply({request.value()});
  }

  void SendString(SendStringRequest& request, SendStringCompleter::Sync& completer) override {
    ZX_ASSERT(binding_ref_.has_value());

    fitx::result result = fidl::SendEvent(*binding_ref_)->OnString({request.value()});
    if (!result.is_ok()) {
      FX_LOGS(ERROR) << "Error sending event: " << result.error_value();
    }
  }

 private:
  std::optional<fidl::ServerBindingRef<fuchsia_examples::Echo>> binding_ref_;
};
// [END server]

// [START main]
int main(int argc, const char** argv) {
  // Initialize the async loop. The Echo server will use the dispatcher of this
  // loop to listen for incoming requests.
  async::Loop loop(&kAsyncLoopConfigNeverAttachToThread);
  async_dispatcher_t* dispatcher = loop.dispatcher();

  // Create an OutgoingDirectory class which will serve incoming requests.
  component::OutgoingDirectory outgoing = component::OutgoingDirectory::Create(dispatcher);

  zx::status result = outgoing.ServeFromStartupInfo();
  if (result.is_error()) {
    FX_LOGS(ERROR) << "Failed to serve outgoing directory: " << result.status_string();
    return -1;
  }

  // Register a handler for components trying to connect to fuchsia.examples.Echo.
  result = outgoing.AddProtocol<fuchsia_examples::Echo>(
      [dispatcher](fidl::ServerEnd<fuchsia_examples::Echo> server_end) {
        FX_LOGS(INFO) << "Incoming connection for "
                      << fidl::DiscoverableProtocolName<fuchsia_examples::Echo>;
        EchoImpl::BindSelfManagedServer(dispatcher, std::move(server_end));
      });
  if (result.is_error()) {
    FX_LOGS(ERROR) << "Failed to add Echo protocol: " << result.status_string();
    return -1;
  }

  FX_LOGS(INFO) << "Running C++ echo server";
  loop.Run();
  return 0;
}
// [END main]
