// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_A11Y_LIB_INPUT_INJECTION_TESTS_MOCKS_MOCK_INJECTOR_MANAGER_H_
#define SRC_UI_A11Y_LIB_INPUT_INJECTION_TESTS_MOCKS_MOCK_INJECTOR_MANAGER_H_

#include <unordered_map>
#include <vector>

#include "src/ui/a11y/lib/input_injection/injector_manager.h"

namespace accessibility_test {

class MockInjectorManager : public a11y::InjectorManagerInterface {
 public:
  MockInjectorManager() = default;
  ~MockInjectorManager() override = default;

  // |InjectorManager|
  bool InjectEventIntoView(fuchsia::ui::input::InputEvent& event, zx_koid_t koid) override;

  // |InjectorManager|
  bool MarkViewReadyForInjection(zx_koid_t koid, bool ready) override { return true; }

  // Returns a list of events received for the given koid.
  // Note that this method will return an empty vector for a koid for which no
  // events were received.
  const std::vector<fuchsia::ui::input::InputEvent>& GetEventsForKoid(zx_koid_t koid);

 private:
  std::map<zx_koid_t /* koid */, std::vector<fuchsia::ui::input::InputEvent> /* events for koid */>
      events_by_koid_;
};

}  // namespace accessibility_test

#endif  // SRC_UI_A11Y_LIB_INPUT_INJECTION_TESTS_MOCKS_MOCK_INJECTOR_MANAGER_H_
