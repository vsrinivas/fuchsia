// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zbitl/view.h>

namespace zbitl {

using namespace std::literals;

fitx::result<std::string_view> ZbiTraits::CheckItemHeader(const zbi_header_t& header) {
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

fitx::result<std::string_view> ZbiTraits::CheckContainerHeader(const zbi_header_t& header) {
  if (auto result = CheckItemHeader(header); result.is_error()) {
    return result.take_error();
  }
  if (header.type != ZBI_TYPE_CONTAINER) {
    return fitx::error("bad container type"sv);
  }
  if (header.extra != ZBI_CONTAINER_MAGIC) {
    return fitx::error("bad container magic"sv);
  }
  if (header.flags & ZBI_FLAG_CRC32) {
    return fitx::error("container header has CRC32 flag"sv);
  }
  if (header.length % ZBI_ALIGNMENT != 0) {
    return fitx::error("container header has misaligned length"sv);
  }
  return fitx::ok();
}

}  // namespace zbitl
