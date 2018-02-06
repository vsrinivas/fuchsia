// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fidl/source_file.h"

#include <assert.h>

#include <algorithm>
#include <functional>

namespace fidl {

SourceFile::SourceFile(std::string filename, std::string data) :
    filename_(std::move(filename)),
    data_(std::move(data)) {
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

StringView SourceFile::LineContaining(StringView view, int* line_number_out) const {
    auto ptr_order = [](const char* left, const char* right) {
        return std::less_equal<const char*>()(left, right);
    };

    assert(ptr_order(data().data(), view.data()) &&
           "The view is not part of this SourceFile");
    assert(ptr_order(view.data() + view.size(),
                     data().data() + data().size()) &&
           "The view is not part of this SourceFile");

    auto is_in_line = [&view, &ptr_order](const StringView& current) {
        return ptr_order(view.data() + view.size(),
                         current.data() + current.size());
    };

    auto line = std::find_if(lines_.cbegin(), lines_.cend(), is_in_line);
    assert(line != lines_.cend());

    if (line_number_out != nullptr) {
        // Humans number lines from 1.
        *line_number_out = (line - lines_.cbegin()) + 1;
    }
    return *line;
}

} // namespace fidl
