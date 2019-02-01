// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/client/thread_controller.h"

#include <stdarg.h>

#include "garnet/bin/zxdb/client/thread.h"

namespace zxdb {

ThreadController::ThreadController() = default;

ThreadController::~ThreadController() = default;

#if defined(DEBUG_THREAD_CONTROLLERS)
void ThreadController::Log(const char* format, ...) const {
  va_list ap;
  va_start(ap, format);

  printf("%s controller: ", GetName());
  vprintf(format, ap);

  // Manually add \r so output will be reasonable even if the terminal is in
  // raw mode.
  printf("\r\n");

  va_end(ap);
}

// static
void ThreadController::LogRaw(const char* format, ...) {
  va_list ap;
  va_start(ap, format);
  vprintf(format, ap);
  printf("\r\n");
  va_end(ap);
}
#endif

void ThreadController::NotifyControllerDone() {
  thread_->NotifyControllerDone(this);
  // Warning: |this| is likely deleted.
}

}  // namespace zxdb
