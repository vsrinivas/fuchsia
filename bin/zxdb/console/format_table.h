// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <string>
#include <vector>

#include "garnet/bin/zxdb/console/output_buffer.h"

namespace zxdb {

enum class Align { kLeft, kRight };

struct ColSpec {
  explicit ColSpec(Align align = Align::kLeft, int max_width = 0,
                   const std::string& head = std::string(), int pad_left = 0)
      : align(align), max_width(max_width), head(head), pad_left(pad_left) {}

  Align align = Align::kLeft;

  // If anything is above the max width, we'll give up and push the remaining
  // cells for that row to the right as necessary.
  //
  // 0 means no maximum.
  int max_width = 0;

  // Empty string means no heading.
  std::string head;

  // Extra padding to the left of this column.
  int pad_left = 0;

  // Syntax highlighting style for this column when printing strings.
  // When using the OutputBuffer variants this is ignored because it's assumed
  // that the OutputBuffers contain the desired formatting.
  Syntax syntax = Syntax::kNormal;
};

// Formats the given rows in the output as a series of horizontally aligned (if
// possible) columns.
//
// If the number of items in a row is less than the number of items in the
// spec, the last element in the rows will occupy the remaining space and it
// won't affect other columns (like you used colspan in HTML). Such items will
// always be left-aligned.
void FormatTable(const std::vector<ColSpec>& spec,
                 const std::vector<std::vector<std::string>>& rows,
                 OutputBuffer* out);
void FormatTable(const std::vector<ColSpec>& spec,
                 const std::vector<std::vector<OutputBuffer>>& rows,
                 OutputBuffer* out);

// Helper function to save some typing for static column specs.
inline void FormatTable(std::initializer_list<ColSpec> spec,
                        const std::vector<std::vector<std::string>>& rows,
                        OutputBuffer* out) {
  return FormatTable(std::vector<ColSpec>(spec), rows, out);
}
inline void FormatTable(std::initializer_list<ColSpec> spec,
                        const std::vector<std::vector<OutputBuffer>>& rows,
                        OutputBuffer* out) {
  return FormatTable(std::vector<ColSpec>(spec), rows, out);
}

}  // namespace zxdb
