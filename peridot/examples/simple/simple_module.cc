// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/modular/cpp/fidl.h>
#include <fuchsia/modular/examples/simple/cpp/fidl.h>
#include <fuchsia/sys/cpp/fidl.h>
#include <fuchsia/ui/app/cpp/fidl.h>
#include <lib/app_driver/cpp/module_driver.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/component/cpp/connect.h>
#include <lib/message_queue/cpp/message_queue_client.h>
#include <lib/sys/cpp/component_context.h>

using ::fuchsia::modular::examples::simple::SimplePtr;

namespace simple {

class SimpleModule : public fuchsia::ui::app::ViewProvider {
 public:
  SimpleModule(modular::ModuleHost* const module_host)
      : view_provider_binding_(this) {
    // Get the component context from the module context.
    fuchsia::modular::ComponentContextPtr component_context;
    module_host->module_context()->GetComponentContext(
        component_context.NewRequest());

    // Connect to the agent to retrieve it's outgoing services.
    fuchsia::modular::AgentControllerPtr agent_controller;
    fuchsia::sys::ServiceProviderPtr agent_services;
    component_context->ConnectToAgent("simple_agent",
                                      agent_services.NewRequest(),
                                      agent_controller.NewRequest());

    // Connect to the SimpleService in the agent's services.
    SimplePtr agent_service;
    component::ConnectToService(agent_services.get(),
                                agent_service.NewRequest());

    // Request a new message queue from the component context.
    component_context->ObtainMessageQueue("agent_queue",
                                          message_queue_.NewRequest());

    // Register a callback that receives new messages that SimpleAgent sends.
    message_queue_.RegisterReceiver(
        [](std::string msg, fit::function<void()> ack) {
          ack();
          FXL_LOG(INFO) << "new message: " << msg;
        });

    // Get the token for the message queue and send it to the agent.
    message_queue_.GetToken(
        [agent_service = std::move(agent_service)](fidl::StringPtr token) {
          agent_service->SetMessageQueue(token.value_or(""));
        });
    FXL_LOG(INFO) << "Initialized Simple Module.";
  }

  SimpleModule(modular::ModuleHost* const module_host,
               fidl::InterfaceRequest<fuchsia::ui::app::ViewProvider>
                   view_provider_request)
      : SimpleModule(module_host) {
    view_provider_binding_.Bind(std::move(view_provider_request));
  }

  // Called by ModuleDriver.
  void Terminate(fit::function<void()> done) { done(); }

 private:
  // |fuchsia::ui::app::ViewProvider|
  void CreateView(
      zx::eventpair view_token,
      fidl::InterfaceRequest<fuchsia::sys::ServiceProvider> incoming_services,
      fidl::InterfaceHandle<fuchsia::sys::ServiceProvider> outgoing_services)
      override {}

  fidl::Binding<fuchsia::ui::app::ViewProvider> view_provider_binding_;
  modular::MessageQueueClient message_queue_;
};

}  // namespace simple

int main(int argc, const char** argv) {
  async::Loop loop(&kAsyncLoopConfigAttachToThread);
  auto context = sys::ComponentContext::Create();
  modular::ModuleDriver<simple::SimpleModule> driver(context.get(),
                                                     [&loop] { loop.Quit(); });
  loop.Run();
  return 0;
}
