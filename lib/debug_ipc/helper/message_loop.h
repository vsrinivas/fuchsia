// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <deque>
#include <functional>
#include <map>
#include <mutex>

#include "garnet/public/lib/fxl/macros.h"

#if defined(__Fuchsia__)
#include <zircon/compiler.h>
#else
// The macros for thread annotations aren't set up for non-Fuchsia builds.
#define __TA_REQUIRES(arg)
#endif

namespace debug_ipc {

class FDWatcher;

// Message loop implementation. Unlike the one in FXL, this will run on the
// host in addition to a Zircon target.
class MessageLoop {
 public:
  enum class WatchMode { kRead, kWrite, kReadWrite };

  class WatchHandle;

  // There can be only one active MessageLoop in scope per thread at a time.
  //
  // A message loop is active between Init() and Cleanup(). During this
  // period, Current() will return the message loop.
  //
  // Init() / Cleanup() is a separate phase so a message loop can be created
  // and managed on one thread and sent to another thread to actually run (to
  // help with cross-thread task posting).
  MessageLoop();
  virtual ~MessageLoop();

  // These must be called on the same thread as Run().
  virtual void Init();
  virtual void Cleanup();

  // Returns the current message loop or null if there isn't one.
  static MessageLoop* Current();

  // Runs the message loop.
  void Run();

  void PostTask(std::function<void()> fn);

  // Exits the message loop immediately, not running pending functions. This
  // must be called only on the MessageLoop thread.
  void QuitNow();

  // Starts watching the given file descriptor in the given mode. Returns
  // a WatchHandle that scopes the watch operation (when the handle is
  // destroyed the watcher is unregistered).
  //
  // This function must only be called on the message loop thread.
  //
  // The watcher pointer must outlive the returned WatchHandle. Typically
  // the class implementing the FDWatcher would keep the WatchHandle as a
  // member. Must only be called on the message loop thread.
  //
  // You can only watch a handle once. Note that stdin/stdout/stderr can be
  // the same underlying OS handle, so the caller can only watch one of them.
  virtual WatchHandle WatchFD(WatchMode mode, int fd, FDWatcher* watcher) = 0;

 protected:
  virtual void RunImpl() = 0;

  // Used by WatchHandle to unregister a watch. Can be called from any thread
  // without the lock held.
  virtual void StopWatching(int id) = 0;

  // Indicates there are tasks to process. Can be called from any thread
  // and will be called without the lock held.
  virtual void SetHasTasks() = 0;

  // Processes one pending task, returning true if there was work to do, or
  // false if there was nothing. The mutex_ must be held during the call. It
  // will be unlocked during task processing, so the platform implementation
  // that calls it must not assume state did not change across the call.
  bool ProcessPendingTask() __TA_REQUIRES(mutex_);

  // The platform implementation should check should_quit() after every
  // task execution and exit if true.
  bool should_quit() const { return should_quit_; }

  // Style guide says this should be private and we should have a protected
  // getter, but that makes the thread annotations much more complicated.
  std::mutex mutex_;

 private:
  friend WatchHandle;

  std::deque<std::function<void()>> task_queue_;

  bool should_quit_ = false;

  FXL_DISALLOW_COPY_AND_ASSIGN(MessageLoop);
};

// Scopes watching a file handle. When the WatchHandle is destroyed, the
// MessageLoop will stop watching the handle. Must only be destroyed on the
// thread where the MessageLoop is.
//
// Invalid watch handles will have watching() return false.
class MessageLoop::WatchHandle {
 public:
  // Constructs a WatchHandle not watching anything.
  WatchHandle();

  // Constructor used by MessageLoop to make one that watches something.
  WatchHandle(MessageLoop* msg_loop, int id);

  WatchHandle(WatchHandle&&);

  // Stops watching.
  ~WatchHandle();

  WatchHandle& operator=(WatchHandle&& other);

  bool watching() const { return id_ > 0; }

 private:
  friend MessageLoop;

  MessageLoop* msg_loop_ = nullptr;
  int id_ = 0;
};

}  // namespace debug_ipc
