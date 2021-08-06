// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_SHARED_MESSAGE_LOOP_LINUX_H_
#define SRC_DEVELOPER_DEBUG_SHARED_MESSAGE_LOOP_LINUX_H_

#include <sys/types.h>

#include "src/developer/debug/shared/message_loop_poll.h"

namespace debug {

// Extension on MessageLoopPool that adds Linux-specific functionality.
class MessageLoopLinux : public MessageLoopPoll {
 public:
  MessageLoopLinux();
  ~MessageLoopLinux() override;

  // Returns the current message loop or null if there isn't one. This is like
  // MessageLoop::Current() but specifically returns the Linux one.
  static MessageLoopLinux* Current();

  // MessageLoop implementation.
  bool Init(std::string* error_message) override;
  void Cleanup() override;

  using SignalWatcher = fit::function<void(pid_t pid, int status)>;
  WatchHandle WatchChildSignals(pid_t pid, SignalWatcher watcher);

 private:
  struct SignalWatchInfo;

  // MessageLoop protected override.
  void StopWatching(int id) override;

  // Used to wake up for signals.
  fbl::unique_fd signal_fd_;
  WatchHandle signal_fd_watch_;

  using SignalWatchMap = std::map<int, SignalWatchInfo>;
  SignalWatchMap signal_watches_;
};

}  // namespace debug

#endif  // SRC_DEVELOPER_DEBUG_SHARED_MESSAGE_LOOP_LINUX_H_
