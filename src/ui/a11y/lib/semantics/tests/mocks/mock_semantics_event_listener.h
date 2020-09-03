// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_A11Y_LIB_SEMANTICS_TESTS_MOCKS_MOCK_SEMANTICS_EVENT_LISTENER_H_
#define SRC_UI_A11Y_LIB_SEMANTICS_TESTS_MOCKS_MOCK_SEMANTICS_EVENT_LISTENER_H_

#include <fuchsia/ui/views/cpp/fidl.h>

#include <map>
#include <optional>

#include "src/ui/a11y/lib/semantics/semantics_event_listener.h"

namespace accessibility_test {

class MockSemanticsEventListener : public a11y::SemanticsEventListener {
 public:
  MockSemanticsEventListener()
      : listener_factory_(
            std::make_unique<fxl::WeakPtrFactory<a11y::SemanticsEventListener>>(this)) {}
  ~MockSemanticsEventListener() override { listener_factory_->InvalidateWeakPtrs(); }

  // |SemanticsEventListener|
  void OnEvent(a11y::EventInfo event_info) { events_received_.push_back(event_info); }

  // Returns a list of events in the order in which they were received.
  std::vector<a11y::EventInfo> GetReceivedEvents() { return events_received_; }

  fxl::WeakPtr<a11y::SemanticsEventListener> GetWeakPtr() {
    return listener_factory_->GetWeakPtr();
  }

 private:
  std::vector<a11y::EventInfo> events_received_;

  std::unique_ptr<fxl::WeakPtrFactory<a11y::SemanticsEventListener>> listener_factory_;
};

}  // namespace accessibility_test

#endif  // SRC_UI_A11Y_LIB_SEMANTICS_TESTS_MOCKS_MOCK_SEMANTICS_EVENT_LISTENER_H_
