// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <string>
#include <utility>
#include <vector>

#include "string_view.h"

namespace fidl {

class SourceFile {
public:
    SourceFile(std::string filename, std::string data);
    ~SourceFile();

    StringView filename() const { return filename_; }
    StringView data() const { return data_; }

    StringView LineContaining(StringView view, int* line_number_out) const;

private:
    std::string filename_;
    std::string data_;
    std::vector<StringView> lines_;
};

} // namespace fidl
