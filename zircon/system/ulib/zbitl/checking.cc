// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zbitl/checking.h>

namespace zbitl {

using namespace std::literals;

template <>
fitx::result<std::string_view> CheckHeader<Checking::kPermissive>(const zbi_header_t& header,
                                                                  size_t capacity) {
  // Permissive mode only checks things that break the structural navigation.
  if (header.length > capacity) {
    return fitx::error{"item doesn't fit, container truncated?"sv};
  }

  return fitx::ok();
}

template <>
fitx::result<std::string_view> CheckHeader<Checking::kStrict>(const zbi_header_t& header,
                                                              size_t capacity) {
  if (auto result = CheckHeader<Checking::kPermissive>(header, capacity); result.is_error()) {
    return result;
  }

  // Strict mode also checks policy requirements.  Boot loaders do not always
  // bother with setting the fields correctly, but the kernel need not care.
  if (header.magic != ZBI_ITEM_MAGIC) {
    return fitx::error{"bad item magic number"sv};
  }
  if (!(header.flags & ZBI_FLAG_VERSION)) {
    return fitx::error{"bad item header version"sv};
  }
  if (!(header.flags & ZBI_FLAG_CRC32) && header.crc32 != ZBI_ITEM_NO_CRC32) {
    return fitx::error{"bad crc32 field in item without CRC"sv};
  }

  return fitx::ok();
}

}  // namespace zbitl
