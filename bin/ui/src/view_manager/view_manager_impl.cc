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
    fidl::InterfaceRequest<mozart::View> view_request,
    fidl::InterfaceRequest<mozart::ViewOwner> view_owner_request,
    fidl::InterfaceHandle<mozart::ViewListener> view_listener,
    const fidl::String& label) {
  registry_->CreateView(
      std::move(view_request), std::move(view_owner_request),
      mozart::ViewListenerPtr::Create(std::move(view_listener)), label);
}

void ViewManagerImpl::CreateViewTree(
    fidl::InterfaceRequest<mozart::ViewTree> view_tree_request,
    fidl::InterfaceHandle<mozart::ViewTreeListener> view_tree_listener,
    const fidl::String& label) {
  registry_->CreateViewTree(
      std::move(view_tree_request),
      mozart::ViewTreeListenerPtr::Create(std::move(view_tree_listener)),
      label);
}

// TODO(mikejurka): This should only be called by trusted code (ie launcher),
// once we have a security story.
void ViewManagerImpl::RegisterViewAssociate(
    fidl::InterfaceHandle<mozart::ViewAssociate> view_associate,
    fidl::InterfaceRequest<mozart::ViewAssociateOwner> view_associate_owner,
    const fidl::String& label) {
  registry_->RegisterViewAssociate(
      registry_, mozart::ViewAssociatePtr::Create(std::move(view_associate)),
      std::move(view_associate_owner), label);
}

void ViewManagerImpl::FinishedRegisteringViewAssociates() {
  registry_->FinishedRegisteringViewAssociates();
}

}  // namespace view_manager
