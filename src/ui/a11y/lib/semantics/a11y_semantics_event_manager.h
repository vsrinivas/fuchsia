// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_A11Y_LIB_SEMANTICS_A11Y_SEMANTICS_EVENT_MANAGER_H_
#define SRC_UI_A11Y_LIB_SEMANTICS_A11Y_SEMANTICS_EVENT_MANAGER_H_

#include <zircon/types.h>

#include <list>

#include "src/ui/a11y/lib/semantics/semantics_event.h"
#include "src/ui/a11y/lib/semantics/semantics_event_manager.h"

namespace a11y {

// Collects semantics events on existing semantic trees and
// notifies registered listeners.
class A11ySemanticsEventManager : SemanticsEventManager {
 public:
  A11ySemanticsEventManager() = default;
  ~A11ySemanticsEventManager() override = default;

  // |SemanticsEventManager|
  void Register(fxl::WeakPtr<SemanticsEventListener> listener) override;

  // |SemanticsEventManager|
  void OnEvent(EventInfo event_info) override;

 private:
  // List of registered listeners.
  // NOTE: Using std::list as opposed to std::set to avoid
  // writing custom comparator/hash function for
  // fxl::WeakPtr<SemanticsEventListener>.
  std::list<fxl::WeakPtr<SemanticsEventListener>> listeners_;
};

}  // namespace a11y

#endif  // SRC_UI_A11Y_LIB_SEMANTICS_A11Y_SEMANTICS_EVENT_MANAGER_H_
