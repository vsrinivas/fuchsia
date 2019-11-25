// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/client/thread_controller.h"

#include <stdarg.h>

#include "src/developer/debug/zxdb/client/frame.h"
#include "src/developer/debug/zxdb/client/setting_schema_definition.h"
#include "src/developer/debug/zxdb/client/thread.h"
#include "src/developer/debug/zxdb/symbols/function.h"

namespace zxdb {

ThreadController::ThreadController() = default;

ThreadController::~ThreadController() = default;

void ThreadController::Log(const char* format, ...) const {
  FXL_DCHECK(thread_);  // If uninitialized, the log setting hasn't been read yet.
  if (!enable_debug_logging_)
    return;

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
std::string ThreadController::FrameFunctionNameForLog(const Frame* frame) {
  const char kNone[] = "<none>";

  const Location& loc = frame->GetLocation();
  if (!loc.symbol())
    return kNone;

  const Function* func = loc.symbol().Get()->AsFunction();
  if (!func)
    return kNone;

  return func->GetFullName();
}

void ThreadController::SetThread(Thread* thread) {
  thread_ = thread;
  enable_debug_logging_ = thread_->settings().GetBool(ClientSettings::Thread::kDebugStepping);
}

void ThreadController::SetInlineFrameIfAmbiguous(InlineFrameIs comparison,
                                                 FrameFingerprint fingerprint) {
  Stack& stack = thread()->GetStack();

  // Reset any hidden inline frames so we can iterate through all of them (and we'll leave this
  // reset to 0 if the requested one isn't found).
  size_t old_hide_count = stack.hide_ambiguous_inline_frame_count();
  stack.SetHideAmbiguousInlineFrameCount(0);

  for (size_t i = 0; i < stack.size(); i++) {
    const Frame* frame = stack[i];
    auto found = stack.GetFrameFingerprint(i);

    // To be ambiguous, all frames to here need to be at the same address and all inline frames need
    // to be at the beginning of one of their ranges. (the physical frame also needs matching but
    // its range doesn't count).
    bool is_inline = frame->IsInline();

    if (found == fingerprint) {
      // Found it.
      if (comparison == InlineFrameIs::kEqual) {
        // Make this one the top of the stack.
        stack.SetHideAmbiguousInlineFrameCount(i);
        return;
      } else {  // comparison == InlineFrameIs::kOneBefore.
        // Make the one below this frame topmost. That requires the current frame be inline since it
        // will be hidden.
        if (is_inline) {
          stack.SetHideAmbiguousInlineFrameCount(i + 1);
          return;
        }
      }
      break;
    }

    if (!is_inline)
      break;  // Don't check below the first physical frame.

    // The fingerprint can be set on a frame as long as all frames above it were ambiguous, but the
    // frame being set to is usually not ambiguous (it's often the physical frame that calls an
    // inline function, for example).
    if (!frame->IsAmbiguousInlineLocation())
      break;
  }

  if (old_hide_count)
    stack.SetHideAmbiguousInlineFrameCount(old_hide_count);
}

void ThreadController::NotifyControllerDone() {
  thread_->NotifyControllerDone(this);
  // Warning: |this| is likely deleted.
}

}  // namespace zxdb
