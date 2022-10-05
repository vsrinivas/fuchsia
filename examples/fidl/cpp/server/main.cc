// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// ============================================================================
// This is an accompanying example code for the C++ server tutorial. Head over
// there for the full walk-through:
// https://fuchsia.dev/fuchsia-src/development/languages/fidl/tutorials/cpp/basics/server
// ============================================================================

// [START includes]
#include <fidl/fuchsia.examples/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/sys/component/cpp/outgoing_directory.h>
#include <lib/syslog/cpp/macros.h>
// [END includes]

// [START server]
class EchoImpl : public fidl::Server<fuchsia_examples::Echo> {
 public:
  // [START impl]
  // The handler for `fuchsia.examples/Echo.EchoString`.
  //
  // For two-way methods (those with a response) like this one, the completer is
  // used complete the call: either to send the reply via |completer.Reply|, or
  // close the channel via |completer.Close|.
  //
  // |EchoStringRequest| exposes the same API as the request struct domain
  // object, that is |fuchsia_examples::EchoEchoStringRequest|.
  // [START impl-echo-string]
  void EchoString(EchoStringRequest& request, EchoStringCompleter::Sync& completer) override {
    // Call |Reply| to reply synchronously with the request value.
    completer.Reply({{.response = request.value()}});
  }
  // [END impl-echo-string]

  // The handler for `fuchsia.examples/Echo.SendString`.
  //
  // For fire-and-forget methods like this one, the completer is normally not
  // used, but its |Close(zx_status_t)| method can be used to close the channel,
  // either when the protocol has reached its intended terminal state or the
  // server encountered an unrecoverable error.
  //
  // |SendStringRequest| exposes the same API as the request struct domain
  // object, that is |fuchsia_examples::EchoSendStringRequest|.
  void SendString(SendStringRequest& request, SendStringCompleter::Sync& completer) override {
    ZX_ASSERT(binding_ref_.has_value());

    // Handle a SendString request by sending an |OnString| event (an
    // unsolicited server-to-client message) back to the client.
    fit::result result = fidl::SendEvent(*binding_ref_)->OnString({request.value()});
    if (!result.is_ok()) {
      FX_LOGS(ERROR) << "Error sending event: " << result.error_value();
    }
  }
  // [END impl]

  // [START bind_server]
  // Bind a new implementation of |EchoImpl| to handle requests coming from
  // the server endpoint |server_end|.
  static void BindSelfManagedServer(async_dispatcher_t* dispatcher,
                                    fidl::ServerEnd<fuchsia_examples::Echo> server_end) {
    // Create a new instance of |EchoImpl|.
    std::unique_ptr impl = std::make_unique<EchoImpl>();
    EchoImpl* impl_ptr = impl.get();

    // |fidl::BindServer| takes a FIDL protocol server implementation and a
    // channel. It asynchronously reads requests off the channel, decodes them
    // and dispatches them to the correct handler on the server implementation.
    //
    // The FIDL protocol server implementation can be passed as a
    // |std::shared_ptr|, |std::unique_ptr|, or raw pointer. For shared and
    // unique pointers, the binding will manage the lifetime of the
    // implementation object. For raw pointers, it's up to the caller to ensure
    // that the implementation object outlives the binding but does not leak.
    //
    // See the documentation comment of |fidl::BindServer|.
    fidl::ServerBindingRef binding_ref = fidl::BindServer(
        dispatcher, std::move(server_end), std::move(impl), std::mem_fn(&EchoImpl::OnUnbound));
    // Put the returned |binding_ref| into the |EchoImpl| object.
    impl_ptr->binding_ref_.emplace(std::move(binding_ref));
  }

  // This method is passed to the |BindServer| call as the last argument,
  // which means it will be called when the connection is torn down.
  // In this example we use it to log some connection lifecycle information.
  // Production code could do more things such as resource cleanup.
  void OnUnbound(fidl::UnbindInfo info, fidl::ServerEnd<fuchsia_examples::Echo> server_end) {
    // |is_user_initiated| returns true if the server code called |Close| on a
    // completer, or |Unbind| / |Close| on the |binding_ref_|, to proactively
    // teardown the connection. These cases are usually part of normal server
    // shutdown, so logging is unnecessary.
    if (info.is_user_initiated()) {
      return;
    }
    if (info.is_peer_closed()) {
      // If the peer (the client) closed their endpoint, log that as INFO.
      FX_LOGS(INFO) << "Client disconnected";
    } else {
      // Treat other unbind causes as errors.
      FX_LOGS(ERROR) << "Server error: " << info;
    }
  }
  // [END bind_server]

  // [START binding_ref]
 private:
  // `ServerBindingRef` can be used to:
  // - Control the binding, such as to unbind the server from the channel or
  //   close the channel.
  // - Send events back to the client.
  // See the documentation comments on |fidl::ServerBindingRef|.
  std::optional<fidl::ServerBindingRef<fuchsia_examples::Echo>> binding_ref_;
  // [END binding_ref]
};
// [END server]

// [START main]
int main(int argc, const char** argv) {
  // [START serve-out-dir]
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
  zx::status result = outgoing.ServeFromStartupInfo();
  if (result.is_error()) {
    FX_LOGS(ERROR) << "Failed to serve outgoing directory: " << result.status_string();
    return -1;
  }
  // [END serve-out-dir]

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

  FX_LOGS(INFO) << "Running C++ echo server with natural types";

  // This runs the event loop and blocks until the loop is quit or shutdown.
  // See documentation comments on |async::Loop|.
  loop.Run();
  return 0;
}
// [END main]
