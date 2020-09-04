// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_A11Y_LIB_SEMANTICS_TESTS_MOCKS_MOCK_SEMANTICS_EVENT_MANAGER_H_
#define SRC_UI_A11Y_LIB_SEMANTICS_TESTS_MOCKS_MOCK_SEMANTICS_EVENT_MANAGER_H_

#include <fuchsia/ui/views/cpp/fidl.h>

#include <map>
#include <optional>

#include "src/ui/a11y/lib/semantics/semantics_event_manager.h"

namespace accessibility_test {

class MockSemanticsEventManager : public a11y::SemanticsEventManager {
 public:
  MockSemanticsEventManager() = default;
  ~MockSemanticsEventManager() override = default;

  // |SemanticsEventManager|
  void OnEvent(a11y::SemanticsEventInfo event_info) override {
    events_received_.push_back(event_info);
  }

  // |SemanticsEventManager|
  // NOTE: The mock event manager is used to test upstream producers of events
  // (e.g. semantic trees), so we don't need to worry about registering
  // listeners.
  void Register(fxl::WeakPtr<a11y::SemanticsEventListener> listener) override {}

  // Returns a list of events in the order in which they were received.
  std::vector<a11y::SemanticsEventInfo> GetReceivedEvents() { return events_received_; }

 private:
  std::vector<a11y::SemanticsEventInfo> events_received_;
};

}  // namespace accessibility_test

#endif  // SRC_UI_A11Y_LIB_SEMANTICS_TESTS_MOCKS_MOCK_SEMANTICS_EVENT_MANAGER_H_
