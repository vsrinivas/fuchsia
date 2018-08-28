// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/client/thread_controller.h"

#include "garnet/bin/zxdb/client/thread.h"

namespace zxdb {

ThreadController::ThreadController(Thread* thread) : thread_(thread) {}

ThreadController::~ThreadController() = default;

void ThreadController::NotifyControllerDone() {
  thread_->NotifyControllerDone(this);
  // Warning: |this| is likely deleted.
}

}  // namespace zxdb
