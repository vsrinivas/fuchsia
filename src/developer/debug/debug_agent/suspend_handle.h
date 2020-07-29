// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_DEBUG_AGENT_SUSPEND_HANDLE_H_
#define SRC_DEVELOPER_DEBUG_DEBUG_AGENT_SUSPEND_HANDLE_H_

namespace debug_agent {

// Represents a suspension of a thread. As long as one of these is alive on a thread, that thread
// will remain suspended.
class SuspendHandle {
 public:
  virtual ~SuspendHandle() = default;

 protected:
  // This is a virtual base class even though it has no members.
  SuspendHandle() = default;
};

}  // namespace debug_agent

#endif  // SRC_DEVELOPER_DEBUG_DEBUG_AGENT_SUSPEND_HANDLE_H_
