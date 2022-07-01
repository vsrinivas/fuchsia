// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// [START fidl_includes]
#include <fidl/fuchsia.examples/cpp/wire.h>
// [END fidl_includes]

// [START includes]
#include <lib/async-loop/cpp/loop.h>
#include <lib/sys/component/cpp/outgoing_directory.h>
#include <lib/syslog/cpp/macros.h>
// [END includes]

// [START impl]
// An implementation of the Echo protocol. Protocols are implemented in LLCPP by
// creating a subclass of the fidl::WireServer class for the protocol.
class EchoImpl final : public fidl::WireServer<fuchsia_examples::Echo> {
 public:
  // [START bind_server]
  // Bind this implementation to a channel.
  EchoImpl(async_dispatcher_t* dispatcher, fidl::ServerEnd<fuchsia_examples::Echo> server_end)
      : binding_(fidl::BindServer(dispatcher, std::move(server_end), this,
                                  // This is a fidl::OnUnboundFn<EchoImpl>.
                                  [this](EchoImpl* impl, fidl::UnbindInfo info,
                                         fidl::ServerEnd<fuchsia_examples::Echo> server_end) {
                                    if (info.is_peer_closed()) {
                                      FX_LOGS(INFO) << "Client disconnected";
                                    } else if (!info.is_user_initiated()) {
                                      FX_LOGS(ERROR) << "Server error: " << info;
                                    }
                                    delete this;
                                  })) {}
  // [END bind_server]

  // Handle a SendString request by sending on OnString "event" (an unsolicited server-to-client
  // message) back on the same channel.
  //
  // For fire-and-forget methods like this one, the completer is normally not used but its
  // Close(zx_status_t) method can be used to close the channel (either if the connection is "done"
  // or it encountered an unrecoverable error).
  void SendString(SendStringRequestView request, SendStringCompleter::Sync& completer) override {
    fidl::Status status = fidl::WireSendEvent(binding_)->OnString(request->value);
    ZX_ASSERT(status.ok());
  }

  // Handle an EchoString request by responding with the request value. For two-way methods (those
  // with a response) like this one, the completer is used to send the response.
  void EchoString(EchoStringRequestView request, EchoStringCompleter::Sync& completer) override {
    completer.Reply(request->value);
  }

 private:
  // A reference back to the Binding that this class is bound to, which is used
  // to send events to the client.
  fidl::ServerBindingRef<fuchsia_examples::Echo> binding_;
};
// [END impl]

// [START main]
int main(int argc, char** argv) {
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
        // [START create_server]
        // Create an instance of our EchoImpl that destroys itself when the connection closes.
        new EchoImpl(dispatcher, std::move(server_end));
        // [END create_server]
      });
  if (result.is_error()) {
    FX_LOGS(ERROR) << "Failed to add Echo protocol: " << result.status_string();
    return -1;
  }

  FX_LOGS(INFO) << "Running echo server";
  loop.Run();
  return 0;
}
// [END main]
