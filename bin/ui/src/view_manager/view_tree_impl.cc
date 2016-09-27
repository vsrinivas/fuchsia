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
  callback.Run(state_->view_tree_token()->Clone());
}

void ViewTreeImpl::GetServiceProvider(
    mojo::InterfaceRequest<mojo::ServiceProvider> service_provider) {
  service_provider_bindings_.AddBinding(this, service_provider.Pass());
}

void ViewTreeImpl::SetRenderer(
    mojo::InterfaceHandle<mojo::gfx::composition::Renderer> renderer) {
  registry_->SetRenderer(
      state_, mojo::gfx::composition::RendererPtr::Create(std::move(renderer)));
}

void ViewTreeImpl::GetContainer(
    mojo::InterfaceRequest<mojo::ui::ViewContainer> view_container_request) {
  container_bindings_.AddBinding(this, view_container_request.Pass());
}

void ViewTreeImpl::SetListener(
    mojo::InterfaceHandle<mojo::ui::ViewContainerListener> listener) {
  state_->set_view_container_listener(
      mojo::ui::ViewContainerListenerPtr::Create(std::move(listener)));
}

void ViewTreeImpl::AddChild(
    uint32_t child_key,
    mojo::InterfaceHandle<mojo::ui::ViewOwner> child_view_owner) {
  registry_->AddChild(state_, child_key, child_view_owner.Pass());
}

void ViewTreeImpl::RemoveChild(uint32_t child_key,
                               mojo::InterfaceRequest<mojo::ui::ViewOwner>
                                   transferred_view_owner_request) {
  registry_->RemoveChild(state_, child_key,
                         transferred_view_owner_request.Pass());
}

void ViewTreeImpl::SetChildProperties(
    uint32_t child_key,
    uint32_t child_scene_version,
    mojo::ui::ViewPropertiesPtr child_view_properties) {
  registry_->SetChildProperties(state_, child_key, child_scene_version,
                                child_view_properties.Pass());
}

void ViewTreeImpl::FlushChildren(uint32_t flush_token) {
  registry_->FlushChildren(state_, flush_token);
}

void ViewTreeImpl::ConnectToService(
    const mojo::String& service_name,
    mojo::ScopedMessagePipeHandle client_handle) {
  registry_->ConnectToViewTreeService(state_, service_name,
                                      client_handle.Pass());
}

}  // namespace view_manager
