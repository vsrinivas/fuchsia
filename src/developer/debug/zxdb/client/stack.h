// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_STACK_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_STACK_H_

#include <optional>
#include <vector>

#include "lib/fit/function.h"
#include "src/developer/debug/ipc/protocol.h"
#include "src/developer/debug/zxdb/symbols/location.h"
#include "src/lib/fxl/macros.h"
#include "src/lib/fxl/memory/weak_ptr.h"

namespace zxdb {

class Err;
class Frame;
class FrameFingerprint;

// Represents the stack of a thread that's suspended or blocked in an exception. If a thread is
// running, blocked (not in an exception), or in any other state, the stack frames are not
// available.
//
// EMPTY, PARTIAL AND COMPLETE STACKS
// ----------------------------------
// When a thread is suspended or blocked in an exception, it will UNUSUALLY have its top frame
// available (the current IP and stack position) and the next (the calling frame) if possible.
//
// Sometimes a thread might have an empty stack (and hence no current location) from an exception.
// This is because exceptions are delivered from the kernel asynchronously, and by the time an
// exception is handled in the debug agent, the thread may have been killed. This will result in
// failred register reads for the thread and no stack or location. Code should never assume there
// are any stack frames.
//
// If the full backtrace is needed, SyncFrames() can be called which will compute the full backtrace
// and issue the callback when complete. This backtrace will be cached until the thread is resumed.
//
// INLINE FRAMES
// -------------
// The thread's current position can be in multiple inline frames at the same time (the first
// address of an inline function is both the first instruction of that function, and the virtual
// "call" of that function in the outer frame). This only applies to the topmost set of inline
// frames since anything below the first physical frame is unambiguous).
//
// To make stepping work as expected, code can adjust which of these ambiguous inline frames the
// stack reports is the top, and inline frames above that are hidden from the normal size() and
// operator[] functions.
class Stack {
 public:
  // Provides a way for this class to talk to the environment.
  class Delegate {
   public:
    // Requests that the Stack be provided with a new set of frames. The implementation should
    // asynchronously request the frame information, call Stack::SetFrames(), then issue the
    // callback to indicate completion.
    //
    // The callback should be issued with an error if the object is destroyed during processing.
    virtual void SyncFramesForStack(fit::callback<void(const Err&)> callback) = 0;

    // Constructs a Frame implementation for the given IPC stack frame and location. The location
    // must be an input since inline frame expansion requires stack frames be constructed with
    // different symbols than just looking up the address in the symbols.
    virtual std::unique_ptr<Frame> MakeFrameForStack(const debug_ipc::StackFrame& input,
                                                     Location location) = 0;

    virtual Location GetSymbolizedLocationForStackFrame(const debug_ipc::StackFrame& input) = 0;
  };

  // The delegate must outlive this class.
  explicit Stack(Delegate* delegate);

  ~Stack();

  fxl::WeakPtr<Stack> GetWeakPtr();

  // Returns whether the frames in this backtrace are all the frames or only the top 1-2 (see
  // class-level comment above).
  bool has_all_frames() const { return has_all_frames_; }

  size_t size() const { return frames_.size() - hide_ambiguous_inline_frame_count_; }
  bool empty() const { return frames_.empty(); }

  // Access into the individual frames. The topmost stack frame is index 0. There may be hidden
  // inline frames above index 0. Callers should always check the stack size() before accessing
  // since the stack is never guaranteed to have elements in it (even during processing an
  // exception -- see class comment above).
  Frame* operator[](size_t index) {
    return frames_[index + hide_ambiguous_inline_frame_count_].get();
  }
  const Frame* operator[](size_t index) const {
    return frames_[index + hide_ambiguous_inline_frame_count_].get();
  }

  // Returns the index of the frame pointer in this stack if it is there.
  std::optional<size_t> IndexForFrame(const Frame* frame) const;

