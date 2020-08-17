// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/fidl/cpp/decoder.h"

#include <utility>

namespace fidl {

Decoder::Decoder(Message message) : message_(std::move(message)) {}

Decoder::~Decoder() = default;

}  // namespace fidl
