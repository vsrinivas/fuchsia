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

// Returns a channel connected to the /svc directory. The
// remote end of the channel implements the io.Directory protocol and contains
// the capabilities provided to this component.
zx::channel get_svc_directory() {
  zx::channel server_end, client_end;
  ZX_ASSERT(zx::channel::create(0, &client_end, &server_end) == ZX_OK);
  ZX_ASSERT(fdio_service_connect("/svc/.", server_end.release()) == ZX_OK);
  return client_end;
}

int main(int argc, const char** argv) {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  async_dispatcher_t* dispatcher = loop.dispatcher();
  int num_responses = 0;

  auto svc = get_svc_directory();

  zx::channel server_end, client_end;
  ZX_ASSERT(zx::channel::create(0, &client_end, &server_end) == ZX_OK);
  // Connect to the fuchsia.examples.Echo protocol.
  ZX_ASSERT(fdio_service_connect_at(svc.get(), "fuchsia.examples.Echo", server_end.release()) ==
            ZX_OK);
  // Define the event handlers for the client. The OnString event handler
  // prints the event, then quits if this is the second received message.
  llcpp::fuchsia::examples::Echo::AsyncEventHandlers handlers = {
      .on_string = [&](fidl::StringView resp) {
        std::string event(resp.data(), resp.size());
        std::cout << "Got event: " << event << std::endl;
        if (++num_responses == 2) {
          loop.Quit();
        }
      }};
  // Create a client to the Echo protocol
  fidl::Client<llcpp::fuchsia::examples::Echo> client(std::move(client_end), dispatcher,
                                                      std::move(handlers));

  // Make an EchoString call, passing it a lambda to handle the response asynchronously.
  client->EchoString("hello", [&](fidl::StringView resp) {
    std::string reply(resp.data(), resp.size());
    std::cout << "Got response: " << reply << std::endl;
    if (++num_responses == 2) {
      loop.Quit();
    }
  });

  // Make a synchronous EchoString call, which blocks until it receives the response,
  // then returns a ResultOf object for the response.
  auto result = client->EchoString_Sync("hello");
  ZX_ASSERT(result.ok());
  std::string reply_string(result->response.data(), result->response.size());
  std::cout << "Got synchronous response: " << reply_string << std::endl;

  // Make a SendString request. The resulting OnString event will be handled by
  // the event handler defined above.
  client->SendString("hi");

  loop.Run();
  // Exit succesfully if two messages have been received asynchronously (one EchoString
  // response and one OnString event)
  return num_responses == 2 ? 0 : 1;
}
