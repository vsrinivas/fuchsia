// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/console/commands/verb_disassemble.h"

#include "src/developer/debug/zxdb/client/arch_info.h"
#include "src/developer/debug/zxdb/client/frame.h"
#include "src/developer/debug/zxdb/client/memory_dump.h"
#include "src/developer/debug/zxdb/client/process.h"
#include "src/developer/debug/zxdb/client/session.h"
#include "src/developer/debug/zxdb/client/source_file_provider_impl.h"
#include "src/developer/debug/zxdb/console/command.h"
#include "src/developer/debug/zxdb/console/command_utils.h"
#include "src/developer/debug/zxdb/console/console.h"
#include "src/developer/debug/zxdb/console/format_context.h"
#include "src/developer/debug/zxdb/console/input_location_parser.h"
#include "src/developer/debug/zxdb/console/output_buffer.h"
#include "src/developer/debug/zxdb/console/verbs.h"
#include "src/developer/debug/zxdb/symbols/code_block.h"
#include "src/developer/debug/zxdb/symbols/location.h"

namespace zxdb {

namespace {

constexpr int kNumSwitch = 1;
constexpr int kRawSwitch = 2;

const char kDisassembleShortHelp[] = "disassemble / di: Disassemble machine instructions.";
const char kDisassembleHelp[] =
    R"(disassemble [ --num=<lines> ] [ --raw ] [ <location> ]

  Alias: "di"

  Disassembles machine instructions at the given location. If no location is
  given, the instruction pointer of the thread/frame will be used. If the
  thread is not stopped, you must specify a start address.

Location arguments

)" LOCATION_ARG_HELP("mem-analyze")
        R"(
Arguments

  --num=<lines> | -n <lines>
      The number of lines/instructions to emit. Defaults to the instructions
      in the given function (if the location is a function name), or 16
      otherwise.

  --raw | -r
      Output raw bytes in addition to the decoded instructions.

Examples

  di
  disassemble
      Disassembles starting at the current thread's instruction pointer.

  thread 3 disassemble -n 128
      Disassembles 128 instructions starting at thread 3's instruction
      pointer.

  di MyClass::MyFunc
      Disassembles the given function.

  frame 3 disassemble
  thread 2 frame 3 disassemble
      Disassembles starting at the thread's "frame 3" instruction pointer
      (which will be the call return address).

  process 1 disassemble 0x7b851239a0
      Disassembles instructions in process 1 starting at the given address.
)";

// Converts argument 0 (required or it will produce an error) and converts it to a unique location
// (or error). If the input indicates a thing that has an intrinsic size like a function name, the
// size will be placed in *location_size. Otherwise, *location_size will be 0.
//
// The command_name is used for writing the current command to error messages.
Err ReadLocation(const Command& cmd, const char* command_name, Location* location,
                 uint64_t* location_size) {
  *location_size = 0;
  if (cmd.args().size() != 1)
    return Err("%s requires exactly one argument specifying a location.", command_name);

  // We need to check the type of the parsed input location so parse and resolve in two steps.
  std::vector<InputLocation> input_locations;
  if (Err err = ParseLocalInputLocation(cmd.frame(), cmd.args()[0], &input_locations);
      err.has_error())
    return err;
  FXL_DCHECK(!input_locations.empty());

  if (Err err = ResolveUniqueInputLocation(cmd.target()->GetProcess()->GetSymbols(),
                                           input_locations, true, location);
      err.has_error())
    return err;

  // Some symbols can give us sizes. All input locations will have the same type (matching the user
  // input type).
  if (input_locations[0].type == InputLocation::Type::kName) {
    if (location->symbol()) {
      if (const CodeBlock* block = location->symbol().Get()->AsCodeBlock()) {
        *location_size = block->GetFullRange(location->symbol_context()).size();
      }
    }
  }
  return Err();
}

