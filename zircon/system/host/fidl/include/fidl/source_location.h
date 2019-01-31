// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_HOST_FIDL_INCLUDE_FIDL_SOURCE_LOCATION_H_
#define ZIRCON_SYSTEM_HOST_FIDL_INCLUDE_FIDL_SOURCE_LOCATION_H_

#include <stdint.h>

#include "source_manager.h"
#include "string_view.h"

namespace fidl {

// A SourceLocation represents a range of a source file. It consists
// of a StringView, and a reference to the SourceFile that is backing
// the StringView.

class SourceLocation {
public:
    SourceLocation(StringView data, const SourceFile& source_file)
        : data_(data), source_file_(&source_file) {}

    SourceLocation() : data_(StringView()), source_file_(nullptr) {}

    bool valid() const { return source_file_ != nullptr; }

    const StringView& data() const { return data_; }
    const SourceFile& source_file() const { return *source_file_; }

    StringView SourceLine(SourceFile::Position* position_out) const;

    std::string position() const;

private:
    StringView data_;
    const SourceFile* source_file_;
};

} // namespace fidl

#endif // ZIRCON_SYSTEM_HOST_FIDL_INCLUDE_FIDL_SOURCE_LOCATION_H_
