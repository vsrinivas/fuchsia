// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zbitl/checking.h>

namespace zbitl {

using namespace std::literals;

fitx::result<std::string_view> CheckHeader(const zbi_header_t& header) {
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
