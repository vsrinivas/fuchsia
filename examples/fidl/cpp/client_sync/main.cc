// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// ============================================================================
// This is an accompanying example code for the C++ client tutorial. Head over
// there for the full walk-through:
// https://fuchsia.dev/fuchsia-src/development/languages/fidl/tutorials/cpp/basics/sync-client
// ============================================================================

// [START includes]
#include <fidl/fuchsia.examples/cpp/fidl.h>
#include <lib/sys/component/cpp/service_client.h>
#include <lib/syslog/cpp/macros.h>
// [END includes]

// [START main]
int main(int argc, const char** argv) {
  // [START connect]
  // Connect to the |fuchsia.examples/Echo| protocol inside the component's
  // namespace. This can fail so it's wrapped in a |zx::result| and it must be
  // checked for errors.
  zx::result client_end = component::Connect<fuchsia_examples::Echo>();
  if (!client_end.is_ok()) {
    FX_LOGS(ERROR) << "Synchronous error when connecting to the |Echo| protocol: "
                   << client_end.status_string();
    return -1;
  }
  // [END connect]

  // [START init-client]
  fidl::SyncClient client{std::move(*client_end)};
  // [END init-client]

  {
    // Make an EchoString call.
    // [START echo-string]
    fidl::Result result = client->EchoString({"hello"});
    // Check if the FIDL call succeeded or not.
    if (!result.is_ok()) {
      // If the call failed, log the error, and quit the program.
      // Production code should do more graceful error handling depending
      // on the situation.
      FX_LOGS(ERROR) << "EchoString failed: " << result.error_value();
      return -1;
    }
    const std::string& reply_string = result->response();
    FX_LOGS(INFO) << "Got response: " << reply_string;
    // [END echo-string]
  }

  {
    // [START echo-string-designated]
    // [START echo-string-designated-first-line]
    fidl::Result result = client->EchoString({{.value = "hello"}});
    // [END echo-string-designated-first-line]
    if (!result.is_ok()) {
      FX_LOGS(ERROR) << "EchoString failed: " << result.error_value();
      return -1;
    }
    const std::string& reply_string = result->response();
    FX_LOGS(INFO) << "Got response: " << reply_string;
    // [END echo-string-designated]
  }

  {
    // Make a SendString call.
    // [START send-string]
    // [START send-string-first-line]
    fit::result<fidl::Error> result = client->SendString({"hi"});
    // [END send-string-first-line]
    if (!result.is_ok()) {
      FX_LOGS(ERROR) << "SendString failed: " << result.error_value();
      return -1;
    }
    // [END send-string]

    // [START event-handler]
    // Define the event handler implementation for the client.
    //
    // The event handler should be an object that implements
    // |fidl::SyncEventHandler<Echo>|, and override all pure virtual methods
    // in that class corresponding to the events offered by the protocol.
    class EventHandler : public fidl::SyncEventHandler<fuchsia_examples::Echo> {
     public:
      EventHandler() = default;

      void OnString(fidl::Event<fuchsia_examples::Echo::OnString>& event) override {
        const std::string& reply_string = event.response();
        FX_LOGS(INFO) << "Got event: " << reply_string;
      }
    };
    // [END event-handler]

    // [START handle-one-event]
    // Block to receive exactly one event from the server, which is handled using
    // the event handler defined above.
    EventHandler event_handler;
    fidl::Status status = client.HandleOneEvent(event_handler);
    if (!status.ok()) {
      FX_LOGS(ERROR) << "HandleOneEvent failed: " << status.error();
      return -1;
    }
    // [END handle-one-event]

    // Make a SendString call using wire types.
    // [START send-string-wire]
    // [START send-string-wire-first-line]
    fidl::WireResult wire_result = client.wire()->SendString("hi");
    // [END send-string-wire-first-line]
    if (!wire_result.ok()) {
      FX_LOGS(ERROR) << "SendString failed: " << wire_result.error();
      return -1;
    }
    // [END send-string-wire]
    status = client.HandleOneEvent(event_handler);
    if (!status.ok()) {
      FX_LOGS(ERROR) << "HandleOneEvent failed: " << status.error();
      return -1;
    }
  }

  {
    // Make an EchoString call using wire types.
    // [START echo-string-wire]
    // [START echo-string-wire-first-line]
    fidl::WireResult result = client.wire()->EchoString("hello");
    // [END echo-string-wire-first-line]
    if (!result.ok()) {
      FX_LOGS(ERROR) << "EchoString failed: " << result.error();
      return -1;
    }
    FX_LOGS(INFO) << "Got response: " << result->response.get();
    // [END echo-string-wire]
  }

  return 0;
}
// [END main]
