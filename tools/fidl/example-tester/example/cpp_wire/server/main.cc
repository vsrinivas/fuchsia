// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/test.exampletester/cpp/wire.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/sys/component/cpp/outgoing_directory.h>
#include <lib/syslog/cpp/macros.h>

// An implementation of the Simple protocol.
class SimpleImpl final : public fidl::WireServer<test_exampletester::Simple> {
 public:
  // Bind this implementation to a channel.
  SimpleImpl(async_dispatcher_t* dispatcher, fidl::ServerEnd<test_exampletester::Simple> server_end)
      : binding_(fidl::BindServer(
            dispatcher, std::move(server_end), this,
            [this](SimpleImpl* impl, fidl::UnbindInfo info,
                   fidl::ServerEnd<test_exampletester::Simple> server_end) { delete this; })) {}

  void Add(AddRequestView request, AddCompleter::Sync& completer) override {
    // Call |Reply| to reply synchronously with the request value.
    FX_LOGS(INFO) << "Request received";
    completer.Reply((uint16_t)(request->augend + request->addend));
    FX_LOGS(INFO) << "Response sent";
  }

 private:
  fidl::ServerBindingRef<test_exampletester::Simple> binding_;
};

int main(int argc, char** argv) {
  FX_LOGS(INFO) << "Started";
  FX_LOGS(INFO) << "trim me (C++ wire)";

  // The event loop is used to asynchronously listen for incoming connections and requests from the
  // client. The following initializes the loop, and obtains the dispatcher, which will be used when
  // binding the server implementation to a channel.
  async::Loop loop(&kAsyncLoopConfigNeverAttachToThread);
  async_dispatcher_t* dispatcher = loop.dispatcher();

  // Create an |OutgoingDirectory| instance.
  //
  // The |component::OutgoingDirectory| class serves the outgoing directory for our component. This
  // directory is where the outgoing FIDL protocols are installed so that they can be provided to
  // other components.
  component::OutgoingDirectory outgoing = component::OutgoingDirectory::Create(dispatcher);

  // The `ServeFromStartupInfo()` function sets up the outgoing directory with the startup handle.
  // The startup handle is a handle provided to every component by the system, so that they can
  // serve capabilities (e.g. FIDL protocols) to other components.
  zx::status result = outgoing.ServeFromStartupInfo();
  if (result.is_error()) {
    FX_LOGS(ERROR) << "Failed to serve outgoing directory: " << result.status_string();
    return -1;
  }

  // Register a handler for components trying to connect to fuchsia.examples.Simple.
  result = outgoing.AddProtocol<test_exampletester::Simple>(
      [dispatcher](fidl::ServerEnd<test_exampletester::Simple> server_end) {
        // Create an instance of our SimpleImpl that destroys itself when the connection closes.
        new SimpleImpl(dispatcher, std::move(server_end));
      });
  if (result.is_error()) {
    FX_LOGS(ERROR) << "Failed to add Simple protocol: " << result.status_string();
    return -1;
  }

  // Everything is wired up. Sit back and run the loop until an incoming connection wakes us up.
  FX_LOGS(INFO) << "Listening for incoming connections";
  loop.Run();
  return 0;
}
