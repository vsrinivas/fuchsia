// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/console/commands/verb_sym_debug.h"

#include "src/developer/debug/zxdb/client/frame.h"
#include "src/developer/debug/zxdb/client/process.h"
#include "src/developer/debug/zxdb/client/target.h"
#include "src/developer/debug/zxdb/common/string_util.h"
#include "src/developer/debug/zxdb/console/command.h"
#include "src/developer/debug/zxdb/console/command_utils.h"
#include "src/developer/debug/zxdb/console/console.h"
#include "src/developer/debug/zxdb/console/format_location.h"
#include "src/developer/debug/zxdb/console/format_name.h"
#include "src/developer/debug/zxdb/console/format_table.h"
#include "src/developer/debug/zxdb/console/output_buffer.h"
#include "src/developer/debug/zxdb/console/string_util.h"
#include "src/developer/debug/zxdb/console/verbs.h"
#include "src/developer/debug/zxdb/symbols/dwarf_unit.h"
#include "src/developer/debug/zxdb/symbols/function.h"
#include "src/developer/debug/zxdb/symbols/line_table.h"
#include "src/developer/debug/zxdb/symbols/loaded_module_symbols.h"
#include "src/developer/debug/zxdb/symbols/module_symbols.h"
#include "src/developer/debug/zxdb/symbols/process_symbols.h"
#include "src/developer/debug/zxdb/symbols/target_symbols.h"

namespace zxdb {

namespace {

constexpr int kInlineSwitch = 1;
constexpr int kInlineTreeSwitch = 2;
constexpr int kLineSwitch = 3;

const char kSymDebugShortHelp[] = "sym-debug: Print debug symbol information.";
const char kSymDebugHelp[] =
    R"(sym-debug ( -i | -t | -l ) [ <address-expression> ]

  This command takes a flag for what to print and an optional address. If no
  address-expression is given, the current frame's instruction pointer will be
  used.

Options

  --inlines | -i
      Prints the chain of inline functions covering the address. The plysical
      (non-inlined) function will be at the bottom. The address ranges for the
      inline will be shows after the name.

  --inline-tree | -t
      Dumps the inline tree for the function covering the address. This will
      show each inline function indented according to its nesting. Each inline
      will also contain the set of address ranges

  --line | -l
      Dumps the DWARF line table sequence containing the address.

Examples

