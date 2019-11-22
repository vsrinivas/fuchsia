// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/console/format_frame.h"

#include <inttypes.h>

#include "src/developer/debug/zxdb/client/frame.h"
#include "src/developer/debug/zxdb/client/process.h"
#include "src/developer/debug/zxdb/client/thread.h"
#include "src/developer/debug/zxdb/console/command_utils.h"
#include "src/developer/debug/zxdb/console/console.h"
#include "src/developer/debug/zxdb/console/format_node_console.h"
#include "src/developer/debug/zxdb/console/output_buffer.h"
#include "src/developer/debug/zxdb/console/string_util.h"
#include "src/developer/debug/zxdb/symbols/function.h"
#include "src/developer/debug/zxdb/symbols/location.h"
#include "src/developer/debug/zxdb/symbols/value.h"
#include "src/lib/fxl/logging.h"
#include "src/lib/fxl/strings/string_printf.h"

namespace zxdb {

namespace {

fxl::RefPtr<AsyncOutputBuffer> ListCompletedFrames(Thread* thread,
                                                   const FormatLocationOptions& loc_opts,
                                                   bool long_format) {
  int active_frame_id = Console::get()->context().GetActiveFrameIdForThread(thread);

  auto out = fxl::MakeRefCounted<AsyncOutputBuffer>();

  // Formatting used for long format mode. These are minimal since there is often a lot of data.
  ConsoleFormatOptions format_options;
  format_options.verbosity = ConsoleFormatOptions::Verbosity::kMinimal;
  format_options.pointer_expand_depth = 1;
  format_options.max_depth = 3;

  // This doesn't use table output since the format of the stack frames is usually so unpredictable.
  const Stack& stack = thread->GetStack();
  if (stack.empty()) {
    if (thread->GetState() != debug_ipc::ThreadRecord::State::kSuspended &&
        !(thread->GetState() == debug_ipc::ThreadRecord::State::kBlocked &&
          thread->GetBlockedReason() == debug_ipc::ThreadRecord::BlockedReason::kException)) {
      // Make a nicer error message for the common case of requesting stack frames when the thread
      // is in the wrong state.
      out->Append(
          "Stack frames are only available when the thread is either suspended "
          "or blocked\nin an exception. Use \"pause\" to suspend it.");
    } else {
      out->Append("No stack frames.\n");
    }
  } else {
    for (int i = 0; i < static_cast<int>(stack.size()); i++) {
      if (i == active_frame_id)
        out->Append(GetCurrentRowMarker() + " ");
      else
        out->Append("  ");

      out->Append(OutputBuffer(Syntax::kSpecial, fxl::StringPrintf("%d ", i)));

      // Supply "-1" for the frame index to suppress printing (we already did it above).
      if (long_format)
        out->Append(FormatFrameLong(stack[i], loc_opts, format_options, -1));
      else
        out->Append(FormatFrame(stack[i], loc_opts, -1));

      out->Append("\n");
    }
  }

  out->Complete();
  return out;
}

}  // namespace

fxl::RefPtr<AsyncOutputBuffer> FormatFrameList(Thread* thread, const FormatLocationOptions& opts,
                                               bool long_format) {
  // Always request an up-to-date frame list from the agent. Various things could have changed and
  // the user is manually requesting a new list, so don't rely on the cached copy even if
  // Stack::has_all_frames() is true.
  auto out = fxl::MakeRefCounted<AsyncOutputBuffer>();
  thread->GetStack().SyncFrames(
      [thread = thread->GetWeakPtr(), opts, long_format, out](const Err& err) {
        if (!err.has_error() && thread)
          out->Complete(ListCompletedFrames(thread.get(), opts, long_format));
        else
          out->Complete("Thread exited, no frames.\n");
      });
  return out;
}

OutputBuffer FormatFrame(const Frame* frame, const FormatLocationOptions& opts, int id) {
  OutputBuffer out;

  if (id >= 0)
    out.Append(fxl::StringPrintf("Frame %d ", id));

  out.Append(FormatLocation(frame->GetLocation(), opts));
  return out;
}

fxl::RefPtr<AsyncOutputBuffer> FormatFrameLong(const Frame* frame,
                                               const FormatLocationOptions& loc_opts,
                                               const ConsoleFormatOptions& console_opts, int id) {
  auto out = fxl::MakeRefCounted<AsyncOutputBuffer>();

  if (id >= 0)
    out->Append(fxl::StringPrintf("Frame %d ", id));

  // Only print the location if it has symbols, otherwise the hex
  // address will be shown twice.
  const Location& location = frame->GetLocation();
  if (location.has_symbols())
    out->Append(FormatLocation(location, loc_opts));

  if (frame->IsInline())
    out->Append(Syntax::kComment, " (inline)");

  // Long format includes the IP address and stack pointer.
  out->Append(Syntax::kComment, fxl::StringPrintf("\n      IP = 0x%" PRIx64 ", SP = 0x%" PRIx64,
                                                  frame->GetAddress(), frame->GetStackPointer()));

  // Base pointer.
  // TODO(brettw) make this work when the frame base is asynchronous.
  if (auto bp = frame->GetBasePointer())
    out->Append(Syntax::kComment, fxl::StringPrintf(", base = 0x%" PRIx64, *bp));

  if (location.symbol()) {
    const Function* func = location.symbol().Get()->AsFunction();
    if (func) {
      // Always list function parameters in the order specified.
      for (const auto& param : func->parameters()) {
        const Variable* value = param.Get()->AsVariable();
        if (!value)
          continue;  // Symbols are corrupt.

        out->Append("\n      ");  // Indent.
        out->Append(FormatVariableForConsole(value, console_opts, frame->GetEvalContext()));
      }
    }
  }

  out->Complete();
  return out;
}

}  // namespace zxdb
