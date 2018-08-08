// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/ui/view_manager/view_tree_impl.h"

#include "garnet/bin/ui/view_manager/view_registry.h"
#include "garnet/bin/ui/view_manager/view_tree_state.h"

namespace view_manager {

ViewTreeImpl::ViewTreeImpl(ViewRegistry* registry, ViewTreeState* state)
    : registry_(registry), state_(state) {}

ViewTreeImpl::~ViewTreeImpl() {}

void ViewTreeImpl::GetToken(GetTokenCallback callback) {
  callback(state_->view_tree_token());
}

void ViewTreeImpl::GetServiceProvider(
    fidl::InterfaceRequest<fuchsia::sys::ServiceProvider> service_provider) {
  service_provider_bindings_.AddBinding(this, std::move(service_provider));
}

void ViewTreeImpl::GetContainer(
    fidl::InterfaceRequest<::fuchsia::ui::viewsv1::ViewContainer>
        view_container_request) {
  container_bindings_.AddBinding(this, std::move(view_container_request));
}

void ViewTreeImpl::SetListener(
    fidl::InterfaceHandle<::fuchsia::ui::viewsv1::ViewContainerListener>
        listener) {
  state_->set_view_container_listener(listener.Bind());
}

void ViewTreeImpl::AddChild(
    uint32_t child_key,
    fidl::InterfaceHandle<::fuchsia::ui::viewsv1token::ViewOwner>
        child_view_owner,
    zx::eventpair host_import_token) {
  registry_->AddChild(state_, child_key, std::move(child_view_owner),
                      std::move(host_import_token));
}

void ViewTreeImpl::RemoveChild(
    uint32_t child_key,
    fidl::InterfaceRequest<::fuchsia::ui::viewsv1token::ViewOwner>
        transferred_view_owner_request) {
  registry_->RemoveChild(state_, child_key,
                         std::move(transferred_view_owner_request));
}

void ViewTreeImpl::SetChildProperties(
    uint32_t child_key,
    ::fuchsia::ui::viewsv1::ViewPropertiesPtr child_view_properties) {
  registry_->SetChildProperties(state_, child_key,
                                std::move(child_view_properties));
}

void ViewTreeImpl::RequestFocus(uint32_t child_key) {
  registry_->RequestFocus(state_, child_key);
}

void ViewTreeImpl::ConnectToService(fidl::StringPtr service_name,
                                    zx::channel client_handle) {
  registry_->ConnectToViewTreeService(state_, service_name,
                                      std::move(client_handle));
}

}  // namespace view_manager
