// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "garnet/lib/debug_ipc/protocol.h"

namespace zxdb {

class Thread;

// Note: thread lifetime notifications are on the ProcessObserver.
class ThreadObserver {
 public:
  // TODO(brettw) Remove the address when we implement frames. If you want the
  // stopped address it should be available via stack frame 0.
  virtual void OnThreadStopped(Thread* thread,
                               debug_ipc::NotifyException::Type type,
                               uint64_t address) {}
};

}  // namespace zxdb
