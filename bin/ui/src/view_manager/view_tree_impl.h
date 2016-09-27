// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SERVICES_UI_VIEW_MANAGER_VIEW_TREE_IMPL_H_
#define SERVICES_UI_VIEW_MANAGER_VIEW_TREE_IMPL_H_

#include "apps/mozart/services/views/interfaces/view_trees.mojom.h"
#include "lib/ftl/macros.h"
#include "mojo/public/cpp/bindings/binding_set.h"

namespace view_manager {

class ViewRegistry;
class ViewTreeState;

// ViewTree interface implementation.
// This object is owned by its associated ViewTreeState.
class ViewTreeImpl : public mojo::ui::ViewTree,
                     public mojo::ui::ViewContainer,
                     public mojo::ServiceProvider {
 public:
  ViewTreeImpl(ViewRegistry* registry, ViewTreeState* state);
  ~ViewTreeImpl() override;

 private:
  // |ViewTree|:
  void GetToken(const GetTokenCallback& callback) override;
  void GetServiceProvider(
      mojo::InterfaceRequest<mojo::ServiceProvider> service_provider) override;
  void SetRenderer(mojo::InterfaceHandle<mojo::gfx::composition::Renderer>
                       renderer) override;
  void GetContainer(mojo::InterfaceRequest<mojo::ui::ViewContainer>
                        view_container_request) override;

  // |ViewContainer|:
  void SetListener(
      mojo::InterfaceHandle<mojo::ui::ViewContainerListener> listener) override;
  void AddChild(
      uint32_t child_key,
      mojo::InterfaceHandle<mojo::ui::ViewOwner> child_view_owner) override;
  void RemoveChild(uint32_t child_key,
                   mojo::InterfaceRequest<mojo::ui::ViewOwner>
                       transferred_view_owner_request) override;
  void SetChildProperties(
      uint32_t child_key,
      uint32_t child_scene_version,
      mojo::ui::ViewPropertiesPtr child_view_properties) override;
  void FlushChildren(uint32_t flush_token) override;

  // |ServiceProvider|:
  void ConnectToService(const mojo::String& service_name,
                        mojo::ScopedMessagePipeHandle client_handle) override;

  ViewRegistry* const registry_;
  ViewTreeState* const state_;
  mojo::BindingSet<mojo::ServiceProvider> service_provider_bindings_;
  mojo::BindingSet<mojo::ui::ViewContainer> container_bindings_;

  FTL_DISALLOW_COPY_AND_ASSIGN(ViewTreeImpl);
};

}  // namespace view_manager

#endif  // SERVICES_UI_VIEW_MANAGER_VIEW_TREE_IMPL_H_
