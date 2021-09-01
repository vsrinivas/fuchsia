// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// [START includes]
#include <fidl/fuchsia.examples/cpp/wire.h>
#include <lib/service/llcpp/service.h>
#include <zircon/assert.h>

#include <iostream>
// [END includes]

// [START main]
int main(int argc, const char** argv) {
  // |service::OpenServiceRoot| returns a channel connected to the /svc directory.
  // The remote end of the channel implements the |fuchsia.io/Directory| protocol
  // and contains the capabilities provided to this component.
  auto svc = service::OpenServiceRoot();
  ZX_ASSERT(svc.is_ok());

  // Connect to the fuchsia.examples.Echo protocol.
  auto client_end = service::ConnectAt<fuchsia_examples::Echo>(*svc);
  ZX_ASSERT(client_end.is_ok());

  // Create a synchronous-only client to the Echo protocol.
  auto client = fidl::BindSyncClient(std::move(*client_end));

  {
    // Make an EchoString request, then print out the response.
    auto result = client.EchoString("hello");
    ZX_ASSERT(result.ok());
    std::string reply_string(result->response.data(), result->response.size());
    std::cout << "Got response: " << reply_string << std::endl;
  }

  {
    // Make a SendString request
    auto result = client.SendString("hi");
    // Check that the request was sent successfully.
    ZX_ASSERT(result.ok());

    class EventHandler : public fidl::WireSyncEventHandler<fuchsia_examples::Echo> {
     public:
      EventHandler() = default;

      void OnString(fidl::WireResponse<fuchsia_examples::Echo::OnString>* event) override {
        std::string reply_string(event->response.data(), event->response.size());
        std::cout << "Got event: " << reply_string << std::endl;
      }

      zx_status_t Unknown() override { return ZX_ERR_NOT_SUPPORTED; }
    };

    // Block to receive exactly one event from the server, which is handled using
    // the event handlers defined above.
    EventHandler event_handler;
    ZX_ASSERT(client.HandleOneEvent(event_handler).ok());
  }

  return 0;
}
// [END main]
