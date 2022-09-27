// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// ============================================================================
// This is an accompanying example code for the C++ client tutorial. Head over
// there for the full walk-through:
// https://fuchsia.dev/fuchsia-src/development/languages/fidl/tutorials/cpp/basics/client
// ============================================================================

// [START includes]
#include <fidl/fuchsia.examples/cpp/wire.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/sys/component/cpp/service_client.h>
#include <lib/syslog/cpp/macros.h>
// [END includes]

// [START main]
int main(int argc, const char** argv) {
  // Connect to the |fuchsia.examples/Echo| protocol inside the component's
  // namespace. This can fail so it's wrapped in a |zx::status| and it must be
  // checked for errors.
  zx::status client_end = component::Connect<fuchsia_examples::Echo>();
  if (!client_end.is_ok()) {
    FX_LOGS(ERROR) << "Synchronous error when connecting to the |Echo| protocol: "
                   << client_end.status_string();
    return -1;
  }

  async::Loop loop(&kAsyncLoopConfigNeverAttachToThread);
  async_dispatcher_t* dispatcher = loop.dispatcher();

  // Define the event handler implementation for the client.
  //
  // The event handler delegate should be an object that implements the
  // |fidl::WireAsyncEventHandler<Echo>| virtual class, which has methods
  // corresponding to the events offered by the protocol. By default those
  // methods are no-ops.
  // [START event-handler-short]
  class EventHandler : public fidl::WireAsyncEventHandler<fuchsia_examples::Echo> {
   public:
    void OnString(fidl::WireEvent<fuchsia_examples::Echo::OnString>* event) override {
      FX_LOGS(INFO) << "(Natural types) got event: " << event->response.get();
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
  EventHandler event_handler(loop);

  // Create a client to the Echo protocol, passing our |event_handler| created
  // earlier. The |event_handler| must live for at least as long as the
  // |client|. The |client| holds a reference to the |event_handler| so it is a
  // bug for the |event_handler| to be destroyed before the |client|.
  // [START init-client-short]
  fidl::WireClient client(std::move(*client_end), dispatcher, &event_handler);
  // [END init-client-short]

  // Make an EchoString call, passing it a lambda to handle the result asynchronously.
  // |result| contains the method response or a transport error if applicable.
  client->EchoString("hello").ThenExactlyOnce(
      [&](fidl::WireUnownedResult<fuchsia_examples::Echo::EchoString>& result) {
        // Check if the FIDL call succeeded or not.
        if (!result.ok()) {
          // If the call failed, log the error, and crash the program.
          // Production code should do more graceful error handling depending
          // on the situation.
          FX_LOGS(ERROR) << "EchoString failed: " << result.error();
          ZX_PANIC("%s", result.error().FormatDescription().c_str());
        }
        // Dereference (->) the result object to access the response payload.
        std::string_view response = result->response.get();
        FX_LOGS(INFO) << "Got response: " << response;
        loop.Quit();
      });
  // Run the dispatch loop, until we receive a reply, in which case the callback
  // above will quit the loop, breaking us out of the |Run| function.
  loop.Run();
  loop.ResetQuit();

  // [START sync-call]
  // Make a synchronous EchoString call, which blocks until it receives the response,
  // then returns a WireResult object for the response.
  fidl::WireResult result_sync = client.sync()->EchoString("hello");
  ZX_ASSERT(result_sync.ok());
  std::string_view response = result_sync->response.get();
  FX_LOGS(INFO) << "Got synchronous response: " << response;
  // [END sync-call]

  // Make a SendString request. The resulting OnString event will be handled by
  // the event handler defined above.
  fidl::Status status_oneway = client->SendString("hi");
  // Check for any synchronous errors.
  ZX_ASSERT(status_oneway.ok());
  loop.Run();

  return 0;
}
// [END main]
