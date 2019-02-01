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
    fidl::InterfaceRequest<fuchsia::ui::scenic::Scenic> scenic_request) {
  registry_->GetScenic(std::move(scenic_request));
}

void ViewManagerImpl::CreateView(
    fidl::InterfaceRequest<::fuchsia::ui::viewsv1::View> view_request,
    fidl::InterfaceRequest<::fuchsia::ui::viewsv1token::ViewOwner>
        view_owner_request,
    fidl::InterfaceHandle<::fuchsia::ui::viewsv1::ViewListener> view_listener,
    zx::eventpair parent_export_token, fidl::StringPtr label) {
  // "Cast" the ViewOwner channel endpoint to an eventpair endpoint.  Should
  // work for the time being while this interface is being deprecated.
  // TODO(SCN-1018): Remove this along with the interface.
  CreateView2(std::move(view_request),
              zx::eventpair(view_owner_request.TakeChannel().release()),
              view_listener.Bind(), std::move(parent_export_token), label);
}

void ViewManagerImpl::CreateView2(
    fidl::InterfaceRequest<::fuchsia::ui::viewsv1::View> view_request,
    zx::eventpair view_token,
    fidl::InterfaceHandle<::fuchsia::ui::viewsv1::ViewListener> view_listener,
    zx::eventpair parent_export_token, fidl::StringPtr label) {
  registry_->CreateView(std::move(view_request), std::move(view_token),
                        view_listener.Bind(), std::move(parent_export_token),
                        label);
}

void ViewManagerImpl::CreateViewTree(
    fidl::InterfaceRequest<::fuchsia::ui::viewsv1::ViewTree> view_tree_request,
    fidl::InterfaceHandle<::fuchsia::ui::viewsv1::ViewTreeListener>
        view_tree_listener,
    fidl::StringPtr label) {
  registry_->CreateViewTree(std::move(view_tree_request),
                            view_tree_listener.Bind(), label);
}

}  // namespace view_manager
