// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// [START includes]
#include <fuchsia/examples/llcpp/fidl.h>
#include <lib/fdio/directory.h>
#include <lib/zx/channel.h>
#include <zircon/status.h>

#include <iostream>
// [END includes]

// [START connect]
// Returns a channel connected to the /svc directory. The
// remote end of the channel implements the io.Directory protocol and contains
// the capabilities provided to this component.
zx::channel get_svc_directory() {
  zx::channel server_end, client_end;
  ZX_ASSERT(zx::channel::create(0, &client_end, &server_end) == ZX_OK);
  ZX_ASSERT(fdio_service_connect("/svc/.", server_end.release()) == ZX_OK);
  return client_end;
}
// [END connect]

// [START main]
int main(int argc, const char** argv) {
  auto svc = get_svc_directory();
  zx::channel server_end, client_end;
  ZX_ASSERT(zx::channel::create(0, &client_end, &server_end) == ZX_OK);
  // Connect to the fuchsia.examples.Echo protocol.
  ZX_ASSERT(fdio_service_connect_at(svc.get(), "fuchsia.examples.Echo", server_end.release()) ==
            ZX_OK);
  // Create a synchronous-only client to the Echo protocol.
  ::llcpp::fuchsia::examples::Echo::SyncClient client(std::move(client_end));

  // Make an EchoString request, then print out the response.
  {
    auto result = client.EchoString("hello");
    ZX_ASSERT(result.ok());
    std::string reply_string(result->response.data(), result->response.size());
    std::cout << "Got response: " << reply_string << std::endl;
  }

  {
    // Make a SendString request
    auto result = client.SendString("hi");
    // Check that the request was sent succesfully
    ZX_ASSERT(result.ok());

    llcpp::fuchsia::examples::Echo::EventHandlers handlers{
        .on_string =
            [](llcpp::fuchsia::examples::Echo::OnStringResponse* message) {
              std::string reply_string(message->response.data(), message->response.size());
              std::cout << "Got event: " << reply_string << std::endl;
              return ZX_OK;
            },
        .unknown = []() { return ZX_ERR_INVALID_ARGS; }};
    // Block to receive exactly one event from the server, which is handled using
    // the event handlers defined above.
    ZX_ASSERT(client.HandleEvents(handlers).ok());
  }

  return 0;
}
// [END main]
