// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/debug_ipc/helper/message_loop_poll.h"

#include <fcntl.h>
#include <poll.h>
#include <unistd.h>

#include "garnet/public/lib/fxl/build_config.h"
#include "garnet/public/lib/fxl/files/eintr_wrapper.h"

namespace debug_ipc {

namespace {

#if !defined(OS_LINUX)
bool SetCloseOnExec(int fd) {
  const int flags = fcntl(fd, F_GETFD);
  if (flags == -1)
    return false;
  if (flags & FD_CLOEXEC)
    return true;
  if (HANDLE_EINTR(fcntl(fd, F_SETFD, flags | FD_CLOEXEC)) == -1)
    return false;
  return true;
}

bool SetNonBlocking(int fd) {
  const int flags = fcntl(fd, F_GETFL);
  if (flags == -1)
    return false;
  if (flags & O_NONBLOCK)
    return true;
  if (HANDLE_EINTR(fcntl(fd, F_SETFL, flags | O_NONBLOCK)) == -1)
    return false;
  return true;
}
#endif

// Creates a nonblocking temporary pipe pipe and assigns the two ends of it to
// the two out parameters. Returns true on success.
bool CreateLocalNonBlockingPipe(fxl::UniqueFD* out_end, fxl::UniqueFD* in_end) {
#if defined(OS_LINUX)
  int fds[2];
  if (pipe2(fds, O_CLOEXEC | O_NONBLOCK) != 0)
    return false;
  out_end->reset(fds[0]);
  in_end->reset(fds[1]);
  return true;
#else
  int fds[2];
  if (pipe(fds) != 0)
    return false;

  fxl::UniqueFD fd_out(fds[0]);
  fxl::UniqueFD fd_in(fds[1]);
  if (!SetCloseOnExec(fd_out.get()))
    return false;
  if (!SetCloseOnExec(fd_in.get()))
    return false;
  if (!SetNonBlocking(fd_out.get()))
    return false;
  if (!SetNonBlocking(fd_in.get()))
    return false;

  *out_end = std::move(fd_out);
  *in_end = std::move(fd_in);
  return true;
#endif
}

}  // namespace

struct MessageLoopPoll::WatchInfo {
  int fd = 0;
  WatchMode mode = WatchMode::kReadWrite;
  FDWatcher* watcher = nullptr;
};

MessageLoopPoll::MessageLoopPoll() {
  if (!CreateLocalNonBlockingPipe(&wakeup_pipe_out_, &wakeup_pipe_in_))
    fprintf(stderr, "Can't create pipe.\n");
}

MessageLoopPoll::~MessageLoopPoll() = default;

void MessageLoopPoll::Init() {
  MessageLoop::Init();
  wakeup_pipe_watch_ = WatchFD(WatchMode::kRead, wakeup_pipe_out_.get(), this);
}

void MessageLoopPoll::Cleanup() {
  // Force unregister out watch before cleaning up current MessageLoop.
  wakeup_pipe_watch_ = WatchHandle();
  MessageLoop::Cleanup();
}

MessageLoop::WatchHandle MessageLoopPoll::WatchFD(WatchMode mode, int fd,
                                                  FDWatcher* watcher) {
  // The dispatch code for watch callbacks requires this be called on the
  // same thread as the message loop is.
  FXL_DCHECK(Current() == static_cast<MessageLoop*>(this));

  WatchInfo info;
  info.fd = fd;
  info.mode = mode;
  info.watcher = watcher;

  // The reason this function must be called on the message loop thread is that
  // otherwise adding a new watch would require synchronously breaking out of
  // the existing poll() call to add the new handle and then resuming it.
  int watch_id = next_watch_id_;
  next_watch_id_++;
  watches_[watch_id] = info;

  return WatchHandle(this, watch_id);
}

void MessageLoopPoll::RunImpl() {
  std::vector<pollfd> poll_vect;
  std::vector<size_t> map_indices;

  while (!should_quit()) {
    // This could be optimized to avoid recomputing every time.
    ConstructFDMapping(&poll_vect, &map_indices);
    FXL_DCHECK(!poll_vect.empty());
    FXL_DCHECK(poll_vect.size() == map_indices.size());

    poll(&poll_vect[0], static_cast<nfds_t>(poll_vect.size()), -1);
    for (size_t i = 0; i < poll_vect.size(); i++) {
      if (poll_vect[i].revents)
        OnHandleSignaled(poll_vect[i].fd, poll_vect[i].revents, map_indices[i]);
    }
  }
}

void MessageLoopPoll::StopWatching(int id) {
  // The dispatch code for watch callbacks requires this be called on the
  // same thread as the message loop is.
  FXL_DCHECK(Current() == this);

  std::lock_guard<std::mutex> guard(mutex_);

  auto found = watches_.find(id);
  if (found == watches_.end()) {
    FXL_NOTREACHED();
    return;
  }
  watches_.erase(found);
}

void MessageLoopPoll::OnFDReadable(int fd) {
  FXL_DCHECK(fd == wakeup_pipe_out_.get());

  // Remove and discard the wakeup byte.
  char buf;
  int nread = HANDLE_EINTR(read(wakeup_pipe_out_.get(), &buf, 1));
  FXL_DCHECK(nread == 1);

  // ProcessPendingTask must be called with the lock held.
  std::lock_guard<std::mutex> guard(mutex_);
  if (ProcessPendingTask())
    SetHasTasks();
}

void MessageLoopPoll::SetHasTasks() {
  // Wake up the poll() by writing to the pipe.
  char buf = 0;
  int written = HANDLE_EINTR(write(wakeup_pipe_in_.get(), &buf, 1));
  FXL_DCHECK(written == 1 || errno == EAGAIN);
}

void MessageLoopPoll::ConstructFDMapping(
    std::vector<pollfd>* poll_vect, std::vector<size_t>* map_indices) const {
  // The watches_ vector is not threadsafe.
  FXL_DCHECK(Current() == this);

  poll_vect->resize(watches_.size());
  map_indices->resize(watches_.size());

  size_t i = 0;
  for (const auto& pair : watches_) {
    pollfd& pfd = (*poll_vect)[i];
    pfd.fd = pair.second.fd;

    pfd.events = 0;
    if (pair.second.mode == WatchMode::kRead ||
        pair.second.mode == WatchMode::kReadWrite)
      pfd.events |= POLLIN;
    if (pair.second.mode == WatchMode::kWrite ||
        pair.second.mode == WatchMode::kReadWrite)
      pfd.events |= POLLOUT;

    pfd.revents = 0;

    (*map_indices)[i] = pair.first;

    i++;
  }
}

void MessageLoopPoll::OnHandleSignaled(int fd, short events, int watch_id) {
  // The watches_ vector is not threadsafe.
  FXL_DCHECK(Current() == this);

  auto found = watches_.find(watch_id);
  if (found == watches_.end()) {
    // Handle could have been just closed. Since all signaled handles are
    // notified for one call to poll(), a previous callback could have removed
    // a watch.
    return;
  }
  FXL_DCHECK(fd == found->second.fd);

  // Since notifications can cause the watcher to be removed, this flag tracks
  // if anything has been issued and therefore we should re-check the watcher
  // registration before dereferencing anything.
  bool sent_notification = false;

  if (events & POLLIN) {
    FXL_DCHECK(found->second.mode == WatchMode::kRead ||
               found->second.mode == WatchMode::kReadWrite);
    found->second.watcher->OnFDReadable(fd);
    sent_notification = true;
  }

  if (events & POLLOUT) {
    if (sent_notification) {
      found = watches_.find(watch_id);
      if (found == watches_.end())
        return;
    }
    FXL_DCHECK(found->second.mode == WatchMode::kWrite ||
               found->second.mode == WatchMode::kReadWrite);
    found->second.watcher->OnFDWritable(fd);
    sent_notification = true;
  }

  if (sent_notification)
    return;  // ERASEME

  if ((events & POLLERR) || (events & POLLHUP) ||
      (events & POLLNVAL)
#if defined(POLLRDHUP)  // Mac doesn't have this.
      || (events & POLLRDHUP)
#endif
  ) {
    if (sent_notification) {
      found = watches_.find(watch_id);
      if (found == watches_.end())
        return;
    }
    found->second.watcher->OnFDError(fd);
    sent_notification = true;
  }
}

}  // namespace debug_ipc
