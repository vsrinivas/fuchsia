// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/src/view_manager/view_impl.h"

#include "apps/mozart/src/view_manager/view_registry.h"
#include "apps/mozart/src/view_manager/view_state.h"

namespace view_manager {

ViewImpl::ViewImpl(ViewRegistry* registry, ViewState* state)
    : registry_(registry), state_(state) {}

ViewImpl::~ViewImpl() {}

void ViewImpl::GetToken(const mozart::View::GetTokenCallback& callback) {
  callback(state_->view_token()->Clone());
}

void ViewImpl::GetServiceProvider(
    fidl::InterfaceRequest<app::ServiceProvider> service_provider_request) {
  service_provider_bindings_.AddBinding(this,
                                        std::move(service_provider_request));
}

void ViewImpl::OfferServiceProvider(
    ::fidl::InterfaceHandle<app::ServiceProvider> service_provider) {
  state_->set_service_provider(std::move(service_provider));
}

void ViewImpl::CreateScene(fidl::InterfaceRequest<mozart::Scene> scene) {
  registry_->CreateScene(state_, std::move(scene));
}

void ViewImpl::GetContainer(
    fidl::InterfaceRequest<mozart::ViewContainer> view_container_request) {
  container_bindings_.AddBinding(this, std::move(view_container_request));
}

void ViewImpl::Invalidate() {
  registry_->Invalidate(state_);
}

void ViewImpl::SetListener(
    fidl::InterfaceHandle<mozart::ViewContainerListener> listener) {
  state_->set_view_container_listener(
      mozart::ViewContainerListenerPtr::Create(std::move(listener)));
}

void ViewImpl::AddChild(
    uint32_t child_key,
    fidl::InterfaceHandle<mozart::ViewOwner> child_view_owner) {
  registry_->AddChild(state_, child_key, std::move(child_view_owner));
}

void ViewImpl::RemoveChild(
    uint32_t child_key,
    fidl::InterfaceRequest<mozart::ViewOwner> transferred_view_owner_request) {
  registry_->RemoveChild(state_, child_key,
                         std::move(transferred_view_owner_request));
}

void ViewImpl::SetChildProperties(
    uint32_t child_key,
    uint32_t child_scene_version,
    mozart::ViewPropertiesPtr child_view_properties) {
  registry_->SetChildProperties(state_, child_key, child_scene_version,
                                std::move(child_view_properties));
}

void ViewImpl::RequestFocus(uint32_t child_key) {
  registry_->RequestFocus(state_, child_key);
}

void ViewImpl::FlushChildren(uint32_t flush_token) {
  registry_->FlushChildren(state_, flush_token);
}

void ViewImpl::ConnectToService(const fidl::String& service_name,
                                mx::channel client_handle) {
  registry_->ConnectToViewService(state_, service_name,
                                  std::move(client_handle));
}

}  // namespace view_manager
