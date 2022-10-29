// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/console/format_frame.h"

#include <inttypes.h>
#include <lib/syslog/cpp/macros.h>

#include "src/developer/debug/zxdb/client/frame.h"
#include "src/developer/debug/zxdb/client/process.h"
#include "src/developer/debug/zxdb/client/thread.h"
#include "src/developer/debug/zxdb/console/command_utils.h"
#include "src/developer/debug/zxdb/console/console.h"
#include "src/developer/debug/zxdb/console/format_location.h"
#include "src/developer/debug/zxdb/console/format_node_console.h"
#include "src/developer/debug/zxdb/console/output_buffer.h"
#include "src/developer/debug/zxdb/console/string_util.h"
#include "src/developer/debug/zxdb/symbols/function.h"
#include "src/developer/debug/zxdb/symbols/location.h"
#include "src/developer/debug/zxdb/symbols/value.h"
#include "src/lib/fxl/strings/string_printf.h"

namespace zxdb {

namespace {

// Appends the frame heading (indent, active marker, frame number) for a stack entry. The frame
// numbers are expressed in a range (inclusive) to support pretty-printing. They should be the same
// for normal stack entries.
void AppendFrameNumber(size_t begin_range, size_t end_range, size_t active_frame_index,
                       AsyncOutputBuffer* out) {
  if (active_frame_index >= begin_range && active_frame_index <= end_range)
    out->Append(GetCurrentRowMarker() + " ");
  else
    out->Append("  ");

  if (begin_range == end_range) {
    out->Append(OutputBuffer(Syntax::kSpecial, fxl::StringPrintf("%zu ", begin_range)));
  } else {
    out->Append(OutputBuffer(Syntax::kSpecial, fxl::StringPrintf("%zu", begin_range)));
    out->Append(Syntax::kComment, "…");
    out->Append(OutputBuffer(Syntax::kSpecial, fxl::StringPrintf("%zu ", end_range)));
  }
}

bool ThreadIsSuspendedOrBlockedOnException(Thread* thread) {
  std::optional<debug_ipc::ThreadRecord::State> state_or = thread->GetState();
  if (!state_or)
    return false;  // Unknown state.

  return *state_or == debug_ipc::ThreadRecord::State::kSuspended ||
         (*state_or == debug_ipc::ThreadRecord::State::kBlocked &&
          thread->GetBlockedReason() == debug_ipc::ThreadRecord::BlockedReason::kException);
}

fxl::RefPtr<AsyncOutputBuffer> ListCompletedFrames(Thread* thread, const FormatStackOptions& opts) {
  size_t active_frame_id =
      static_cast<size_t>(Console::get()->context().GetActiveFrameIdForThread(thread));

  auto out = fxl::MakeRefCounted<AsyncOutputBuffer>();

  // This doesn't use table output since the format of the stack frames is usually so unpredictable.
  const Stack& stack = thread->GetStack();
  if (stack.empty()) {
    if (!ThreadIsSuspendedOrBlockedOnException(thread)) {
      // Make a nicer error message for the common case of requesting stack frames when the thread
      // is in the wrong state.
      out->Append(
          "Stack frames are only available when the thread is either suspended "
          "or blocked\nin an exception. Use \"pause\" to suspend it.");
    } else {
      out->Append("No stack frames.\n");
    }
    out->Complete();
    return out;
  }

  std::vector<PrettyStackManager::FrameEntry> pretty_stack;
  if (opts.pretty_stack) {
    pretty_stack = opts.pretty_stack->ProcessStack(stack);
  } else {
    pretty_stack.resize(stack.size());
    for (size_t i = 0; i < stack.size(); i++) {
      pretty_stack[i].begin_index = i;
      pretty_stack[i].frames.push_back(stack[i]);
    }
  }

  for (const auto& entry : pretty_stack) {
    // Stack item pretty-printing only happens if there's a pretty match and the current entry
    // isn't within the range of midden frames.
    //
    // One case this doesn't handle is if expanding the range of pretty-stacks means a smaller
    // matcher might apply that doesn't overlap the user's current frame. To support that, we'd
    // need to move the logic of no prettifying the current frame to the PrettyStackManager,
    bool use_pretty = entry.match && !(active_frame_id > entry.begin_index &&
                                       active_frame_id < entry.begin_index + entry.frames.size());

    if (use_pretty) {
      AppendFrameNumber(entry.begin_index, entry.begin_index + entry.frames.size() - 1,
                        active_frame_id, out.get());
      out->Append("«" + entry.match.description + "»");
      out->Append(Syntax::kComment, " (-r expands)\n");
    } else {
      for (size_t i = 0; i < entry.frames.size(); i++) {
        AppendFrameNumber(entry.begin_index + i, entry.begin_index + i, active_frame_id, out.get());

        // Supply "-1" for the frame index to suppress printing (we already did it).
        out->Append(FormatFrame(stack[entry.begin_index + i], opts.frame, -1));
        out->Append("\n");
      }
    }
  }

  out->Complete();
  return out;
}

}  // namespace

fxl::RefPtr<AsyncOutputBuffer> FormatStack(Thread* thread, bool force_update,
                                           const FormatStackOptions& opts) {
  auto out = fxl::MakeRefCounted<AsyncOutputBuffer>();
  if (!force_update && thread->GetStack().has_all_frames()) {
    out->Complete(ListCompletedFrames(thread, opts));
    return out;
  }

  // Request a stack update.
  thread->GetStack().SyncFrames([thread = thread->GetWeakPtr(), opts, out](const Err& err) {
    if (!err.has_error() && thread)
      out->Complete(ListCompletedFrames(thread.get(), opts));
    else
      out->Complete("Thread exited, no frames.\n");
  });
  return out;
}

fxl::RefPtr<AsyncOutputBuffer> FormatFrame(const Frame* frame, const FormatFrameOptions& opts,
                                           int id) {
  auto out = fxl::MakeRefCounted<AsyncOutputBuffer>();

  if (id >= 0) {
    out->Append("Frame ");
    out->Append(Syntax::kSpecial, std::to_string(id));
    out->Append(" ");
  }

  const Location& location = frame->GetLocation();
  out->Append(FormatLocation(location, opts.loc));

  if (frame->IsInline())
    out->Append(Syntax::kComment, " (inline)");

  // IP address and stack pointers.
  if (opts.detail == FormatFrameOptions::kVerbose) {
    out->Append(Syntax::kComment, fxl::StringPrintf("\n      IP = 0x%" PRIx64 ", SP = 0x%" PRIx64,
                                                    frame->GetAddress(), frame->GetStackPointer()));

    // TODO(brettw) make this work when the frame base is asynchronous.
    if (auto bp = frame->GetBasePointer())
      out->Append(Syntax::kComment, fxl::StringPrintf(", base = 0x%" PRIx64, *bp));
  }

  if (opts.detail != FormatFrameOptions::kSimple && location.symbol()) {
    const Function* func = location.symbol().Get()->As<Function>();
    if (func) {
      // Always list function parameters in the order specified.
      for (const auto& param : func->parameters()) {
        const Variable* value = param.Get()->As<Variable>();
        if (!value)
          continue;  // Symbols are corrupt.

        out->Append("\n      ");  // Indent.
        out->Append(FormatVariableForConsole(value, opts.variable, frame->GetEvalContext()));
      }
    }
  }

  out->Complete();
  return out;
}

}  // namespace zxdb
