// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.examples/cpp/wire.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fdio/directory.h>
#include <lib/fidl/llcpp/client.h>
#include <lib/service/llcpp/service.h>
#include <lib/zx/channel.h>
#include <zircon/status.h>

#include <iostream>
#include <memory>

// [START main]
int main(int argc, const char** argv) {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  async_dispatcher_t* dispatcher = loop.dispatcher();
  int num_responses = 0;

  // Connect to the EchoLauncher protocol
  auto launcher_client_end = service::Connect<fuchsia_examples::EchoLauncher>();
  ZX_ASSERT(launcher_client_end.status_value() == ZX_OK);
  fidl::WireClient launcher(std::move(*launcher_client_end), dispatcher);

  // Make a non-pipelined request to get an instance of Echo
  launcher->GetEcho("non pipelined: ",
                    [&](fidl::WireUnownedResult<fuchsia_examples::EchoLauncher::GetEcho>&& result) {
                      ZX_ASSERT(result.ok());
                      // Take the channel to Echo in the response, bind it to the dispatcher, and
                      // make an EchoString request on it.
                      fidl::WireSharedClient echo(std::move(result->response), dispatcher);
                      echo->EchoString(
                          "hello!",
                          // Clone |echo| into the callback so that the client
                          // is only destroyed after we receive the response.
                          [&, echo = echo.Clone()](
                              fidl::WireResponse<fuchsia_examples::Echo::EchoString>* response) {
                            std::string reply(response->response.data(), response->response.size());
                            std::cout << "Got echo response " << reply << std::endl;
                            if (++num_responses == 2) {
                              loop.Quit();
                            }
                          });
                    });

  auto endpoints = fidl::CreateEndpoints<fuchsia_examples::Echo>();
  ZX_ASSERT(endpoints.status_value() == ZX_OK);
  auto [client_end, server_end] = *std::move(endpoints);
  // Make a pipelined request to get an instance of Echo
  ZX_ASSERT(launcher->GetEchoPipelined("pipelined: ", std::move(server_end)).ok());
  // A client can be initialized using the client end without waiting for a response
  fidl::WireClient echo_pipelined(std::move(client_end), dispatcher);
  echo_pipelined->EchoString(
      "hello!", [&](fidl::WireUnownedResult<fuchsia_examples::Echo::EchoString>&& result) {
        ZX_ASSERT_MSG(result.ok(), "EchoString failed: %s",
                      result.error().FormatDescription().c_str());
        std::string reply(result->response.data(), result->response.size());
        std::cout << "Got echo response " << reply << std::endl;
        if (++num_responses == 2) {
          loop.Quit();
        }
      });

  loop.Run();
  return num_responses == 2 ? 0 : 1;
}
// [END main]
