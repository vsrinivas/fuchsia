// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_TOOLS_PRESENT_VIEW_TESTING_FAKE_UNITTEST_VIEW_H_
#define SRC_UI_TOOLS_PRESENT_VIEW_TESTING_FAKE_UNITTEST_VIEW_H_

#include <fuchsia/ui/views/cpp/fidl.h>
#include <lib/sys/cpp/testing/fake_component.h>
#include <lib/sys/cpp/testing/fake_launcher.h>

#include <optional>
#include <utility>

#include "src/ui/tools/present_view/testing/fake_view.h"

namespace present_view::testing {

// This class can stand in for a |fuchsia::ui::app::ViewProvider| in unit tests.
// Normally a component which wants to be displayed by `scenic` vends this interface.
class FakeUnitTestView : public FakeView {
 public:
  explicit FakeUnitTestView(sys::testing::FakeLauncher& fake_launcher)
      : component_(std::in_place_t{}) {
    component_->Register(kFakeViewUri, fake_launcher);
    component_->AddPublicService<fuchsia::ui::app::ViewProvider>(
        [this](fidl::InterfaceRequest<fuchsia::ui::app::ViewProvider> request) {
          BindLegacy(std::move(request));
        });
    component_->AddPublicService<fuchsia::ui::views::View>(
        [this](fidl::InterfaceRequest<fuchsia::ui::views::View> request) {
          Bind(std::move(request));
        });
  }
  ~FakeUnitTestView() override = default;

  void Kill() {
    component_.reset();
    OnKill();
  }

 private:
  std::optional<sys::testing::FakeComponent> component_;
};

}  // namespace present_view::testing

#endif  // SRC_UI_TOOLS_PRESENT_VIEW_TESTING_FAKE_UNITTEST_VIEW_H_
