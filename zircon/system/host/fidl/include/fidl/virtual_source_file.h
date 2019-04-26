// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_HOST_FIDL_INCLUDE_FIDL_VIRTUAL_SOURCE_FILE_H_
#define ZIRCON_SYSTEM_HOST_FIDL_INCLUDE_FIDL_VIRTUAL_SOURCE_FILE_H_

#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "source_file.h"
#include "source_location.h"

namespace fidl {

class VirtualSourceFile : public SourceFile {
public:
    VirtualSourceFile(std::string filename) : SourceFile(filename, "") {}
    virtual ~VirtualSourceFile() = default;

    virtual std::string_view LineContaining(std::string_view view, Position* position_out) const;

    SourceLocation AddLine(const std::string& line);

private:
    std::vector<std::unique_ptr<std::string>> virtual_lines_;
};

}  // namespace fidl

#endif // ZIRCON_SYSTEM_HOST_FIDL_INCLUDE_FIDL_VIRTUAL_SOURCE_FILE_H_
