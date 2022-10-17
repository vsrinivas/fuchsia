// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.examples/cpp/wire.h>
#include <lib/sys/component/cpp/service_client.h>
#include <lib/syslog/cpp/macros.h>

// ============================================================================
// This is an accompanying example code for the C++ client tutorial. Head over
// there for the full walk-through:
// https://fuchsia.dev/fuchsia-src/development/languages/fidl/tutorials/cpp/basics/sync-client
// ============================================================================

int main(int argc, const char** argv) {
  // Connect to the |fuchsia.examples/Echo| protocol inside the component's
  // namespace. This can fail so it's wrapped in a |zx::result| and it must be
  // checked for errors.
  zx::result client_end = component::Connect<fuchsia_examples::Echo>();
  if (!client_end.is_ok()) {
    FX_LOGS(ERROR) << "Synchronous error when connecting to the |Echo| protocol: "
                   << client_end.status_string();
    return -1;
  }

  // [START init-client]
  fidl::WireSyncClient client(std::move(*client_end));
  // [END init-client]

  {
    // Make an EchoString call.
    fidl::WireResult result = client->EchoString("hello");
    // Check if the FIDL call succeeded or not.
    if (!result.ok()) {
      // If the call failed, log the error, and quit the program.
      // Production code should do more graceful error handling depending
      // on the situation.
      FX_LOGS(ERROR) << "EchoString failed: " << result.error();
      return -1;
    }
    FX_LOGS(INFO) << "Got response: " << result->response.get();
  }

  {
    // Make a SendString call.
    fidl::WireResult result = client->SendString("hi");
    if (!result.ok()) {
      FX_LOGS(ERROR) << "SendString failed: " << result.error();
      return -1;
    }

    // [START event-handler]
    // Define the event handler implementation for the client.
    //
    // The event handler should be an object that implements
    // |fidl::WireSyncEventHandler<Echo>|, and override all pure virtual methods
    // in that class corresponding to the events offered by the protocol.
    class EventHandler : public fidl::WireSyncEventHandler<fuchsia_examples::Echo> {
     public:
      EventHandler() = default;

      void OnString(fidl::WireEvent<fuchsia_examples::Echo::OnString>* event) override {
        FX_LOGS(INFO) << "Got event: " << event->response.get();
      }
    };
    // [END event-handler]

    // [START handle-one-event]
    // Block to receive exactly one event from the server, which is handled using
    // the event handlers defined above.
    EventHandler event_handler;
    fidl::Status status = client.HandleOneEvent(event_handler);
    if (!status.ok()) {
      FX_LOGS(ERROR) << "HandleOneEvent failed: " << status.error();
      return -1;
    }
    // [END handle-one-event]
  }

  return 0;
}
