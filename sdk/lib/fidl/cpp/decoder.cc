// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/fidl/cpp/decoder.h"

#include <utility>

#include "lib/fidl/cpp/message.h"

namespace fidl {

Decoder::Decoder(HLCPPIncomingMessage message)
    : body_(std::move(message.body_view())), body_offset_(sizeof(fidl_message_header_t)) {}

Decoder::Decoder(HLCPPIncomingBody body) : body_(std::move(body)) {}

Decoder::~Decoder() = default;

}  // namespace fidl
