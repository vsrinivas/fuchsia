// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/vnext/lib/formats/encryption.h"

#include <fuchsia/mediastreams/cpp/fidl.h>
#include <lib/syslog/cpp/macros.h>

namespace fmlib {

Encryption::Encryption(std::string scheme, fidl::VectorPtr<uint8_t> default_key_id,
                       fidl::VectorPtr<uint8_t> default_init_vector,
                       fuchsia::mediastreams::EncryptionPatternPtr default_pattern)
    : fidl_{.scheme = std::move(scheme),
            .default_key_id = std::move(default_key_id),
            .default_init_vector = std::move(default_init_vector),
            .default_pattern = std::move(default_pattern)} {}

}  // namespace fmlib
