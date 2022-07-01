// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/serversuite/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/cpp/task.h>
#include <lib/async/default.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/sys/cpp/component_context.h>
#include <stdio.h>

#include <iostream>

class TargetServer : public fidl::serversuite::Target {
 public:
  explicit TargetServer(fidl::InterfacePtr<fidl::serversuite::Reporter> reporter)
      : reporter_(std::move(reporter)) {}

  void OneWayNoPayload() override {
    std::cout << "Target.OneWayNoPayload()" << std::endl;
    reporter_->ReceivedOneWayNoPayload();
  }

 private:
  fidl::InterfacePtr<fidl::serversuite::Reporter> reporter_;
};

class RunnerServer : public fidl::serversuite::Runner {
 public:
  explicit RunnerServer(async_dispatcher_t* dispatcher) : dispatcher_(dispatcher) {}

  void Start(fidl::InterfaceHandle<fidl::serversuite::Reporter> reporter,
             StartCallback callback) override {
    target_server_ = std::make_unique<TargetServer>(reporter.Bind());
    target_binding_ =
        std::make_unique<fidl::Binding<fidl::serversuite::Target>>(target_server_.get());

    zx::channel client_end, server_end;
    ZX_ASSERT(ZX_OK == zx::channel::create(0, &client_end, &server_end));
    target_binding_->Bind(fidl::InterfaceRequest<fidl::serversuite::Target>(std::move(server_end)),
                          dispatcher_);
    callback(fidl::InterfaceHandle<fidl::serversuite::Target>(std::move(client_end)));
  }

  void CheckAlive(CheckAliveCallback callback) override { return callback(); }

 private:
  async_dispatcher_t* dispatcher_;
  std::unique_ptr<TargetServer> target_server_;
  std::unique_ptr<fidl::Binding<fidl::serversuite::Target>> target_binding_;
};

int main(int argc, const char** argv) {
  std::cout << "HLCPP server: main" << std::endl;
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);

  RunnerServer server(loop.dispatcher());
  fidl::Binding<fidl::serversuite::Runner> binding(&server);
  fidl::InterfaceRequestHandler<fidl::serversuite::Runner> handler =
      [&](fidl::InterfaceRequest<fidl::serversuite::Runner> server_end) {
        binding.Bind(std::move(server_end));
      };
  auto context = sys::ComponentContext::CreateAndServeOutgoingDirectory();
  context->outgoing()->AddPublicService(std::move(handler));

  std::cout << "HLCPP server: ready!" << std::endl;
  return loop.Run();
}
