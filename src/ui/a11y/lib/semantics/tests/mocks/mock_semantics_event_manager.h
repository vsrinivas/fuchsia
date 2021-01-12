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
    events_received_.push_back(std::move(event_info));
    if (listener_) {
      listener_->OnEvent(events_received_.back().Clone());
    }
  }

  // |SemanticsEventManager|
  void Register(fxl::WeakPtr<a11y::SemanticsEventListener> listener) override {
    listener_ = std::move(listener);
  }

  // Returns a list of events in the order in which they were received.
  const std::vector<a11y::SemanticsEventInfo>& GetReceivedEvents() const {
    return events_received_;
  }

 private:
  std::vector<a11y::SemanticsEventInfo> events_received_;
  fxl::WeakPtr<a11y::SemanticsEventListener> listener_;
};

}  // namespace accessibility_test

#endif  // SRC_UI_A11Y_LIB_SEMANTICS_TESTS_MOCKS_MOCK_SEMANTICS_EVENT_MANAGER_H_
