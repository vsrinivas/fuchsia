// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MODULAR_SRC_AGENT_RUNNER_AGENT_CONTEXT_IMPL_H_
#define APPS_MODULAR_SRC_AGENT_RUNNER_AGENT_CONTEXT_IMPL_H_

#include <string>

#include "application/services/application_controller.fidl.h"
#include "application/services/application_launcher.fidl.h"
#include "application/services/service_provider.fidl.h"
#include "apps/modular/services/agent/agent.fidl.h"
#include "apps/modular/services/agent/agent_context.fidl.h"
#include "apps/modular/services/agent/agent_controller/agent_controller.fidl.h"
#include "apps/modular/services/component/component_context.fidl.h"
#include "apps/modular/src/component/component_context_impl.h"
#include "lib/fidl/cpp/bindings/binding.h"
#include "lib/fidl/cpp/bindings/binding_set.h"
#include "lib/ftl/macros.h"

namespace modular {

class AgentRunner;

// This class manages an agent and its life cycle. AgentRunner owns this class,
// and instantiates one for every instance of an agent running. All requests for
// this agent (identified for now by the agent's URL) are routed to this
// class. This class manages all AgentControllers associated with this agent.
class AgentContextImpl : public AgentContext, public AgentController {
 public:
  explicit AgentContextImpl(app::ApplicationLauncher* app_launcher,
                            AgentRunner* agent_runner,
                            const std::string& url);
  ~AgentContextImpl() override;

  // Called by AgentRunner when a component wants to connect to this agent.
  void NewConnection(
      const std::string& requestor_url,
      fidl::InterfaceRequest<app::ServiceProvider> incoming_services_request,
      fidl::InterfaceRequest<AgentController> agent_controller_request);

 private:
  // |AgentContext|
  void GetComponentContext(
      fidl::InterfaceRequest<ComponentContext> context) override;
  // |AgentContext|
  void ScheduleTask(TaskInfoPtr task_info) override;
  // |AgentContext|
  void DeleteTask(const fidl::String& task_id) override;
  // |AgentContext|
  void Done() override;

  const std::string url_;
  app::ApplicationControllerPtr application_controller_;
  app::ServiceProviderPtr application_services_;
  AgentPtr agent_;
  fidl::Binding<AgentContext> agent_context_binding_;
  fidl::BindingSet<AgentController> agent_controller_bindings_;

  ComponentContextImpl component_context_impl_;
  fidl::BindingSet<ComponentContext> component_context_bindings_;

  FTL_DISALLOW_COPY_AND_ASSIGN(AgentContextImpl);
};

}  // namespace modular

#endif  // APPS_MODULAR_SRC_AGENT_RUNNER_AGENT_CONTEXT_IMPL_H_
