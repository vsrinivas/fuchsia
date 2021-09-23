// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/fuzzing/common/testing/dispatcher.h"

#include <lib/async-loop/default.h>
#include <lib/syslog/cpp/macros.h>
#include <zircon/status.h>

namespace fuzzing {

FakeDispatcher::FakeDispatcher() {
  loop_ = std::make_unique<async::Loop>(&kAsyncLoopConfigNoAttachToCurrentThread);
  auto status = loop_->StartThread();
  FX_DCHECK(status == ZX_OK) << zx_status_get_string(status);
}

async_dispatcher_t* FakeDispatcher::get() const { return loop_->dispatcher(); }

}  // namespace fuzzing
