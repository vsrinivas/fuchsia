// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <vector>

#include "garnet/lib/debug_ipc/protocol.h"

namespace zxdb {

class Frame;
class FrameFingerprint;

// Represents the stack of a thread that's suspended or blocked in an
// exception. If a thread is running, blocked (not in an exception), or in any
// other state, the stack frames are not available.
//
// When a thread is suspended or blocked in an exception, it will have its
// top frame available (the current IP and stack position) and the next (the
// calling frame) if possible.
//
// If the full backtrace is needed, SyncFrames() can be called which will
// compute the full backtrace and issue the callback when complete. This
// backtrace will be cached until the thread is resumed.
class Stack {
 public:
  // Provides a way for this class to talk to the environment.
  class Delegate {
   public:
    // Requests that the Stack be provided with a new set of frames. The
    // implementation should asynchronously request the frame information, call
    // Stack::SetFrames(), then issue the callback to indicate completion.
    //
    // The callback should be dropped if the object is destroyed during
    // processing.
    virtual void SyncFramesForStack(std::function<void()> callback) = 0;

    // Constructs a Frame implementation for the given IPC stack frame
    // information.
    virtual std::unique_ptr<Frame> MakeFrameForStack(
        const debug_ipc::StackFrame& input) = 0;
  };

  // The delegate must outlive this class.
  explicit Stack(Delegate* delegate);

  ~Stack();

  // Returns whether the frames in this backtrace are all the frames or only
  // the top 1-2 (see class-level comment above).
  bool has_all_frames() const { return has_all_frames_; }

  // Returns the current stack trace.
  const std::vector<Frame*>& GetFrames() const;

  // Computes the stack frame fingerprint for the stack frame at the given
  // index. This function requires that that the previous stack frame
  // (frame_index + 1) be present since the stack base is the SP of the
  // calling function.
  //
  // This function can always return the fingerprint for frame 0. Other
  // frames requires has_all_frames() == true or it will assert.
  //
  // See frame.h for a discussion on stack frames.
  FrameFingerprint GetFrameFingerprint(size_t frame_index) const;

  // Requests that all frame information be updated. This can be used to
  // (asynchronously) populate the frames when a Stack has only partial
  // frame information, and it can be used to force an update from the remote
  // system in case anything changed.
  void SyncFrames(std::function<void()> callback);

  // Provides a new set of frames computed by a backtrace in the debug_agent.
  // In normal operation this is called by the Thread.
  void SetFrames(debug_ipc::ThreadRecord::StackAmount amount,
                 const std::vector<debug_ipc::StackFrame>& frames);

  // Sets the frames to a known set to provide synthetic stacks for tests.
  void SetFramesForTest(std::vector<std::unique_ptr<Frame>> frames,
                        bool has_all);

  // Removes all frames. In normal operation this is called by the Thread when
  // things happen that invalidate all frames such as resuming the thread.
  //
  // Returns true if anything was modified (false means there were no frames to
  // clear).
  bool ClearFrames();

 private:
  Delegate* delegate_;

  std::vector<std::unique_ptr<Frame>> frames_;
  bool has_all_frames_ = false;

  // Cached version of frames_ containing non-owning pointers to the base type.
  // This is the backing store for GetFrames() which can be called frequently
  // so we don't want to return a copy every time.
  //
  // When empty, it should be repopulated from frames_.
  mutable std::vector<Frame*> frames_cache_;
};

}  // namespace zxdb
