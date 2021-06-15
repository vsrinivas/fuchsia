// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_A11Y_LIB_VIEW_VIEW_INJECTOR_FACTORY_H_
#define SRC_UI_A11Y_LIB_VIEW_VIEW_INJECTOR_FACTORY_H_

#include <fuchsia/ui/views/cpp/fidl.h>
#include <lib/sys/cpp/component_context.h>

#include <memory>

#include "src/ui/a11y/lib/view/a11y_view.h"
#include "src/ui/input/lib/injector/injector.h"

namespace a11y {

// Interface of factory that can build injectors configured for a11y.
class ViewInjectorFactoryInterface {
 public:
  ViewInjectorFactoryInterface() = default;
  virtual ~ViewInjectorFactoryInterface() = default;

  // Builds and configures an injector with |context| as its context view, with an exclusive
  // injection policy into |target|. |a11y_view| is used to set the view port of the injector.
  // Please see input::Injector for a full documentation.
  virtual std::shared_ptr<input::Injector> BuildAndConfigureInjector(
      AccessibilityViewInterface* a11y_view, sys::ComponentContext* component_context,
      fuchsia::ui::views::ViewRef context, fuchsia::ui::views::ViewRef target) = 0;
};

class ViewInjectorFactory : public ViewInjectorFactoryInterface {
 public:
  ViewInjectorFactory() = default;
  ~ViewInjectorFactory() override = default;

  //  |ViewInjectorFactoryInterface|
  std::shared_ptr<input::Injector> BuildAndConfigureInjector(
      AccessibilityViewInterface* a11y_view, sys::ComponentContext* component_context,
      fuchsia::ui::views::ViewRef context, fuchsia::ui::views::ViewRef target) override;
};

}  // namespace a11y

#endif  // SRC_UI_A11Y_LIB_VIEW_VIEW_INJECTOR_FACTORY_H_
