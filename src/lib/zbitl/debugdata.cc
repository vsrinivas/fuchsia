// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/zbitl/items/debugdata.h"

#include <zircon/boot/image.h>

#include <string_view>
#include <type_traits>

namespace zbitl {
namespace {

constexpr std::string_view kBadTrailer = "ZBI_TYPE_DEBUGDATA item too small for debugdata trailer";
constexpr std::string_view kBadContents = "ZBI_TYPE_DEBUGDATA item too small for content size";
constexpr std::string_view kBadSinkName = "ZBI_TYPE_DEBUGDATA item too small for data-sink name";
constexpr std::string_view kBadVmoName = "ZBI_TYPE_DEBUGDATA item too small for VMO name";
constexpr std::string_view kBadLog = "ZBI_TYPE_DEBUGDATA item too small for log text";
constexpr std::string_view kBadAlign = "ZBI_TYPE_DEBUGDATA item size not aligned";
constexpr std::string_view kBadSize = "ZBI_TYPE_DEBUGDATA item too large for encoded sizes";

}  // namespace

fit::result<std::string_view> Debugdata::Init(cpp20::span<const std::byte> payload) {
  if (payload.size_bytes() < sizeof(zbi_debugdata_t)) {
    return fit::error{kBadTrailer};
  }

  if (payload.size_bytes() % ZBI_ALIGNMENT != 0) {
    return fit::error{kBadAlign};
  }

  const zbi_debugdata_t& header = *reinterpret_cast<const zbi_debugdata_t*>(
      payload.subspan(payload.size() - sizeof(zbi_debugdata_t), sizeof(zbi_debugdata_t)).data());
  payload = payload.subspan(0, payload.size() - sizeof(zbi_debugdata_t));

  auto get = [&payload](auto& result, size_t size,
                        std::string_view bad_size) -> fit::result<std::string_view> {
    using Byte = std::decay_t<decltype(result.front())>;
    if (size > payload.size_bytes()) {
      return fit::error{bad_size};
    }
    result = {reinterpret_cast<const Byte*>(payload.data()), size};
    payload = payload.subspan(size);
    return fit::ok();
  };

  auto result = get(contents_, header.content_size, kBadContents);
  if (result.is_ok()) {
    result = get(sink_name_, header.sink_name_size, kBadSinkName);
  }
  if (result.is_ok()) {
    result = get(vmo_name_, header.vmo_name_size, kBadVmoName);
  }
  if (result.is_ok()) {
    result = get(log_, header.log_size, kBadLog);
  }

  if (result.is_ok() && payload.size_bytes() >= ZBI_ALIGNMENT) {
    return fit::error{kBadSize};
  }

  return result;
}

}  // namespace zbitl
