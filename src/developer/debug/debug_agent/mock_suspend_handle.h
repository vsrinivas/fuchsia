// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_DEBUG_AGENT_MOCK_SUSPEND_HANDLE_H_
#define SRC_DEVELOPER_DEBUG_DEBUG_AGENT_MOCK_SUSPEND_HANDLE_H_

#include <memory>

#include "src/developer/debug/debug_agent/suspend_handle.h"

namespace debug_agent {

// This mock handle adjusts a shared counter in the MockThreadHandle to indicate suspension.
class MockSuspendHandle : public SuspendHandle {
 public:
  explicit MockSuspendHandle(std::shared_ptr<int> count) : count_(std::move(count)) { ++(*count_); }

  ~MockSuspendHandle() override { --(*count_); }

 private:
  std::shared_ptr<int> count_;
};

}  // namespace debug_agent

#endif  // SRC_DEVELOPER_DEBUG_DEBUG_AGENT_MOCK_SUSPEND_HANDLE_H_
