// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/src/view_manager/view_manager_impl.h"

#include <utility>

#include "apps/mozart/src/view_manager/view_impl.h"
#include "apps/mozart/src/view_manager/view_tree_impl.h"

namespace view_manager {

ViewManagerImpl::ViewManagerImpl(ViewRegistry* registry)
    : registry_(registry) {}

ViewManagerImpl::~ViewManagerImpl() {}

void ViewManagerImpl::CreateView(
    mojo::InterfaceRequest<mozart::View> view_request,
    mojo::InterfaceRequest<mozart::ViewOwner> view_owner_request,
    mojo::InterfaceHandle<mozart::ViewListener> view_listener,
    const mojo::String& label) {
  registry_->CreateView(
      view_request.Pass(), view_owner_request.Pass(),
      mozart::ViewListenerPtr::Create(std::move(view_listener)), label);
}

void ViewManagerImpl::CreateViewTree(
    mojo::InterfaceRequest<mozart::ViewTree> view_tree_request,
    mojo::InterfaceHandle<mozart::ViewTreeListener> view_tree_listener,
    const mojo::String& label) {
  registry_->CreateViewTree(
      view_tree_request.Pass(),
      mozart::ViewTreeListenerPtr::Create(std::move(view_tree_listener)),
      label);
}

// TODO(mikejurka): This should only be called by trusted code (ie launcher),
// once we have a security story.
void ViewManagerImpl::RegisterViewAssociate(
    mojo::InterfaceHandle<mozart::ViewAssociate> view_associate,
    mojo::InterfaceRequest<mozart::ViewAssociateOwner> view_associate_owner,
    const mojo::String& label) {
  registry_->RegisterViewAssociate(
      registry_, mozart::ViewAssociatePtr::Create(std::move(view_associate)),
      view_associate_owner.Pass(), label);
}

void ViewManagerImpl::FinishedRegisteringViewAssociates() {
  registry_->FinishedRegisteringViewAssociates();
}

}  // namespace view_manager
