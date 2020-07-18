// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ZBITL_STDIO_H_
#define LIB_ZBITL_STDIO_H_

#include <sys/types.h>  // off_t

#include <cstdio>

#include <fbl/unique_fd.h>

#include "storage_traits.h"

namespace zbitl {

template <>
struct StorageTraits<FILE*> {
  /// File I/O errors are represented by an errno value.
  using error_type = int;

  /// Offset into file where the ZBI item payload begins.
  using payload_type = decltype(ftell(nullptr));

  static fitx::result<error_type, uint32_t> Capacity(FILE*);

  static fitx::result<error_type, zbi_header_t> Header(FILE*, uint32_t offset);

  static fitx::result<error_type, payload_type> Payload(FILE*, uint32_t offset, uint32_t length) {
    return fitx::ok(offset);
  }

  static fitx::result<error_type, uint32_t> Crc32(FILE*, uint32_t offset, uint32_t length);

  static fitx::result<error_type> Write(FILE*, uint32_t offset, ByteView data);
};

}  // namespace zbitl

#endif  // LIB_ZBITL_STDIO_H_
