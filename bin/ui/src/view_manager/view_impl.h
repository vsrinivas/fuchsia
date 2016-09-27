// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SERVICES_UI_VIEW_MANAGER_VIEW_IMPL_H_
#define SERVICES_UI_VIEW_MANAGER_VIEW_IMPL_H_

#include "apps/mozart/services/views/interfaces/views.mojom.h"
#include "lib/ftl/macros.h"
#include "mojo/public/cpp/bindings/binding_set.h"

namespace view_manager {

class ViewRegistry;
class ViewState;

// View interface implementation.
// This object is owned by its associated ViewState.
class ViewImpl : public mojo::ui::View,
                 public mojo::ui::ViewContainer,
                 public mojo::ui::ViewOwner,
                 public mojo::ServiceProvider {
 public:
  ViewImpl(ViewRegistry* registry, ViewState* state);
  ~ViewImpl() override;

 private:
  // |View|:
  void GetToken(const mojo::ui::View::GetTokenCallback& callback) override;
  void GetServiceProvider(mojo::InterfaceRequest<mojo::ServiceProvider>
                              service_provider_request) override;
  void CreateScene(
      mojo::InterfaceRequest<mojo::gfx::composition::Scene> scene) override;
  void GetContainer(mojo::InterfaceRequest<mojo::ui::ViewContainer>
                        view_container_request) override;
  void Invalidate() override;

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
  ViewState* const state_;
  mojo::BindingSet<mojo::ServiceProvider> service_provider_bindings_;
  mojo::BindingSet<mojo::ui::ViewContainer> container_bindings_;

  FTL_DISALLOW_COPY_AND_ASSIGN(ViewImpl);
};

}  // namespace view_manager

#endif  // SERVICES_UI_VIEW_MANAGER_VIEW_IMPL_H_
