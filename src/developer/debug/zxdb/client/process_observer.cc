// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/client/process_observer.h"

#include "src/lib/fxl/logging.h"

namespace zxdb {

// static
const char* ProcessObserver::DestroyReasonToString(DestroyReason reason) {
  switch (reason) {
    case DestroyReason::kExit:
      return "Exit";
    case DestroyReason::kDetach:
      return "Detach";
    case DestroyReason::kKill:
      return "Kill";
  }
  FXL_NOTREACHED();
  return nullptr;
}

}  // namespace zxdb
