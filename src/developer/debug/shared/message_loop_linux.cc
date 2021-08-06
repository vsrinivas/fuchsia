// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/shared/message_loop_linux.h"

#include <lib/syslog/cpp/macros.h>
#include <signal.h>
#include <sys/signalfd.h>
#include <sys/wait.h>

#include "src/lib/files/eintr_wrapper.h"

namespace debug {

struct MessageLoopLinux::SignalWatchInfo {
  pid_t pid = -1;
  SignalWatcher watcher;
};

MessageLoopLinux::MessageLoopLinux() {
  // Register for signals from child processes. We may need to add to the set of signals in the
  // future as requirements grow.
  sigset_t mask;
  sigemptyset(&mask);
  sigaddset(&mask, SIGCHLD);

  // Prevents the signals that are being processed via the signal fd from being sent via the normal
  // signal channel.
  sigprocmask(SIG_BLOCK, &mask, NULL);

  signal_fd_.reset(signalfd(-1, &mask, 0));
}

MessageLoopLinux::~MessageLoopLinux() = default;

MessageLoopLinux* MessageLoopLinux::Current() {
  return reinterpret_cast<MessageLoopLinux*>(MessageLoop::Current());
}

bool MessageLoopLinux::Init(std::string* error_message) {
  if (!MessageLoopPoll::Init(error_message))
    return false;

  signal_fd_watch_ =
      WatchFD(WatchMode::kRead, signal_fd_.get(), [this](int fd, bool readable, bool, bool) {
        if (!readable)
          return;

        FX_DCHECK(fd == signal_fd_.get());

        struct signalfd_siginfo fdsi;
        auto nread = HANDLE_EINTR(read(signal_fd_.get(), &fdsi, sizeof(struct signalfd_siginfo)));
        FX_DCHECK(nread == sizeof(signalfd_siginfo));

        for (const auto& [index, info] : signal_watches_) {
          if (info.pid == static_cast<int>(fdsi.ssi_pid)) {
            // The full status is available only from waitpid, the fdsi.status only contains the
            // child signal number.
            int status = 0;
            if (waitpid(info.pid, &status, __WALL | WUNTRACED | WNOHANG) >= 0)
              info.watcher(fdsi.ssi_pid, status);
            return;
          }
        }
      });
  return true;
}

void MessageLoopLinux::Cleanup() {
  // Force unregister our signal watch before cleaning up current MessageLoop.
  signal_fd_watch_ = WatchHandle();

  MessageLoopPoll::Cleanup();
}

MessageLoop::WatchHandle MessageLoopLinux::WatchChildSignals(pid_t pid, SignalWatcher watcher) {
  // The dispatch code for watch callbacks requires this be called on the same thread as the message
  // loop is.
  FX_DCHECK(Current() == static_cast<MessageLoop*>(this));

  SignalWatchInfo info;
  info.pid = pid;
  info.watcher = std::move(watcher);

  // The reason this function must be called on the message loop thread is that otherwise adding a
  // new watch would require synchronously breaking out of the existing poll() call to add the new
  // handle and then resuming it.
  int watch_id = GetNextWatchId();
  signal_watches_[watch_id] = std::move(info);

  return WatchHandle(this, watch_id);
}

void MessageLoopLinux::StopWatching(int id) {
  // The dispatch code for watch callbacks requires this be called on the
  // same thread as the message loop is.
  FX_DCHECK(MessageLoopLinux::Current() == this);

  {
    std::lock_guard<std::mutex> guard(mutex_);

    // Check for signal watches.
    auto found = signal_watches_.find(id);
    if (found != signal_watches_.end()) {
      signal_watches_.erase(found);
      return;
    }
  }

  // All other watches are removed by the base class.
  MessageLoopPoll::StopWatching(id);
}

}  // namespace debug
