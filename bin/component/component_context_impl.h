// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MODULAR_SRC_COMPONENT_COMPONENT_CONTEXT_IMPL_H_
#define APPS_MODULAR_SRC_COMPONENT_COMPONENT_CONTEXT_IMPL_H_

#include <string>

#include "apps/modular/services/component/component_context.fidl.h"
#include "apps/modular/src/component/message_queue_manager.h"
#include "lib/fidl/cpp/bindings/interface_request.h"
#include "lib/fidl/cpp/bindings/string.h"
#include "lib/ftl/macros.h"

namespace modular {

class AgentRunner;

// This class implements the ComponentContext interface, which is provided to
// modules and agents.
// TODO: Investigate using a ComponentContextFactory that'll help initialize
// ComponentContextImpls with the AgentRunner and MessageQueueManager.
class ComponentContextImpl : public ComponentContext {
 public:
  explicit ComponentContextImpl(MessageQueueManager* message_queue_manager,
                                AgentRunner* agent_runner,
                                const std::string& component_id);
  ~ComponentContextImpl();

  const std::string& component_id() { return component_id_; }

 private:
  void ConnectToAgent(
      const fidl::String& url,
      fidl::InterfaceRequest<app::ServiceProvider> incoming_services_request,
      fidl::InterfaceRequest<AgentController> agent_controller_request)
      override;
  void ObtainMessageQueue(
      const fidl::String& name,
      fidl::InterfaceRequest<MessageQueue> request) override;
  void DeleteMessageQueue(const fidl::String& name) override;
  void GetMessageSender(const fidl::String& queue_token,
                        fidl::InterfaceRequest<MessageSender> request) override;

  MessageQueueManager* const message_queue_manager_;
  AgentRunner* const agent_runner_;
  const std::string component_id_;

  FTL_DISALLOW_COPY_AND_ASSIGN(ComponentContextImpl);
};

}  // namespace modular

#endif  // APPS_MODULAR_SRC_COMPONENT_COMPONENT_CONTEXT_IMPL_H_
