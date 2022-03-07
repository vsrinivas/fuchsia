// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/tests/lib/view_provider_server.h"

#include <lib/sys/component/cpp/testing/realm_builder_types.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/syslog/cpp/macros.h>
#include <zircon/types.h>

#include <gtest/gtest.h>

using fuchsia::sys::ServiceProvider;
using fuchsia::ui::app::CreateView2Args;
using fuchsia::ui::app::ViewProvider;
using fuchsia::ui::views::ViewRef;
using fuchsia::ui::views::ViewRefControl;
using sys::testing::LocalComponent;
using sys::testing::LocalComponentHandles;

ViewProviderServer::ViewProviderServer(async_dispatcher_t* dispatcher) : dispatcher_(dispatcher) {}

void ViewProviderServer::Bind(fidl::InterfaceRequest<ViewProvider> request) {
  bindings_.AddBinding(this, std::move(request), dispatcher_);
}

void ViewProviderServer::SetCreateView2Callback(std::function<void(CreateView2Args)> callback) {
  create_view2_callback_ = std::move(callback);
}

void ViewProviderServer::Start(std::unique_ptr<LocalComponentHandles> local_handles) {
  // When this component starts, add a binding to the test.touch.ResponseListener
  // protocol to this component's outgoing directory.
  FX_CHECK(local_handles->outgoing()->AddPublicService(
               fidl::InterfaceRequestHandler<ViewProvider>([this](auto request) {
                 bindings_.AddBinding(this, std::move(request), dispatcher_);
               })) == ZX_OK);
  local_handles_.emplace_back(std::move(local_handles));
}

void ViewProviderServer::CreateView(::zx::eventpair token,
                                    ::fidl::InterfaceRequest<ServiceProvider> incoming_services,
                                    ::fidl::InterfaceHandle<ServiceProvider> outgoing_services) {
  FAIL() << "CreateView is not supported.";
}

void ViewProviderServer::CreateView2(CreateView2Args args) {
  create_view2_callback_(std::move(args));
}

void ViewProviderServer::CreateViewWithViewRef(::zx::eventpair token,
                                               ViewRefControl view_ref_control, ViewRef view_ref) {
  FAIL() << "CreateViewWithRef is not supported.";
}
