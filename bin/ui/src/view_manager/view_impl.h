// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MOZART_SRC_VIEW_MANAGER_VIEW_IMPL_H_
#define APPS_MOZART_SRC_VIEW_MANAGER_VIEW_IMPL_H_

#include "apps/mozart/services/views/views.fidl.h"
#include "lib/ftl/macros.h"
#include "lib/fidl/cpp/bindings/binding_set.h"

namespace view_manager {

class ViewRegistry;
class ViewState;

// View interface implementation.
// This object is owned by its associated ViewState.
class ViewImpl : public mozart::View,
                 public mozart::ViewContainer,
                 public mozart::ViewOwner,
                 public modular::ServiceProvider {
 public:
  ViewImpl(ViewRegistry* registry, ViewState* state);
  ~ViewImpl() override;

 private:
  // |View|:
  void GetToken(const mozart::View::GetTokenCallback& callback) override;
  void GetServiceProvider(fidl::InterfaceRequest<modular::ServiceProvider>
                              service_provider_request) override;
  void CreateScene(fidl::InterfaceRequest<mozart::Scene> scene) override;
  void GetContainer(fidl::InterfaceRequest<mozart::ViewContainer>
                        view_container_request) override;
  void Invalidate() override;

  // |ViewContainer|:
  void SetListener(
      fidl::InterfaceHandle<mozart::ViewContainerListener> listener) override;
  void AddChild(
      uint32_t child_key,
      fidl::InterfaceHandle<mozart::ViewOwner> child_view_owner) override;
  void RemoveChild(uint32_t child_key,
                   fidl::InterfaceRequest<mozart::ViewOwner>
                       transferred_view_owner_request) override;
  void SetChildProperties(
      uint32_t child_key,
      uint32_t child_scene_version,
      mozart::ViewPropertiesPtr child_view_properties) override;
  void FlushChildren(uint32_t flush_token) override;

  // |modular::ServiceProvider|:
  void ConnectToService(const fidl::String& service_name,
                        mx::channel client_handle) override;

  ViewRegistry* const registry_;
  ViewState* const state_;
  fidl::BindingSet<modular::ServiceProvider> service_provider_bindings_;
  fidl::BindingSet<mozart::ViewContainer> container_bindings_;

  FTL_DISALLOW_COPY_AND_ASSIGN(ViewImpl);
};

}  // namespace view_manager

#endif  // APPS_MOZART_SRC_VIEW_MANAGER_VIEW_IMPL_H_
