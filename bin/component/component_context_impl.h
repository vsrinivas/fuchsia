// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_COMPONENT_COMPONENT_CONTEXT_IMPL_H_
#define PERIDOT_BIN_COMPONENT_COMPONENT_CONTEXT_IMPL_H_

#include <string>

#include <fuchsia/cpp/modular.h>
#include "lib/fidl/cpp/interface_request.h"
#include "lib/fidl/cpp/string.h"
#include "lib/fxl/macros.h"
#include "peridot/bin/component/message_queue_manager.h"
#include "peridot/bin/entity/entity_provider_runner.h"
#include <fuchsia/cpp/ledger_internal.h>

namespace modular {

class AgentRunner;

// The parameters of component context that do not vary by instance.
struct ComponentContextInfo {
  MessageQueueManager* const message_queue_manager;
  AgentRunner* const agent_runner;
  ledger_internal::LedgerRepository* const ledger_repository;
  EntityProviderRunner* const entity_provider_runner;
};

// Implements the ComponentContext interface, which is provided to
// modules and agents. The interface is public, because the class
// doesn't contain the Bindings for this interface. TODO(mesch): Move
// bindings into the class.
class ComponentContextImpl : public ComponentContext {
 public:
  // * A component namespace identifies components whose lifetimes are related,
  //   where all of their persisted information will live together; for modules
  //   this is the story id, for agents it is kAgentComponentNamespace, etc.
  // * A component instance ID identifies a particular instance of a component;
  //   for modules, this is the module path in their story. For agents, it is
  //   the agent URL.
  // * A component URL is the origin from which the executable associated with
  //   the component was fetched from.
  explicit ComponentContextImpl(const ComponentContextInfo& info,
                                std::string component_namespace,
                                std::string component_instance_id,
                                std::string component_url);

  ~ComponentContextImpl() override;

  const std::string& component_instance_id() { return component_instance_id_; }

  void Connect(fidl::InterfaceRequest<ComponentContext> request);
  ComponentContextPtr NewBinding();

 private:
  // |ComponentContext|
  void GetLedger(fidl::InterfaceRequest<ledger::Ledger> request,
                 GetLedgerCallback result) override;

  // |ComponentContext|
  void ConnectToAgent(fidl::StringPtr url,
                      fidl::InterfaceRequest<component::ServiceProvider>
                          incoming_services_request,
                      fidl::InterfaceRequest<AgentController>
                          agent_controller_request) override;

  // |ComponentContext|
  void ObtainMessageQueue(
      fidl::StringPtr name,
      fidl::InterfaceRequest<MessageQueue> request) override;

  // |ComponentContext|
  void DeleteMessageQueue(fidl::StringPtr name) override;

  // |ComponentContext|
  void GetMessageSender(fidl::StringPtr queue_token,
                        fidl::InterfaceRequest<MessageSender> request) override;

  // |ComponentContext|
  void GetEntityResolver(
      fidl::InterfaceRequest<EntityResolver> request) override;

  // |ComponentContext|
  void CreateEntityWithData(
      fidl::VectorPtr<TypeToDataEntry> type_to_data,
      CreateEntityWithDataCallback result) override;

  MessageQueueManager* const message_queue_manager_;
  AgentRunner* const agent_runner_;
  ledger_internal::LedgerRepository* const ledger_repository_;
  EntityProviderRunner* const entity_provider_runner_;

  const std::string component_namespace_;
  const std::string component_instance_id_;
  const std::string component_url_;

  fidl::BindingSet<ComponentContext> bindings_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ComponentContextImpl);
};

}  // namespace modular

#endif  // PERIDOT_BIN_COMPONENT_COMPONENT_CONTEXT_IMPL_H_
