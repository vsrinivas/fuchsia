// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/shared/logging/logging.h"

#include <stdio.h>

namespace debug_ipc {

LogStatement::LogStatement(FileLineFunction origin, LogCategory category)
    : origin_(std::move(origin)), category_(category) {}

LogStatement::~LogStatement() {
  // If we're not on debug mode, we don't output anything.
  if (!IsDebugModeActive())
    return;

  if (!IsLogCategoryActive(category_))
    return;

  auto preamble = LogPreamble(category_, origin_);
  fprintf(stderr, "\r%s %s\r\n", preamble.c_str(), stream_.str().c_str());
}

}
// namespace debug_ipc
