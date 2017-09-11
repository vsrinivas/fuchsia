// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MOZART_SRC_VIEW_MANAGER_VIEW_MANAGER_IMPL_H_
#define APPS_MOZART_SRC_VIEW_MANAGER_VIEW_MANAGER_IMPL_H_

#include "lib/ui/views/fidl/view_manager.fidl.h"
#include "garnet/bin/ui/view_manager/view_registry.h"
#include "lib/fxl/macros.h"

namespace view_manager {

// ViewManager interface implementation.
class ViewManagerImpl : public mozart::ViewManager {
 public:
  explicit ViewManagerImpl(ViewRegistry* registry);
  ~ViewManagerImpl() override;

 private:
  // |ViewManager|:
  void GetSceneManager(fidl::InterfaceRequest<scenic::SceneManager>
                           scene_manager_request) override;
  void CreateView(fidl::InterfaceRequest<mozart::View> view_request,
                  fidl::InterfaceRequest<mozart::ViewOwner> view_owner_request,
                  fidl::InterfaceHandle<mozart::ViewListener> view_listener,
                  mx::eventpair parent_export_token,
                  const fidl::String& label) override;
  void CreateViewTree(
      fidl::InterfaceRequest<mozart::ViewTree> view_tree_request,
      fidl::InterfaceHandle<mozart::ViewTreeListener> view_tree_listener,
      const fidl::String& label) override;

  ViewRegistry* registry_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ViewManagerImpl);
};

}  // namespace view_manager

#endif  // APPS_MOZART_SRC_VIEW_MANAGER_VIEW_MANAGER_IMPL_H_
