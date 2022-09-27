// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/test.exampletester/cpp/wire.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/sys/component/cpp/outgoing_directory.h>
#include <lib/sys/component/cpp/service_client.h>
#include <lib/syslog/cpp/macros.h>

// An implementation of the Simple protocol.
class SimpleImpl final : public fidl::WireServer<test_exampletester::Simple> {
 public:
  // Bind this implementation to a channel, and store the corresponding client implementation to
  // forward to.
  SimpleImpl(async_dispatcher_t* dispatcher, fidl::ServerEnd<test_exampletester::Simple> server_end,
             fidl::WireClient<test_exampletester::Simple> client_)
      : client_(std::move(client_)),
        binding_(fidl::BindServer(
            dispatcher, std::move(server_end), this,
            [this](SimpleImpl* impl, fidl::UnbindInfo info,
                   fidl::ServerEnd<test_exampletester::Simple> server_end) { delete this; })) {}

  void Add(AddRequestView request, AddCompleter::Sync& completer) override {
    // Forward the request to the server.
    FX_LOGS(INFO) << "Request received";
    client_->Add(request->augend, request->addend)
        .ThenExactlyOnce(
            [completer = completer.ToAsync()](
                fidl::WireUnownedResult<test_exampletester::Simple::Add>& result) mutable {
              // When the request result has bee received, use it to call the async completer and
              // resolve the reply.
              completer.Reply(result->sum);
              FX_LOGS(INFO) << "Response sent";
            });
  }

 private:
  fidl::WireClient<test_exampletester::Simple> client_;
  fidl::ServerBindingRef<test_exampletester::Simple> binding_;
};

int main(int argc, char** argv) {
  FX_LOGS(INFO) << "Started";

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

  // Connect to the protocol inside the component's namespace. This can fail so it's wrapped in a
  // |zx::status| and it must be checked for errors.
  zx::status client_end = component::Connect<test_exampletester::Simple>();
  if (!client_end.is_ok()) {
    FX_LOGS(ERROR) << "Synchronous error when connecting to the |Simple| protocol: "
                   << client_end.status_string();
    return -1;
  }
  FX_LOGS(INFO) << "Outgoing connection enabled";

  // Create an asynchronous client using the newly-established connection.
  fidl::WireClient client(std::move(*client_end), dispatcher);

  // Register a handler for components trying to connect to fuchsia.examples.Simple. Each such
  // connection is naively proxied to the server component.
  result = outgoing.AddProtocol<test_exampletester::Simple>(
      [dispatcher, &client](fidl::ServerEnd<test_exampletester::Simple> server_end) {
        new SimpleImpl(dispatcher, std::move(server_end), std::move(client));
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
