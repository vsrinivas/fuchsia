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
  virtual void OnThreadStopped(Thread* thread,
                               debug_ipc::NotifyException::Type type) {}

  // A thread's backtrace (consisting of a vector of Frames) will be static
  // as long as the thread is not running. When the thread is resumed, the
  // frames will be cleared and this notification will be issued. Code that
  // caches state based on frames should clear the cache at this point.
  //
  // An initially stopped thread will only have one Frame (the topmost one),
  // and the full backtrace can be filled out on-demand. This function will
  // NOT be called when the full backtrace is filled out. Frame 0 will be
  // unchanged in this case, so nothing has been invalidate, just more data
  // is available.
  virtual void OnThreadFramesInvalidated(Thread* thread) {}
};

}  // namespace zxdb
