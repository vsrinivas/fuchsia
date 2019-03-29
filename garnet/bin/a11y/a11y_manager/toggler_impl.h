// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_A11Y_A11Y_MANAGER_TOGGLER_IMPL_H_
#define GARNET_BIN_A11Y_A11Y_MANAGER_TOGGLER_IMPL_H_

#include <fuchsia/accessibility/cpp/fidl.h>

#include "lib/fidl/cpp/binding.h"
#include "lib/fidl/cpp/binding_set.h"
#include "src/lib/fxl/logging.h"
#include "src/lib/fxl/macros.h"

namespace a11y_manager {

// Accessibility manager toggler interface implementation.
// See toggler.fidl for documentation.
class TogglerImpl : public fuchsia::accessibility::Toggler,
                    public fuchsia::accessibility::ToggleBroadcaster {
 public:
  TogglerImpl();
  ~TogglerImpl() = default;

  void AddTogglerBinding(
      fidl::InterfaceRequest<fuchsia::accessibility::Toggler> request);

  void AddToggleBroadcasterBinding(
      fidl::InterfaceRequest<fuchsia::accessibility::ToggleBroadcaster>
          request);

 private:
  // |fuchsia::accessibility::Toggler|
  // Sends an OnAccessibilityToggle event to every binding in
  // |broadcaster_bindings_|.
  void ToggleAccessibilitySupport(bool enabled) override;

  fidl::Binding<fuchsia::accessibility::Toggler> toggler_binding_;
  fidl::BindingSet<fuchsia::accessibility::ToggleBroadcaster>
      broadcaster_bindings_;

  // The current state of whether accessibility should be enabled.
  bool is_enabled_ = false;

  FXL_DISALLOW_COPY_AND_ASSIGN(TogglerImpl);
};

}  // namespace a11y_manager

#endif  // GARNET_BIN_A11Y_A11Y_MANAGER_TOGGLER_IMPL_H_
