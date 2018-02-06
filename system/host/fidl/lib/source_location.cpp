// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fidl/source_location.h"

namespace fidl {

StringView SourceLocation::SourceLine(int* line_number_out) const {
    return source_file_->LineContaining(data(), line_number_out);
}

} // namespace fidl
