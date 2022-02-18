// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fidl/cpp/message.h>
#include <lib/fidl/cpp/natural_decoder.h>

#include <utility>

namespace fidl::internal {

NaturalDecoder::NaturalDecoder(HLCPPIncomingMessage message)
    : body_(std::move(message.body_view())), body_offset_(sizeof(fidl_message_header_t)) {}

NaturalDecoder::NaturalDecoder(HLCPPIncomingBody body) : body_(std::move(body)) {}

NaturalDecoder::~NaturalDecoder() = default;

}  // namespace fidl::internal
