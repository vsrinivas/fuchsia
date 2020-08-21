// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/examples/llcpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/cpp/task.h>
#include <lib/fdio/directory.h>
#include <lib/fidl/llcpp/server.h>
#include <lib/svc/dir.h>
#include <unistd.h>
#include <zircon/process.h>
#include <zircon/processargs.h>
#include <zircon/status.h>

#include <iostream>

// [START impl]
class EchoImpl final : public llcpp::fuchsia::examples::Echo::Interface {
 public:
  explicit EchoImpl(async_dispatcher_t* dispatcher) : dispatcher_(dispatcher) {}
  // SendString is not used in this example, so requests are just ignored.
  void SendString(fidl::StringView value, SendStringCompleter::Sync completer) override {}
  void EchoString(fidl::StringView value, EchoStringCompleter::Sync completer) override {
    // Respond to the request asynchronously by using ToAsync() and moving it into
    // a lambda capture. This allows multiple requests to EchoString to wait concurrently
    // rather than in sequence.
    async::PostDelayedTask(
        dispatcher_,
        [value = std::move(value), completer = completer.ToAsync()]() mutable {
          completer.Reply(std::move(value));
        },
        zx::duration(ZX_SEC(5)));
  }

  // Contains a pointer to the dispatcher in order to wait asynchronously using
  // PostDelayedTask.
  async_dispatcher_t* dispatcher_;
};
// [END impl]

struct ConnectRequestContext {
  async_dispatcher_t* dispatcher;
  std::unique_ptr<EchoImpl> server;
};

static void connect(void* untyped_context, const char* service_name, zx_handle_t service_request) {
  auto context = static_cast<ConnectRequestContext*>(untyped_context);
  std::cout << "echo_server_llcpp: Incoming connection for " << service_name << std::endl;
  fidl::BindServer(context->dispatcher, zx::channel(service_request), context->server.get());
}

int main(int argc, char** argv) {
  zx_handle_t directory_request = zx_take_startup_handle(PA_DIRECTORY_REQUEST);
  if (directory_request == ZX_HANDLE_INVALID) {
    std::cerr << "error: directory_request was ZX_HANDLE_INVALID" << std::endl;
    return -1;
  }

  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  async_dispatcher_t* dispatcher = loop.dispatcher();

  svc_dir_t* dir = nullptr;
  zx_status_t status = svc_dir_create(dispatcher, directory_request, &dir);
  if (status != ZX_OK) {
    std::cerr << "error: svc_dir_create returned: " << status << " ("
              << zx_status_get_string(status) << ")" << std::endl;
    return status;
  }

  ConnectRequestContext context = {.dispatcher = dispatcher,
                                   .server = std::make_unique<EchoImpl>(dispatcher)};
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
