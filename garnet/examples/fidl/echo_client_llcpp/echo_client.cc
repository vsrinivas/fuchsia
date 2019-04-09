// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/examples/echo/llcpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async/default.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/zx/channel.h>
#include <lib/zx/process.h>
#include <zircon/processargs.h>
#include <zircon/status.h>

#include <iostream>
#include <string>
#include <vector>

int main(int argc, const char** argv) {
  std::string server_url =
      "fuchsia-pkg://fuchsia.com/echo_server_llcpp#meta/echo_server_llcpp.cmx";
  std::string msg = "hello world";

  for (int i = 1; i < argc - 1; ++i) {
    if (!strcmp("--server", argv[i])) {
      server_url = argv[++i];
    } else if (!strcmp("-m", argv[i])) {
      msg = argv[++i];
    }
  }

  // Using high-level C++ bindings to launch the server component
  async::Loop loop(&kAsyncLoopConfigAttachToThread);
  fidl::InterfaceHandle<fuchsia::io::Directory> directory;
  fuchsia::sys::LaunchInfo launch_info;
  launch_info.url = server_url;
  launch_info.directory_request = directory.NewRequest().TakeChannel();
  fuchsia::sys::LauncherPtr launcher;
  auto context = sys::ComponentContext::Create();
  context->svc()->Connect(launcher.NewRequest());
  fuchsia::sys::ComponentControllerPtr controller;
  launcher->CreateComponent(std::move(launch_info), controller.NewRequest());

  sys::ServiceDirectory echo_provider(std::move(directory));
  zx::channel server_end, client_end;
  assert(zx::channel::create(0, &client_end, &server_end) == ZX_OK);
  echo_provider.Connect("fidl.examples.echo.Echo", std::move(server_end));

  // Using low-level C++ bindings to perform a call
  fidl::examples::echo::Echo::SyncClient client(std::move(client_end));
  std::vector<uint8_t> request_buffer(512);
  std::vector<uint8_t> response_buffer(512);
  fidl::StringView out_str = {};
  zx_status_t status = client.EchoString(
      fidl::BytePart(&request_buffer[0], request_buffer.size()),
      fidl::StringView(msg.size(), &msg[0]),
      fidl::BytePart(&response_buffer[0], response_buffer.size()), &out_str);
  if (status != ZX_OK) {
    std::cerr << "Failed to call server: " << status << " ("
              << zx_status_get_string(status) << ")" << std::endl;
    return status;
  }

  std::string reply_string(out_str.data(), out_str.size());
  std::cout << "Reply: " << reply_string << std::endl;

  return 0;
}
