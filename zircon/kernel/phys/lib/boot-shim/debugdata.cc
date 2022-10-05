// Copyright 2022 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "lib/boot-shim/debugdata.h"

#include <zircon/assert.h>
#include <zircon/boot/image.h>

#include <limits>

namespace boot_shim {

size_t DebugdataItem::payload_size_bytes() const {
  size_t size = content_size_;
  if (size > 0 || !log_.empty()) {
    for (std::string_view str : {sink_name_, vmo_name_, vmo_name_suffix_, log_}) {
      size += str.size();
    }
    size = ZBI_ALIGN(static_cast<uint32_t>(size)) + sizeof(zbi_debugdata_t);
  }
  return size;
}

auto DebugdataItem::AppendItems(DataZbi& zbi) -> fit::result<DataZbi::Error> {
  const uint32_t size = static_cast<uint32_t>(payload_size_bytes());
  if (size == 0) {
    return fit::ok();
  }

  WritableBytes payload;
  if (auto result = zbi.Append({.type = ZBI_TYPE_DEBUGDATA, .length = size}); result.is_ok()) {
    payload = result.value()->payload;
  } else {
    return result.take_error();
  }
  ZX_ASSERT(payload.size_bytes() >= size);

  contents_ = payload.data();
  size_t used_size = content_size_;
  payload = payload.subspan(content_size_);

  for (std::string_view str : {sink_name_, vmo_name_, vmo_name_suffix_, log_}) {
    used_size += str.size();
    payload = payload.subspan(str.copy(reinterpret_cast<char*>(payload.data()), payload.size()));
  }

  // Skip over any needed alignment padding.
  payload = payload.subspan(ZBI_ALIGN(static_cast<uint32_t>(used_size)) - used_size);

  ZX_ASSERT_MSG(payload.size_bytes() >= sizeof(zbi_debugdata_t),
                "%zu bytes left for %zu-byte trailer", payload.size_bytes(),
                sizeof(zbi_debugdata_t));
  *reinterpret_cast<zbi_debugdata_t*>(payload.data()) = {
      .content_size = static_cast<uint32_t>(content_size_),
      .sink_name_size = static_cast<uint32_t>(sink_name_.size()),
      .vmo_name_size = static_cast<uint32_t>(vmo_name_.size() + vmo_name_suffix_.size()),
      .log_size = static_cast<uint32_t>(log_.size()),
  };

  return fit::ok();
}

}  // namespace boot_shim
