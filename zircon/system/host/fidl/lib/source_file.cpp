// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fidl/source_file.h"

#include <assert.h>

#include <algorithm>
#include <functional>

namespace fidl {

SourceFile::SourceFile(std::string filename, std::string data)
    : filename_(std::move(filename)), data_(std::move(data)) {
    size_t size = 0u;
    auto start_of_line = data_.cbegin();

    for (auto it = data_.cbegin(); it != data_.cend(); ++it) {
        ++size;
        if (*it == '\n' || *it == '\0') {
            auto& position = *start_of_line;
            lines_.push_back(StringView(&position, size));

            size = 0u;
            start_of_line = it + 1;
        }
    }
}

SourceFile::~SourceFile() = default;

StringView SourceFile::LineContaining(StringView view, Position* position_out) const {
    auto ptr_less_equal = std::less_equal<const char*>();

    assert(ptr_less_equal(data().data(), view.data()) && "The view is not part of this SourceFile");
    assert(ptr_less_equal(view.data() + view.size(), data().data() + data().size()) &&
           "The view is not part of this SourceFile");

    // We are looking from the end of the file backwards (hence
    // crbegin and crend), looking for the first line (hence
    // upper_bound) to start at or before before the token in
    // question.
    auto is_in_line = [&ptr_less_equal](const StringView& left, const StringView& right) {
        return ptr_less_equal(right.data(), left.data());
    };
    auto line = std::upper_bound(lines_.crbegin(), lines_.crend(), view, is_in_line);
    assert(line != lines_.crend());

    if (position_out != nullptr) {
        // Humans number lines from 1. Calculating this from the end
        // accounts for this.
        int line_number = static_cast<int>(lines_.crend() - line);
        // Columns should also start from 1.
        int column_number = static_cast<int>(view.data() - line->data()) + 1;
        *position_out = {line_number, column_number};
    }
    return *line;
}

} // namespace fidl
