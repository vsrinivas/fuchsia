// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/client/session_observer.h"

#include "lib/fxl/logging.h"

namespace zxdb {

const char* SessionObserver::NotificationTypeToString(NotificationType type) {
  switch (type) {
    case NotificationType::kError:
      return "Error";
    case NotificationType::kProcessStderr:
      return "kProcessStderr";
    case NotificationType::kProcessStdout:
      return "kProcessStdout";
    case NotificationType::kWarning:
      return "Warning";
  }

  FXL_NOTREACHED();
  return nullptr;
}

}  // namespace zxdb
