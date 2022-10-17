// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// ============================================================================
// This is an accompanying example code for the C++ protocol request pipelining
// tutorial. Head over there for the full walk-through:
// https://fuchsia.dev/fuchsia-src/development/languages/fidl/tutorials/cpp/topics/request-pipelining
// ============================================================================

#include <fidl/fuchsia.examples/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/sys/component/cpp/outgoing_directory.h>
#include <lib/syslog/cpp/macros.h>

// [START echo-impl]
// Implementation of the Echo protocol that prepends a prefix to every response.
class EchoImpl final : public fidl::Server<fuchsia_examples::Echo> {
 public:
  explicit EchoImpl(std::string prefix) : prefix_(prefix) {}
  // This method is not used in the request pipelining example, so requests are ignored.
  void SendString(SendStringRequest& request, SendStringCompleter::Sync& completer) override {}

  void EchoString(EchoStringRequest& request, EchoStringCompleter::Sync& completer) override {
    FX_LOGS(INFO) << "Got echo request for prefix " << prefix_;
    completer.Reply(prefix_ + request.value());
  }

  const std::string prefix_;
};
// [END echo-impl]

// [START launcher-impl]
// Implementation of EchoLauncher. Each method creates an instance of EchoImpl
// with the specified prefix.
class EchoLauncherImpl final : public fidl::Server<fuchsia_examples::EchoLauncher> {
 public:
  explicit EchoLauncherImpl(async_dispatcher_t* dispatcher) : dispatcher_(dispatcher) {}

  void GetEcho(GetEchoRequest& request, GetEchoCompleter::Sync& completer) override {
    FX_LOGS(INFO) << "Got non-pipelined request";
    auto endpoints = fidl::CreateEndpoints<fuchsia_examples::Echo>();
    ZX_ASSERT(endpoints.is_ok());
    auto [client_end, server_end] = *std::move(endpoints);
    fidl::BindServer(dispatcher_, std::move(server_end),
                     std::make_unique<EchoImpl>(request.echo_prefix()));
    completer.Reply(std::move(client_end));
  }

  void GetEchoPipelined(GetEchoPipelinedRequest& request,
                        GetEchoPipelinedCompleter::Sync& completer) override {
    FX_LOGS(INFO) << "Got pipelined request";
    fidl::BindServer(dispatcher_, std::move(request.request()),
                     std::make_unique<EchoImpl>(request.echo_prefix()));
  }

  async_dispatcher_t* dispatcher_;
};
// [END launcher-impl]

// [START main]
int main(int argc, char** argv) {
  async::Loop loop(&kAsyncLoopConfigNeverAttachToThread);
  async_dispatcher_t* dispatcher = loop.dispatcher();

  component::OutgoingDirectory outgoing = component::OutgoingDirectory::Create(dispatcher);
  zx::result result = outgoing.ServeFromStartupInfo();
  if (result.is_error()) {
    FX_LOGS(ERROR) << "Failed to serve outgoing directory: " << result.status_string();
    return -1;
  }

  result = outgoing.AddProtocol<fuchsia_examples::EchoLauncher>(
      [dispatcher](fidl::ServerEnd<fuchsia_examples::EchoLauncher> server_end) {
        FX_LOGS(INFO) << "Incoming connection for "
                      << fidl::DiscoverableProtocolName<fuchsia_examples::EchoLauncher>;
        fidl::BindServer(dispatcher, std::move(server_end),
                         std::make_unique<EchoLauncherImpl>(dispatcher));
      });
  if (result.is_error()) {
    FX_LOGS(ERROR) << "Failed to add EchoLauncher protocol: " << result.status_string();
    return -1;
  }

  FX_LOGS(INFO) << "Running echo launcher server" << std::endl;
  loop.Run();
  return 0;
}
// [END main]
