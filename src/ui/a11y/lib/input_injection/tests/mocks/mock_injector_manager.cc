// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/input_injection/tests/mocks/mock_injector_manager.h"

namespace accessibility_test {

bool MockInjectorManager::InjectEventIntoView(fuchsia::ui::input::InputEvent& event,
                                              zx_koid_t koid) {
  auto& events_for_koid = events_by_koid_[koid];
  events_for_koid.emplace_back(std::move(event));
  return true;
}

const std::vector<fuchsia::ui::input::InputEvent>& MockInjectorManager::GetEventsForKoid(
    zx_koid_t koid) {
  return events_by_koid_[koid];
}

}  // namespace accessibility_test
