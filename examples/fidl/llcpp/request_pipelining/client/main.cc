// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/examples/llcpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fdio/directory.h>
#include <lib/fidl/llcpp/client.h>
#include <lib/zx/channel.h>
#include <zircon/status.h>

#include <iostream>

zx::channel get_svc_directory() {
  zx::channel server_end, client_end;
  ZX_ASSERT(zx::channel::create(0, &client_end, &server_end) == ZX_OK);
  ZX_ASSERT(fdio_service_connect("/svc/.", server_end.release()) == ZX_OK);
  return client_end;
}

// [START main]
int main(int argc, const char** argv) {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  async_dispatcher_t* dispatcher = loop.dispatcher();
  int num_responses = 0;

  auto svc = get_svc_directory();

  // Connect to the EchoLauncher protocol
  zx::channel server_end, client_end;
  ZX_ASSERT(zx::channel::create(0, &client_end, &server_end) == ZX_OK);
  ZX_ASSERT(fdio_service_connect_at(svc.get(), "fuchsia.examples.EchoLauncher",
                                    server_end.release()) == ZX_OK);
  fidl::Client<llcpp::fuchsia::examples::EchoLauncher> launcher(std::move(client_end), dispatcher);

  fidl::Client<llcpp::fuchsia::examples::Echo> echo;
  // Make a non-pipelined request to get an instance of Echo
  auto result = launcher->GetEcho("non pipelined: ", [&](zx::channel client_end) {
    // Take the channel to Echo in the response, bind it to the dispatcher, and
    // make an EchoString request on it.
    echo.Bind(std::move(client_end), dispatcher);
    echo->EchoString("hello!", [&](fidl::StringView resp) {
      std::string reply(resp.data(), resp.size());
      std::cout << "Got echo response " << reply << std::endl;
      if (++num_responses == 2) {
        loop.Quit();
      }
    });
  });
  ZX_ASSERT(result.ok());

  zx::channel se, ce;
  ZX_ASSERT(zx::channel::create(0, &ce, &se) == ZX_OK);
  // Make a pipelined request to get an instance of Echo
  ZX_ASSERT(launcher->GetEchoPipelined("pipelined: ", std::move(se)).ok());
  // A client can be initialized using the client end without waiting for a response
  fidl::Client<llcpp::fuchsia::examples::Echo> echo_pipelined(std::move(ce), dispatcher);
  echo_pipelined->EchoString("hello!", [&](fidl::StringView resp) {
    std::string reply(resp.data(), resp.size());
    std::cout << "Got echo response " << reply << std::endl;
    if (++num_responses == 2) {
      loop.Quit();
    }
  });

  loop.Run();
  return num_responses == 2 ? 0 : 1;
}
// [END main]
