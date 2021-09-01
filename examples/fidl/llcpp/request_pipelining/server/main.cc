// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.examples/cpp/wire.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fdio/directory.h>
#include <lib/fidl/llcpp/server.h>
#include <lib/svc/dir.h>
#include <zircon/process.h>
#include <zircon/processargs.h>
#include <zircon/status.h>

#include <iostream>
#include <vector>

// [START echo-impl]
// Implementation of the Echo protocol that prepends a prefix to every response.
class EchoImpl final : public fidl::WireServer<fuchsia_examples::Echo> {
 public:
  explicit EchoImpl(std::string prefix) : prefix_(prefix) {}
  // This method is not used in the request pipelining example, so requests are ignored.
  void SendString(SendStringRequestView request, SendStringCompleter::Sync& completer) override {}
  void EchoString(EchoStringRequestView request, EchoStringCompleter::Sync& completer) override {
    std::cout << "Got echo request for prefix " << prefix_ << std::endl;
    auto value_str = std::string(request->value.data(), request->value.size());
    auto response = prefix_ + value_str;
    completer.Reply(fidl::StringView::FromExternal(response));
  }

  const std::string prefix_;
};
// [END echo-impl]

// [START launcher-impl]
// Implementation of EchoLauncher. Each method creates an instance of EchoImpl
// with the specified prefix.
class EchoLauncherImpl final : public fidl::WireServer<fuchsia_examples::EchoLauncher> {
 public:
  explicit EchoLauncherImpl(async_dispatcher_t* dispatcher) : dispatcher_(dispatcher) {}

  void GetEcho(GetEchoRequestView request, GetEchoCompleter::Sync& completer) override {
    std::cout << "Got non pipelined request" << std::endl;
    auto endpoints = fidl::CreateEndpoints<fuchsia_examples::Echo>();
    ZX_ASSERT(endpoints.is_ok());
    auto [client_end, server_end] = *std::move(endpoints);
    RunEchoServer(request->echo_prefix, std::move(server_end));
    completer.Reply(std::move(client_end));
  }

  void GetEchoPipelined(GetEchoPipelinedRequestView request,
                        GetEchoPipelinedCompleter::Sync& completer) override {
    std::cout << "Got pipelined request" << std::endl;
    RunEchoServer(request->echo_prefix, std::move(request->request));
  }

  void RunEchoServer(fidl::StringView prefix, fidl::ServerEnd<fuchsia_examples::Echo> server_end) {
    // The binding stays alive as long as the EchoImpl class that is bound is kept in
    // scope, so store them in the class.
    server_instances_.push_back(
        std::make_unique<EchoImpl>(std::string(prefix.data(), prefix.size())));
    fidl::BindServer(dispatcher_, std::move(server_end), server_instances_.back().get());
  }

  // Keep track of all running EchoImpl instances so that they share the same lifetime
  // as this class.
  std::vector<std::unique_ptr<EchoImpl>> server_instances_;
  async_dispatcher_t* dispatcher_;
};
// [END launcher-impl]

struct ConnectRequestContext {
  async_dispatcher_t* dispatcher;
  std::unique_ptr<EchoLauncherImpl> server;
};

static void connect(void* untyped_context, const char* service_name, zx_handle_t service_request) {
  auto context = static_cast<ConnectRequestContext*>(untyped_context);
  std::cout << "echo_server_llcpp: Incoming connection for " << service_name << std::endl;
  fidl::BindServer(context->dispatcher,
                   fidl::ServerEnd<fuchsia_examples::EchoLauncher>(zx::channel(service_request)),
                   context->server.get());
}

// [START main]
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
                                   .server = std::make_unique<EchoLauncherImpl>(dispatcher)};
  status = svc_dir_add_service(dir, "svc", "fuchsia.examples.EchoLauncher", &context, connect);
  if (status != ZX_OK) {
    std::cerr << "error: svc_dir_add_service returned: " << status << " ("
              << zx_status_get_string(status) << ")" << std::endl;
    return status;
  }

  std::cout << "Running echo launcher server" << std::endl;
  loop.Run();
  return 0;
}
// [END main]
