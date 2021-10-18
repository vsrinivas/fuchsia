// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fidl.examples.echo/cpp/wire.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/fidl-async/cpp/bind.h>
#include <lib/svc/dir.h>
#include <lib/zx/time.h>
#include <zircon/process.h>
#include <zircon/processargs.h>
#include <zircon/status.h>

#include <iostream>
#include <memory>
#include <string>
#include <utility>

struct ConnectRequestContext {
  bool quiet;
  async_dispatcher_t* dispatcher;
  std::unique_ptr<fidl::WireServer<fidl_examples_echo::Echo>> server;
};

class Server final : public fidl::WireServer<fidl_examples_echo::Echo> {
 public:
  void EchoString(EchoStringRequestView request, EchoStringCompleter::Sync& completer) override {
    completer.Reply(request->value);
  }
};

static void connect(void* untyped_context, const char* service_name, zx_handle_t service_request) {
  auto context = static_cast<ConnectRequestContext*>(untyped_context);
  if (!context->quiet) {
    std::cout << "echo_server_llcpp: Incoming connection for " << service_name << std::endl;
  }
  fidl::BindSingleInFlightOnly(
      context->dispatcher, fidl::ServerEnd<fidl_examples_echo::Echo>(zx::channel(service_request)),
      context->server.get());
}

int main(int argc, char** argv) {
  bool quiet = (argc >= 2) && std::string("-q") == argv[1];

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

  ConnectRequestContext context = {
      .quiet = quiet, .dispatcher = dispatcher, .server = std::make_unique<Server>()};
  status = svc_dir_add_service(dir, "public", "fidl.examples.echo.Echo", &context, connect);
  if (status != ZX_OK) {
    std::cerr << "error: svc_dir_add_service returned: " << status << " ("
              << zx_status_get_string(status) << ")" << std::endl;
    return status;
  }

  loop.Run();
  return 0;
}
