// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <iomanip>
#include <sstream>

#include "src/developer/debug/zxdb/client/arch_info.h"
#include "src/developer/debug/zxdb/client/disassembler.h"
#include "src/developer/debug/zxdb/client/frame.h"
#include "src/developer/debug/zxdb/client/memory_dump.h"
#include "src/developer/debug/zxdb/client/process.h"
#include "src/developer/debug/zxdb/client/session.h"
#include "src/developer/debug/zxdb/client/source_file_provider_impl.h"
#include "src/developer/debug/zxdb/client/system.h"
#include "src/developer/debug/zxdb/client/target.h"
#include "src/developer/debug/zxdb/common/err.h"
#include "src/developer/debug/zxdb/console/analyze_memory.h"
#include "src/developer/debug/zxdb/console/async_output_buffer.h"
#include "src/developer/debug/zxdb/console/command.h"
#include "src/developer/debug/zxdb/console/command_utils.h"
#include "src/developer/debug/zxdb/console/console.h"
#include "src/developer/debug/zxdb/console/format_context.h"
#include "src/developer/debug/zxdb/console/format_memory.h"
#include "src/developer/debug/zxdb/console/format_table.h"
#include "src/developer/debug/zxdb/console/input_location_parser.h"
#include "src/developer/debug/zxdb/console/output_buffer.h"
#include "src/developer/debug/zxdb/console/verbs.h"
#include "src/developer/debug/zxdb/symbols/code_block.h"
#include "src/developer/debug/zxdb/symbols/location.h"
#include "src/lib/fxl/strings/string_printf.h"

namespace zxdb {

namespace {

constexpr int kSizeSwitch = 1;
constexpr int kNumSwitch = 2;
constexpr int kOffsetSwitch = 3;
constexpr int kRawSwitch = 4;

// Gives 20 lines of output which fits on a terminal without scrolling (plus one line of help text,
// the next prompt, and the command itself).
constexpr uint32_t kDefaultAnalyzeByteSize = 160;

// Shared for commands that take both a num (lines, 8 bytes each), or a byte size.
Err ReadNumAndSize(const Command& cmd, std::optional<uint32_t>* out_size) {
  if (cmd.HasSwitch(kNumSwitch) && cmd.HasSwitch(kSizeSwitch))
    return Err("Can't specify both --num and --size.");

  if (cmd.HasSwitch(kSizeSwitch)) {
    // Size argument.
    uint32_t parsed;
    Err err = StringToUint32(cmd.GetSwitchValue(kSizeSwitch), &parsed);
    if (err.has_error())
      return err;
    *out_size = parsed;
  } else if (cmd.HasSwitch(kNumSwitch)) {
    // Num lines argument.
    uint32_t num_lines;
    Err err = StringToUint32(cmd.GetSwitchValue(kNumSwitch), &num_lines);
    if (err.has_error())
      return err;
    *out_size = num_lines * sizeof(uint64_t);  // Convert pointer count to size.
  }
  return Err();
}

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

// stack -------------------------------------------------------------------------------------------

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
Err DoStack(ConsoleContext* context, const Command& cmd) {
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
  err = ReadNumAndSize(cmd, &input_size);
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

// mem-analyze -------------------------------------------------------------------------------------

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
Err DoMemAnalyze(ConsoleContext* context, const Command& cmd) {
  // Only a process can have its memory read.
  Err err = cmd.ValidateNouns({Noun::kProcess});
  if (err.has_error())
    return err;

  // Length parameters.
  std::optional<uint32_t> input_size;
  err = ReadNumAndSize(cmd, &input_size);
  if (err.has_error())
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

// mem-read ----------------------------------------------------------------------------------------

void MemoryReadComplete(const Err& err, MemoryDump dump) {
  OutputBuffer out;
  if (err.has_error()) {
    out.Append(err);
  } else {
    MemoryFormatOptions opts;
    opts.show_addrs = true;
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
Err DoMemRead(ConsoleContext* context, const Command& cmd) {
  // Only a process can have its memory read.
  Err err = cmd.ValidateNouns({Noun::kProcess});
  if (err.has_error())
    return err;

  // Size argument (optional).
  std::optional<uint64_t> input_size;
  if (cmd.HasSwitch(kSizeSwitch)) {
    uint64_t read_size = 0;
    err = StringToUint64(cmd.GetSwitchValue(kSizeSwitch), &read_size);
    if (err.has_error())
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

// disassemble -------------------------------------------------------------------------------------

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
Err DoDisassemble(ConsoleContext* context, const Command& cmd) {
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

void AppendMemoryVerbs(std::map<Verb, VerbRecord>* verbs) {
  SwitchRecord size_switch(kSizeSwitch, true, "size", 's');
  SwitchRecord num_switch(kNumSwitch, true, "num", 'n');

  // Disassemble.
  VerbRecord disass(&DoDisassemble, &CompleteInputLocation, {"disassemble", "di"},
                    kDisassembleShortHelp, kDisassembleHelp, CommandGroup::kAssembly,
                    SourceAffinity::kAssembly);
  disass.switches.push_back(num_switch);
  disass.switches.push_back(SwitchRecord(kRawSwitch, false, "raw", 'r'));
  (*verbs)[Verb::kDisassemble] = std::move(disass);

  // Mem-analyze
  VerbRecord mem_analyze(&DoMemAnalyze, {"mem-analyze", "ma"}, kMemAnalyzeShortHelp,
                         kMemAnalyzeHelp, CommandGroup::kQuery);
  mem_analyze.switches.push_back(num_switch);
  mem_analyze.switches.push_back(size_switch);
  mem_analyze.param_type = VerbRecord::kOneParam;
  (*verbs)[Verb::kMemAnalyze] = std::move(mem_analyze);

  // Mem-read. Note: "x" is the GDB command to read memory.
  VerbRecord mem_read(&DoMemRead, &CompleteInputLocation, {"mem-read", "x"}, kMemReadShortHelp,
                      kMemReadHelp, CommandGroup::kQuery);
  mem_read.switches.push_back(size_switch);
  mem_read.param_type = VerbRecord::kOneParam;
  (*verbs)[Verb::kMemRead] = std::move(mem_read);

  // Stack.
  VerbRecord stack(&DoStack, {"stack", "st"}, kStackShortHelp, kStackHelp, CommandGroup::kQuery);
  stack.switches.push_back(num_switch);
  stack.switches.push_back(size_switch);
  stack.switches.push_back(SwitchRecord(kOffsetSwitch, true, "offset", 'o'));
  (*verbs)[Verb::kStack] = std::move(stack);
}

}  // namespace zxdb
