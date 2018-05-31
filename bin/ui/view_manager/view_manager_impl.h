// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_UI_VIEW_MANAGER_VIEW_MANAGER_IMPL_H_
#define GARNET_BIN_UI_VIEW_MANAGER_VIEW_MANAGER_IMPL_H_

#include <fuchsia/ui/views/cpp/fidl.h>
#include "garnet/bin/ui/view_manager/view_registry.h"
#include "lib/fxl/macros.h"

namespace view_manager {

// ViewManager interface implementation.
class ViewManagerImpl : public ::fuchsia::ui::views_v1::ViewManager {
 public:
  explicit ViewManagerImpl(ViewRegistry* registry);
  ~ViewManagerImpl() override;

 private:
  // |ViewManager|:
  void GetScenic(fidl::InterfaceRequest<fuchsia::ui::scenic::Scenic>
                     scenic_request) override;
  void CreateView(
      fidl::InterfaceRequest<::fuchsia::ui::views_v1::View> view_request,
      fidl::InterfaceRequest<::fuchsia::ui::views_v1_token::ViewOwner> view_owner_request,
      fidl::InterfaceHandle<::fuchsia::ui::views_v1::ViewListener> view_listener,
      zx::eventpair parent_export_token, fidl::StringPtr label) override;
  void CreateViewTree(
      fidl::InterfaceRequest<::fuchsia::ui::views_v1::ViewTree> view_tree_request,
      fidl::InterfaceHandle<::fuchsia::ui::views_v1::ViewTreeListener> view_tree_listener,
      fidl::StringPtr label) override;

  ViewRegistry* registry_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ViewManagerImpl);
};

}  // namespace view_manager

#endif  // GARNET_BIN_UI_VIEW_MANAGER_VIEW_MANAGER_IMPL_H_
