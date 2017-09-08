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

void ViewManagerImpl::GetSceneManager(
    fidl::InterfaceRequest<scenic::SceneManager> scene_manager_request) {
  registry_->GetSceneManager(std::move(scene_manager_request));
}

void ViewManagerImpl::CreateView(
    fidl::InterfaceRequest<mozart::View> view_request,
    fidl::InterfaceRequest<mozart::ViewOwner> view_owner_request,
    fidl::InterfaceHandle<mozart::ViewListener> view_listener,
    mx::eventpair parent_export_token,
    const fidl::String& label) {
  registry_->CreateView(
      std::move(view_request), std::move(view_owner_request),
      mozart::ViewListenerPtr::Create(std::move(view_listener)),
      std::move(parent_export_token), label);
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

}  // namespace view_manager
