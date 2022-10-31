// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tools/fidl/fidlc/include/fidl/virtual_source_file.h"

#include <zircon/assert.h>

namespace fidl {

SourceSpan VirtualSourceFile::AddLine(std::string_view line) {
  ZX_ASSERT_MSG(line.find('\n') == std::string::npos,
                "a single line should not contain a newline character");
  return SourceSpan(*virtual_lines_.emplace_back(std::make_unique<std::string>(line)), *this);
}

std::string_view VirtualSourceFile::LineContaining(std::string_view view,
                                                   Position* position_out) const {
  for (int i = 0; i < static_cast<int>(virtual_lines_.size()); i++) {
    const std::string& line = *virtual_lines_[i];
    const char* line_begin = &*line.cbegin();
    const char* line_end = &*line.cend();
    if (view.data() < line_begin || view.data() + view.size() > line_end)
      continue;
    if (position_out != nullptr) {
      auto column = (view.data() - line_begin) + 1;
      ZX_ASSERT(column < std::numeric_limits<int>::max());
      *position_out = {i + 1, static_cast<int>(column)};
    }
    return line;
  }
  return std::string_view();
}

}  // namespace fidl
