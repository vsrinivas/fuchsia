// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fidl/cpp/natural_decoder.h>
#include <lib/fidl/cpp/wire/message.h>

#include <utility>

namespace fidl::internal {

NaturalDecoder::NaturalDecoder(fidl::EncodedMessage message,
                               fidl::internal::WireFormatVersion wire_format_version)
    : body_(std::move(message)), wire_format_version_(wire_format_version) {}

NaturalDecoder::~NaturalDecoder() = default;

}  // namespace fidl::internal
