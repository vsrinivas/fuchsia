// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <vector>

#include "garnet/bin/zxdb/symbols/location.h"
#include "garnet/lib/debug_ipc/protocol.h"
#include "lib/fxl/macros.h"

namespace zxdb {

class Frame;
class FrameFingerprint;

// Represents the stack of a thread that's suspended or blocked in an
// exception. If a thread is running, blocked (not in an exception), or in any
// other state, the stack frames are not available.
//
// PARTIAL AND COMPLETE STACKS
// ---------------------------
// When a thread is suspended or blocked in an exception, it will have its
// top frame available (the current IP and stack position) and the next (the
// calling frame) if possible.
//
// If the full backtrace is needed, SyncFrames() can be called which will
// compute the full backtrace and issue the callback when complete. This
// backtrace will be cached until the thread is resumed.
//
// INLINE FRAMES
// -------------
// The thread's current position can be in multiple inline frames at the same
// time (the first address of an inline function is both the first instruction
// of that function, and the virtual "call" of that function in the outer
// frame). This only applies to the topmost set of inline frames since anything
// below the first physical frame is unambiguous).
//
// To make stepping work as expected, code can adjust which of these ambiguous
// inline frames the stack reports is the top, and inline frames above that are
// hidden from the normal size() and operator[] functions.
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

    // Constructs a Frame implementation for the given IPC stack frame and
    // location. The location must be an input since inline frame expansion
    // requires stack frames be constructed with different symbols than just
    // looking up the address in the symbols.
    virtual std::unique_ptr<Frame> MakeFrameForStack(
        const debug_ipc::StackFrame& input, Location location) = 0;

    virtual Location GetSymbolizedLocationForStackFrame(
        const debug_ipc::StackFrame& input) = 0;
  };

  // The delegate must outlive this class.
  explicit Stack(Delegate* delegate);

  ~Stack();

  // Returns whether the frames in this backtrace are all the frames or only
  // the top 1-2 (see class-level comment above).
  bool has_all_frames() const { return has_all_frames_; }

  size_t size() const { return frames_.size() - hide_top_inline_frame_count_; }
  bool empty() const { return frames_.empty(); }

  // Access into the individual frames. The topmost stack frame is index 0.
  // There may be hidden inline frames above index 0.
  Frame* operator[](size_t index) {
    return frames_[index + hide_top_inline_frame_count_].get();
  }
  const Frame* operator[](size_t index) const {
    return frames_[index + hide_top_inline_frame_count_].get();
  }

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

  // Sets the number of inline frames at the top of the stack to show. See the
  // class-level comment above for more.
  //
  // Anything trimmed should have its current position at the beginning of a
  // code range of an inline function for this trimming to make logical sense.
  // The number of inline frames at the top of the stack can be modified.
  //
  // The "top inline frame count" is the number of inline frames above the
  // topmost physical frame that exist in the stack. This does not change when
  // the hide count is modified.
  //
  // From 0 to "top inline frame count" of inline frames can be hidden or
  // unhidden. By default they are all visible (hide count = 0).
  size_t GetTopInlineFrameCount() const;
  size_t hide_top_inline_frame_count() const {
    return hide_top_inline_frame_count_;
  }
  void SetHideTopInlineFrameCount(size_t hide_count);

  // Queries the size and for frames at indices ignoring any hidden inline
  // frames. With FrameAtIndexIndcludingHiddenInline(), the 0th index is always
  // the innermost inline frame and is not affected by
  // SetTopInlineFrameShowCount().
  size_t SizeIncludingHiddenInline() const { return frames_.size(); }
  Frame* FrameAtIndexIncludingHiddenInline(size_t index) {
    return frames_[index].get();
  }

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
  // Adds the given stack frame to the end of the current stack (going
  // backwards in time). Inline frames will be expanded so this may append more
  // than one frame.
  void AppendFrame(const debug_ipc::StackFrame& record);

  Delegate* delegate_;

  std::vector<std::unique_ptr<Frame>> frames_;
  bool has_all_frames_ = false;

  // Number of frames to hide from size() and operator[] that are inline frames
  // at the top of the stack that shouldn't be exposed right now.
  size_t hide_top_inline_frame_count_ = 0;

  FXL_DISALLOW_COPY_AND_ASSIGN(Stack);
};

}  // namespace zxdb
