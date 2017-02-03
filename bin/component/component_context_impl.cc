// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/modular/src/component/component_context_impl.h"

#include "lib/ftl/logging.h"

namespace modular {

ComponentContextImpl::ComponentContextImpl() = default;

ComponentContextImpl::~ComponentContextImpl() = default;

void ComponentContextImpl::ConnectToAgent(
    const fidl::String& url,
    fidl::InterfaceRequest<modular::ServiceProvider> incoming_services,
    fidl::InterfaceRequest<modular::AgentController> controller) {
  // Unimplemented.
  FTL_LOG(INFO) << "ComponentContextImpl::ConnectToAgent";
}

void ComponentContextImpl::ObtainMessageQueue(
    const fidl::String& name,
    fidl::InterfaceRequest<modular::MessageQueue> queue) {
  // Unimplemented.
  FTL_LOG(INFO) << "ComponentContextImpl::ObtainMessageQueue";
}

void ComponentContextImpl::DeleteMessageQueue(const fidl::String& name) {
  // Unimplemented.
  FTL_LOG(INFO) << "ComponentContextImpl::DeleteMessageQueue";
}

void ComponentContextImpl::GetMessageSender(
    const fidl::String& queue_token,
    fidl::InterfaceRequest<modular::MessageSender> sender) {
  // Unimplemented.
  FTL_LOG(INFO) << "ComponentContextImpl::GetMessageSender";
}

}  // namespace modular
