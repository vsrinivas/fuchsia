// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// ============================================================================
// This is an accompanying example code for the C++ client tutorial. Head over
// there for the full walk-through:
// https://fuchsia.dev/fuchsia-src/development/languages/fidl/tutorials/cpp/basics/client
// ============================================================================

// [START includes]
#include <fidl/fuchsia.examples/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/sys/component/cpp/service_client.h>
#include <lib/syslog/cpp/macros.h>
// [END includes]

int main(int argc, const char** argv) {
  // [START connect]
  // Connect to the |fuchsia.examples/Echo| protocol inside the component's
  // namespace. This can fail so it's wrapped in a |zx::status| and it must be
  // checked for errors.
  zx::status client_end = component::Connect<fuchsia_examples::Echo>();
  if (!client_end.is_ok()) {
    FX_LOGS(ERROR) << "Synchronous error when connecting to the |Echo| protocol: "
                   << client_end.status_string();
    return -1;
  }
  // [END connect]

  // [START async-loop]
  // As in the server, the code sets up an async loop so that the client can
  // listen for incoming responses from the server without blocking.
  async::Loop loop(&kAsyncLoopConfigNeverAttachToThread);
  async_dispatcher_t* dispatcher = loop.dispatcher();
  // [END async-loop]

  // [START event-handler]
  // Define the event handler implementation for the client.
  //
  // The event handler delegate should be an object that implements the
  // |fidl::AsyncEventHandler<Echo>| virtual class, which has methods
  // corresponding to the events offered by the protocol. By default those
  // methods are no-ops.
  // [START event-handler-short]
  class EventHandler : public fidl::AsyncEventHandler<fuchsia_examples::Echo> {
   public:
    void OnString(fidl::Event<::fuchsia_examples::Echo::OnString>& event) override {
      FX_LOGS(INFO) << "(Natural types) got event: " << event.response();
      loop_.Quit();
    }
    // [END event-handler-short]

    // One may also override the |on_fidl_error| method, which is called
    // when the client encounters an error and is going to teardown.
    void on_fidl_error(fidl::UnbindInfo error) override { FX_LOGS(ERROR) << error; }

    explicit EventHandler(async::Loop& loop) : loop_(loop) {}

   private:
    async::Loop& loop_;
  };

  // Create an instance of the event handler.
  EventHandler event_handler{loop};
  // [END event-handler]

  // [START init-client]
  // Create a client to the Echo protocol, passing our |event_handler| created
  // earlier. The |event_handler| must live for at least as long as the
  // |client|. The |client| holds a reference to the |event_handler| so it is a
  // bug for the |event_handler| to be destroyed before the |client|.
  fidl::Client client(std::move(*client_end), dispatcher, &event_handler);
  // [END init-client]

  // Make an EchoString call with natural types, building the request object inline.
  // [START two_way_natural_result]
  client->EchoString({"hello"}).ThenExactlyOnce(
      [&](fidl::Result<fuchsia_examples::Echo::EchoString>& result) {
        // Check if the FIDL call succeeded or not.
        if (!result.is_ok()) {
          // If the call failed, log the error, and crash the program.
          // Production code should do more graceful error handling depending
          // on the situation.
          FX_LOGS(ERROR) << "EchoString failed: " << result.error_value();
          ZX_PANIC("%s", result.error_value().FormatDescription().c_str());
        }
        // Dereference (->) the result object to access the response payload.
        FX_LOGS(INFO) << "(Natural types) got response: " << result->response();
        loop.Quit();
      });
  // Run the dispatch loop, until we receive a reply, in which case the callback
  // above will quit the loop, breaking us out of the |Run| function.
  loop.Run();
  loop.ResetQuit();
  // [END two_way_natural_result]

  // Make an EchoString call with natural types, using named arguments in the request object.
  // [START two_way_designated_natural_result]
  client->EchoString({{.value = "hello"}})
      .ThenExactlyOnce(
          // [END two_way_designated_natural_result]
          [&](fidl::Result<fuchsia_examples::Echo::EchoString>& result) {
            if (!result.is_ok()) {
              FX_LOGS(ERROR) << "EchoString failed: " << result.error_value();
              ZX_PANIC("%s", result.error_value().FormatDescription().c_str());
            }
            FX_LOGS(INFO) << "(Natural types) got response: " << result->response();
            loop.Quit();
          });
  loop.Run();
  loop.ResetQuit();

  // [START one_way_natural]
  // [START one_way_natural_first_line]
  // Make a SendString one way call with natural types.
  fit::result<::fidl::Error> result = client->SendString({"hello"});
  // [END one_way_natural_first_line]
  if (!result.is_ok()) {
    FX_LOGS(ERROR) << "SendString failed: " << result.error_value();
    ZX_PANIC("%s", result.error_value().FormatDescription().c_str());
  }
  // [END one_way_natural]
  loop.Run();
  loop.ResetQuit();

  // [START two_way_wire_result]
  // [START two_way_wire_result_first_line]
  client.wire()->EchoString("hello").ThenExactlyOnce(
      // [END two_way_wire_result_first_line]
      [&](fidl::WireUnownedResult<fuchsia_examples::Echo::EchoString>& result) {
        if (!result.ok()) {
          FX_LOGS(ERROR) << "EchoString failed: " << result.error();
          ZX_PANIC("%s", result.error().FormatDescription().c_str());
          return;
        }
        fidl::WireResponse<fuchsia_examples::Echo::EchoString>& response = result.value();
        std::string reply(response.response.data(), response.response.size());
        FX_LOGS(INFO) << "(Wire types) got response: " << reply;
        loop.Quit();
      });
  // [END two_way_wire_result]
  loop.Run();
  loop.ResetQuit();

  // [START one_way_wire]
  // [START one_way_wire_first_line]
  fidl::Status wire_status = client.wire()->SendString("hello");
  // [END one_way_wire_first_line]
  if (!wire_status.ok()) {
    FX_LOGS(ERROR) << "SendString failed: " << result.error_value();
    ZX_PANIC("%s", result.error_value().FormatDescription().c_str());
  }
  // [END one_way_wire]
  loop.Run();
  loop.ResetQuit();

  return 0;
}
