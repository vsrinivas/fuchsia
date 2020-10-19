// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// [START includes]
#include <fuchsia/examples/llcpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fdio/directory.h>
#include <lib/fidl/llcpp/server.h>
#include <lib/svc/outgoing.h>
#include <zircon/process.h>
#include <zircon/processargs.h>
#include <zircon/status.h>
// [END includes]

#include <iostream>

// [START impl]
// An implementation of the Echo protocol. Protocols are implemented in LLCPP by
// creating a subclass of the ::Interface class for the protocol.
class EchoImpl final : public llcpp::fuchsia::examples::Echo::Interface {
 public:
  // Bind this implementation to a channel.
  zx_status_t Bind(async_dispatcher_t* dispatcher, zx::channel request) {
    auto result = fidl::BindServer(dispatcher, std::move(request), this);
    if (!result.is_ok()) {
      return result.error();
    }
    binding_ = result.take_value();
    return ZX_OK;
  }

  // Handle a SendString request by sending on OnString event with the request value. For
  // fire and forget methods, the completer can be used to close the channel with an epitaph.
  void SendString(fidl::StringView value, SendStringCompleter::Sync& completer) override {
    if (binding_) {
      binding_.value()->OnString(std::move(value));
    }
  }
  // Handle an EchoString request by responding with the request value. For two-way
  // methods, the completer is also used to send a response.
  void EchoString(fidl::StringView value, EchoStringCompleter::Sync& completer) override {
    completer.Reply(std::move(value));
  }

  // A reference back to the Binding that this class is bound to, which is used
  // to send events to the client.
  fit::optional<fidl::ServerBindingRef<llcpp::fuchsia::examples::Echo>> binding_;
};
// [END impl]

// [START main]
int main(int argc, char** argv) {
  // Initialize the async loop. The Echo server will use the dispatcher of this
  // loop to listen for incoming requests.
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  async_dispatcher_t* dispatcher = loop.dispatcher();

  // Create an instance of our EchoImpl.
  EchoImpl server;

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
      llcpp::fuchsia::examples::Echo::Name,
      fbl::MakeRefCounted<fs::Service>([&server, dispatcher](zx::channel request) mutable {
        zx_status_t status = server.Bind(dispatcher, std::move(request));
        if (status != ZX_OK) {
          printf("vc: error binding new server: %d\n", status);
        }
        return status;
      }));

  std::cout << "Running echo server" << std::endl;
  loop.Run();
  return 0;
}
// [END main]
