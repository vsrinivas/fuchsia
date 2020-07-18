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

  static fitx::result<error_type, uint32_t> Crc32(const zx::vmo&, uint32_t offset, uint32_t length);
};

}  // namespace zbitl

#endif  // LIB_ZBITL_VMO_H_
