// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SERVICES_UI_VIEW_MANAGER_VIEW_MANAGER_IMPL_H_
#define SERVICES_UI_VIEW_MANAGER_VIEW_MANAGER_IMPL_H_

#include "lib/ftl/macros.h"
#include "apps/mozart/services/views/interfaces/view_manager.mojom.h"
#include "apps/mozart/src/view_manager/view_registry.h"

namespace view_manager {

// ViewManager interface implementation.
class ViewManagerImpl : public mojo::ui::ViewManager {
 public:
  explicit ViewManagerImpl(ViewRegistry* registry);
  ~ViewManagerImpl() override;

 private:
  // |ViewManager|:
  void CreateView(
      mojo::InterfaceRequest<mojo::ui::View> view_request,
      mojo::InterfaceRequest<mojo::ui::ViewOwner> view_owner_request,
      mojo::InterfaceHandle<mojo::ui::ViewListener> view_listener,
      const mojo::String& label) override;
  void CreateViewTree(
      mojo::InterfaceRequest<mojo::ui::ViewTree> view_tree_request,
      mojo::InterfaceHandle<mojo::ui::ViewTreeListener> view_tree_listener,
      const mojo::String& label) override;
  void RegisterViewAssociate(
      mojo::InterfaceHandle<mojo::ui::ViewAssociate> view_associate,
      mojo::InterfaceRequest<mojo::ui::ViewAssociateOwner> view_associate_owner,
      const mojo::String& label) override;
  void FinishedRegisteringViewAssociates() override;

  ViewRegistry* registry_;

  FTL_DISALLOW_COPY_AND_ASSIGN(ViewManagerImpl);
};

}  // namespace view_manager

#endif  // SERVICES_UI_VIEW_MANAGER_VIEW_MANAGER_IMPL_H_
