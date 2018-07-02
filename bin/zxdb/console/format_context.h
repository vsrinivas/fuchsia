// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <limits>
#include <map>
#include <string>

#include "garnet/bin/zxdb/client/err.h"
#include "garnet/bin/zxdb/console/command.h"

namespace zxdb {

class ArchInfo;
class Location;
class MemoryDump;
class OutputBuffer;
class Process;

// Formats the given location and writes it to the console.
//
// If the location is symbolized and the source affinity is not "assembly", a
// source-code dump will be displayed. Otherwise, a disassembly dump will be
// displayed.
//
// Disassembly dumps will be done asynchronously since the memory must be
// requested from the target system. Source dumps will be synchonous.
//
// An error will be returned if the location is symbolized but the file can't
// be found or doesn't contain that line. In this case, nothing will be output.
Err OutputSourceContext(Process* process, const Location& location,
                        SourceAffinity source_affinity);

struct FormatSourceOpts {
  // Range of lines to print, inclusive. This can be outside of the range of
  // file lines, the result will just be clamped to the end of the file.
  int first_line = 0;
  int last_line = 0;

  // 1-based line to show as the active one. This line will be shown in bold
  // with an arrow indicator in the left column. When 0, no active line will be
  // highlighted.
  int active_line = 0;

  // 1-based column number of the active line to start highlighting from. When
  // 0 or 1, the entire line will be highlighted.
  int active_column = 0;

  // When true, all nonactive lines will be dimmed in source listings.
  // Otherwise they will be shown in the normal color.
  bool dim_nonactive = false;

  // Contains the lines with breakpoints (1-based key) mapped to whether that
  // breakpoint is enabled or not.
  std::map<int, bool> bp_lines;
};

// Formats the contents of the given local file name to the output. See
// FormatSourceFileContext for error handling.
//
// The build_dir indicates the directory where relative file names will be
// treated as relative to.
Err FormatSourceFileContext(const std::string& file_name,
                            const std::string& build_dir,
                            const FormatSourceOpts& opts, OutputBuffer* out);

// Formats the given source to the output.
//
// If the active line is nonzero but is not in the file, an error will be
// returned and no output will be generated. The file_name_for_errors will be
// used to generate this string, it will not be used for any other purpose.
Err FormatSourceContext(const std::string& file_name_for_errors,
                        const std::string& file_contents,
                        const FormatSourceOpts& opts, OutputBuffer* out);

struct FormatAsmOpts {
  bool emit_addresses = true;
  bool emit_bytes = false;

  // When nonzero, a line with this address will be marked as active.
  uint64_t active_address = 0;

  // When nonzero, disassembly will stop after this many assembly instructions.
  size_t max_instructions = std::numeric_limits<size_t>::max();

  // Contains the addresses with breakpoints mapped to whether that breakpoint
  // is enabled or not.
  std::map<uint64_t, bool> bp_addrs;
};

// On error, returns it and does nothing.
Err FormatAsmContext(const ArchInfo* arch_info, const MemoryDump& dump,
                     const FormatAsmOpts& opts, OutputBuffer* out);

// Creates a source code context of the given location and puts it in the
// output buffer. This does not write disassembly since that would require
// asynchronously getting the memory which isn't as important for breakpoints.
//
// If there are no symbols or the file can't be found, returns the error and
// doesn't write anything to the buffer.
//
// Generally the location passed here should be the location of a resolved
// BreakpointLocation since the breakpoint itself won't have a fully qualified
// file name, and the breakpoint may move slightly when it's actually applied.
//
// Build_dir is used to find relative files by FormatSourceFileContext().
Err FormatBreakpointContext(const Location& location,
                            const std::string& build_dur, bool enabled,
                            OutputBuffer* out);

}  // namespace zxdb
