// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// [START includes]
#include <fidl/fuchsia.examples/cpp/wire.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fdio/directory.h>
#include <lib/fidl/llcpp/server.h>
#include <lib/stdcompat/optional.h>
#include <lib/svc/outgoing.h>
#include <zircon/process.h>
#include <zircon/processargs.h>
#include <zircon/status.h>
// [END includes]

#include <iostream>

// [START impl]
// An implementation of the Echo protocol. Protocols are implemented in LLCPP by
// creating a subclass of the ::Interface class for the protocol.
class EchoImpl final : public fidl::WireServer<fuchsia_examples::Echo> {
 public:
  // [START bind_server]
  // Bind this implementation to a channel.
  void Bind(std::unique_ptr<EchoImpl> self, async_dispatcher_t* dispatcher,
            fidl::ServerEnd<fuchsia_examples::Echo> request) {
    // Destroy |self| when the unbound handler completes.
    fidl::OnUnboundFn<EchoImpl> unbound_handler =
        [self = std::move(self)](EchoImpl* impl, fidl::UnbindInfo info,
                                 fidl::ServerEnd<fuchsia_examples::Echo> server_end) {
          switch (info.reason()) {
            case fidl::Reason::kClose:
            case fidl::Reason::kUnbind:
              // These are initiated by ourself.
              break;
            default:
              std::cerr << "server error: " << info << std::endl;
          }
        };
    binding_ = fidl::BindServer(dispatcher, std::move(request), this, std::move(unbound_handler));
  }
  // [END bind_server]

  // Handle a SendString request by sending on OnString event with the request value. For
  // fire and forget methods, the completer can be used to close the channel with an epitaph.
  void SendString(SendStringRequestView request, SendStringCompleter::Sync& completer) override {
    if (binding_) {
      binding_.value()->OnString(request->value);
    }
  }

  // Handle an EchoString request by responding with the request value. For two-way
  // methods, the completer is also used to send a response.
  void EchoString(EchoStringRequestView request, EchoStringCompleter::Sync& completer) override {
    completer.Reply(request->value);
  }

 private:
  // A reference back to the Binding that this class is bound to, which is used
  // to send events to the client.
  cpp17::optional<fidl::ServerBindingRef<fuchsia_examples::Echo>> binding_;
};
// [END impl]

// [START main]
int main(int argc, char** argv) {
  // Initialize the async loop. The Echo server will use the dispatcher of this
  // loop to listen for incoming requests.
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  async_dispatcher_t* dispatcher = loop.dispatcher();

  // Create an Outgoing class which will serve requests from the /svc/ directory.
  svc::Outgoing outgoing(loop.dispatcher());
  zx_status_t status = outgoing.ServeFromStartupInfo();
  if (status != ZX_OK) {
    std::cerr << "error: ServeFromStartupInfo returned: " << status << " ("
              << zx_status_get_string(status) << ")" << std::endl;
    return -1;
  }

  // Register a handler for components trying to connect to fuchsia.examples.Echo.
  status = outgoing.svc_dir()->AddEntry(
      fidl::DiscoverableProtocolName<fuchsia_examples::Echo>,
      fbl::MakeRefCounted<fs::Service>(
          [dispatcher](fidl::ServerEnd<fuchsia_examples::Echo> request) mutable {
            std::cout << "Incoming connection for "
                      << fidl::DiscoverableProtocolName<fuchsia_examples::Echo> << std::endl;
            // [START create_server]
            // Create an instance of our EchoImpl.
            std::unique_ptr server = std::make_unique<EchoImpl>();
            server->Bind(std::move(server), dispatcher, std::move(request));
            // [END create_server]
            return ZX_OK;
          }));

  std::cout << "Running echo server" << std::endl;
  loop.Run();
  return 0;
}
// [END main]
