// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_UI_VIEW_MANAGER_VIEW_IMPL_H_
#define GARNET_BIN_UI_VIEW_MANAGER_VIEW_IMPL_H_

#include <fuchsia/ui/views_v1/cpp/fidl.h>
#include "lib/fidl/cpp/binding_set.h"
#include "lib/fxl/macros.h"

namespace view_manager {

class ViewRegistry;
class ViewState;

// View interface implementation.
// This object is owned by its associated ViewState.
class ViewImpl : public ::fuchsia::ui::views_v1::View,
                 public ::fuchsia::ui::views_v1::ViewContainer,
                 public ::fuchsia::ui::views_v1_token::ViewOwner,
                 public component::ServiceProvider {
 public:
  ViewImpl(ViewRegistry* registry, ViewState* state);
  ~ViewImpl() override;

 private:
  // |View|:
  void GetToken(::fuchsia::ui::views_v1::View::GetTokenCallback callback) override;
  void GetServiceProvider(fidl::InterfaceRequest<component::ServiceProvider>
                              service_provider_request) override;
  void OfferServiceProvider(
      fidl::InterfaceHandle<component::ServiceProvider> service_provider,
      fidl::VectorPtr<fidl::StringPtr> service_names) override;
  void GetContainer(fidl::InterfaceRequest<::fuchsia::ui::views_v1::ViewContainer>
                        view_container_request) override;

  // |ViewContainer|:
  void SetListener(
      fidl::InterfaceHandle<::fuchsia::ui::views_v1::ViewContainerListener> listener) override;
  void AddChild(
      uint32_t child_key,
      fidl::InterfaceHandle<::fuchsia::ui::views_v1_token::ViewOwner> child_view_owner,
      zx::eventpair host_import_token) override;
  void RemoveChild(uint32_t child_key,
                   fidl::InterfaceRequest<::fuchsia::ui::views_v1_token::ViewOwner>
                       transferred_view_owner_request) override;
  void SetChildProperties(
      uint32_t child_key,
      ::fuchsia::ui::views_v1::ViewPropertiesPtr child_view_properties) override;
  void RequestFocus(uint32_t child_key) override;

  // |component::ServiceProvider|:
  void ConnectToService(fidl::StringPtr service_name,
                        zx::channel client_handle) override;

  ViewRegistry* const registry_;
  ViewState* const state_;
  fidl::BindingSet<component::ServiceProvider> service_provider_bindings_;
  fidl::BindingSet<::fuchsia::ui::views_v1::ViewContainer> container_bindings_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ViewImpl);
};

}  // namespace view_manager

#endif  // GARNET_BIN_UI_VIEW_MANAGER_VIEW_IMPL_H_
