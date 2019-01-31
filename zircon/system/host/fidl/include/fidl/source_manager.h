// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_HOST_FIDL_INCLUDE_FIDL_SOURCE_MANAGER_H_
#define ZIRCON_SYSTEM_HOST_FIDL_INCLUDE_FIDL_SOURCE_MANAGER_H_

#include <memory>
#include <vector>

#include "fidl/source_file.h"
#include "string_view.h"

namespace fidl {

class SourceManager {
public:
    // Returns whether the filename was successfully read.
    bool CreateSource(StringView filename);
    void AddSourceFile(std::unique_ptr<SourceFile> file);

    const std::vector<std::unique_ptr<SourceFile>>& sources() const { return sources_; }

private:
    std::vector<std::unique_ptr<SourceFile>> sources_;
};

} // namespace fidl

#endif // ZIRCON_SYSTEM_HOST_FIDL_INCLUDE_FIDL_SOURCE_MANAGER_H_
