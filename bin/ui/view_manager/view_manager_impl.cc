// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/ui/view_manager/view_manager_impl.h"

#include <utility>

#include "garnet/bin/ui/view_manager/view_impl.h"
#include "garnet/bin/ui/view_manager/view_tree_impl.h"

namespace view_manager {

ViewManagerImpl::ViewManagerImpl(ViewRegistry* registry)
    : registry_(registry) {}

ViewManagerImpl::~ViewManagerImpl() {}

void ViewManagerImpl::GetScenic(
    f1dl::InterfaceRequest<ui::Scenic> scenic_request) {
  registry_->GetScenic(std::move(scenic_request));
}

void ViewManagerImpl::CreateView(
    f1dl::InterfaceRequest<mozart::View> view_request,
    f1dl::InterfaceRequest<mozart::ViewOwner> view_owner_request,
    f1dl::InterfaceHandle<mozart::ViewListener> view_listener,
    zx::eventpair parent_export_token,
    const f1dl::String& label) {
  registry_->CreateView(std::move(view_request), std::move(view_owner_request),
                        view_listener.Bind(), std::move(parent_export_token),
                        label);
}

void ViewManagerImpl::CreateViewTree(
    f1dl::InterfaceRequest<mozart::ViewTree> view_tree_request,
    f1dl::InterfaceHandle<mozart::ViewTreeListener> view_tree_listener,
    const f1dl::String& label) {
  registry_->CreateViewTree(std::move(view_tree_request),
                            view_tree_listener.Bind(), label);
}

}  // namespace view_manager
