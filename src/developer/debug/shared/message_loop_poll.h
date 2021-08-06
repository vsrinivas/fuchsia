// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_SHARED_MESSAGE_LOOP_POLL_H_
#define SRC_DEVELOPER_DEBUG_SHARED_MESSAGE_LOOP_POLL_H_

#include <map>
#include <vector>

#include <fbl/unique_fd.h>

#include "src/developer/debug/shared/message_loop.h"

struct pollfd;

namespace debug {

// This MessageLoop implementation uses the Unix poll() function.
class MessageLoopPoll : public MessageLoop {
 public:
  MessageLoopPoll();
  ~MessageLoopPoll() override;

  // MessageLoop implementation.
  bool Init(std::string* error_message) override;
  void Cleanup() override;
  WatchHandle WatchFD(WatchMode mode, int fd, FDWatcher watcher) override;

 protected:
  int GetNextWatchId();

  // MessageLoop protected implementation.
  uint64_t GetMonotonicNowNS() const override;
  void RunImpl() override;
  void StopWatching(int id) override;
  void SetHasTasks() override;

 private:
  struct WatchInfo;

  // Prepares the pollfd vector with all the handles we will be watching for poll(). The map_indices
  // vector will be of the same length and will contain the key into watches_ of each item in the
  // pollfd vector.
  void ConstructFDMapping(std::vector<pollfd>* poll_vect, std::vector<int>* map_indices) const;

  // Called when poll detects an event. The poll event mask is in |events|.
  void OnHandleSignaled(int fd, short events, int watch_id);

  // Whether the loop is watching a particular id.
  bool HasWatch(int watch_id);

  // This must only be accessed on the same thread as the message loop, so is not protected by the
  // lock.
  using WatchMap = std::map<int, WatchInfo>;
  WatchMap watches_;

  // ID used as an index into watches_.
  int next_watch_id_ = 1;

  // Pipe used to wake up the message loop for posted events.
  fbl::unique_fd wakeup_pipe_out_;
  fbl::unique_fd wakeup_pipe_in_;
  WatchHandle wakeup_pipe_watch_;

  FXL_DISALLOW_COPY_AND_ASSIGN(MessageLoopPoll);
};

}  // namespace debug

#endif  // SRC_DEVELOPER_DEBUG_SHARED_MESSAGE_LOOP_POLL_H_
