// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <string>
#include <vector>

#include "string_view.h"

namespace fidl {

class SourceManager {
public:
    bool CreateSource(const char* filename, StringView* out_source);

private:
    std::vector<std::string> sources_;
};

} // namespace fidl
