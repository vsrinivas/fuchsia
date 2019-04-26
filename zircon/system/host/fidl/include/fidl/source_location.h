// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_HOST_FIDL_INCLUDE_FIDL_SOURCE_LOCATION_H_
#define ZIRCON_SYSTEM_HOST_FIDL_INCLUDE_FIDL_SOURCE_LOCATION_H_

#include <cstdint>
#include <string_view>

#include "source_manager.h"

namespace fidl {

// A SourceLocation represents a range of a source file. It consists
// of a std::string_view, and a reference to the SourceFile that is backing
// the std::string_view.

class SourceLocation {
public:
    SourceLocation(std::string_view data, const SourceFile& source_file)
        : data_(data), source_file_(&source_file) {}

    SourceLocation() : data_(std::string_view()), source_file_(nullptr) {}

    bool valid() const { return source_file_ != nullptr; }

    const std::string_view& data() const { return data_; }
    const SourceFile& source_file() const { return *source_file_; }

    std::string_view SourceLine(SourceFile::Position* position_out) const;

    std::string position() const;

private:
    std::string_view data_;
    const SourceFile* source_file_;
};

} // namespace fidl

#endif // ZIRCON_SYSTEM_HOST_FIDL_INCLUDE_FIDL_SOURCE_LOCATION_H_
