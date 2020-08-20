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

    // Store all release fences.
    pending_release_fences_[{session_id, next_present_id}] = std::move(release_fences);

    return next_present_id;
  }

  // For access from testing only.
  void ApplySessionUpdatesAndSignalFences() {
    uber_struct_system_->ForceUpdateAllSessions();

    for (auto& fences_kv : pending_release_fences_) {
      for (auto& event : fences_kv.second) {
        event.signal(0, ZX_EVENT_SIGNALED);
      }
    }

    pending_release_fences_.clear();
  }

 private:
  UberStructSystem* uber_struct_system_;
  std::unordered_map<scheduling::SchedulingIdPair, std::vector<zx::event>> pending_release_fences_;
};

}  // namespace flatland

#endif  // SRC_UI_SCENIC_LIB_FLATLAND_TESTS_MOCK_FLATLAND_PRESENTER_H_
