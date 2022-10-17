// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// ============================================================================
// This is an accompanying example code for the C++ server tutorial. Head over
// there for the full walk-through:
// https://fuchsia.dev/fuchsia-src/development/languages/fidl/tutorials/cpp/basics/server
// ============================================================================

#include <fidl/fuchsia.examples/cpp/wire.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/sys/component/cpp/outgoing_directory.h>
#include <lib/syslog/cpp/macros.h>

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

  // [START handlers]
  // The handler for `fuchsia.examples/Echo.EchoString`.
  //
  // For two-way methods (those with a response) like this one, the completer is
  // used complete the call: either to send the reply via |completer.Reply|, or
  // close the channel via |completer.Close|.
  //
  // |EchoStringRequestView| exposes the same API as a pointer to the request
  // struct domain object, that is
  // |fuchsia_examples::wire::EchoEchoStringRequest*|.
  // [START impl-echo-string]
  void EchoString(EchoStringRequestView request, EchoStringCompleter::Sync& completer) override {
    // Call |Reply| to reply synchronously with the request value.
    completer.Reply(request->value);
  }
  // [END impl-echo-string]

  // The handler for `fuchsia.examples/Echo.SendString`.
  //
  // For fire-and-forget methods like this one, the completer is normally not
  // used, but its |Close(zx_status_t)| method can be used to close the channel,
  // either when the protocol has reached its intended terminal state or the
  // server encountered an unrecoverable error.
  //
  // |SendStringRequestView| exposes the same API as a pointer to the request
  // struct domain object, that is
  // |fuchsia_examples::wire::EchoSendStringRequest*|.
  void SendString(SendStringRequestView request, SendStringCompleter::Sync& completer) override {
    // Handle a SendString request by sending an |OnString| event (an
    // unsolicited server-to-client message) back to the client.
    fidl::Status status = fidl::WireSendEvent(binding_)->OnString(request->value);
    if (!status.ok()) {
      FX_LOGS(ERROR) << "Error sending event: " << status.error();
    }
  }
  // [END handlers]

 private:
  // `ServerBindingRef` can be used to:
  // - Control the binding, such as to unbind the server from the channel or
  //   close the channel.
  // - Send events back to the client.
  // See the documentation comments on |fidl::ServerBindingRef|.
  fidl::ServerBindingRef<fuchsia_examples::Echo> binding_;
};

int main(int argc, char** argv) {
  // The event loop is used to asynchronously listen for incoming connections
  // and requests from the client. The following initializes the loop, and
  // obtains the dispatcher, which will be used when binding the server
  // implementation to a channel.
  async::Loop loop(&kAsyncLoopConfigNeverAttachToThread);
  async_dispatcher_t* dispatcher = loop.dispatcher();

  // Create an |OutgoingDirectory| instance.
  //
  // The |component::OutgoingDirectory| class serves the outgoing directory for
  // our component. This directory is where the outgoing FIDL protocols are
  // installed so that they can be provided to other components.
  component::OutgoingDirectory outgoing = component::OutgoingDirectory::Create(dispatcher);

  // The `ServeFromStartupInfo()` function sets up the outgoing directory with
  // the startup handle. The startup handle is a handle provided to every
  // component by the system, so that they can serve capabilities (e.g. FIDL
  // protocols) to other components.
  zx::result result = outgoing.ServeFromStartupInfo();
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

  FX_LOGS(INFO) << "Running C++ echo server with wire types";
  loop.Run();
  return 0;
}
