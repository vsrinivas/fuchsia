// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <cstdint>

namespace debugserver {
namespace util {

// Decodes the 2 character ASCII string |hex| and returns the result in
// |out_byte|. Returns false if |hex| contains invalid characters.
bool DecodeByteString(const uint8_t hex[2], uint8_t* out_byte);

// Encodes |byte| into a 2 character ASCII string and returns the result in
// |out_hex|.
void EncodeByteString(uint8_t byte, uint8_t out_hex[2]);

}  // namespace util
}  // namespace debugserver