// Completion callback after reading process memory.
void CompleteDisassemble(const Err& err, MemoryDump dump, fxl::WeakPtr<Process> weak_process,
                         const FormatAsmOpts& options) {
  Console* console = Console::get();
  if (err.has_error()) {
    console->Output(err);
    return;
  }

  if (!weak_process)
    return;  // Give up if the process went away.

  OutputBuffer out;
  Err format_err =
      FormatAsmContext(weak_process->session()->arch_info(), dump, options, weak_process.get(),
                       SourceFileProviderImpl(weak_process->GetTarget()->settings()), &out);
  if (format_err.has_error()) {
    console->Output(err);
    return;
  }

  console->Output(out);
}

Err RunDisassembleVerb(ConsoleContext* context, const Command& cmd) {
  // Can take process overrides (to specify which process to read) and thread and frame ones (to
  // specify which thread to read the instruction pointer from).
  Err err = cmd.ValidateNouns({Noun::kProcess, Noun::kThread, Noun::kFrame});
  if (err.has_error())
    return err;

  err = AssertRunningTarget(context, "disassemble", cmd.target());
  if (err.has_error())
    return err;

  Location location;
  uint64_t location_size = 0;
  if (cmd.args().empty()) {
    // No args: implicitly read the frame's instruction pointer.
    //
    // TODO(brettw) by default it would be nice if this showed a few lines of disassembly before the
    // given address. Going backwards in x86 can be dicey though, the formatter may have to
    // guess-and-check about a good starting boundary for the dump.
    Frame* frame = cmd.frame();
    if (!frame) {
      return Err(
          "There is no frame to read the instruction pointer from. The thread\n"
          "must be stopped to use the implicit current address. Otherwise,\n"
          "you must supply an explicit address to disassemble.");
    }
    location = frame->GetLocation();
  } else {
    err = ReadLocation(cmd, "disassemble", &location, &location_size);
    if (err.has_error())
      return err;
  }

  FormatAsmOpts options;
  options.emit_addresses = true;

  if (cmd.frame())
    options.active_address = cmd.frame()->GetAddress();

  // We may want to add an option for this.
  options.include_source = true;

  // Num argument (optional).
  //
  // When there is no known byte size, compute the max bytes requires to get the requested
  // instructions. It doesn't matter if we request more memory than necessary so use a high bound.
  size_t size = 0;
  if (cmd.HasSwitch(kNumSwitch)) {
    // Num lines explicitly given.
    uint64_t num_instr = 0;
    err = StringToUint64(cmd.GetSwitchValue(kNumSwitch), &num_instr);
    if (err.has_error())
      return err;
    options.max_instructions = num_instr;
    size = options.max_instructions * context->session()->arch_info()->max_instr_len();
  } else if (location_size > 0) {
    // Byte size is known.
    size = location_size;
  } else {
    // Default instruction count when no symbol and no explicit size is given.
    options.max_instructions = 16;
    size = options.max_instructions * context->session()->arch_info()->max_instr_len();
  }

  // Show bytes.
  options.emit_bytes = cmd.HasSwitch(kRawSwitch);

  // Schedule memory request.
  Process* process = cmd.target()->GetProcess();
  process->ReadMemory(location.address(), size,
                      [options, process = process->GetWeakPtr()](const Err& err, MemoryDump dump) {
                        CompleteDisassemble(err, std::move(dump), std::move(process), options);
                      });
  return Err();
}

}  // namespace

VerbRecord GetDisassembleVerbRecord() {
  VerbRecord disass(&RunDisassembleVerb, &CompleteInputLocation, {"disassemble", "di"},
                    kDisassembleShortHelp, kDisassembleHelp, CommandGroup::kAssembly,
                    SourceAffinity::kAssembly);
  disass.switches.emplace_back(kNumSwitch, true, "num", 'n');
  disass.switches.emplace_back(kRawSwitch, false, "raw", 'r');
  return disass;
}

}  // namespace zxdb
