// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_SESSION_OBSERVER_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_SESSION_OBSERVER_H_

#include <string>
#include <vector>

#include "src/developer/debug/ipc/records.h"
#include "src/developer/debug/zxdb/common/err.h"

namespace zxdb {

class SessionObserver {
 public:
  // The session can send notifications over to whatever UI is controlling it. This is similar to
  // what the System does, but permits sending more generic messages and let the UI decide how to
  // handle, instead of just logging to stdout/stderr.
  //
  // This is specially important when receiving arbitrary messages from the debug agent and the fact
  // the the cli console has special states for the terminal state.
  enum class NotificationType {
    kNone,  // Meant to signal a no-op.
    kError,
    kProcessEnteredLimbo,
    kProcessStderr,
    kProcessStdout,
    kWarning,
  };
  static const char* NotificationTypeToString(NotificationType);

  virtual void HandleNotification(NotificationType, const std::string&) {}

  virtual void HandlePreviousConnectedProcesses(const std::vector<debug_ipc::ProcessRecord>&) {}

  virtual void HandleProcessesInLimbo(const std::vector<debug_ipc::ProcessRecord>&) {}

  // Called when a connection is resolved, either successful or failed.
  virtual void DidConnect(const Err& err) {}
};

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_SESSION_OBSERVER_H_
