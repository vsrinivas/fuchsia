// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include <fuchsia/examples/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/sys/cpp/component_context.h>

#include <iostream>

// [START echo-impl]
class EchoImpl : public fuchsia::examples::Echo {
 public:
  explicit EchoImpl(std::string prefix) : prefix_(prefix) {}
  void EchoString(std::string value, EchoStringCallback callback) override {
    std::cout << "Got echo request for prefix " << prefix_ << std::endl;
    callback(prefix_ + value);
  }
  void SendString(std::string value) override {}

  const std::string prefix_;
};
// [END echo-impl]

// [START launcher-impl]
class EchoLauncherImpl : public fuchsia::examples::EchoLauncher {
 public:
  void GetEcho(std::string echo_prefix, GetEchoCallback callback) override {
    std::cout << "Got non pipelined request" << std::endl;
    fidl::InterfaceHandle<fuchsia::examples::Echo> client_end;
    fidl::InterfaceRequest<fuchsia::examples::Echo> server_end = client_end.NewRequest();
    bindings_.AddBinding(std::make_unique<EchoImpl>(echo_prefix), std::move(server_end));
    callback(std::move(client_end));
  }

  void GetEchoPipelined(std::string echo_prefix,
                        fidl::InterfaceRequest<fuchsia::examples::Echo> server_end) override {
    std::cout << "Got pipelined request" << std::endl;
    bindings_.AddBinding(std::make_unique<EchoImpl>(echo_prefix), std::move(server_end));
  }

  fidl::BindingSet<fuchsia::examples::Echo, std::unique_ptr<fuchsia::examples::Echo>> bindings_;
};
// [END launcher-impl]

// [START main]
int main(int argc, const char** argv) {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);

  EchoLauncherImpl impl;
  fidl::Binding<fuchsia::examples::EchoLauncher> binding(&impl);
  fidl::InterfaceRequestHandler<fuchsia::examples::EchoLauncher> handler =
      [&](fidl::InterfaceRequest<fuchsia::examples::EchoLauncher> request) {
        binding.Bind(std::move(request));
      };
  auto context = sys::ComponentContext::CreateAndServeOutgoingDirectory();
  context->outgoing()->AddPublicService(std::move(handler));

  std::cout << "Running echo launcher server" << std::endl;
  return loop.Run();
}
// [END main]
