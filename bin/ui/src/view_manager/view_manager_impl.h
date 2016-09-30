// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MOZART_SRC_VIEW_MANAGER_VIEW_MANAGER_IMPL_H_
#define APPS_MOZART_SRC_VIEW_MANAGER_VIEW_MANAGER_IMPL_H_

#include "apps/mozart/services/views/interfaces/view_manager.mojom.h"
#include "apps/mozart/src/view_manager/view_registry.h"
#include "lib/ftl/macros.h"

namespace view_manager {

// ViewManager interface implementation.
class ViewManagerImpl : public mozart::ViewManager {
 public:
  explicit ViewManagerImpl(ViewRegistry* registry);
  ~ViewManagerImpl() override;

 private:
  // |ViewManager|:
  void CreateView(mojo::InterfaceRequest<mozart::View> view_request,
                  mojo::InterfaceRequest<mozart::ViewOwner> view_owner_request,
                  mojo::InterfaceHandle<mozart::ViewListener> view_listener,
                  const mojo::String& label) override;
  void CreateViewTree(
      mojo::InterfaceRequest<mozart::ViewTree> view_tree_request,
      mojo::InterfaceHandle<mozart::ViewTreeListener> view_tree_listener,
      const mojo::String& label) override;
  void RegisterViewAssociate(
      mojo::InterfaceHandle<mozart::ViewAssociate> view_associate,
      mojo::InterfaceRequest<mozart::ViewAssociateOwner> view_associate_owner,
      const mojo::String& label) override;
  void FinishedRegisteringViewAssociates() override;

  ViewRegistry* registry_;

  FTL_DISALLOW_COPY_AND_ASSIGN(ViewManagerImpl);
};

}  // namespace view_manager

#endif  // APPS_MOZART_SRC_VIEW_MANAGER_VIEW_MANAGER_IMPL_H_
