// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_FLATLAND_TESTS_MOCK_FLATLAND_PRESENTER_H_
#define SRC_UI_SCENIC_LIB_FLATLAND_TESTS_MOCK_FLATLAND_PRESENTER_H_

#include <gmock/gmock.h>

#include "src/ui/scenic/lib/flatland/flatland.h"
#include "src/ui/scenic/lib/flatland/flatland_presenter.h"
#include "src/ui/scenic/lib/flatland/uber_struct_system.h"

namespace flatland {

// Mock class of FlatlandPresenter for Flatland API testing.
class MockFlatlandPresenter : public FlatlandPresenter {
 public:
  MockFlatlandPresenter(UberStructSystem* uber_struct_system)
      : uber_struct_system_(uber_struct_system) {}

  // |FlatlandPresenter|
  scheduling::PresentId RegisterPresent(scheduling::SessionId session_id,
                                        std::vector<zx::event> release_fences) override {
    const auto next_present_id = scheduling::GetNextPresentId();

    // Only store the latest Present, since the real FrameScheduler only delivers the latest Present
    // to SessionUpdaters.
    pending_session_updates_[session_id] = next_present_id;
    return next_present_id;
  }

  // For access from testing only.
  void ApplySessionUpdates() {
    uber_struct_system_->UpdateSessions(pending_session_updates_);
    pending_session_updates_.clear();
  }

 private:
  UberStructSystem* uber_struct_system_;
  std::unordered_map<scheduling::SessionId, scheduling::PresentId> pending_session_updates_;
};

}  // namespace flatland

#endif  // SRC_UI_SCENIC_LIB_FLATLAND_TESTS_MOCK_FLATLAND_PRESENTER_H_
