// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MODULAR_SRC_COMPONENT_COMPONENT_CONTEXT_IMPL_H_
#define APPS_MODULAR_SRC_COMPONENT_COMPONENT_CONTEXT_IMPL_H_

#include "apps/modular/services/component/component_context.fidl.h"
#include "lib/fidl/cpp/bindings/interface_request.h"
#include "lib/fidl/cpp/bindings/string.h"
#include "lib/ftl/macros.h"

namespace modular {

// This class implements the ComponentContext interface, which is provided to
// modules and agents.
class ComponentContextImpl : public ComponentContext {
 public:
  ComponentContextImpl();
  ~ComponentContextImpl();

 private:
  void ConnectToAgent(
      const fidl::String& url,
      fidl::InterfaceRequest<modular::ServiceProvider> incoming_services,
      fidl::InterfaceRequest<modular::AgentController> controller) override;
  void ObtainMessageQueue(
      const fidl::String& name,
      fidl::InterfaceRequest<modular::MessageQueue> queue) override;
  void DeleteMessageQueue(const fidl::String& name) override;
  void GetMessageSender(
      const fidl::String& queue_token,
      fidl::InterfaceRequest<modular::MessageSender> sender) override;

  FTL_DISALLOW_COPY_AND_ASSIGN(ComponentContextImpl);
};

}  // namespace modular

#endif  // APPS_MODULAR_SRC_COMPONENT_COMPONENT_CONTEXT_IMPL_H_
