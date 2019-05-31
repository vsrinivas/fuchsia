// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_MODULAR_TEST_HARNESS_CPP_FAKE_AGENT_H_
#define LIB_MODULAR_TEST_HARNESS_CPP_FAKE_AGENT_H_

#include <fuchsia/modular/cpp/fidl.h>
#include <fuchsia/modular/testing/cpp/fidl.h>
#include <lib/modular_test_harness/cpp/fake_component.h>
#include <lib/svc/cpp/service_namespace.h>
#include <src/lib/fxl/logging.h>

namespace modular {
namespace testing {

// An Agent implementation which provides access to
// |fuchsia::modular::AgentContext|, implements boilerplate for exposing
// services, and exposes task running callback registration.
class FakeAgent : public modular::testing::FakeComponent,
                  fuchsia::modular::Agent {
 public:
  // Returns a vector of services names which are expected to be provided to the
  // agent.
  static std::vector<std::string> GetSandboxServices();

  FakeAgent();

  // Constructs a FakeAgent which calls |connect_callback| whenever a connection
  // is initiated by the modular framework. |client_url| is the url of the
  // connecting component.
  explicit FakeAgent(
      fit::function<void(std::string client_url)> connect_callback);

  ~FakeAgent();

  // Returns the agent's |fuchsia::modular::ComponentContext|.
  fuchsia::modular::ComponentContext* modular_component_context() {
    return component_context_.get();
  }

  // Returns the agent's |fuchsia::modular::AgentContext|.
  fuchsia::modular::AgentContext* agent_context() {
    return agent_context_.get();
  }

  // Adds a service to the service namespace which is exposed to clients
  // connecting to the agent.
  template <typename ServiceType>
  void AddService(
      fidl::InterfaceRequestHandler<ServiceType> connection_handler) {
    services_.AddService<ServiceType>(std::move(connection_handler));
  }

  // Sets a callback which will be called when a
  // |fuchsia::modular::modular::Agent/RunTask| is received.
  void set_on_run_task(
      fit::function<void(std::string task_id, RunTaskCallback callback)>
          on_run_task);

 private:
  // |FakeComponent|
  void OnCreate(fuchsia::sys::StartupInfo startup_info) override;

  // |fuchsia::modular::Agent|
  void Connect(std::string requestor_url,
               fidl::InterfaceRequest<fuchsia::sys::ServiceProvider>
                   services_request) override;

  // |fuchsia::modular::Agent|
  void RunTask(std::string task_id, RunTaskCallback callback) override;

  // The callback which is called when the modular framework tells the agent to
  // trigger a task.
  fit::function<void(std::string task_id, RunTaskCallback callback)>
      on_run_task_;

  // The callback which is called when the modular framework connects to the
  // agent.
  fit::function<void(std::string client_url)> on_connect_;

  fuchsia::modular::ComponentContextPtr component_context_;
  fuchsia::modular::AgentContextPtr agent_context_;

  fidl::BindingSet<fuchsia::modular::Agent> bindings_;

  component::ServiceNamespace services_;
};

}  // namespace testing
}  // namespace modular

#endif  // LIB_MODULAR_TEST_HARNESS_CPP_FAKE_AGENT_H_
