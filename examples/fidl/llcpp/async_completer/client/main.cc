// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/examples/llcpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fdio/directory.h>
#include <lib/fidl/llcpp/client.h>
#include <lib/zx/channel.h>
#include <time.h>
#include <zircon/status.h>

#include <iostream>

constexpr int kNumEchoes = 3;

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

  zx::channel server_end, client_end;
  ZX_ASSERT(zx::channel::create(0, &client_end, &server_end) == ZX_OK);
  ZX_ASSERT(fdio_service_connect_at(svc.get(), "fuchsia.examples.Echo", server_end.release()) ==
            ZX_OK);
  fidl::Client<llcpp::fuchsia::examples::Echo> client(std::move(client_end), dispatcher);

  auto start = time(nullptr);

  // Make |kNumEchoes| EchoString requests to the server, and print the result when
  // it is received.
  for (int i = 0; i < kNumEchoes; i++) {
    client->EchoString("hello", [&](fidl::StringView resp) {
      std::string reply(resp.data(), resp.size());
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
