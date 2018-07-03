// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/console/format_table.h"

#include "garnet/bin/zxdb/console/output_buffer.h"
#include "garnet/bin/zxdb/console/string_util.h"
#include "garnet/public/lib/fxl/logging.h"

namespace zxdb {

namespace {

template<typename CellType>
bool SpansRemainingCols(const std::vector<ColSpec>& spec,
                        const std::vector<CellType>& row,
                        size_t column) {
  // The last item in a row will span all remaining columns if there are more
  // in the column spec.
  return column == row.size() - 1 && column < spec.size() - 1;
}

// Character width for both cell types.
int CellWidth(const std::string& str) {
  return static_cast<int>(UnicodeCharWidth(str));
}
int CellWidth(const OutputBuffer& buf) {
  return static_cast<int>(buf.UnicodeCharWidth());
}

// Appends the given cell type to the OutputBuffer. The Syntax is ignored
// in the OutputBuffer->OutputBuffer variant because it will contain its own
// syntax.
void AppendCell(const std::string& str, Syntax syntax, OutputBuffer* out) {
  out->Append(syntax, str);
}
void AppendCell(const OutputBuffer& buf, Syntax, OutputBuffer* out) {
  out->Append(buf);
}

// Appends the given string to the output, padding with spaces to the width
// as necessary.
template <typename CellType>
void AppendPadded(const CellType& cell, int width, Align align, Syntax syntax,
                  bool is_last_col, OutputBuffer* out) {
  int pad = std::max(0, width - CellWidth(cell));
  if (pad > 0 && align == Align::kRight)
    out->Append(std::string(pad, ' '));

  AppendCell(cell, syntax, out);

  // Padding on the right. Don't add for the last col.
  if (pad > 0 && !is_last_col && align == Align::kLeft)
    out->Append(std::string(pad, ' '));

  // Separator after columns for all but the last.
  if (!is_last_col)
    out->Append(std::string(1, ' '));
}

// Backend for FormatColumns variants. The requirements are that CellWidth()
// and AppendCell() are define for CellType.
template <typename CellType>
void FormatTableT(const std::vector<ColSpec>& spec,
                  const std::vector<std::vector<CellType>>& rows,
                  OutputBuffer* out) {
  std::vector<int> max;  // Max width of each column.

  // Max widths of headings.
  bool has_head = false;
  for (const auto& col : spec) {
    max.push_back(UnicodeCharWidth(col.head));
    has_head |= !col.head.empty();
  }

  // Max widths of contents.
  for (const auto& row : rows) {
    FXL_DCHECK(row.size() <= max.size()) << "Column spec size is too small.";
    for (size_t i = 0; i < row.size(); i++) {
      // Only count the ones that don't overflow (either because they span the
      // remaining columns, or because they're beyond max_width).
      if (!SpansRemainingCols(spec, row, i)) {
        int cell_size = CellWidth(row[i]);
        if (spec[i].max_width == 0 || cell_size <= spec[i].max_width)
          max[i] = std::max(max[i], cell_size);
      }
    }
  }

  // Print heading.
  if (has_head) {
    for (size_t i = 0; i < max.size(); i++) {
      const ColSpec& col = spec[i];
      if (col.pad_left)
        out->Append(Syntax::kNormal, std::string(col.pad_left, ' '));
      AppendPadded(col.head, max[i], col.align, Syntax::kHeading,
                   i == max.size() - 1, out);
    }
    out->Append("\n");
  }

  // Print rows.
  for (const auto& row : rows) {
    std::string text;
    for (size_t i = 0; i < row.size(); i++) {
      const ColSpec& col = spec[i];
      if (SpansRemainingCols(spec, row, i)) {
        AppendCell(row[i], col.syntax, out);
      } else {
        if (col.pad_left)
          out->Append(Syntax::kNormal, std::string(col.pad_left, ' '));
        AppendPadded(row[i], max[i], col.align, col.syntax, i == max.size() - 1,
                     out);
      }
    }
    out->Append("\n");
  }
}

}  // namespace

void FormatTable(const std::vector<ColSpec>& spec,
                 const std::vector<std::vector<std::string>>& rows,
                 OutputBuffer* out) {
  FormatTableT<std::string>(spec, rows, out);
}

void FormatTable(const std::vector<ColSpec>& spec,
                 const std::vector<std::vector<OutputBuffer>>& rows,
                 OutputBuffer* out) {
  FormatTableT<OutputBuffer>(spec, rows, out);
}

}  // namespace zxdb
