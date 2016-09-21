// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "services/ui/view_manager/view_manager_impl.h"

#include <utility>

#include "base/bind.h"
#include "services/ui/view_manager/view_impl.h"
#include "services/ui/view_manager/view_tree_impl.h"

namespace view_manager {

ViewManagerImpl::ViewManagerImpl(ViewRegistry* registry)
    : registry_(registry) {}

ViewManagerImpl::~ViewManagerImpl() {}

void ViewManagerImpl::CreateView(
    mojo::InterfaceRequest<mojo::ui::View> view_request,
    mojo::InterfaceRequest<mojo::ui::ViewOwner> view_owner_request,
    mojo::InterfaceHandle<mojo::ui::ViewListener> view_listener,
    const mojo::String& label) {
  registry_->CreateView(
      view_request.Pass(), view_owner_request.Pass(),
      mojo::ui::ViewListenerPtr::Create(std::move(view_listener)), label);
}

void ViewManagerImpl::CreateViewTree(
    mojo::InterfaceRequest<mojo::ui::ViewTree> view_tree_request,
    mojo::InterfaceHandle<mojo::ui::ViewTreeListener> view_tree_listener,
    const mojo::String& label) {
  registry_->CreateViewTree(
      view_tree_request.Pass(),
      mojo::ui::ViewTreeListenerPtr::Create(std::move(view_tree_listener)),
      label);
}

// TODO(mikejurka): This should only be called by trusted code (ie launcher),
// once we have a security story.
void ViewManagerImpl::RegisterViewAssociate(
    mojo::InterfaceHandle<mojo::ui::ViewAssociate> view_associate,
    mojo::InterfaceRequest<mojo::ui::ViewAssociateOwner> view_associate_owner,
    const mojo::String& label) {
  registry_->RegisterViewAssociate(
      registry_, mojo::ui::ViewAssociatePtr::Create(std::move(view_associate)),
      view_associate_owner.Pass(), label);
}

void ViewManagerImpl::FinishedRegisteringViewAssociates() {
  registry_->FinishedRegisteringViewAssociates();
}

}  // namespace view_manager
