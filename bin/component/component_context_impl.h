// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MODULAR_SRC_COMPONENT_COMPONENT_CONTEXT_IMPL_H_
#define APPS_MODULAR_SRC_COMPONENT_COMPONENT_CONTEXT_IMPL_H_

#include <string>

#include "apps/ledger/services/internal/internal.fidl.h"
#include "apps/modular/services/component/component_context.fidl.h"
#include "apps/modular/src/component/message_queue_manager.h"
#include "lib/fidl/cpp/bindings/interface_request.h"
#include "lib/fidl/cpp/bindings/string.h"
#include "lib/ftl/macros.h"

namespace modular {

class AgentRunner;

// The parameters of component context that do not vary by instance.
struct ComponentContextInfo {
  MessageQueueManager* const message_queue_manager;
  AgentRunner* const agent_runner;
  ledger::LedgerRepository* const ledger_repository;
};

// Implements the ComponentContext interface, which is provided to
// modules and agents. The interface is public, because the class
// doesn't contain the Bindings for this interface. TODO(mesch): Move
// bindings into the class.
class ComponentContextImpl : public ComponentContext {
 public:
  explicit ComponentContextImpl(const ComponentContextInfo& info,
                                const std::string& component_instance_id);

  ~ComponentContextImpl() override;

  const std::string& component_id() { return component_id_; }

 private:
  // |ComponentContext|
  void GetLedger(fidl::InterfaceRequest<ledger::Ledger> request,
                 const GetLedgerCallback& result) override;

  // |ComponentContext|
  void ConnectToAgent(
      const fidl::String& url,
      fidl::InterfaceRequest<app::ServiceProvider> incoming_services_request,
      fidl::InterfaceRequest<AgentController> agent_controller_request)
      override;

  // |ComponentContext|
  void ObtainMessageQueue(
      const fidl::String& name,
      fidl::InterfaceRequest<MessageQueue> request) override;

  // |ComponentContext|
  void DeleteMessageQueue(const fidl::String& name) override;

  // |ComponentContext|
  void GetMessageSender(const fidl::String& queue_token,
                        fidl::InterfaceRequest<MessageSender> request) override;

  MessageQueueManager* const message_queue_manager_;
  AgentRunner* const agent_runner_;
  ledger::LedgerRepository* const ledger_repository_;

  // TODO(mesch,vardhan): This component ID is used both as the
  // component ID in order to obtain the ledger for this component, as
  // well as as the component *instance* ID when accessing message
  // queue manager. In the case og Agents, this makes no difference
  // (because Agents are singletons), but it's wrong in the case of
  // Modules, for which the ledger is per Module, but the message
  // queues are per instance.
  const std::string component_id_;

  FTL_DISALLOW_COPY_AND_ASSIGN(ComponentContextImpl);
};

}  // namespace modular

#endif  // APPS_MODULAR_SRC_COMPONENT_COMPONENT_CONTEXT_IMPL_H_
