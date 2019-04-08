// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fidl/virtual_source_file.h"

namespace fidl {

SourceLocation VirtualSourceFile::AddLine(const std::string& line) {
    return SourceLocation(*virtual_lines_.emplace_back(std::make_unique<std::string>(line)), *this);
}

StringView VirtualSourceFile::LineContaining(StringView view, Position* position_out) const {
    for (int i = 0; i < static_cast<int>(virtual_lines_.size()); i++) {
        const std::string& line = *virtual_lines_[i];
        const char* line_begin = &*line.cbegin();
        const char* line_end = &*line.cend();
        if (view.data() < line_begin || view.data() + view.size() > line_end)
            continue;
        if (position_out != nullptr) {
            auto column = (view.data() - line_begin) + 1;
            assert(column < std::numeric_limits<int>::max());
            *position_out = {i + 1, static_cast<int>(column)};
        }
        return StringView(line);
    }
    return StringView();
}

}  // namespace fidl
