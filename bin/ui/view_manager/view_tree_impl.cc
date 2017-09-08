// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/src/view_manager/view_tree_impl.h"

#include "apps/mozart/src/view_manager/view_registry.h"
#include "apps/mozart/src/view_manager/view_tree_state.h"

namespace view_manager {

ViewTreeImpl::ViewTreeImpl(ViewRegistry* registry, ViewTreeState* state)
    : registry_(registry), state_(state) {}

ViewTreeImpl::~ViewTreeImpl() {}

void ViewTreeImpl::GetToken(const GetTokenCallback& callback) {
  callback(state_->view_tree_token()->Clone());
}

void ViewTreeImpl::GetServiceProvider(
    fidl::InterfaceRequest<app::ServiceProvider> service_provider) {
  service_provider_bindings_.AddBinding(this, std::move(service_provider));
}

void ViewTreeImpl::GetContainer(
    fidl::InterfaceRequest<mozart::ViewContainer> view_container_request) {
  container_bindings_.AddBinding(this, std::move(view_container_request));
}

void ViewTreeImpl::SetListener(
    fidl::InterfaceHandle<mozart::ViewContainerListener> listener) {
  state_->set_view_container_listener(
      mozart::ViewContainerListenerPtr::Create(std::move(listener)));
}

void ViewTreeImpl::AddChild(
    uint32_t child_key,
    fidl::InterfaceHandle<mozart::ViewOwner> child_view_owner,
    mx::eventpair host_import_token) {
  registry_->AddChild(state_, child_key, std::move(child_view_owner),
                      std::move(host_import_token));
}

void ViewTreeImpl::RemoveChild(
    uint32_t child_key,
    fidl::InterfaceRequest<mozart::ViewOwner> transferred_view_owner_request) {
  registry_->RemoveChild(state_, child_key,
                         std::move(transferred_view_owner_request));
}

void ViewTreeImpl::SetChildProperties(
    uint32_t child_key,
    mozart::ViewPropertiesPtr child_view_properties) {
  registry_->SetChildProperties(state_, child_key,
                                std::move(child_view_properties));
}

void ViewTreeImpl::RequestFocus(uint32_t child_key) {
  registry_->RequestFocus(state_, child_key);
}

void ViewTreeImpl::ConnectToService(const fidl::String& service_name,
                                    mx::channel client_handle) {
  registry_->ConnectToViewTreeService(state_, service_name,
                                      std::move(client_handle));
}

}  // namespace view_manager
