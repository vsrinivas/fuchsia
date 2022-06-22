// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// [START includes]
#include <fidl/fuchsia.examples/cpp/fidl.h>
#include <lib/service/llcpp/service.h>
#include <zircon/assert.h>

#include <iostream>
// [END includes]

// [START main]
int main(int argc, const char** argv) {
  // |service::OpenServiceRoot| returns a channel connected to the /svc directory.
  // The remote end of the channel implements the |fuchsia.io/Directory| protocol
  // and contains the capabilities provided to this component.
  zx::status svc = service::OpenServiceRoot();
  ZX_ASSERT(svc.is_ok());

  // Connect to the fuchsia.examples/Echo protocol.
  zx::status client_end = service::ConnectAt<fuchsia_examples::Echo>(*svc);
  ZX_ASSERT(client_end.is_ok());

  // Create a synchronous client to the Echo protocol.
  fidl::SyncClient client{std::move(*client_end)};

  {
    // Make an EchoString call, then print out the response.
    fidl::Result result = client->EchoString({"hello"});
    ZX_ASSERT(result.is_ok());
    const std::string& reply_string = result->response();
    std::cout << "Got response: " << reply_string << std::endl;
  }

  {
    // Make the same call using wire types.
    fidl::WireResult result = client.wire()->EchoString("hello");
    ZX_ASSERT(result.ok());
    std::string reply_string{result.value().response.get()};
    std::cout << "Got response: " << reply_string << std::endl;
  }

  {
    // Make a SendString one-way call, then wait for the server to respond
    // with an event.
    fitx::result<fidl::Error> result = client->SendString({"hi"});
    // Check that the request was sent successfully.
    ZX_ASSERT(result.is_ok());

    class EventHandler : public fidl::SyncEventHandler<fuchsia_examples::Echo> {
     public:
      EventHandler() = default;

      void OnString(fidl::Event<fuchsia_examples::Echo::OnString>& event) override {
        const std::string& reply_string = event.response();
        std::cout << "Got event: " << reply_string << std::endl;
      }
    };

    // Block to receive exactly one event from the server, which is handled using
    // the event handlers defined above.
    EventHandler event_handler;
    ZX_ASSERT(client.HandleOneEvent(event_handler).ok());
  }

  return 0;
}
// [END main]
