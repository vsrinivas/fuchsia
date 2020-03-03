// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_THREAD_OBSERVER_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_THREAD_OBSERVER_H_

#include "src/developer/debug/ipc/protocol.h"
#include "src/developer/debug/zxdb/client/stop_info.h"
#include "src/lib/fxl/memory/weak_ptr.h"

namespace zxdb {

class Breakpoint;
class Thread;

class ThreadObserver {
 public:
  virtual void DidCreateThread(Thread* thread) {}
  virtual void WillDestroyThread(Thread* thread) {}

  // Notification that a thread has stopped. The thread and all breakpoint statistics will be
  // up-to-date.
  //
  // IMPORTANT: The thread's stack may be empty during this notification. See the Stack object for
  // more information.
  virtual void OnThreadStopped(Thread* thread, const StopInfo& info) {}

  // A thread's backtrace (consisting of a vector of Frames) will be static as long as the thread is
  // not running. When the thread is resumed, the frames will be cleared and this notification will
  // be issued. Code that caches state based on frames should clear the cache at this point.
  //
  // An initially stopped thread will only have one Frame (the topmost one), and the full backtrace
  // can be filled out on-demand. This function will NOT be called when the full backtrace is filled
  // out. Frame 0 will be unchanged in this case, so nothing has been invalidate, just more data is
  // available.
  virtual void OnThreadFramesInvalidated(Thread* thread) {}
};

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_THREAD_OBSERVER_H_
