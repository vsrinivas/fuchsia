// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/client/session_observer.h"

#include "src/lib/fxl/logging.h"

namespace zxdb {

const char* SessionObserver::NotificationTypeToString(NotificationType type) {
  switch (type) {
    case NotificationType::kError:
      return "Error";
    case NotificationType::kProcessEnteredLimbo:
      return "kProcessEnteredLimbo";
    case NotificationType::kProcessStderr:
      return "ProcessStderr";
    case NotificationType::kProcessStdout:
      return "ProcessStdout";
    case NotificationType::kWarning:
      return "Warning";
    case NotificationType::kNone:
      return "None";
  }

  FXL_NOTREACHED();
  return "<unknown>";
}

}  // namespace zxdb
