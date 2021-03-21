// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_TOOLS_PRESENT_VIEW_TESTING_FAKE_INTEGRATION_TEST_VIEW_H_
#define SRC_UI_TOOLS_PRESENT_VIEW_TESTING_FAKE_INTEGRATION_TEST_VIEW_H_

#include <fuchsia/sys/cpp/fidl.h>
#include <fuchsia/ui/views/cpp/fidl.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/sys/cpp/testing/component_interceptor.h>

#include "src/ui/tools/present_view/testing/fake_view.h"

namespace present_view::testing {

// This class can stand in for a |fuchsia::ui::app::ViewProvider| in integration tests.
// Normally a component which wants to be displayed by `scenic` vends this interface.
class FakeIntegrationTestView : public FakeView {
 public:
  FakeIntegrationTestView(fuchsia::sys::StartupInfo startup_info,
                          std::unique_ptr<sys::testing::InterceptedComponent> intercepted_component)
      : component_context_(sys::ServiceDirectory::CreateFromNamespace(),
                           std::move(startup_info.launch_info.directory_request)),
        intercepted_component_(std::move(intercepted_component)) {
    component_context_.outgoing()->AddPublicService<fuchsia::ui::app::ViewProvider>(
        [this](fidl::InterfaceRequest<fuchsia::ui::app::ViewProvider> request) {
          BindLegacy(std::move(request));
        });
    component_context_.outgoing()->AddPublicService<fuchsia::ui::views::View>(
        [this](fidl::InterfaceRequest<fuchsia::ui::views::View> request) {
          Bind(std::move(request));
        });
    intercepted_component_->set_on_kill([this]() { OnKill(); });
  }

  ~FakeIntegrationTestView() override {
    component_context_.outgoing()->RemovePublicService<fuchsia::ui::views::View>();
    component_context_.outgoing()->RemovePublicService<fuchsia::ui::app::ViewProvider>();
  }

 private:
  sys::ComponentContext component_context_;

  std::unique_ptr<sys::testing::InterceptedComponent> intercepted_component_;
};

}  // namespace present_view::testing

#endif  // SRC_UI_TOOLS_PRESENT_VIEW_TESTING_FAKE_INTEGRATION_TEST_VIEW_H_
