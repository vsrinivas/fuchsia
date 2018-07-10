// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/console/format_frame.h"

#include <inttypes.h>

#include "garnet/bin/zxdb/client/frame.h"
#include "garnet/bin/zxdb/client/symbols/location.h"
#include "garnet/bin/zxdb/client/thread.h"
#include "garnet/bin/zxdb/console/command_utils.h"
#include "garnet/bin/zxdb/console/console.h"
#include "garnet/bin/zxdb/console/output_buffer.h"
#include "garnet/bin/zxdb/console/string_util.h"
#include "lib/fxl/strings/string_printf.h"

namespace zxdb {

namespace {

void ListCompletedFrames(Thread* thread, bool long_format) {
  Console* console = Console::get();
  int active_frame_id = console->context().GetActiveFrameIdForThread(thread);

  OutputBuffer out;

  // This doesn't use table output since the format of the stack frames is
  // usually so unpredictable.
  const auto& frames = thread->GetFrames();
  if (frames.empty()) {
    out.Append("No stack frames.\n");
  } else {
    for (int i = 0; i < static_cast<int>(frames.size()); i++) {
      if (i == active_frame_id)
        out.Append(GetRightArrow() + " ");
      else
        out.Append("  ");

      // The frames can get very long due to templates so use reverse video
      // to highlight the indices so the lines can be found.
      out.Append(Syntax::kReversed, fxl::StringPrintf(" %d ", i));
      out.Append(" ");
      FormatFrame(frames[i], &out, long_format);

      out.Append("\n");
    }
  }
  console->Output(std::move(out));
}

}  // namespace

void OutputFrameList(Thread* thread, bool long_format) {
  if (thread->HasAllFrames()) {
    ListCompletedFrames(thread, long_format);
  } else {
    thread->SyncFrames([ thread = thread->GetWeakPtr(), long_format ]() {
      Console* console = Console::get();
      if (thread)
        ListCompletedFrames(thread.get(), long_format);
      else
        console->Output("Thread exited, no frames.\n");
    });
  }
}

void FormatFrame(const Frame* frame, OutputBuffer* out, bool long_format,
                 int id) {
  if (id >= 0)
    out->Append(fxl::StringPrintf("Frame %d ", id));

  if (long_format) {
    // Long format.
    out->Append(
        fxl::StringPrintf("0x%" PRIx64, frame->GetAddress()));

    // Only print the location if it has symbols, otherwise the hex
    // address will be shown twice.
    if (frame->GetLocation().has_symbols())
      out->Append(" " + DescribeLocation(frame->GetLocation(), false));

    // TODO(brettw) add function parameters here.
  } else {
    // Short format.
    out->Append(DescribeLocation(frame->GetLocation(), false));
  }
}

}  // namespace zxdb
