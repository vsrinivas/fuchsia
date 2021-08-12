// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/shared/message_loop_poll.h"

#include <fcntl.h>
#include <lib/syslog/cpp/macros.h>
#include <poll.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "src/lib/files/eintr_wrapper.h"

namespace debug {

struct MessageLoopPoll::WatchInfo {
  int fd = -1;
  WatchMode mode = WatchMode::kReadWrite;
  FDWatcher watcher;
};

MessageLoopPoll::MessageLoopPoll() {
  if (!CreateLocalNonBlockingPipe(&wakeup_pipe_out_, &wakeup_pipe_in_))
    fprintf(stderr, "Can't create pipe.\n");
}

MessageLoopPoll::~MessageLoopPoll() = default;

bool MessageLoopPoll::Init(std::string* error_message) {
  if (!MessageLoop::Init(error_message))
    return false;

  wakeup_pipe_watch_ =
      WatchFD(WatchMode::kRead, wakeup_pipe_out_.get(), [this](int fd, bool readable, bool, bool) {
        if (!readable) {
          return;
        }

        FX_DCHECK(fd == wakeup_pipe_out_.get());

        // Remove and discard the wakeup byte.
        char buf;
        auto nread = HANDLE_EINTR(read(wakeup_pipe_out_.get(), &buf, 1));
        FX_DCHECK(nread == 1);

        // This is just here to wake us up and run the loop again. We don't need to
        // actually respond to the data.
      });

  return true;
}

void MessageLoopPoll::Cleanup() {
  // Force unregister our watch before cleaning up current MessageLoop.
  wakeup_pipe_watch_ = WatchHandle();

  // Destruct the FDWatcher first, because they may call StopWatching and expect the key is still
  // there.
  std::vector<FDWatcher> to_delete;
  to_delete.reserve(watches_.size());
  for (auto& [key, info] : watches_) {
    to_delete.push_back(std::move(info.watcher));
  }
  to_delete.clear();
  watches_.clear();
  MessageLoop::Cleanup();
}

MessageLoop::WatchHandle MessageLoopPoll::WatchFD(WatchMode mode, int fd, FDWatcher watcher) {
  // The dispatch code for watch callbacks requires this be called on the
  // same thread as the message loop is.
  FX_DCHECK(Current() == static_cast<MessageLoop*>(this));

  WatchInfo info;
  info.fd = fd;
  info.mode = mode;
  info.watcher = std::move(watcher);

  // The reason this function must be called on the message loop thread is that
  // otherwise adding a new watch would require synchronously breaking out of
  // the existing poll() call to add the new handle and then resuming it.
  int watch_id = next_watch_id_;
  next_watch_id_++;
  watches_[watch_id] = std::move(info);

  return WatchHandle(this, watch_id);
}

int MessageLoopPoll::GetNextWatchId() {
  int watch_id = next_watch_id_;
  next_watch_id_++;
  return watch_id;
}

uint64_t MessageLoopPoll::GetMonotonicNowNS() const {
  struct timespec ts;

  int ret = clock_gettime(CLOCK_MONOTONIC, &ts);
  FX_DCHECK(!ret);

  return static_cast<uint64_t>(ts.tv_sec) * 1000000000 + ts.tv_nsec;
}

void MessageLoopPoll::RunImpl() {
  std::vector<pollfd> poll_vect;
  std::vector<int> map_indices;

  while (!should_quit()) {
    // This could be optimized to avoid recomputing every time.
    ConstructFDMapping(&poll_vect, &map_indices);
    FX_DCHECK(!poll_vect.empty());
    FX_DCHECK(poll_vect.size() == map_indices.size());

    int poll_timeout;
    uint64_t delay = DelayNS();
    if (delay == MessageLoop::kMaxDelay) {
      poll_timeout = -1;
    } else {
      delay += 999999;
      delay /= 1000000;
      poll_timeout = static_cast<int>(delay);
    }

    int res = poll(poll_vect.data(), static_cast<nfds_t>(poll_vect.size()), poll_timeout);
    FX_DCHECK(res >= 0 || errno == EINTR) << "poll() failed: " << strerror(errno);

    for (size_t i = 0; i < poll_vect.size(); i++) {
      if (poll_vect[i].revents)
        OnHandleSignaled(poll_vect[i].fd, poll_vect[i].revents, map_indices[i]);
    }

    // Process one pending task. If there are more set us to wake up again.
    // ProcessPendingTask must be called with the lock held.
    std::lock_guard<std::mutex> guard(mutex_);
    if (ProcessPendingTask())
      SetHasTasks();
  }
}

void MessageLoopPoll::StopWatching(int id) {
  // The dispatch code for watch callbacks requires this be called on the
  // same thread as the message loop is.
  FX_DCHECK(Current() == this);

  std::lock_guard<std::mutex> guard(mutex_);

  auto found = watches_.find(id);
  if (found == watches_.end()) {
    FX_NOTREACHED();
    return;
  }
  watches_.erase(found);
}

void MessageLoopPoll::SetHasTasks() {
  // Wake up the poll() by writing to the pipe.
  char buf = 0;
  auto written = HANDLE_EINTR(write(wakeup_pipe_in_.get(), &buf, 1));
  FX_DCHECK(written == 1 || errno == EAGAIN);
}

void MessageLoopPoll::ConstructFDMapping(std::vector<pollfd>* poll_vect,
                                         std::vector<int>* map_indices) const {
  // The watches_ vector is not threadsafe.
  FX_DCHECK(Current() == this);

  poll_vect->resize(watches_.size());
  map_indices->resize(watches_.size());

  size_t i = 0;
  for (const auto& pair : watches_) {
    pollfd& pfd = (*poll_vect)[i];
    pfd.fd = pair.second.fd;

    pfd.events = 0;
    if (pair.second.mode == WatchMode::kRead || pair.second.mode == WatchMode::kReadWrite)
      pfd.events |= POLLIN;
    if (pair.second.mode == WatchMode::kWrite || pair.second.mode == WatchMode::kReadWrite)
      pfd.events |= POLLOUT;

    pfd.revents = 0;

    (*map_indices)[i] = pair.first;

    i++;
  }
}

bool MessageLoopPoll::HasWatch(int watch_id) { return watches_.find(watch_id) != watches_.end(); }

void MessageLoopPoll::OnHandleSignaled(int fd, short events, int watch_id) {
  // The watches_ vector is not threadsafe.
  FX_DCHECK(Current() == this);

  // Handle could have been just closed. Since all signaled handles are
  // notified for one call to poll(), a previous callback could have removed
  // a watch.
  if (!HasWatch(watch_id))
    return;

  // We obtain the watch info and see what kind of signal we received.
  auto& watch_info = watches_[watch_id];
  FX_DCHECK(fd == watch_info.fd);

  bool error = (events & POLLERR) || (events & POLLHUP) || (events & POLLNVAL);
#if defined(POLLRDHUP)  // Mac doesn't have this.
  error = error || (events & POLLRDHUP);
#endif

  bool readable = !!(events & POLLIN);
  bool writable = !!(events & POLLOUT);
  watch_info.watcher(fd, readable, writable, error);
  // watch_info might be invalid because watcher could have called StopWatching().
}

}  // namespace debug
