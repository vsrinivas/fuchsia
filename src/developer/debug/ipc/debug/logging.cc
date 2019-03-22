// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/ipc/debug/logging.h"

#include <stdio.h>

#include "src/developer/debug/ipc/debug/debug.h"

namespace debug_ipc {

LogStatement::LogStatement(FileLineFunction location) : location_(location) {}

LogStatement::~LogStatement() {
  // If we're not on debug mode, we don't output anything.
  if (!IsDebugModeActive())
    return;

  auto location_str = location_.ToStringWithBasename();
  auto log_str = stream_.str();
  fprintf(stderr, "\r[%.3fs]%s %s\r\n", SecondsSinceStart(),
          location_str.c_str(), log_str.c_str());
}

}  // namespace debug_ipc
