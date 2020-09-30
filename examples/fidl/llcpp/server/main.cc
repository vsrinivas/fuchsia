// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// [START includes]
#include <fuchsia/examples/llcpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fdio/directory.h>
#include <lib/fidl/llcpp/server.h>
#include <lib/svc/dir.h>
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

// [START handler]
// The extra data that svc_dir_add_service passes to our connect function.
struct ConnectRequestContext {
  async_dispatcher_t* dispatcher;
  std::unique_ptr<EchoImpl> server;
};

// This function is called when another component tries to connect to the Echo protocol.
// It takes the channel handle that is sent by the connecting component, and binds it to
// an instance of our Echo implementation.
static void connect(void* untyped_context, const char* service_name, zx_handle_t service_request) {
  auto context = static_cast<ConnectRequestContext*>(untyped_context);
  std::cout << "echo_server_llcpp: Incoming connection for " << service_name << std::endl;
  auto result =
      fidl::BindServer(context->dispatcher, zx::channel(service_request), context->server.get());
  if (result.is_ok()) {
    context->server->binding_ = result.take_value();
  }
}
// [END handler]

// [START main]
int main(int argc, char** argv) {
  // Initialize the async loop. The Echo server will use the dispatcher of this
  // loop to listen for incoming requests.
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  async_dispatcher_t* dispatcher = loop.dispatcher();

  // Get the startup handle provided to this component by the component framework.
  // Every component has a startup handle, and can use it to provide capabilities
  // (like FIDL protocols) to other components.
  zx_handle_t directory_request = zx_take_startup_handle(PA_DIRECTORY_REQUEST);
  if (directory_request == ZX_HANDLE_INVALID) {
    std::cerr << "error: directory_request was ZX_HANDLE_INVALID" << std::endl;
    return -1;
  }

  // Wrap the raw startup handle in an svc_dir_t, which is used by the fdio svc_* functions.
  svc_dir_t* dir = nullptr;
  zx_status_t status = svc_dir_create(dispatcher, directory_request, &dir);
  if (status != ZX_OK) {
    std::cerr << "error: svc_dir_create returned: " << status << " ("
              << zx_status_get_string(status) << ")" << std::endl;
    return status;
  }

  ConnectRequestContext context = {.dispatcher = dispatcher,
                                   .server = std::make_unique<EchoImpl>()};
  // Register a handler for components trying to connect to fuchsia.examples.Echo.
  status = svc_dir_add_service(dir, "svc", "fuchsia.examples.Echo", &context, connect);
  if (status != ZX_OK) {
    std::cerr << "error: svc_dir_add_service returned: " << status << " ("
              << zx_status_get_string(status) << ")" << std::endl;
    return status;
  }

  std::cout << "Running echo server" << std::endl;
  loop.Run();
  return 0;
}
// [END main]
