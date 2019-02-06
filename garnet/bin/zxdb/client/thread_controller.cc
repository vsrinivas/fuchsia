// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/client/thread_controller.h"

#include <stdarg.h>

#include "garnet/bin/zxdb/client/frame.h"
#include "garnet/bin/zxdb/client/thread.h"
#include "garnet/bin/zxdb/symbols/function.h"

namespace zxdb {

namespace {

// Returns true if the given address is at the beginning of any of the ranges
// defined by the inline function for the given location. If there isn't an
// inline function in the symbols, it counts as "false".
bool AddressAtBeginningOfInlineRange(const Location& loc) {
  // Extract the inline function.
  if (!loc.symbol())
    return false;
  const Function* function = loc.symbol().Get()->AsFunction();
  if (!function)
    return false;
  if (!function->is_inline())
    return false;

  for (const auto& cur :
       function->GetAbsoluteCodeRanges(loc.symbol_context())) {
    if (loc.address() == cur.begin())
      return true;
  }
  return false;
}

}  // namespace

ThreadController::ThreadController() = default;

ThreadController::~ThreadController() = default;

#if defined(DEBUG_THREAD_CONTROLLERS)
void ThreadController::Log(const char* format, ...) const {
  va_list ap;
  va_start(ap, format);

  printf("%s controller: ", GetName());
  vprintf(format, ap);

  // Manually add \r so output will be reasonable even if the terminal is in
  // raw mode.
  printf("\r\n");

  va_end(ap);
}

// static
void ThreadController::LogRaw(const char* format, ...) {
  va_list ap;
  va_start(ap, format);
  vprintf(format, ap);
  printf("\r\n");
  va_end(ap);
}
#endif

void ThreadController::SetInlineFrameIfAmbiguous(FrameFingerprint fingerprint) {
  Stack& stack = thread()->GetStack();

  // Reset any hidden inline frames so we can iterate through all of them
  // (and we'll leave this reset to 0 if the requested one isn't found).
  if (stack.hide_top_inline_frame_count())
    stack.SetHideTopInlineFrameCount(0);

  for (size_t i = 0; i < stack.size(); i++) {
    const Frame* frame = stack[i];
    auto found = stack.GetFrameFingerprint(i);
    if (!found)
      break;  // Got to bottom of computable fingerprints, give up.

    // To be ambiguous, all frames to here need to be at the same address and
    // all inline frames need to be at the beginning of one of their ranges.
    // (the physical frame also needs matching but its range doesn't count).
    bool is_inline = frame->IsInline();
    if (is_inline) {
      if (!AddressAtBeginningOfInlineRange(frame->GetLocation()))
        break;  // Not an ambiguous address.
    }

    if (*found == fingerprint) {
      // Found it, make this one the top of the stack.
      stack.SetHideTopInlineFrameCount(i);
      break;
    }

    if (!is_inline)
      break;  // Don't check below the first physical frame.
  }
}

void ThreadController::NotifyControllerDone() {
  thread_->NotifyControllerDone(this);
  // Warning: |this| is likely deleted.
}

}  // namespace zxdb
