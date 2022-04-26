// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/vnext/lib/formats/compression.h"

#include <fuchsia/mediastreams/cpp/fidl.h>
#include <lib/syslog/cpp/macros.h>

namespace fmlib {

Compression::Compression(std::string type, fidl::VectorPtr<uint8_t> parameters)
    : fidl_{.type = std::move(type), .parameters = std::move(parameters)} {}

Compression::Compression(fuchsia::mediastreams::Compression compression)
    : fidl_(std::move(compression)) {}

Compression::operator fuchsia::mediastreams::Compression() const { return fidl::Clone(fidl_); }

}  // namespace fmlib
