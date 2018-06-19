// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <map>
#include <vector>

#include "garnet/lib/debug_ipc/helper/fd_watcher.h"
#include "garnet/lib/debug_ipc/helper/message_loop.h"
#include "garnet/public/lib/fxl/files/unique_fd.h"

struct pollfd;

namespace debug_ipc {

// This MessageLoop implementation uses the Unix poll() function.
class MessageLoopPoll : public MessageLoop, public FDWatcher {
 public:
  MessageLoopPoll();
  ~MessageLoopPoll();

  // MessageLoop implementation.
  void Init() override;
  void Cleanup() override;
  WatchHandle WatchFD(WatchMode mode, int fd, FDWatcher* watcher) override;

 private:
  struct WatchInfo;

  // MessageLoop protected implementation.
  void RunImpl() override;
  void StopWatching(int id) override;
  void SetHasTasks() override;

  // FDWatcher implementation (for waking up for posted tasks).
  void OnFDReadable(int fd) override;

  // Prepares the pollfd vector with all the handles we will be watching for
  // poll(). The map_indices vector will be of the same length and will contain
  // the key into watches_ of each item in the pollfd vector.
  void ConstructFDMapping(std::vector<pollfd>* poll_vect,
                          std::vector<size_t>* map_indices) const;

  // Called when poll detects an event. The poll event mask is in |events|.
  void OnHandleSignaled(int fd, short events, int watch_id);

  // This must only be accessed on the same thread as the message loop, so is
  // not protected by the lock.
  using WatchMap = std::map<int, WatchInfo>;
  WatchMap watches_;

  // ID used as an index into watches_.
  int next_watch_id_ = 1;

  // Pipe used to wake up the message loop for posted events.
  fxl::UniqueFD wakeup_pipe_out_;
  fxl::UniqueFD wakeup_pipe_in_;
  WatchHandle wakeup_pipe_watch_;

  FXL_DISALLOW_COPY_AND_ASSIGN(MessageLoopPoll);
};

}  // namespace debug_ipc
