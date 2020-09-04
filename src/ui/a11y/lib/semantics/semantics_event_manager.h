// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_A11Y_LIB_SEMANTICS_SEMANTICS_EVENT_MANAGER_H_
#define SRC_UI_A11Y_LIB_SEMANTICS_SEMANTICS_EVENT_MANAGER_H_

#include <zircon/types.h>

#include <optional>

#include "src/lib/fxl/memory/weak_ptr.h"
#include "src/ui/a11y/lib/semantics/semantics_event.h"
#include "src/ui/a11y/lib/semantics/semantics_event_listener.h"

namespace a11y {

// An interface for collecting semantics events on existing semantic trees and
// notifying registered listeners.
class SemanticsEventManager {
 public:
  SemanticsEventManager() = default;
  virtual ~SemanticsEventManager() = default;

  // Semantics consumers can use this method to register to receive notifications
  // when semantics events occur.
  // NOTE: If |listener| becomes invalid, it is automatically removed from the list of registered
  // listeners.
  virtual void Register(fxl::WeakPtr<SemanticsEventListener> listener) = 0;

  // This method is called when a semantics event is detected (e.g. when a provider
  // successfully calls |SemanticTreeService::Commit|. This method will
  // call |SemanticsEventListener::OnEvent| for each registered
  // semantics listener.
  virtual void OnEvent(SemanticsEventInfo event_info) = 0;
};

}  // namespace a11y

#endif  // SRC_UI_A11Y_LIB_SEMANTICS_SEMANTICS_EVENT_MANAGER_H_
