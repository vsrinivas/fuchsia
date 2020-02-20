// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_BIN_ROOT_PRESENTER_TESTS_FAKES_FAKE_FOCUSER_H_
#define SRC_UI_BIN_ROOT_PRESENTER_TESTS_FAKES_FAKE_FOCUSER_H_

#include <fuchsia/ui/views/cpp/fidl.h>
#include <fuchsia/ui/views/cpp/fidl_test_base.h>
#include <lib/fidl/cpp/binding.h>

namespace root_presenter {
namespace testing {

class FakeFocuser : public fuchsia::ui::views::testing::Focuser_TestBase {
 public:
  FakeFocuser();
  ~FakeFocuser() override;

  void Bind(fidl::InterfaceRequest<fuchsia::ui::views::Focuser> request);

  void NotImplemented_(const std::string& name) final {}

  // Focuser implementation.
  void RequestFocus(fuchsia::ui::views::ViewRef view_ref, RequestFocusCallback callback) override;

  // Test methods.
 private:
  fidl::Binding<fuchsia::ui::views::Focuser> binding_;
};

}  // namespace testing
}  // namespace root_presenter

#endif  // SRC_UI_BIN_ROOT_PRESENTER_TESTS_FAKES_FAKE_FOCUSER_H_