  sym-debug -l
  sym-debug -i 0x56cfe7b4
)";

// Returns the inline chain of functions corresponding to the given address. Guaranteed to return
// nonempty if there's no error.
ErrOr<std::vector<fxl::RefPtr<Function>>> GetInlineChainAtAddress(ProcessSymbols* process_symbols,
                                                                  uint64_t address) {
  // With an address, we should get exactly one location back.
  std::vector<Location> locs = process_symbols->ResolveInputLocation(InputLocation(address));
  if (locs.size() != 1)
    return Err("Error: more than one location matched.");
  const Location& loc = locs[0];

  if (!loc.symbol())
    return Err("No symbol at address " + to_hex_string(address) + ".\n");

  const Function* func = loc.symbol().Get()->AsFunction();
  if (!func)
    return Err("No function at address " + to_hex_string(address) + ".\n");

  return func->GetInlineChain();
}

// Appends the function name and its code ranges to the given output buffer.
void AppendFunctionAndRanges(const SymbolContext& context, const Function* function,
                             OutputBuffer& out) {
  FormatFunctionNameOptions options;
  options.name.bold_last = true;
  // Lots of inline functions are long templates and they make things much harder to follow. For the
  // typical debug tasks, this information is not needed, so omit the templates.
  options.name.elide_templates = true;
  options.params = FormatFunctionNameOptions::kNoParams;

  out.Append(FormatFunctionName(function, options));
  out.Append(": ");
  out.Append(Syntax::kComment, function->GetAbsoluteCodeRanges(context).ToString());
  out.Append("\n");
}

OutputBuffer DumpInlineChain(ProcessSymbols* process_symbols, uint64_t address) {
  OutputBuffer out;

  auto chain = GetInlineChainAtAddress(process_symbols, address);
  if (chain.has_error()) {
    out.Append(chain.err());
    return out;
  }

  // Expect all functions in an inline chain to have the same context (because they share the same
  // physical function).
  SymbolContext context = chain.value()[0]->GetSymbolContext(process_symbols);

  // Pring each inline with the inline depth (going back in time to the non-inlined function).
  int index = static_cast<int>(chain.value().size()) - 1;
  for (const auto& func : chain.value()) {
    out.Append(Syntax::kSpecial, "  " + std::to_string(index) + " ");
    index--;
    AppendFunctionAndRanges(context, func.get(), out);
  }
  return out;
}

// Recursive function to print the inlines.
void PrintInlineRecursive(const SymbolContext& context, uint64_t address, const CodeBlock* block,
                          int indent, OutputBuffer& output) {
  const Function* function = block->AsFunction();

  // This block could be a lexical block (takes no indents and produces no output), or a function.
  int next_indent = indent;
  if (function) {
    // Mark the inlines that contain the given address.
    AddressRanges ranges = function->GetAbsoluteCodeRanges(context);
    if (ranges.InRange(address))
      output.Append(GetCurrentRowMarker() + " ");
    else
      output.Append("  ");  // Spacer since there's no marker.

    output.Append(std::string(indent, ' '));  // Indentation.
    AppendFunctionAndRanges(context, function, output);

    next_indent += 2;  // When there's a function, indent the children.
  }

  // Print child blocks.
  for (auto& child : block->inner_blocks()) {
    if (const CodeBlock* child_block = child.Get()->AsCodeBlock())
      PrintInlineRecursive(context, address, child_block, next_indent, output);
  }
}

OutputBuffer DumpInlineTree(ProcessSymbols* process_symbols, uint64_t address) {
  OutputBuffer out;

  auto chain = GetInlineChainAtAddress(process_symbols, address);
  if (chain.has_error()) {
    out.Append(chain.err());
    return out;
  }

  const Function* function = chain.value().back().get();
  SymbolContext context = function->GetSymbolContext(process_symbols);

  // The containing function will be the first element of the inline chain.
  PrintInlineRecursive(context, address, function, 0, out);
  return out;
}

OutputBuffer DumpLineTable(ProcessSymbols* process_symbols, uint64_t address) {
  const LoadedModuleSymbols* loaded_module = process_symbols->GetModuleForAddress(address);
  if (!loaded_module)
    return OutputBuffer("The address " + to_hex_string(address) + " is not covered by a module.\n");
  SymbolContext symbol_context = loaded_module->symbol_context();

  fxl::RefPtr<DwarfUnit> unit =
      loaded_module->module_symbols()->GetDwarfUnit(symbol_context, address);
  if (!unit) {
    return OutputBuffer("This address " + to_hex_string(address) +
                        " is not covered by a compilation unit.\n");
  }

  const LineTable& line_table = unit->GetLineTable();
  containers::array_view<LineTable::Row> sequence =
      line_table.GetRowSequenceForAddress(symbol_context, address);
  if (sequence.empty())
    return OutputBuffer("No row sequence covers " + to_hex_string(address) + ".\n");

  std::vector<std::vector<OutputBuffer>> table;
  bool seen_address = false;
  for (const auto& row : sequence) {
    auto& line = table.emplace_back();

    // Line marker and address.
    Syntax syntax;
    uint64_t absolute_line_addr = symbol_context.RelativeToAbsolute(row.Address);
    if (!seen_address && absolute_line_addr >= address) {
      // Since the sequence is in order and contains the address, the first row that contains the
      // address is the one in question.
      seen_address = true;
      syntax = Syntax::kHeading;
      line.emplace_back(syntax, GetCurrentRowMarker());
    } else {
      // No current row marker.
      syntax = Syntax::kNormal;
      line.emplace_back();
    }

    // Basic info.
    line.emplace_back(syntax, to_hex_string(absolute_line_addr));
    if (auto file_name = line_table.GetFileNameForRow(row)) {
      line.emplace_back(syntax,
                        process_symbols->target_symbols()->GetShortestUniqueFileName(*file_name));
    } else {
      line.emplace_back();
    }
    line.emplace_back(syntax, std::to_string(row.Line));

    // Accumulate the flags.
    std::string flags;
    auto append_flag_if = [&flags](bool condition, const char* text) {
      if (condition) {
        if (!flags.empty())
          flags += " | ";
        flags += text;
      }
    };
    append_flag_if(row.IsStmt, "IsStmt");
    append_flag_if(row.BasicBlock, "BasicBlock");
    append_flag_if(row.EndSequence, "EndSequence");
    append_flag_if(row.PrologueEnd, "PrologueEnd");
    append_flag_if(row.EpilogueBegin, "EpilogueBegin");
    line.emplace_back(syntax, flags);
  }

  OutputBuffer result;
  FormatTable({ColSpec(Align::kLeft, 0, "", 1), ColSpec(Align::kRight, 0, "Address"),
               ColSpec(Align::kLeft, 0, "File"), ColSpec(Align::kRight, 0, "Line"),
               ColSpec(Align::kLeft, 0, "Flags")},
              table, &result);
  return result;
}

Err RunVerbSymDebug(ConsoleContext* context, const Command& cmd) {
  if (Err err = cmd.ValidateNouns({Noun::kProcess}); err.has_error())
    return err;
  if (Err err = AssertRunningTarget(context, "sym-debug", cmd.target()); err.has_error())
    return err;

  OutputBuffer (*dumper)(ProcessSymbols*, uint64_t) = nullptr;
  if (cmd.HasSwitch(kInlineSwitch)) {
    dumper = &DumpInlineChain;
  } else if (cmd.HasSwitch(kInlineTreeSwitch)) {
    dumper = &DumpInlineTree;
  } else if (cmd.HasSwitch(kLineSwitch)) {
    dumper = &DumpLineTable;
  } else {
    return Err(
        "Missing a switch to indicate what to print.\n"
        "See \"help sym-debug\" for available options.");
  }

  if (cmd.args().empty()) {
    // Use the current location.
    if (!cmd.frame())
      return Err("No current frame, please specify an address.");

    Console::get()->Output(
        dumper(cmd.target()->GetProcess()->GetSymbols(), cmd.frame()->GetAddress()));
    return Err();
  }

  // Evaluate the expression to get the location.
  return EvalCommandAddressExpression(
      cmd, "sym-debug", GetEvalContextForCommand(cmd),
      [weak_process = cmd.target()->GetProcess()->GetWeakPtr(), dumper](
          const Err& err, uint64_t address, std::optional<uint64_t> size) {
        Console* console = Console::get();
        if (err.has_error()) {
          console->Output(err);  // Evaluation error.
          return;
        }
        if (!weak_process) {
          // Process has been destroyed during evaluation. Normally a message will be printed when
          // that happens so we can skip reporting the error.
          return;
        }

        console->Output(dumper(weak_process->GetSymbols(), address));
      });
}

}  // namespace

VerbRecord GetSymDebugVerbRecord() {
  VerbRecord sym_debug(&RunVerbSymDebug, {"sym-debug"}, kSymDebugShortHelp, kSymDebugHelp,
                       CommandGroup::kSymbol);
  sym_debug.param_type = VerbRecord::kOneParam;

  SwitchRecord inline_switch(kInlineSwitch, false, "inlines", 'i');
  SwitchRecord inline_tree_switch(kInlineTreeSwitch, false, "inline-tree", 't');
  SwitchRecord line_switch(kLineSwitch, false, "line", 'l');
  sym_debug.switches = {inline_switch, inline_tree_switch, line_switch};

  return sym_debug;
}

}  // namespace zxdb
