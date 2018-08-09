// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_A11Y_TESTS_MOCKS_MOCK_TOUCH_DISPATCHER_H_
#define GARNET_BIN_A11Y_TESTS_MOCKS_MOCK_TOUCH_DISPATCHER_H_

#include <fuchsia/accessibility/cpp/fidl.h>
#include <fuchsia/ui/input/cpp/fidl.h>
#include <fuchsia/ui/viewsv1/cpp/fidl.h>

#include "lib/component/cpp/testing/fake_component.h"
#include "lib/fidl/cpp/binding.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/macros.h"

namespace accessibility_test {

using OnSimulatedPointerEventCallback =
    fit::function<void(fuchsia::ui::input::PointerEvent event)>;

class MockTouchDispatcher : public fuchsia::accessibility::TouchDispatcher {
 public:
  MockTouchDispatcher() : callback_(nullptr), binding_(this) {}
  ~MockTouchDispatcher() = default;

  void Bind(
      fidl::InterfaceRequest<fuchsia::accessibility::TouchDispatcher> request);

  void SendPointerEventToClient(fuchsia::ui::input::PointerEvent event);

  OnSimulatedPointerEventCallback callback_;
  fidl::Binding<fuchsia::accessibility::TouchDispatcher> binding_;

 private:
  void SendSimulatedPointerEvent(
      fuchsia::ui::input::PointerEvent event) override;

  FXL_DISALLOW_COPY_AND_ASSIGN(MockTouchDispatcher);
};

}  // namespace accessibility_test

#endif  // GARNET_BIN_A11Y_TESTS_MOCKS_MOCK_TOUCH_DISPATCHER_H_