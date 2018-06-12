// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/console/format_file_context.h"

#include <algorithm>
#include <vector>

#include "garnet/bin/zxdb/client/symbols/location.h"
#include "garnet/bin/zxdb/console/format_table.h"
#include "garnet/bin/zxdb/console/output_buffer.h"
#include "lib/fxl/files/file.h"
#include "lib/fxl/strings/string_printf.h"

namespace zxdb {

namespace {

// U+25B6 BLACK RIGHT-POINTING TRIANGLE.
static const char kRightArrow[] = "\xe2\x96\xb6";

using LineInfo = std::pair<int, std::string>;  // Line #, Line contents.
using LineVector = std::vector<LineInfo>;

// Extracts the lines of context and returns them.
// This can't use fxl::SplitString because we want to allow "<CR><LF>"
// (greedy), or either <CR> or <LF> by itself to indicate EOL.
LineVector ExtractContext(const std::string& contents, int line) {
  LineVector result;

  constexpr int kLineContext = 2;  // # Lines above and below.
  constexpr char kCR = 13;
  constexpr char kLF = 10;

  int first_line = std::max(0, line - kLineContext);
  int last_line = line + kLineContext;

  int cur_line = 1;
  size_t line_begin = 0;  // Byte offset.
  while (line_begin < contents.size() && cur_line <= last_line) {
    size_t cur = line_begin;

    // Locate extent of current line.
    size_t next_line_begin = contents.size();
    size_t line_end = contents.size();
    while (cur < contents.size()) {
      if (contents[cur] == kCR) {
        // Either CR or CR+LF
        line_end = cur;
        if (cur < contents.size() - 1 && contents[cur + 1] == kLF) {
          next_line_begin = cur + 2;
        } else {
          next_line_begin = cur + 1;
        }
        break;
      } else if (contents[cur] == kLF) {
        // LF by itself.
        line_end = cur;
        next_line_begin = cur + 1;
        break;
      }
      cur++;
    }

    if (cur_line >= first_line && cur_line <= last_line) {
      result.emplace_back(
          std::piecewise_construct, std::forward_as_tuple(cur_line),
          std::forward_as_tuple(&contents[line_begin], line_end - line_begin));
    }

    // Advance to next line.
    line_begin = next_line_begin;
    cur_line++;
  }

  return result;
}

// Formats the given line, highting from the column to the end of the line.
// The column is 1-based but we also accept 0.
OutputBuffer HighlightLine(std::string str, int column) {
  // Convert to 0-based with clamping (since the offsets come from symbols,
  // they could be invalid).
  int str_size = static_cast<int>(str.size());
  int col_index = std::min(std::max(0, column - 1), str_size);

  OutputBuffer result;
  if (column == 0) {
    result.Append(Syntax::kHeading, std::move(str));
  } else {
    result.Append(Syntax::kNormal, str.substr(0, col_index));
    if (column < str_size)
      result.Append(Syntax::kHeading, str.substr(col_index));
  }
  return result;
}

}  // namespace

// This doesn't cache the file contents. We may want to add that for
// performance, but we should be careful to always pick the latest version
// since it can get updated.
bool FormatFileContext(const Location& location, OutputBuffer* out) {
  std::string contents;
  if (!files::ReadFileToString(location.file_line().file(), &contents))
    return false;
  return FormatFileContext(contents, location.file_line().line(),
                           location.column(), out);
}

bool FormatFileContext(const std::string& file_contents, int line, int column,
                       OutputBuffer* out) {
  LineVector context = ExtractContext(file_contents, line);
  if (context.empty() || context.back().first < line)
    return false;  // The requested line is not in the file.

  std::vector<std::vector<OutputBuffer>> rows;
  for (LineInfo& info : context) {
    rows.emplace_back();
    std::vector<OutputBuffer>& row = rows.back();

    std::string number = fxl::StringPrintf("%d", info.first);
    if (info.first == line) {
      // This is the line to mark.
      row.push_back(OutputBuffer::WithContents(Syntax::kHeading, kRightArrow));
      row.push_back(
          OutputBuffer::WithContents(Syntax::kHeading, std::move(number)));
      row.push_back(HighlightLine(std::move(info.second), column));
    } else {
      // Normal context line.
      row.push_back(
          OutputBuffer::WithContents(Syntax::kComment, std::string()));
      row.push_back(
          OutputBuffer::WithContents(Syntax::kComment, std::move(number)));
      row.push_back(
          OutputBuffer::WithContents(Syntax::kComment, std::move(info.second)));
    }
  }

  FormatTable({ColSpec(Align::kLeft), ColSpec(Align::kRight),
               ColSpec(Align::kLeft, 0, std::string(), 1)},
              rows, out);
  return true;
}

}  // namespace zxdb
