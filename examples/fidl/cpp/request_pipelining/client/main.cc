// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// ============================================================================
// This is an accompanying example code for the C++ protocol request pipelining
// tutorial. Head over there for the full walk-through:
// https://fuchsia.dev/fuchsia-src/development/languages/fidl/tutorials/cpp/topics/request-pipelining
// ============================================================================

#include <fidl/fuchsia.examples/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/sys/component/cpp/service_client.h>
#include <lib/syslog/cpp/macros.h>

#include <memory>

// [START main]
int main(int argc, const char** argv) {
  async::Loop loop(&kAsyncLoopConfigNeverAttachToThread);
  async_dispatcher_t* dispatcher = loop.dispatcher();
  int num_responses = 0;

  // Connect to the EchoLauncher protocol
  zx::status launcher_client_end = component::Connect<fuchsia_examples::EchoLauncher>();
  ZX_ASSERT(launcher_client_end.is_ok());
  fidl::Client launcher(std::move(*launcher_client_end), dispatcher);

  // Make a non-pipelined request to get an instance of Echo
  launcher->GetEcho({"non pipelined: "})
      .ThenExactlyOnce([&](fidl::Result<fuchsia_examples::EchoLauncher::GetEcho>& result) {
        ZX_ASSERT(result.is_ok());
        // Take the Echo client end in the response, bind it to another client, and
        // make an EchoString request on it.
        fidl::SharedClient echo(std::move(result->response()), dispatcher);
        echo->EchoString({"hello!"})
            .ThenExactlyOnce(
                // Clone |echo| into the callback so that the client
                // is only destroyed after we receive the response.
                [&, echo = echo.Clone()](fidl::Result<fuchsia_examples::Echo::EchoString>& result) {
                  ZX_ASSERT(result.is_ok());
                  FX_LOGS(INFO) << "Got echo response " << result->response();
                  if (++num_responses == 2) {
                    loop.Quit();
                  }
                });
      });

  zx::status endpoints = fidl::CreateEndpoints<fuchsia_examples::Echo>();
  ZX_ASSERT(endpoints.status_value() == ZX_OK);
  auto [client_end, server_end] = *std::move(endpoints);
  // Make a pipelined request to get an instance of Echo
  ZX_ASSERT(launcher->GetEchoPipelined({"pipelined: ", std::move(server_end)}).is_ok());
  // A client can be initialized using the client end without waiting for a response
  fidl::Client echo_pipelined(std::move(client_end), dispatcher);
  echo_pipelined->EchoString({"hello!"})
      .ThenExactlyOnce([&](fidl::Result<fuchsia_examples::Echo::EchoString>& result) {
        ZX_ASSERT(result.is_ok());
        FX_LOGS(INFO) << "Got echo response " << result->response();
        if (++num_responses == 2) {
          loop.Quit();
        }
      });

  loop.Run();
  return num_responses == 2 ? 0 : 1;
}
// [END main]
