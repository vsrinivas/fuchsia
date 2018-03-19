// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_UI_VIEW_MANAGER_VIEW_MANAGER_IMPL_H_
#define GARNET_BIN_UI_VIEW_MANAGER_VIEW_MANAGER_IMPL_H_

#include "garnet/bin/ui/view_manager/view_registry.h"
#include "lib/fxl/macros.h"
#include "lib/ui/views/fidl/view_manager.fidl.h"

namespace view_manager {

// ViewManager interface implementation.
class ViewManagerImpl : public mozart::ViewManager {
 public:
  explicit ViewManagerImpl(ViewRegistry* registry);
  ~ViewManagerImpl() override;

 private:
  // |ViewManager|:
  void GetScenic(f1dl::InterfaceRequest<ui::Scenic> scenic_request) override;
  void CreateView(f1dl::InterfaceRequest<mozart::View> view_request,
                  f1dl::InterfaceRequest<mozart::ViewOwner> view_owner_request,
                  f1dl::InterfaceHandle<mozart::ViewListener> view_listener,
                  zx::eventpair parent_export_token,
                  const f1dl::StringPtr& label) override;
  void CreateViewTree(
      f1dl::InterfaceRequest<mozart::ViewTree> view_tree_request,
      f1dl::InterfaceHandle<mozart::ViewTreeListener> view_tree_listener,
      const f1dl::StringPtr& label) override;

  ViewRegistry* registry_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ViewManagerImpl);
};

}  // namespace view_manager

#endif  // GARNET_BIN_UI_VIEW_MANAGER_VIEW_MANAGER_IMPL_H_
