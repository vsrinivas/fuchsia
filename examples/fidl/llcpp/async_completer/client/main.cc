// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.examples/cpp/wire.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fdio/directory.h>
#include <lib/fidl/llcpp/client.h>
#include <lib/service/llcpp/service.h>
#include <time.h>
#include <zircon/status.h>

#include <iostream>

constexpr int kNumEchoes = 3;

// [START main]
int main(int argc, const char** argv) {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  async_dispatcher_t* dispatcher = loop.dispatcher();
  int num_responses = 0;

  auto client_end = service::Connect<fuchsia_examples::Echo>();
  ZX_ASSERT(client_end.is_ok());
  fidl::WireClient client(std::move(*client_end), dispatcher);

  auto start = time(nullptr);

  // Make |kNumEchoes| EchoString requests to the server, and print the result when
  // it is received.
  for (int i = 0; i < kNumEchoes; i++) {
    client->EchoString(
        "hello", [&](fidl::WireResponse<fuchsia_examples::Echo::EchoString>* response) {
          std::string reply(response->response.data(), response->response.size());
          std::cout << "Got response after " << time(nullptr) - start << " seconds" << std::endl;
          if (++num_responses == kNumEchoes) {
            loop.Quit();
          }
        });
  }

  loop.Run();
  return num_responses == kNumEchoes ? 0 : 1;
}
// [END main]
