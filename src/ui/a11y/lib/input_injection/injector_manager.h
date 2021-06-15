// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_A11Y_LIB_INPUT_INJECTION_INJECTOR_MANAGER_H_
#define SRC_UI_A11Y_LIB_INPUT_INJECTION_INJECTOR_MANAGER_H_

#include <fuchsia/ui/input/cpp/fidl.h>
#include <zircon/types.h>

namespace a11y {

class InjectorManagerInterface {
 public:
  InjectorManagerInterface() = default;
  virtual ~InjectorManagerInterface() = default;

  // Attempts to inject |event| from the a11y view into the view specified by
  // |koid|.
  // Returns true on success and false on error (e.g. if no injector exists for
  // |koid| or if the view is not ready for injection).
  virtual bool InjectEventIntoView(fuchsia::ui::input::InputEvent& event, zx_koid_t koid) = 0;

  // Marks the view with |koid| that is providing semantics ready / not ready for injection.
  virtual bool MarkViewReadyForInjection(zx_koid_t koid, bool ready) = 0;
};

}  // namespace a11y

#endif  // SRC_UI_A11Y_LIB_INPUT_INJECTION_INJECTOR_MANAGER_H_
