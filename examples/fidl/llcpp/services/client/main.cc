// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/examples/llcpp/fidl.h>
#include <fuchsia/sys/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/service/llcpp/service.h>
#include <lib/sys/cpp/component_context.h>

#include <iostream>

using ::llcpp::fuchsia::examples::Echo;
using ::llcpp::fuchsia::examples::EchoService;

fidl::InterfaceHandle<fuchsia::io::Directory> StartEchoServer(
    sys::ComponentContext* context,
    fidl::InterfaceRequest<fuchsia::sys::ComponentController> controller) {
  fidl::InterfaceHandle<fuchsia::io::Directory> svc;
  fuchsia::sys::LaunchInfo info{
      .url = "fuchsia-pkg://fuchsia.com/echo-hlcpp-service-server#meta/echo-server.cmx",
      .directory_request = svc.NewRequest().TakeChannel(),
  };

  auto launcher = context->svc()->Connect<fuchsia::sys::Launcher>();
  launcher->CreateComponent(std::move(info), std::move(controller));
  return svc;
}

zx_status_t llcpp_example(zx::unowned_channel svc) {
  fidl::result<EchoService::ServiceClient> open_result =
      llcpp::sys::OpenServiceAt<EchoService>(std::move(svc));
  if (open_result.is_error()) {
    std::cerr << "failed to open default instance of EchoService: " << open_result.error()
              << std::endl;
    return open_result.error();
  }

  EchoService::ServiceClient service = open_result.take_value();

  fidl::result<fidl::ClientChannel<Echo>> connect_result = service.connect_regular_echo();
  if (connect_result.is_error()) {
    std::cerr << "failed to connect to member protocol regular_echo of EchoService: "
              << connect_result.error() << std::endl;
    return connect_result.error();
  }

  Echo::SyncClient client = fidl::BindSyncClient(connect_result.take_value());
  Echo::ResultOf::EchoString echo_result = client.EchoString(fidl::StringView("hello"));
  if (!echo_result.ok()) {
    std::cerr << "failed to make EchoString call to member protocol regular_echo of EchoService: "
              << echo_result.error() << std::endl;
    return ZX_ERR_IO;
  }

  auto response = echo_result.Unwrap();

  std::string result_string(response->response.data(), response->response.size());
  if (result_string != "hello") {
    std::cerr << "got unexpected response '" << result_string << "'. expected 'hello'."
              << std::endl;
    return ZX_ERR_INTERNAL;
  }
  return ZX_OK;
}

int main(int argc, const char** argv) {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  auto context = sys::ComponentContext::CreateAndServeOutgoingDirectory();

  // Start the echo service.
  //
  // In a real system, the service would be offered to the client instead of
  // being started by the client.
  fuchsia::sys::ComponentControllerPtr controller;
  auto svc = StartEchoServer(context.get(), controller.NewRequest());

  zx_status_t result = llcpp_example(zx::unowned_channel(svc.channel()));
  if (result != ZX_OK) {
    std::cerr << "llcpp_example failed with status: " << result << std::endl;
    return 1;
  }
  return 0;
}
