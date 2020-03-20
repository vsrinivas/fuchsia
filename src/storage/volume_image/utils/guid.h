// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_VOLUME_IMAGE_UTILS_GUID_H_
#define SRC_STORAGE_VOLUME_IMAGE_UTILS_GUID_H_

#include <lib/fit/result.h>

#include <array>
#include <cstdint>
#include <string_view>

#include <fbl/span.h>

namespace storage::volume_image {

// Size in bytes of a GUID.
constexpr uint8_t kGuidLength = 16;

// Number of characters that String representation of GUID have.
// AAAAAAAA-BBBB-CCCC-DDDD-EEEEEEEEEEEE
constexpr uint8_t kGuidSeparatorCount = 4;
constexpr uint8_t kGuidCharactersPerByte = 2;
constexpr uint8_t kGuidStrLength = kGuidLength * kGuidCharactersPerByte + kGuidSeparatorCount;

struct Guid {
  // Returns a string containing the representation of |guid|.
  //
  // On error returns a string describing the error condition.
  static fit::result<std::string, std::string> ToString(fbl::Span<const uint8_t> guid);

  // Returns an array containing the byte representation of |guid|.
  //
  // On error returns a string describing the error condition.
  static fit::result<std::array<uint8_t, kGuidLength>, std::string> FromString(
      fbl::Span<const char> guid);
};

}  // namespace storage::volume_image

#endif  // SRC_STORAGE_VOLUME_IMAGE_UTILS_GUID_H_
