// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <vector>

#include "fidl/source_file.h"
#include "string_view.h"

namespace fidl {

class SourceManager {
public:
    // Returns nullptr if |filename| could not be read.
    const SourceFile* CreateSource(StringView filename);

private:
    std::vector<SourceFile> sources_;
};

} // namespace fidl
