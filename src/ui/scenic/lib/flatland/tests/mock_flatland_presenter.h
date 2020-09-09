// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_FLATLAND_TESTS_MOCK_FLATLAND_PRESENTER_H_
#define SRC_UI_SCENIC_LIB_FLATLAND_TESTS_MOCK_FLATLAND_PRESENTER_H_

#include <gmock/gmock.h>
#include <gtest/gtest.h>

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

  // |FlatlandPresenter|
  void ScheduleUpdateForSession(scheduling::SchedulingIdPair id_pair) override {
    // The ID must be already registered.
    ASSERT_TRUE(pending_release_fences_.find(id_pair) != pending_release_fences_.end());

    // Ensure IDs are strictly increasing.
    auto current_id_kv = pending_session_updates_.find(id_pair.session_id);
    ASSERT_TRUE(current_id_kv == pending_session_updates_.end() ||
                current_id_kv->second < id_pair.present_id);

    // Only save the latest PresentId: the UberStructSystem will flush all Presents prior to it.
    pending_session_updates_[id_pair.session_id] = id_pair.present_id;
  }

  // Applies the most recently scheduled session update for each session and signals the release
  // fences of all Presents up to and including that update.
  void ApplySessionUpdatesAndSignalFences() {
    uber_struct_system_->UpdateSessions(pending_session_updates_);

    // Signal all release fences up to and including the PresentId in |pending_session_updates_|.
    for (const auto& [session_id, present_id] : pending_session_updates_) {
      auto begin = pending_release_fences_.lower_bound({session_id, 0});
      auto end = pending_release_fences_.upper_bound({session_id, present_id});
      for (auto fences_kv = begin; fences_kv != end; ++fences_kv) {
        for (auto& event : fences_kv->second) {
          event.signal(0, ZX_EVENT_SIGNALED);
        }
      }
      pending_release_fences_.erase(begin, end);
    }

    pending_session_updates_.clear();
  }

  // Gets the list of registered PresentIds for a particular |session_id|.
  std::vector<scheduling::PresentId> GetRegisteredPresents(scheduling::SessionId session_id) const {
    std::vector<scheduling::PresentId> present_ids;

    auto begin = pending_release_fences_.lower_bound({session_id, 0});
    auto end = pending_release_fences_.upper_bound({session_id + 1, 0});
    for (auto fence_kv = begin; fence_kv != end; ++fence_kv) {
      present_ids.push_back(fence_kv->first.present_id);
    }

    return present_ids;
  }

 private:
  UberStructSystem* uber_struct_system_;
  std::map<scheduling::SchedulingIdPair, std::vector<zx::event>> pending_release_fences_;
  std::unordered_map<scheduling::SessionId, scheduling::PresentId> pending_session_updates_;
};

}  // namespace flatland

#endif  // SRC_UI_SCENIC_LIB_FLATLAND_TESTS_MOCK_FLATLAND_PRESENTER_H_
