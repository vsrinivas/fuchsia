// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// [START includes]
#include <fidl/fuchsia.examples/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fidl/cpp/client.h>
#include <lib/service/llcpp/service.h>
#include <zircon/assert.h>

#include <iostream>
// [END includes]

int main(int argc, const char** argv) {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  async_dispatcher_t* dispatcher = loop.dispatcher();

  // |service::OpenServiceRoot| returns a channel connected to the /svc directory.
  // The remote end of the channel implements the |fuchsia.io/Directory| protocol
  // and contains the capabilities provided to this component.
  auto svc = service::OpenServiceRoot();
  ZX_ASSERT(svc.is_ok());

  // Connect to the |fuchsia.examples/Echo| protocol, here we demonstrate
  // using |service::ConnectAt| relative to some service directory.
  // One may also directly call |Connect| to use the default service directory.
  auto client_end = service::ConnectAt<fuchsia_examples::Echo>(*svc);
  ZX_ASSERT(client_end.status_value() == ZX_OK);

  // Define the event handler for the client. The OnString event handler prints the event.
  class EventHandler : public fidl::AsyncEventHandler<fuchsia_examples::Echo> {
   public:
    explicit EventHandler(async::Loop& loop) : loop_(loop) {}

    void OnString(fidl::Event<::fuchsia_examples::Echo::OnString>& event) override {
      std::cout << "(Natural types) got event: " << event->response() << std::endl;
      loop_.Quit();
    }

   private:
    async::Loop& loop_;
  };
  EventHandler event_handler{loop};

  // Create a client to the Echo protocol.
  fidl::Client client(std::move(*client_end), dispatcher, &event_handler);

  // [START two_way_natural_result]
  // Make an EchoString call with natural types and result callback.
  client->EchoString(
      {"hello"},
      [&](fitx::result<fidl::Error, fidl::Response<fuchsia_examples::Echo::EchoString>>& result) {
        ZX_ASSERT(result.is_ok());
        std::cout << "(Natural types) got response: " << result->response() << std::endl;
        loop.Quit();
      });
  // [END two_way_natural_result]
  loop.Run();
  loop.ResetQuit();

  // [START two_way_designated_natural_result]
  // Make an EchoString call with natural types, using named arguments in the request object.
  client->EchoString(
      {{.value = "hello"}},
      [&](fitx::result<fidl::Error, fidl::Response<fuchsia_examples::Echo::EchoString>>& result) {
        ZX_ASSERT(result.is_ok());
        std::cout << "(Natural types) got response: " << result->response() << std::endl;
        loop.Quit();
      });
  // [END two_way_designated_natural_result]
  loop.Run();
  loop.ResetQuit();

  // [START two_way_payload_natural_result]
  // Make an EchoString call with natural types, passing the entire request as one object.
  fuchsia_examples::EchoEchoStringRequest request{"hello"};
  client->EchoString(
      std::move(request),
      [&](fitx::result<fidl::Error, fidl::Response<fuchsia_examples::Echo::EchoString>>& result) {
        ZX_ASSERT(result.is_ok());
        std::cout << "(Natural types) got response: " << result->response() << std::endl;
        loop.Quit();
      });
  // [END two_way_payload_natural_result]
  loop.Run();
  loop.ResetQuit();

  // [START two_way_natural_response]
  // Make an EchoString call with natural types and response callback.
  client->EchoString({"hello"}, [&](fidl::Response<fuchsia_examples::Echo::EchoString>& reply) {
    // Response arguments are accessed through an arrow indirection.
    std::cout << "(Natural types) got response: " << reply->response() << std::endl;
    // Alternatively, you may access the response payload object (a struct in
    // this case) directly. They are equivalent.
    fuchsia_examples::EchoEchoStringTopResponse& response = *reply;
    ZX_ASSERT(response.response() == reply->response());

    loop.Quit();
  });
  // [END two_way_natural_response]
  loop.Run();
  loop.ResetQuit();

  // [START two_way_wire_result]
  // Make an EchoString call with wire types and result callback.
  client.wire()->EchoString(
      "hello", [&](fidl::WireUnownedResult<fuchsia_examples::Echo::EchoString>& result) {
        ZX_ASSERT(result.ok());
        fidl::WireResponse<fuchsia_examples::Echo::EchoString>& response = result.value();
        std::string reply(response.response.data(), response.response.size());
        std::cout << "(Wire types) got response: " << reply << std::endl;
        loop.Quit();
      });
  // [END two_way_wire_result]
  loop.Run();
  loop.ResetQuit();

  // [START two_way_wire_response]
  // Make an EchoString call with wire types and response callback.
  client.wire()->EchoString(
      "hello", [&](fidl::WireResponse<fuchsia_examples::Echo::EchoString>* response) {
        std::string reply(response->response.data(), response->response.size());
        std::cout << "(Wire types) got response: " << reply << std::endl;
        loop.Quit();
      });
  // [END two_way_wire_response]
  loop.Run();
  loop.ResetQuit();

  // [START one_way_natural]
  // Make an EchoString call with natural types.
  fitx::result<::fidl::Error> result = client->SendString({"hello"});
  ZX_ASSERT(result.is_ok());
  // [END one_way_natural]
  loop.Run();
  loop.ResetQuit();

  // [START one_way_wire]
  // Make an EchoString call with wire types.
  fidl::Result wire_result = client.wire()->SendString("hello");
  ZX_ASSERT(wire_result.ok());
  // [END one_way_wire]
  loop.Run();
  loop.ResetQuit();

  return 0;
}
