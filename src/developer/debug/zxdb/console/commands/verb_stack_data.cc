// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/console/commands/verb_stack_data.h"

#include "src/developer/debug/zxdb/client/frame.h"
#include "src/developer/debug/zxdb/client/target.h"
#include "src/developer/debug/zxdb/console/analyze_memory.h"
#include "src/developer/debug/zxdb/console/async_output_buffer.h"
#include "src/developer/debug/zxdb/console/command.h"
#include "src/developer/debug/zxdb/console/command_utils.h"
#include "src/developer/debug/zxdb/console/commands/verb_mem_analyze.h"
#include "src/developer/debug/zxdb/console/console.h"
#include "src/developer/debug/zxdb/console/output_buffer.h"
#include "src/developer/debug/zxdb/console/verbs.h"
#include "src/lib/fxl/strings/string_printf.h"

namespace zxdb {

namespace {

// Needs to not collide with the kMemAnalyze*Switches.
constexpr int kOffsetSwitch = 100;

const char kStackDataShortHelp[] = "stack-data: Analyze stack data.";
const char kStackDataHelp[] =
    R"(stack-data [ --offset=<offset> ] [ --num=<lines> ] [ --size=<bytes> ]
           [ <address-expression> ]

  Prints a stack analysis. This is a special case of "mem-analyze" that
  defaults to showing the memory address starting at the current frame's stack
  pointer, and annotates the values with the current thread's registers and
  stack frames.

  An explicit address can optionally be provided to begin dumping to dump at
  somewhere other than the current frame's stack pointer (this address can be
  any expression that evaluates to an address, see "help expressions"), or you
  can provide an --offset from the current stack position.

Arguments

  --num=<lines> | -n <lines>
      The number of output lines. Each line is the size of one pointer, so
      the amount of memory displayed on a 64-bit system will be 8 × num_lines.
      Mutually exclusive with --size.

  --offset=<offset> | -o <offset>
      Offset from the stack pointer to begin dumping. Mutually exclusive with
      <address>.

  --size=<bytes> | -s <bytes>
      The number of bytes to analyze. This will be rounded up to the nearest
      pointer boundary. Mutually exclusive with --num.

Examples

  stack-data
  thread 2 stack-data

  stack-data --num=128 0x43011a14bfc8
)";
void RunVerbStackData(const Command& cmd, fxl::RefPtr<CommandContext> cmd_context) {
  Err err =
      AssertStoppedThreadWithFrameCommand(cmd_context->GetConsoleContext(), cmd, "stack-data");
  if (err.has_error())
    return cmd_context->ReportError(err);

  AnalyzeMemoryOptions opts;
  opts.process = cmd.target()->GetProcess();
  opts.thread = cmd.thread();

  // Begin address.
  if (cmd.args().size() == 1) {
    // Explicitly provided start address.
    Err err = StringToUint64(cmd.args()[0], &opts.begin_address);
    if (err.has_error())
      return cmd_context->ReportError(err);
  } else if (cmd.args().size() > 1) {
    return cmd_context->ReportError(Err("Too many args to \"stack-data\", expecting 0 or 1."));
  } else {
    // Use implicit SP from the frame (with optional --offset).
    opts.begin_address = cmd.frame()->GetStackPointer();
    if (cmd.HasSwitch(kOffsetSwitch)) {
      int offset = 0;
      Err err = StringToInt(cmd.GetSwitchValue(kOffsetSwitch), &offset);
      if (err.has_error())
        return cmd_context->ReportError(err);
      opts.begin_address += offset;
    }
  }

  // Length parameters.
  std::optional<uint32_t> input_size;
  err = ReadAnalyzeNumAndSizeSwitches(cmd, &input_size);
  if (err.has_error())
    return cmd_context->ReportError(err);
  if (!input_size)
    opts.bytes_to_read = kDefaultAnalyzeByteSize;
  else
    opts.bytes_to_read = *input_size;

  AnalyzeMemory(opts, [bytes_to_read = opts.bytes_to_read, cmd_context](
                          const Err& err, OutputBuffer output, uint64_t next_addr) {
    if (err.has_error())
      return cmd_context->ReportError(err);

    // Group the otuput into one write (not strictly necessary but the current test infrastructure
    // expects everything to be written in one chunk).
    OutputBuffer out(output);

    // Help text for continuation.
    out.Append(OutputBuffer(
        Syntax::kComment,
        fxl::StringPrintf("↓ For more lines: stack-data -n %d 0x%" PRIx64,
                          static_cast<int>(bytes_to_read / sizeof(uint64_t)), next_addr)));
    cmd_context->Output(out);
  });
}

}  // namespace

VerbRecord GetStackDataVerbRecord() {
  VerbRecord stack(&RunVerbStackData, {"stack-data"}, kStackDataShortHelp, kStackDataHelp,
                   CommandGroup::kQuery);
  stack.switches.emplace_back(kMemAnalyzeSizeSwitch, true, "size", 's');
  stack.switches.emplace_back(kMemAnalyzeNumSwitch, true, "num", 'n');
  stack.switches.push_back(SwitchRecord(kOffsetSwitch, true, "offset", 'o'));
  return stack;
}

}  // namespace zxdb
