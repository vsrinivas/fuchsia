// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_MODULAR_CPP_AGENT_H_
#define LIB_MODULAR_CPP_AGENT_H_

#include <fuchsia/modular/cpp/fidl.h>
#include <fuchsia/sys/cpp/fidl.h>
#include <lib/async/cpp/task.h>
#include <lib/async/default.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/fidl/cpp/interface_request.h>
#include <lib/sys/cpp/outgoing_directory.h>

#include <memory>
#include <unordered_map>

namespace modular {

// Agent is a utility class for implementing an Agent. This utility provides a mechanism to publish
// the Agent interface and participate in Modular lifecycle.
//
// Example:
// ========
//
// class MyAgentServiceImpl : public MyAgentService {
//  public:
//    // |MyAgentService|
//    void MyAgentMethod() override {};
// };
//
// int main(int argc, const char** argv) {
//   async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
//   auto context = sys::ComponentContext::CreateAndServeOutgoingDirectory();
//
//   MyAgentServiceImpl my_service;
//   fidl::BindingSet<MyAgentService> my_service_bindings;
//
//   modular::Agent agent(context->outgoing(),
//                        [&loop] {  /* on terminate */
//                          loop.Quit();
//                        });
//   agent.AddService<MyAgentService>(my_service_bindings_.GetHandler(&my_service));
//
//   loop.Run();
//   return 0;
// }
class Agent final : fuchsia::modular::Agent,
                    fuchsia::modular::Lifecycle,
                    fuchsia::sys::ServiceProvider {
 public:
  // Publishes the |fuchsia.modular.Agent| and |fuchsia.modular.Lifecycle| interfaces over the
  // |publish_dir| directory. When a Terminate signal is received, these interfaces are unpublished
  // and the supplied |on_terminate| is called.
  //
  // |on_terminate| must not be null. |on_terminate| may destroy
  Agent(std::shared_ptr<sys::OutgoingDirectory> publish_dir, fit::closure on_terminate);

  ~Agent() override;

  Agent(const Agent&) = delete;
  Agent(Agent&&) = delete;
  void operator=(const Agent&) = delete;
  void operator=(Agent&&) = delete;

  // Adds the specified interface to the set of published agent interfaces.
  //
  // Adds a supported service with the given |service_name|, using the given
  // |request_handler|. |request_handler| should
  // remain valid for the lifetime of this object.
  template <typename Interface>
  void AddService(fidl::InterfaceRequestHandler<Interface> request_handler,
                  std::string service_name = Interface::Name_) {
    service_name_to_handler_[service_name] = [request_handler =
                                                  std::move(request_handler)](zx::channel req) {
      request_handler(fidl::InterfaceRequest<Interface>(std::move(req)));
    };
  }

 private:
  // |fuchsia::modular::Agent|
  void Connect(
      std::string requestor_id,
      fidl::InterfaceRequest<fuchsia::sys::ServiceProvider> outgoing_services_request) override;

  // |fuchsia::sys::ServiceProvider|
  void ConnectToService(std::string service_name, zx::channel request) override;

  // |fuchsia::modular::Lifecycle|
  void Terminate() override;

  // This directory is where Agent & Lifecycle interfaeces are published.
  std::shared_ptr<sys::OutgoingDirectory> publish_dir_;
  fit::closure on_terminate_;

  fidl::BindingSet<fuchsia::modular::Agent> agent_bindings_;
  fidl::BindingSet<fuchsia::modular::Lifecycle> lifecycle_bindings_;

  fidl::BindingSet<fuchsia::sys::ServiceProvider> agent_service_provider_bindings_;

  // A mapping of `service name -> service connection handle` which is inflated using
  // AddService<>().
  std::unordered_map<std::string, fit::function<void(zx::channel)>> service_name_to_handler_;
};

}  // namespace modular

#endif  // LIB_MODULAR_CPP_AGENT_H_
