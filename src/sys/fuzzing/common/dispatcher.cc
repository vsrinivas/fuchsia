// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/fuzzing/common/dispatcher.h"

#include <lib/async-loop/default.h>
#include <lib/async/cpp/task.h>
#include <lib/syslog/cpp/macros.h>
#include <zircon/status.h>

namespace fuzzing {

Dispatcher::Dispatcher() : shutdown_([this]() { ShutdownImpl(); }) {
  loop_ = std::make_unique<async::Loop>(&kAsyncLoopConfigNoAttachToCurrentThread);
  auto status = loop_->StartThread("fuzzing-dispatcher", &thrd_);
  FX_DCHECK(status == ZX_OK) << zx_status_get_string(status);
}

Dispatcher::~Dispatcher() { Shutdown(); }

zx_status_t Dispatcher::PostTask(fit::closure&& task) {
  return async::PostTask(loop_->dispatcher(), [task = std::move(task)]() { task(); });
}

void Dispatcher::Shutdown() { shutdown_.Run(); }

void Dispatcher::ShutdownImpl() {
  running_ = false;
  loop_->Shutdown();
}

}  // namespace fuzzing
