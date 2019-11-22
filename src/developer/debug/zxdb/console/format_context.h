// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_CONSOLE_FORMAT_CONTEXT_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_CONSOLE_FORMAT_CONTEXT_H_

#include <limits>
#include <map>
#include <string>

#include "src/developer/debug/zxdb/common/err.h"
#include "src/developer/debug/zxdb/console/command.h"
#include "src/lib/fxl/memory/weak_ptr.h"

namespace zxdb {

class ArchInfo;
class FileLine;
class Location;
class MemoryDump;
class ModuleSymbols;
class OutputBuffer;
class Process;
class SourceFileProvider;

// Formats the given location and writes it to the console.
//
// If the location is symbolized and the source affinity is not "assembly", a source-code dump will
// be displayed. Otherwise, a disassembly dump will be displayed.
//
// Disassembly dumps will be done asynchronously since the memory must be requested from the target
// system. Source dumps will be synchronous.
//
// An error will be returned if the location is symbolized but the file can't be found or doesn't
// contain that line. In this case, nothing will be output.
Err OutputSourceContext(Process* process, std::unique_ptr<SourceFileProvider> file_provider,
                        const Location& location, SourceAffinity source_affinity);

struct FormatSourceOpts {
  // Show the full file path before printing source code. Useful for debugging symbol issues.
  //
  // This could be enhanced to be an enum to show a short name or the name from the symbol file.
  bool show_file_name = false;

  // Range of lines to print, inclusive. This can be outside of the range of file lines, the result
  // will just be clamped to the end of the file.
  int first_line = 0;
  int last_line = 0;

  // Number of extra spaces before the "gutter" (where the current line caret goes).
  int left_indent = 0;

  // 1-based line to show as the active one. This line will be shown with an arrow indicator in the
  // left column. When 0, no active line will be highlighted.
  //
  // By convention the active line indicator should be used to show the current execution position
  // only. To highlight for another purpose, use highlight_line.
  int active_line = 0;

  // 1-based line to highlight in bold.
  int highlight_line = 0;

  // 1-based column number of the highlighted line to start highlighting from. When 0 or 1, the
  // entire line will be highlighted.
  int highlight_column = 0;

  // When true, all non-highlighted lines will be dimmed in source listings. Otherwise they will be
  // shown in the normal color.
  bool dim_others = false;

  // Set to true to issue an error if the active line is not present in the file. This would be set
  // if we're showing the current execution location and it would be confusing to show something
  // incorrect. In other cases, the active line is secondary information and it doesn't matter if
  // it's not visible.
  bool require_active_line = false;

  // Contains the lines with breakpoints (1-based key) mapped to whether that breakpoint is enabled
  // or not.
  std::map<int, bool> bp_lines;

  // If set, this will be used to compare modification times for source file symbols when loading a
  // source file (FormatSourceFileContext()). A warning will be prepended to source listings if the
  // source file is newer than the module's symbols. This should be the module corresponding to the
  // file being printed.
  //
  // This is a WeakPtr because sometimes this options struct is saved asynchronously when something
  // needs to be fetched.
  fxl::WeakPtr<ModuleSymbols> module_for_time_warning;
};

// Formats the contents of the given local file name to the output. See FormatSourceFileContext for
// error handling.
//
// The build_dir indicates the directory where relative file names will be treated as relative to.
Err FormatSourceFileContext(const FileLine& file_line, const SourceFileProvider& file_provider,
                            const FormatSourceOpts& opts, OutputBuffer* out);

// Formats the given source to the output.
//
// The file_name_for_display is used for display purposes but is not interpreted in any way. This
// will be printed if the show_file_name flag is set in the options.
//
// If the active line is nonzero but is not in the file, an error will be returned and no output
// will be generated. The file_name_for_display will be used to generate this string.
Err FormatSourceContext(const std::string& file_name_for_display, const std::string& file_contents,
                        const FormatSourceOpts& opts, OutputBuffer* out);

struct FormatAsmOpts {
  bool emit_addresses = true;
  bool emit_bytes = false;

  bool include_source = false;

  // When nonzero, a line with this address will be marked as active.
  uint64_t active_address = 0;

  // When nonzero, disassembly will stop after this many assembly instructions.
  size_t max_instructions = std::numeric_limits<size_t>::max();

  // Contains the addresses with breakpoints mapped to whether that breakpoint is enabled or not.
  std::map<uint64_t, bool> bp_addrs;
};

// Outputs assembly.
//
// The process is used when options.include_source is set to map addresses back to source locations.
// When options.include_source is not set, this can be null.
//
// On error, returns the error and does nothing.
Err FormatAsmContext(const ArchInfo* arch_info, const MemoryDump& dump, const FormatAsmOpts& opts,
                     Process* process, const SourceFileProvider& file_provider, OutputBuffer* out);

// Creates a source code context of the given location and puts it in the output buffer. This does
// not write disassembly since that would require asynchronously getting the memory which isn't as
// important for breakpoints.
//
// If there are no symbols or the file can't be found, returns the error and doesn't write anything
// to the buffer.
//
// Generally the location passed here should be the location of a resolved BreakpointLocation since
// the breakpoint itself won't have a fully qualified file name, and the breakpoint may move
// slightly when it's actually applied.
//
// Build_dir_prefs is used to find relative files by FormatSourceFileContext().
Err FormatBreakpointContext(const Location& location, const SourceFileProvider& file_provider,
                            bool enabled, OutputBuffer* out);

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_CONSOLE_FORMAT_CONTEXT_H_
