// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <string>

namespace zxdb {

class SessionObserver {
 public:
  // The session can send notifications over to whatever UI is controlling it.
  // This is similar to what the System does, but permits sending more generic
  // messages and let the UI decide how to handle, instead of just logging
  // to stdout/stderr.
  //
  // This is specially important when receiving arbitrary messages from the
  // debug agent and the fact the the cli console has special states for the
  // terminal state.
  enum class NotificationType {
    kNone,      // Meant to signal a no-op.
    kError,
    kProcessStderr,
    kProcessStdout,
    kWarning,
  };
  static const char* NotificationTypeToString(NotificationType);

  virtual void HandleNotification(NotificationType, const std::string&) {}
};

}  // namespace zxdb
