// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_A11Y_LIB_VIEW_TESTS_MOCKS_MOCK_VIEW_INJECTOR_FACTORY_H_
#define SRC_UI_A11Y_LIB_VIEW_TESTS_MOCKS_MOCK_VIEW_INJECTOR_FACTORY_H_

#include <fuchsia/ui/views/cpp/fidl.h>
#include <lib/sys/cpp/component_context.h>

#include <memory>

#include "src/ui/a11y/lib/view/a11y_view.h"
#include "src/ui/a11y/lib/view/view_injector_factory.h"
#include "src/ui/input/lib/injector/injector.h"

namespace accessibility_test {

class MockViewInjectorFactory : public a11y::ViewInjectorFactoryInterface {
 public:
  MockViewInjectorFactory() = default;
  ~MockViewInjectorFactory() override = default;

  // Sets the injector that will be returned by this factory when |BuildAndConfigureInjector| is
  // called.
  void set_injector(std::shared_ptr<input::Injector> injector) { injector_ = std::move(injector); }

  //  |ViewInjectorFactoryInterface|
  std::shared_ptr<input::Injector> BuildAndConfigureInjector(
      a11y::AccessibilityViewInterface* a11y_view, sys::ComponentContext* component_context,
      fuchsia::ui::views::ViewRef context, fuchsia::ui::views::ViewRef target) override;

 private:
  std::shared_ptr<input::Injector> injector_;
};

}  // namespace accessibility_test

#endif  // SRC_UI_A11Y_LIB_VIEW_TESTS_MOCKS_MOCK_VIEW_INJECTOR_FACTORY_H_
