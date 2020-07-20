// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_DEBUG_AGENT_PROCESS_HANDLE_OBSERVER_H_
#define SRC_DEVELOPER_DEBUG_DEBUG_AGENT_PROCESS_HANDLE_OBSERVER_H_

#include <memory>

namespace debug_agent {

class ExceptionHandle;

// Callback interface used by ProcessHandle to notify about events. This is like the native
// ZirconExceptionWatcher but adapted to use the abstract ExceptionHandle types.
class ProcessHandleObserver {
 public:
  virtual void OnProcessTerminated() = 0;
  virtual void OnThreadStarting(std::unique_ptr<ExceptionHandle> exception) = 0;
  virtual void OnThreadExiting(std::unique_ptr<ExceptionHandle> exception) = 0;
  virtual void OnException(std::unique_ptr<ExceptionHandle> exception) = 0;
};

}  // namespace debug_agent

#endif  // SRC_DEVELOPER_DEBUG_DEBUG_AGENT_PROCESS_HANDLE_OBSERVER_H_
