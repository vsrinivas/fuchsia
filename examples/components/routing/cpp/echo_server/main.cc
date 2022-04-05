// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// [START imports]
#include <fidl/examples/routing/echo/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/sys/inspect/cpp/component.h>
// [END imports]

// [START handler]
// Handler for incoming service requests
class EchoImplementation : public fidl::examples::routing::echo::Echo {
 public:
  void EchoString(fidl::StringPtr value, EchoStringCallback callback) override { callback(value); }
  fidl::examples::routing::echo::Echo_EventSender* event_sender_;
};
// [END handler]

// [START main_body]
int main(int argc, const char** argv) {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  auto context = sys::ComponentContext::CreateAndServeOutgoingDirectory();

  // Initialize inspect
  sys::ComponentInspector inspector(context.get());
  inspector.Health().StartingUp();

  // Serve the Echo protocol
  EchoImplementation echo_instance;
  fidl::Binding<fidl::examples::routing::echo::Echo> binding(&echo_instance);
  echo_instance.event_sender_ = &binding.events();
  fidl::InterfaceRequestHandler<fidl::examples::routing::echo::Echo> handler =
      [&](fidl::InterfaceRequest<fidl::examples::routing::echo::Echo> request) {
        binding.Bind(std::move(request));
      };
  context->outgoing()->AddPublicService(std::move(handler));

  // Component is serving and ready to handle incoming requests
  inspector.Health().Ok();

  return loop.Run();
}
// [END main_body]
