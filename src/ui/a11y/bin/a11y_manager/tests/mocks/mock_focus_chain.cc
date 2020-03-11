// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/bin/a11y_manager/tests/mocks/mock_focus_chain.h"

#include <lib/sys/cpp/testing/component_context_provider.h>

namespace accessibility_test {

MockFocusChain::MockFocusChain(sys::testing::ComponentContextProvider* context)
    : focuser_binding_(this) {
  context->service_directory_provider()->AddService(focuser_registry_bindings_.GetHandler(this));
  context->service_directory_provider()->AddService(listener_registry_bindings_.GetHandler(this));
}

void MockFocusChain::RegisterFocuser(
    fidl::InterfaceRequest<fuchsia::ui::views::Focuser> view_focuser) {
  focuser_binding_.Bind(std::move(view_focuser));
}

void MockFocusChain::Register(
    fidl::InterfaceHandle<fuchsia::ui::focus::FocusChainListener> listener) {
  listener_.Bind(std::move(listener));
}

void MockFocusChain::RequestFocus(fuchsia::ui::views::ViewRef view_ref,
                                  RequestFocusCallback callback) {
  fuchsia::ui::views::Focuser_RequestFocus_Result result;
  result.set_response(fuchsia::ui::views::Focuser_RequestFocus_Response{});
  callback(std::move(result));
  if (listener_) {
    // Updates the Focus Chain about the new view in focus.
    fuchsia::ui::focus::FocusChain focus_chain;
    focus_chain.mutable_focus_chain()->push_back(std::move(view_ref));
    listener_->OnFocusChange(std::move(focus_chain), []() {});
  }
}

}  // namespace accessibility_test
