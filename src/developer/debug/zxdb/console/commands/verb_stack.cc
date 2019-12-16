// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/console/commands/verb_stack.h"

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

const char kStackShortHelp[] = "stack / st: Analyze the stack.";
const char kStackHelp[] =
    R"(stack [ --offset=<offset> ] [ --num=<lines> ] [ --size=<bytes> ]
           [ <address-expression> ]

  Alias: "st"

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

  stack
  thread 2 stack

  stack --num=128 0x43011a14bfc8
)";
Err RunVerbStack(ConsoleContext* context, const Command& cmd) {
  Err err = AssertStoppedThreadWithFrameCommand(context, cmd, "stack");
  if (err.has_error())
    return err;

  AnalyzeMemoryOptions opts;
  opts.process = cmd.target()->GetProcess();
  opts.thread = cmd.thread();

  // Begin address.
  if (cmd.args().size() == 1) {
    // Explicitly provided start address.
    Err err = StringToUint64(cmd.args()[0], &opts.begin_address);
    if (err.has_error())
      return err;
  } else if (cmd.args().size() > 1) {
    return Err("Too many args to \"stack\", expecting 0 or 1.");
  } else {
    // Use implicit SP from the frame (with optional --offset).
    opts.begin_address = cmd.frame()->GetStackPointer();
    if (cmd.HasSwitch(kOffsetSwitch)) {
      int offset = 0;
      Err err = StringToInt(cmd.GetSwitchValue(kOffsetSwitch), &offset);
      if (err.has_error())
        return err;
      opts.begin_address += offset;
    }
  }

  // Length parameters.
  std::optional<uint32_t> input_size;
  err = ReadAnalyzeNumAndSizeSwitches(cmd, &input_size);
  if (err.has_error())
    return err;
  if (!input_size)
    opts.bytes_to_read = kDefaultAnalyzeByteSize;
  else
    opts.bytes_to_read = *input_size;

  auto async_output = fxl::MakeRefCounted<AsyncOutputBuffer>();
  Console::get()->Output(async_output);

  AnalyzeMemory(opts, [bytes_to_read = opts.bytes_to_read, async_output](
                          const Err& err, OutputBuffer output, uint64_t next_addr) {
    async_output->Append(std::move(output));
    if (err.has_error()) {
      async_output->Append(err);
    } else {
      // Help text for continuation.
      async_output->Append(
          Syntax::kComment,
          fxl::StringPrintf("↓ For more lines: stack -n %d 0x%" PRIx64,
                            static_cast<int>(bytes_to_read / sizeof(uint64_t)), next_addr));
    }
    async_output->Complete();
  });
  return Err();
}

}  // namespace

VerbRecord GetStackVerbRecord() {
  VerbRecord stack(&RunVerbStack, {"stack", "st"}, kStackShortHelp, kStackHelp,
                   CommandGroup::kQuery);
  stack.switches.emplace_back(kMemAnalyzeSizeSwitch, true, "size", 's');
  stack.switches.emplace_back(kMemAnalyzeNumSwitch, true, "num", 'n');
  stack.switches.push_back(SwitchRecord(kOffsetSwitch, true, "offset", 'o'));
  return stack;
}

}  // namespace zxdb
