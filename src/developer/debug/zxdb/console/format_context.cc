// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/console/format_context.h"

#include <algorithm>
#include <vector>

#include "src/developer/debug/zxdb/client/arch_info.h"
#include "src/developer/debug/zxdb/client/disassembler.h"
#include "src/developer/debug/zxdb/client/memory_dump.h"
#include "src/developer/debug/zxdb/client/process.h"
#include "src/developer/debug/zxdb/client/session.h"
#include "src/developer/debug/zxdb/common/file_util.h"
#include "src/developer/debug/zxdb/console/console.h"
#include "src/developer/debug/zxdb/console/format_table.h"
#include "src/developer/debug/zxdb/console/output_buffer.h"
#include "src/developer/debug/zxdb/console/source_util.h"
#include "src/developer/debug/zxdb/console/string_util.h"
#include "src/developer/debug/zxdb/symbols/location.h"
#include "src/lib/files/file.h"
#include "src/lib/fxl/strings/string_printf.h"

namespace zxdb {

namespace {

using LineInfo = std::pair<int, std::string>;  // Line #, Line contents.
using LineVector = std::vector<LineInfo>;

// Formats the given line, highlighting from the column to the end of the line.
// The column is 1-based but we also accept 0.
OutputBuffer HighlightLine(std::string str, int column) {
  // Convert to 0-based with clamping (since the offsets come from symbols,
  // they could be invalid).
  int str_size = static_cast<int>(str.size());
  int col_index = std::min(std::max(0, column - 1), str_size);

  OutputBuffer result;
  if (col_index == 0) {
    result.Append(Syntax::kHeading, std::move(str));
  } else {
    result.Append(Syntax::kNormal, str.substr(0, col_index));
    if (col_index < str_size)
      result.Append(Syntax::kHeading, str.substr(col_index));
  }
  return result;
}

}  // namespace

Err OutputSourceContext(Process* process, const Location& location,
                        SourceAffinity source_affinity) {
  if (source_affinity != SourceAffinity::kAssembly &&
      location.file_line().is_valid()) {
    // Synchronous source output.
    FormatSourceOpts source_opts;
    source_opts.active_line = location.file_line().line();
    source_opts.highlight_line = source_opts.active_line;
    source_opts.highlight_column = location.column();
    source_opts.first_line = source_opts.active_line - 2;
    source_opts.last_line = source_opts.active_line + 2;
    source_opts.dim_others = true;

    OutputBuffer out;
    Err err = FormatSourceFileContext(
        location.file_line().file(),
        process->session()->system().GetSymbols()->build_dir(), source_opts,
        &out);
    if (err.has_error())
      return err;

    Console::get()->Output(out);
  } else {
    // Fall back to disassembly.
    FormatAsmOpts options;
    options.emit_addresses = true;
    options.emit_bytes = false;
    options.active_address = location.address();

    uint64_t start_address;
    const ArchInfo* arch_info = process->session()->arch_info();
    if (arch_info->is_fixed_instr()) {
      // Fixed instruction length, back up 2 instructions to provide context.
      start_address = location.address() - 2 * arch_info->max_instr_len();
      options.max_instructions = 5;
    } else {
      // Variable length instructions. Since this code path is triggered when
      // there are no symbols, we can't back up reliably. Just disassemble
      // starting from the current location.
      //
      // In the future it might be nice to keep some record of recently stepped
      // instructions since usually this address will be the one after the one
      // that was just stepped.
      start_address = location.address();
      options.max_instructions = 4;
    }

    size_t size = options.max_instructions * arch_info->max_instr_len();

    process->ReadMemory(start_address, size,
                        [options, weak_process = process->GetWeakPtr()](
                            const Err& in_err, MemoryDump dump) {
                          if (!weak_process)
                            return;  // Give up when the process went away.

                          Console* console = Console::get();
                          if (in_err.has_error()) {
                            console->Output(in_err);
                            return;
                          }
                          OutputBuffer out;
                          Err err = FormatAsmContext(
                              weak_process->session()->arch_info(), dump,
                              options, &out);
                          if (err.has_error())
                            console->Output(err);
                          else
                            console->Output(out);
                        });
  }
  return Err();
}

// This doesn't cache the file contents. We may want to add that for
// performance, but we should be careful to always pick the latest version
// since it can get updated.
Err FormatSourceFileContext(const std::string& file_name,
                            const std::string& build_dir,
                            const FormatSourceOpts& opts, OutputBuffer* out) {
  std::string contents;
  Err err = GetFileContents(file_name, build_dir, &contents);
  if (err.has_error())
    return err;
  return FormatSourceContext(file_name, contents, opts, out);
}

Err FormatSourceContext(const std::string& file_name_for_errors,
                        const std::string& file_contents,
                        const FormatSourceOpts& opts, OutputBuffer* out) {
  FXL_DCHECK(opts.active_line == 0 || !opts.require_active_line ||
             (opts.active_line >= opts.first_line &&
              opts.active_line <= opts.last_line));

  // Allow the beginning to be out-of-range. This mirrors the end handling
  // (clamped to end-of-file) so callers can blindly create offsets from
  // a current line without clamping.
  int first_line = std::max(1, opts.first_line);

  std::vector<std::string> context =
      ExtractSourceLines(file_contents, first_line, opts.last_line);
  if (context.empty()) {
    // No source found for this location. If highlight_line exists, assume
    // it's the one the user cares about.
    int err_line = opts.highlight_line ? opts.highlight_line : first_line;
    return Err(fxl::StringPrintf("There is no line %d in the file %s", err_line,
                                 file_name_for_errors.c_str()));
  }
  if (opts.active_line != 0 && opts.require_active_line &&
      first_line + static_cast<int>(context.size()) < opts.active_line) {
    return Err(fxl::StringPrintf("There is no line %d in the file %s",
                                 opts.active_line,
                                 file_name_for_errors.c_str()));
  }

  std::vector<std::vector<OutputBuffer>> rows;
  for (size_t i = 0; i < context.size(); i++) {
    int line_number = first_line + i;
    std::string& line_text = context[i];  // Moved out of.

    rows.emplace_back();
    std::vector<OutputBuffer>& row = rows.back();

    // Compute markers in the left margin.
    OutputBuffer margin;
    auto found_bp = opts.bp_lines.find(line_number);
    if (found_bp != opts.bp_lines.end()) {
      std::string breakpoint_marker = found_bp->second
                                          ? GetBreakpointMarker()
                                          : GetDisabledBreakpointMarker();

      if (line_number == opts.active_line) {
        // Active + breakpoint.
        margin.Append(Syntax::kError, breakpoint_marker);
        margin.Append(Syntax::kHeading, GetRightArrow());
      } else {
        // Breakpoint.
        margin.Append(Syntax::kError, " " + breakpoint_marker);
      }
    } else {
      if (line_number == opts.active_line) {
        // Active line.
        margin.Append(Syntax::kHeading, " " + GetRightArrow());
      } else {
        // Inactive line with no breakpoint.
        margin.Append("  ");
      }
    }
    row.push_back(std::move(margin));

    std::string number = fxl::StringPrintf("%d", line_number);
    if (line_number == opts.highlight_line) {
      // This is the line to mark.
      row.emplace_back(Syntax::kHeading, std::move(number));
      row.push_back(HighlightLine(std::move(line_text), opts.highlight_column));
    } else {
      // Normal context line.
      Syntax syntax = opts.dim_others ? Syntax::kComment : Syntax::kNormal;
      row.emplace_back(syntax, std::move(number));
      row.emplace_back(syntax, std::move(line_text));
    }
  }

  FormatTable({ColSpec(Align::kLeft), ColSpec(Align::kRight),
               ColSpec(Align::kLeft, 0, std::string(), 0)},
              rows, out);
  return Err();
}

Err FormatAsmContext(const ArchInfo* arch_info, const MemoryDump& dump,
                     const FormatAsmOpts& opts, OutputBuffer* out) {
  // Make the disassembler.
  Disassembler disassembler;
  Err my_err = disassembler.Init(arch_info);
  if (my_err.has_error())
    return my_err;

  Disassembler::Options options;

  std::vector<Disassembler::Row> rows;
  disassembler.DisassembleDump(dump, dump.address(), options,
                               opts.max_instructions, &rows);

  std::vector<std::vector<OutputBuffer>> table;
  for (auto& row : rows) {
    table.emplace_back();
    std::vector<OutputBuffer>& out_row = table.back();

    // Compute markers in the left margin.
    OutputBuffer margin;
    auto found_bp = opts.bp_addrs.find(row.address);
    if (found_bp != opts.bp_addrs.end()) {
      std::string breakpoint_marker = found_bp->second
                                          ? GetBreakpointMarker()
                                          : GetDisabledBreakpointMarker();

      if (row.address == opts.active_address) {
        // Active + breakpoint.
        margin.Append(Syntax::kError, breakpoint_marker);
        margin.Append(Syntax::kHeading, GetRightArrow());
      } else {
        // Breakpoint.
        margin.Append(Syntax::kError, " " + breakpoint_marker);
      }
    } else {
      if (row.address == opts.active_address) {
        // Active line.
        margin.Append(Syntax::kHeading, " " + GetRightArrow());
      } else {
        // Inactive line with no breakpoint.
        margin.Append("  ");
      }
    }
    out_row.push_back(std::move(margin));

    if (opts.emit_addresses) {
      out_row.emplace_back(Syntax::kComment,
                           fxl::StringPrintf("0x%" PRIx64, row.address));
    }
    if (opts.emit_bytes) {
      std::string bytes_str;
      for (size_t i = 0; i < row.bytes.size(); i++) {
        if (i > 0)
          bytes_str.push_back(' ');
        bytes_str.append(fxl::StringPrintf("%2.2x", row.bytes[i]));
      }
      out_row.emplace_back(Syntax::kComment, std::move(bytes_str));
    }

    Syntax op_param_syntax =
        row.address == opts.active_address ? Syntax::kHeading : Syntax::kNormal;
    out_row.emplace_back(op_param_syntax, std::move(row.op));
    out_row.emplace_back(op_param_syntax, std::move(row.params));
    out_row.emplace_back(Syntax::kComment, std::move(row.comment));
  }

  std::vector<ColSpec> spec;
  spec.emplace_back(Align::kLeft);  // Margin.
  if (opts.emit_addresses)
    spec.emplace_back(Align::kRight);
  if (opts.emit_bytes) {
    // Max out the bytes @ 17 cols (holds 6 bytes) to keep it from pushing
    // things too far over in the common case.
    spec.emplace_back(Align::kLeft, 17, std::string(), 1);
  }

  // When there was an address or byte listing, put 1 extra column of space
  // to separate the opcode. Otherwise keep it by the left margin.
  if (spec.size() > 1)
    spec.emplace_back(Align::kLeft, 0, std::string(), 1);  // Instructions.
  else
    spec.emplace_back(Align::kLeft, 0, std::string(), 0);  // Instructions.

  // Params. Some can be very long so provide a max so the comments don't get
  // pushed too far out.
  spec.emplace_back(Align::kLeft, 10, std::string(), 1);
  spec.emplace_back(Align::kLeft);  // Comments.

  FormatTable(spec, table, out);
  return Err();
}

Err FormatBreakpointContext(const Location& location,
                            const std::string& build_dir, bool enabled,
                            OutputBuffer* out) {
  if (!location.has_symbols())
    return Err("No symbols for this location.");

  int line = location.file_line().line();
  constexpr int kBreakpointContext = 1;

  FormatSourceOpts opts;
  opts.first_line = line - kBreakpointContext;
  opts.last_line = line + kBreakpointContext;
  opts.highlight_line = line;
  opts.bp_lines[line] = enabled;
  return FormatSourceFileContext(location.file_line().file(), build_dir, opts,
                                 out);
}

}  // namespace zxdb
