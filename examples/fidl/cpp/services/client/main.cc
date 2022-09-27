// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.examples/cpp/wire.h>
#include <fuchsia/sys/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/sys/component/cpp/service_client.h>
#include <lib/sys/cpp/component_context.h>

#include <iostream>

using fuchsia_examples::Echo;
using fuchsia_examples::EchoService;

zx::status<> llcpp_example(fidl::UnownedClientEnd<fuchsia_io::Directory> svc) {
  zx::status<EchoService::ServiceClient> open_result =
      component::OpenServiceAt<EchoService>(std::move(svc));
  if (open_result.is_error()) {
    std::cerr << "failed to open default instance of EchoService: " << open_result.status_string()
              << std::endl;
    return open_result.take_error();
  }

  EchoService::ServiceClient service = std::move(open_result.value());

  zx::status<fidl::ClientEnd<Echo>> connect_result = service.connect_regular_echo();
  if (connect_result.is_error()) {
    std::cerr << "failed to connect to member protocol regular_echo of EchoService: "
              << connect_result.status_string() << std::endl;
    return connect_result.take_error();
  }

  fidl::WireSyncClient client{std::move(connect_result.value())};
  fidl::WireResult<Echo::EchoString> echo_result = client->EchoString(fidl::StringView("hello"));
  if (!echo_result.ok()) {
    std::cerr << "failed to make EchoString call to member protocol regular_echo of EchoService: "
              << echo_result.error() << std::endl;
    return zx::error(ZX_ERR_IO);
  }

  auto response = echo_result.Unwrap();

  std::string result_string(response->response.data(), response->response.size());
  if (result_string != "hello") {
    std::cerr << "got unexpected response '" << result_string << "'. expected 'hello'."
              << std::endl;
    return zx::error(ZX_ERR_INTERNAL);
  }
  std::cout << "Received response: " << result_string << std::endl;
  return zx::ok();
}

int main(int argc, const char** argv) {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  auto context = sys::ComponentContext::CreateAndServeOutgoingDirectory();

  // |component::OpenServiceRoot| returns a channel connected to the /svc directory.
  // The remote end of the channel implements the |fuchsia.io/Directory| protocol
  // and contains the capabilities provided to this component.
  auto svc = component::OpenServiceRoot();
  ZX_ASSERT(svc.is_ok());

  // Convert the typed handle to LLCPP types.
  auto llsvc = fidl::ClientEnd<fuchsia_io::Directory>(svc->TakeChannel());

  zx::status<> result = llcpp_example(llsvc);
  if (result.is_error()) {
    std::cerr << "llcpp_example failed with status: " << result.status_string() << std::endl;
    return 1;
  }
  return 0;
}
