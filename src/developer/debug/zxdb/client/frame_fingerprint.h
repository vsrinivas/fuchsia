// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_FRAME_FINGERPRINT_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_FRAME_FINGERPRINT_H_

#include <stdint.h>
#include <stdlib.h>

#include <string>

namespace zxdb {

// A FrameFingerprint is a way to compare stack frames across pause/resumes of the same thread. The
// Frame pointers themselves are owned by the Thread and will be destroyed when the thread is
// resumed. By saving a FrameFingerprint code can compare whether a future stop is the same or a
// subframe of the previous one.
//
// With stack frame pointers, an x64 prologue looks like this:
//   push rbp
//   mov rbp, rsp
//
// The stack grows to smaller addresses as stuff is pushed (in this diagram, down). Before the call
// say it looks like this:
//   0x1010 [data]      <= BP
//   0x1008 [data]
//   0x1000 [data]      <= SP
//   ...... [garbage]
//
// The CALL instruction will make it look like this:
//   0x1010 [data]      <= BP (same as before call)
//   0x1008 [data]
//   0x1000 [data]      <= FrameFingerprint.frame_address_
//   0x0ff8 [ret addr]  <= SP (new)
//
// And after the called function's prologue it will look like this:
//   0x1010 [data]
//   0x1008 [data]
//   0x1000 [data]      <= FrameFingerprint.frame_address_
//   0x0ff8 [ret addr]
//   0x0ff0 [old BP]    <= BP, SP (both new)
//   ...... [function locals will be appended starting here]
//
// Ideally we want a consistent way to refer to this stack frame that doesn't change across the
// function prologue. GDB and LLDB use a "frame_id" (GDB) / "FrameID" (LLDB) which is a combination
// of the "stack_addr" and "code_addr". Together these uniquely identify a stack frame.
//
// Their "code_addr" is the address of the beginning of the function it's currently in (the
// destination of the call above). This is easy enough to get from Location.function().
//
// Their "stack_addr" for the function being called in this example will be 0x1000 which is the SP
// from right before the call instruction. This is called the frame's "canonical frame address" in
// DWARF. We can get this by looking at the previous frame's SP.
//
// Because the inline count depends on other frames, the getter for this object is on the Stack
// (Stack::GetFrameFingerprint).
//
// INLINE FUNCTIONS
// ----------------
//
// The above description deals with physical stack frames. Inline frames share the same physical
// stack frame.
//
// To differentiate the depth when inside inline frames of the same functions, the fingerprint also
// keeps a depth of inline function calls. The frame address comes from the stack pointer before the
// current physical frame.
class FrameFingerprint {
 public:
  FrameFingerprint() = default;

  // We currently don't have a use for "function begin" so it is not included
  // here. It may be necessary in the future.
  explicit FrameFingerprint(uint64_t frame_address, size_t inline_count)
      : frame_address_(frame_address), inline_count_(inline_count) {}

  bool is_valid() const { return frame_address_ != 0; }

  // Returns true if the input refers to the same frame as this one. This will assert if either
  // frame is !is_valid().
  bool operator==(const FrameFingerprint& other) const;

  // For debugging.
  std::string ToString() const;

  // Computes "left Newer than right". This doesn't use operator < or > because it's ambiguous
  // whether a newer frame is "less" or "greater".
  static bool Newer(const FrameFingerprint& left, const FrameFingerprint& right);

  static bool NewerOrEqual(const FrameFingerprint& left, const FrameFingerprint& right);

 private:
  // The address of the stack immediately before the function call (i.e. the stack pointer of the
  // previous frame). See the class documentation above.
  uint64_t frame_address_ = 0;

  // When this frame is a physical frame, the inline count will be 0. Higher counts indicate the
  // nesting depth of inline function calls at the current location.
  size_t inline_count_ = 0;
};

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_FRAME_FINGERPRINT_H_
