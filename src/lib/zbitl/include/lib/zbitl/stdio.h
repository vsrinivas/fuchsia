// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_ZBITL_INCLUDE_LIB_ZBITL_STDIO_H_
#define SRC_LIB_ZBITL_INCLUDE_LIB_ZBITL_STDIO_H_

#include <sys/types.h>  // off_t

#include <cstdio>
#include <string>

#include <fbl/unique_fd.h>

#include "storage-traits.h"

namespace zbitl {

template <>
class StorageTraits<FILE*> {
 public:
  /// File I/O errors are represented by an errno value.
  using error_type = int;

  /// Offset into file where the ZBI item payload begins.
  using payload_type = decltype(ftell(nullptr));

  static std::string error_string(error_type error) { return strerror(error); }

  static fit::result<error_type, uint32_t> Capacity(FILE* f);

  static fit::result<error_type> EnsureCapacity(FILE* f, uint32_t capacity_bytes);

  static fit::result<error_type, zbi_header_t> Header(FILE* f, uint32_t offset);

  static fit::result<error_type, payload_type> Payload(FILE* f, uint32_t offset, uint32_t length) {
    return fit::ok(offset);
  }

  static fit::result<error_type> Read(FILE* f, payload_type payload, void* buffer, uint32_t length);

  template <typename Callback>
  static auto Read(FILE* f, payload_type payload, uint32_t length, Callback&& callback)
      -> fit::result<error_type, decltype(callback(ByteView{}))> {
    std::optional<decltype(callback(ByteView{}))> result;
    auto cb = [&](ByteView chunk) -> bool {
      result = callback(chunk);
      return result->is_ok();
    };
    using CbType = decltype(cb);
    if (auto read_error = DoRead(
            f, payload, length,
            [](void* cb, ByteView chunk) { return (*static_cast<CbType*>(cb))(chunk); }, &cb);
        read_error.is_error()) {
      return fit::error{read_error.error_value()};
    } else {
      ZX_DEBUG_ASSERT(result);
      return fit::ok(*result);
    }
  }

  static fit::result<error_type, uint32_t> Crc32(FILE* f, uint32_t offset, uint32_t length);

  static fit::result<error_type> Write(FILE* f, uint32_t offset, ByteView data);

 private:
  static fit::result<error_type> DoRead(FILE* f, payload_type offset, uint32_t length,
                                        bool (*)(void*, ByteView), void*);
};

}  // namespace zbitl

#endif  // SRC_LIB_ZBITL_INCLUDE_LIB_ZBITL_STDIO_H_
