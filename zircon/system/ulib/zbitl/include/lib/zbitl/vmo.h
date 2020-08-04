// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ZBITL_VMO_H_
#define LIB_ZBITL_VMO_H_

#include <lib/zx/vmo.h>

#include <utility>

#include "storage_traits.h"

namespace zbitl {

template <>
struct StorageTraits<zx::vmo> {
 public:
  /// Errors from zx::vmo calls.
  using error_type = zx_status_t;

  /// Offset into the VMO where the ZBI item payload begins.
  using payload_type = uint64_t;

  static fitx::result<error_type, uint32_t> Capacity(const zx::vmo&);

  static fitx::result<error_type, zbi_header_t> Header(const zx::vmo&, uint32_t offset);

  static fitx::result<error_type, payload_type> Payload(const zx::vmo&, uint32_t offset,
                                                        uint32_t length) {
    return fitx::ok(offset);
  }

  template <typename Callback>
  static auto Read(const zx::vmo& zbi, payload_type payload, uint32_t length, Callback&& callback)
      -> fitx::result<error_type, decltype(callback(ByteView{}))> {
    decltype(callback(ByteView{})) result = fitx::ok();
    auto cb = [&](ByteView chunk) -> bool {
      result = callback(chunk);
      return result.is_ok();
    };
    using CbType = decltype(cb);
    if (auto read_error = DoRead(
            zbi, payload, length,
            [](void* cb, ByteView chunk) { return (*static_cast<CbType*>(cb))(chunk); }, &cb);
        read_error.is_error()) {
      return fitx::error{read_error.error_value()};
    } else {
      return fitx::ok(result);
    }
  }

  static fitx::result<error_type> Write(const zx::vmo&, uint32_t offset, ByteView);

 private:
  static fitx::result<error_type> DoRead(const zx::vmo& zbi, uint64_t offset, uint32_t length,
                                         bool (*)(void*, ByteView), void*);
};

}  // namespace zbitl

#endif  // LIB_ZBITL_VMO_H_
