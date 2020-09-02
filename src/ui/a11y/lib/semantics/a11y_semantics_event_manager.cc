// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/semantics/a11y_semantics_event_manager.h"

#include <lib/syslog/cpp/macros.h>

#include <algorithm>
#include <optional>

namespace a11y {

void A11ySemanticsEventManager::Register(fxl::WeakPtr<SemanticsEventListener> listener) {
  if (!listener) {
    return;
  }

  auto it = std::find_if(listeners_.begin(), listeners_.end(),
                         [listener](const fxl::WeakPtr<SemanticsEventListener> lhs) {
                           return lhs.get() == listener.get();
                         });

  if (it != listeners_.end()) {
    FX_LOGS(INFO)
        << "A11ySemanticsEventManager::Register: Attempted to re-register existing listener.";
    return;
  }

  listeners_.push_back(listener);
}

void A11ySemanticsEventManager::OnEvent(EventInfo event_info) {
  std::list<fxl::WeakPtr<SemanticsEventListener>> valid_listeners;
  for (auto it = listeners_.begin(); it != listeners_.end(); ++it) {
    // If listener pointer is no longer valid, remove it from the set of
    // registered listeners.
    if (!*it) {
      continue;
    }

    (*it)->OnEvent(event_info);
    valid_listeners.emplace_back(*it);
  }

  listeners_ = std::move(valid_listeners);
}

}  //  namespace a11y