  // Returns the inline depth of the frame at the given index. If the frame is a physical frame,
  // this will be 0.
  size_t InlineDepthForIndex(size_t index) const;

  // Computes the stack frame fingerprint for the stack frame at the given index. The index must be
  // valid in the current set of frames in this stack object.
  //
  // See frame_fingerprint.h for a discussion on fingerprints.
  FrameFingerprint GetFrameFingerprint(size_t frame_index) const;

  // Sets the number of inline frames at the top of the stack to show. See the class-level comment
  // above for more.
  //
  // Anything trimmed should have its current position at the beginning of a code range of an inline
  // function for this trimming to make logical sense. The number of inline frames at the top of the
  // stack can be modified.
  //
  // The "top inline frame count" is the number of inline frames above the topmost physical frame
  // that exist in the stack. This does not change when the hide count is modified.
  //
  // From 0 to "top inline frame count" of inline frames can be hidden or unhidden. By default they
  // are all visible (hide count = 0).
  size_t GetAmbiguousInlineFrameCount() const;
  size_t hide_ambiguous_inline_frame_count() const { return hide_ambiguous_inline_frame_count_; }
  void SetHideAmbiguousInlineFrameCount(size_t hide_count);

  // Queries the size and for frames at indices ignoring any hidden inline frames. With
  // FrameAtIndexIndcludingHiddenInline(), the 0th index is always the innermost inline frame and is
  // not affected by SetTopInlineFrameShowCount().
  size_t SizeIncludingHiddenInline() const { return frames_.size(); }
  Frame* FrameAtIndexIncludingHiddenInline(size_t index) { return frames_[index].get(); }

  // Requests that all frame information be updated. This can be used to (asynchronously) populate
  // the frames when a Stack has only partial frame information, and it can be used to force an
  // update from the remote system in case anything changed.
  //
  // If the stack is destroyed before the frames can be synced, the callback will be issued with an
  // error.
  void SyncFrames(fit::callback<void(const Err&)> callback);

  // Provides a new set of frames computed by a backtrace in the debug_agent. In normal operation
  // this is called by the Thread.
  //
  // This can be called in two cases: (1) when a thread stops to provide a new stack, and (2) when
  // updating a stack with more frames. If there are existing frames when SetFrames is called, it
  // will assume state (2) if possible (the stack could have changed out from under us) and will
  // attempt to preserve the ambiguous inline hide count, etc. consistent with updating an existing
  // stack.
  //
  // If you don't want this behavior, call ClearFrames() first. ClearFrames() will be called whever
  // a thread is resumed so fresh stops should get this behavior by default.
  void SetFrames(debug_ipc::ThreadRecord::StackAmount amount,
                 const std::vector<debug_ipc::StackFrame>& frames);

  // Sets the frames to a known set to provide synthetic stacks for tests.
  void SetFramesForTest(std::vector<std::unique_ptr<Frame>> frames, bool has_all);

  // Removes all frames. In normal operation this is called by the Thread when things happen that
  // invalidate all frames such as resuming the thread.
  //
  // Callers should generally do this via the thread. Code in ThreadImpl should use
  // ThreadImpl::ClearFrames instead which will send observer notifications.
  //
  // Returns true if anything was modified (false means there were no frames to clear).
  bool ClearFrames();

 private:
  // Adds the given stack frame to the end of the current stack (going backwards in time). Inline
  // frames will be expanded so this may append more than one frame.
  void AppendFrame(const debug_ipc::StackFrame& record);

  Delegate* delegate_;

  std::vector<std::unique_ptr<Frame>> frames_;
  bool has_all_frames_ = false;

  // Number of frames to hide from size() and operator[] that are inline frames at the top of the
  // stack that shouldn't be exposed right now.
  size_t hide_ambiguous_inline_frame_count_ = 0;

  fxl::WeakPtrFactory<Stack> weak_factory_;

  FXL_DISALLOW_COPY_AND_ASSIGN(Stack);
};

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_STACK_H_
