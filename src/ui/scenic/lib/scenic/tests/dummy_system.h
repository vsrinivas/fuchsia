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

  uint32_t GetNumDispatchers() { return num_dispatchers_; }

  int64_t GetLastSessionId() { return last_session_; }

 private:
  uint32_t num_dispatchers_ = 0;
  int64_t last_session_ = -1;
};

class DummyCommandDispatcher : public CommandDispatcher {
 public:
  DummyCommandDispatcher() = default;
  ~DummyCommandDispatcher() override = default;

  // |CommandDispatcher|
  void SetDebugName(const std::string& debug_name) override {}

  // |CommandDispatcher|
  void DispatchCommand(const fuchsia::ui::scenic::Command command) override{};
};

}  // namespace test
}  // namespace scenic_impl

#endif  // SRC_UI_SCENIC_LIB_SCENIC_TESTS_DUMMY_SYSTEM_H_
