// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fidl/llcpp/wire_decoder.h>

#include <utility>

namespace fidl::internal {

WireDecoder::WireDecoder(const fidl::internal::CodingConfig* coding_config, uint8_t* bytes,
                         size_t num_bytes, fidl_handle_t* handles,
                         fidl_handle_metadata_t* handle_metadata, size_t num_handles)
    : coding_config_(coding_config),
      bytes_(bytes),
      num_bytes_(num_bytes),
      handles_(handles),
      handle_metadata_(handle_metadata),
      num_handles_(num_handles) {}

WireDecoder::~WireDecoder() = default;

}  // namespace fidl::internal
