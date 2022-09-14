// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/console/commands/verb_mem_analyze.h"

#include "src/developer/debug/zxdb/console/analyze_memory.h"
#include "src/developer/debug/zxdb/console/command.h"
#include "src/developer/debug/zxdb/console/command_utils.h"
#include "src/developer/debug/zxdb/console/console.h"
#include "src/developer/debug/zxdb/console/output_buffer.h"
#include "src/developer/debug/zxdb/console/verbs.h"
#include "src/lib/fxl/strings/string_printf.h"

namespace zxdb {

namespace {

const char kMemAnalyzeShortHelp[] = "mem-analyze / ma: Analyze a memory region.";
const char kMemAnalyzeHelp[] =
    R"(mem-analyze [ --num=<lines> ] [ --size=<size> ] <address-expression>

  Alias: "ma"

  Prints a memory analysis. A memory analysis attempts to find pointers to
  code in pointer-aligned locations and annotates those values.

  The address can be an explicit number or any expression ("help expressions")
  that evaluates to a memory address.

  When no size is given, the size will be the object size if a typed expression
  is given, otherwise 20 lines will be output.

  See also "stack" which is specialized more for stacks (it includes the
  current thread's registers), and "mem-read" to display a simple hex dump.

Arguments

  --num=<lines> | -n <lines>
      The number of output lines. Each line is the size of one pointer, so
      the amount of memory displayed on a 64-bit system will be 8 × num_lines.
      Mutually exclusive with --size.

  --size=<bytes> | -s <bytes>
      The number of bytes to analyze. This will be rounded up to the nearest
      pointer boundary. Mutually exclusive with --num.

Examples

  ma 0x43011a14bfc8

  mem-analyze 0x43011a14bfc8

  process 3 mem-analyze 0x43011a14bfc8

  mem-analyze --num=128 0x43011a14bfc8
)";

Err RunVerbMemAnalyze(ConsoleContext* context, const Command& cmd) {
  // Only a process can have its memory read.
  if (Err err = cmd.ValidateNouns({Noun::kProcess}); err.has_error())
    return err;

  // Length parameters.
  std::optional<uint32_t> input_size;
  if (Err err = ReadAnalyzeNumAndSizeSwitches(cmd, &input_size); err.has_error())
    return err;

  return EvalCommandAddressExpression(
      cmd, "mem-analyze", GetEvalContextForCommand(cmd),
      [weak_target = cmd.target()->GetWeakPtr(), input_size](const Err& err, uint64_t address,
                                                             std::optional<uint64_t> object_size) {
        Console* console = Console::get();
        if (err.has_error()) {
          console->Output(err);  // Evaluation error.
          return;
        }
        if (!weak_target) {
          // Target has been destroyed during evaluation. Normally a message will be printed when
          // that happens so we can skip reporting the error.
          return;
        }

        Err run_err = AssertRunningTarget(&console->context(), "mem-read", weak_target.get());
        if (run_err.has_error()) {
          console->Output(run_err);
          return;
        }

        AnalyzeMemoryOptions opts;
        opts.process = weak_target->GetProcess();
        opts.begin_address = address;

        if (input_size)
          opts.bytes_to_read = *input_size;
        else if (object_size)
          opts.bytes_to_read = *object_size;
        else
          opts.bytes_to_read = kDefaultAnalyzeByteSize;

        AnalyzeMemory(opts, [bytes_to_read = opts.bytes_to_read](
                                const Err& err, OutputBuffer output, uint64_t next_addr) {
          if (err.has_error()) {
            output.Append(err);
          } else {
            // Help text for continuation.
            output.Append(
                Syntax::kComment,
                fxl::StringPrintf("↓ For more lines: ma -n %d 0x%" PRIx64,
                                  static_cast<int>(bytes_to_read / sizeof(uint64_t)), next_addr));
          }
          Console::get()->Output(output);
        });
      });
}

}  // namespace

const int kMemAnalyzeSizeSwitch = 1;
const int kMemAnalyzeNumSwitch = 2;

// Gives 20 lines of output which fits on a terminal without scrolling (plus one line of help text,
// the next prompt, and the command itself).
const uint32_t kDefaultAnalyzeByteSize = 160;

VerbRecord GetMemAnalyzeVerbRecord() {
  VerbRecord mem_analyze(&RunVerbMemAnalyze, {"mem-analyze", "ma"}, kMemAnalyzeShortHelp,
                         kMemAnalyzeHelp, CommandGroup::kQuery);
  mem_analyze.switches.emplace_back(kMemAnalyzeSizeSwitch, true, "size", 's');
  mem_analyze.switches.emplace_back(kMemAnalyzeNumSwitch, true, "num", 'n');
  mem_analyze.param_type = VerbRecord::kOneParam;
  return mem_analyze;
}

Err ReadAnalyzeNumAndSizeSwitches(const Command& cmd, std::optional<uint32_t>* out_size) {
  if (cmd.HasSwitch(kMemAnalyzeNumSwitch) && cmd.HasSwitch(kMemAnalyzeSizeSwitch))
    return Err("Can't specify both --num and --size.");

  if (cmd.HasSwitch(kMemAnalyzeSizeSwitch)) {
    // Size argument.
    uint32_t parsed;
    Err err = StringToUint32(cmd.GetSwitchValue(kMemAnalyzeSizeSwitch), &parsed);
    if (err.has_error())
      return err;
    *out_size = parsed;
  } else if (cmd.HasSwitch(kMemAnalyzeNumSwitch)) {
    // Num lines argument.
    uint32_t num_lines;
    Err err = StringToUint32(cmd.GetSwitchValue(kMemAnalyzeNumSwitch), &num_lines);
    if (err.has_error())
      return err;
    *out_size = num_lines * sizeof(uint64_t);  // Convert pointer count to size.
  }
  return Err();
}

}  // namespace zxdb
