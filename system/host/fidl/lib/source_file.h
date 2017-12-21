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
    SourceFile(std::string filename, std::string data)
        : filename_(std::move(filename)), data_(std::move(data)) {}

    StringView filename() const { return filename_; }
    StringView data() const { return data_; }

private:
    std::string filename_;
    std::string data_;
};

} // namespace fidl
