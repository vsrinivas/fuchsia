// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fidl/cpp/message.h>
#include <lib/fidl/cpp/natural_decoder.h>

#include <utility>

namespace fidl::internal {

NaturalDecoder::NaturalDecoder(fidl::IncomingMessage message)
    : body_(std::move(message)),
      body_offset_(message.is_transactional() ? sizeof(fidl_message_header_t) : 0) {}

NaturalDecoder::~NaturalDecoder() = default;

}  // namespace fidl::internal
