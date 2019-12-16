// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/console/commands/verb_mem_read.h"

#include "src/developer/debug/zxdb/client/memory_dump.h"
#include "src/developer/debug/zxdb/client/process.h"
#include "src/developer/debug/zxdb/client/target.h"
#include "src/developer/debug/zxdb/console/command.h"
#include "src/developer/debug/zxdb/console/command_utils.h"
#include "src/developer/debug/zxdb/console/console.h"
#include "src/developer/debug/zxdb/console/format_memory.h"
#include "src/developer/debug/zxdb/console/input_location_parser.h"
#include "src/developer/debug/zxdb/console/output_buffer.h"
#include "src/developer/debug/zxdb/console/verbs.h"

namespace zxdb {

namespace {

constexpr int kSizeSwitch = 1;

void MemoryReadComplete(const Err& err, MemoryDump dump) {
  OutputBuffer out;
  if (err.has_error()) {
    out.Append(err);
  } else {
    MemoryFormatOptions opts;
    opts.address_mode = MemoryFormatOptions::kAddresses;
    opts.show_ascii = true;
    opts.values_per_line = 16;
    opts.separator_every = 8;
    out.Append(FormatMemory(dump, dump.address(), static_cast<uint32_t>(dump.size()), opts));
  }
  Console::get()->Output(out);
}

const char kMemReadShortHelp[] = R"(mem-read / x: Read memory from debugged process.)";
const char kMemReadHelp[] =
    R"(mem-read [ --size=<bytes> ] <address-expression>

  Alias: "x"

  Reads memory from the process at the given address and prints it to the
  screen. Currently, only a byte-oriented hex dump format is supported.

  The address can be an explicit number or any expression ("help expressions")
  that evaluates to a memory address.

  When no size is given, the size will be the object size if a typed expression
  is given, otherwise 20 lines will be output.

  See also the "mem-analyze" command to print a memory analysis and the "stack"
  command to print a more useful dump of the raw stack.

Arguments

  --size=<bytes> | -s <bytes>
      Bytes to read. This defaults to the size of the function if a function
      name is given as the location, or 64 otherwise.

Examples

  x --size=128 0x75f19ba
  x &foo->bar
  mem-read --size=16 0x8f1763a7
  process 3 mem-read 83242384560
  process 3 mem-read main
)";
Err RunVerbMemRead(ConsoleContext* context, const Command& cmd) {
  // Only a process can have its memory read.
  if (Err err = cmd.ValidateNouns({Noun::kProcess}); err.has_error())
    return err;

  // Size argument (optional).
  std::optional<uint64_t> input_size;
  if (cmd.HasSwitch(kSizeSwitch)) {
    uint64_t read_size = 0;
    if (Err err = StringToUint64(cmd.GetSwitchValue(kSizeSwitch), &read_size); err.has_error())
      return err;
    input_size = read_size;
  }

  return EvalCommandAddressExpression(
      cmd, "mem-read", GetEvalContextForCommand(cmd),
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

        uint64_t read_size;
        if (input_size)
          read_size = *input_size;
        else if (object_size)
          read_size = *object_size;
        else
          read_size = 64;

        weak_target->GetProcess()->ReadMemory(address, read_size, &MemoryReadComplete);
      });

  return Err();
}

}  // namespace

VerbRecord GetMemReadVerbRecord() {
  // Mem-read. Note: "x" is the GDB command to read memory.
  VerbRecord mem_read(&RunVerbMemRead, &CompleteInputLocation, {"mem-read", "x"}, kMemReadShortHelp,
                      kMemReadHelp, CommandGroup::kQuery);
  mem_read.switches.emplace_back(kSizeSwitch, true, "size", 's');
  mem_read.param_type = VerbRecord::kOneParam;
  return mem_read;
}

}  // namespace zxdb
