// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/shared/logging/logging.h"

#include <stdio.h>

namespace debug {

LogStatement::LogStatement(FileLineFunction origin, LogCategory category)
    : origin_(std::move(origin)), category_(category) {
  should_log_ = false;
  if (!IsLogCategoryActive(category_))
    return;

  start_time_ = SecondsSinceStart();
  should_log_ = true;
  PushLogEntry(this);
}

std::string LogStatement::GetMsg() { return stream_.str(); }

LogStatement::~LogStatement() {
  if (!IsLogCategoryActive(category_))
    return;

  if (!should_log_)
    return;

  PopLogEntry(category_, origin_, stream_.str(), start_time_, SecondsSinceStart());
}

}  // namespace debug
