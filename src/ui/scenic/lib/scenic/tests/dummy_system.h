// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_SCENIC_TESTS_DUMMY_SYSTEM_H_
#define SRC_UI_SCENIC_LIB_SCENIC_TESTS_DUMMY_SYSTEM_H_

#include "src/ui/scenic/lib/scenic/system.h"

namespace scenic_impl {
namespace test {

class DummySystem : public System {
 public:
  static constexpr TypeId kTypeId = kDummySystem;
  static const char* kName;

  explicit DummySystem(SystemContext context);

  ~DummySystem() override;

  CommandDispatcherUniquePtr CreateCommandDispatcher(
      scheduling::SessionId session_id, std::shared_ptr<EventReporter> event_reporter,
      std::shared_ptr<ErrorReporter> error_reporter) override;

  scheduling::SessionUpdater::UpdateResults UpdateSessions(
      const std::unordered_map<scheduling::SessionId, scheduling::PresentId>& sessions_to_update,
      uint64_t frame_trace_id,
      fit::function<void(scheduling::SessionId)> destroy_session) override {
    for (auto session_id : update_sessions_return_value_.sessions_with_failed_updates) {
      destroy_session(session_id);
    }
    return update_sessions_return_value_;
  }

  uint32_t GetNumDispatchers() { return num_dispatchers_; }

  int64_t GetLastSessionId() { return last_session_; }

  void SetUpdateSessionsReturnValue(scheduling::SessionUpdater::UpdateResults results) {
    update_sessions_return_value_ = results;
  }

 private:
  uint32_t num_dispatchers_ = 0;
  int64_t last_session_ = -1;

  scheduling::SessionUpdater::UpdateResults update_sessions_return_value_;
};

class DummyCommandDispatcher : public CommandDispatcher {
 public:
  DummyCommandDispatcher() = default;
  ~DummyCommandDispatcher() override = default;

  // |CommandDispatcher|
  void SetDebugName(const std::string& debug_name) override {}

  // |CommandDispatcher|
  void DispatchCommand(const fuchsia::ui::scenic::Command command,
                       scheduling::PresentId present_id) override{}
};

}  // namespace test
}  // namespace scenic_impl

#endif  // SRC_UI_SCENIC_LIB_SCENIC_TESTS_DUMMY_SYSTEM_H_
