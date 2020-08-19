// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ZBITL_MEMORY_H_
#define LIB_ZBITL_MEMORY_H_

#include <fbl/array.h>
#include <fbl/span.h>

#include "storage_traits.h"

namespace zbitl {

// fbl::Array<T> works like std::span<T> + std::unique_ptr<T[]>.

template <typename T>
class StorageTraits<fbl::Array<T>> {
 public:
  using Storage = fbl::Array<T>;

  struct error_type {};

  using payload_type = fbl::Span<T>;

  static fbl::Span<std::byte> AsBytes(const Storage& zbi) {
    return {reinterpret_cast<std::byte*>(zbi.data()), zbi.size() * sizeof(T)};
  }

  static fitx::result<error_type, uint32_t> Capacity(Storage& zbi) {
    return fitx::ok(static_cast<uint32_t>(AsBytes(zbi).size()));
  }

  static fitx::result<error_type, std::reference_wrapper<const zbi_header_t>> Header(
      Storage& zbi, uint32_t offset) {
    return fitx::ok(std::ref(*reinterpret_cast<const zbi_header_t*>(
        AsBytes(zbi).subspan(offset, sizeof(zbi_header_t)).data())));
  }

  static fitx::result<error_type, payload_type> Payload(Storage& zbi, uint32_t offset,
                                                        uint32_t length) {
    auto payload = AsBytes(zbi).subspan(offset, length);
    ZX_DEBUG_ASSERT(payload.size() == length);
    ZX_ASSERT_MSG(payload.size() % sizeof(T) == 0,
                  "payload size not a multiple of storage fbl::Array element_type size");
    return fitx::ok(payload_type{reinterpret_cast<T*>(payload.data()), payload.size() / sizeof(T)});
  }

  template <typename Callback>
  static auto Read(Storage& zbi, payload_type payload, uint32_t length, Callback&& callback)
      -> fitx::result<error_type, decltype(callback(ByteView{}))> {
    auto payload_bytes = fbl::as_bytes(payload);
    ByteView bytes{payload_bytes.data(), payload_bytes.size()};
    ZX_DEBUG_ASSERT(bytes.size() == length);
    return fitx::ok(callback(bytes));
  }

  static fitx::result<error_type> Write(Storage& zbi, uint32_t offset, ByteView data) {
    // The caller is supposed to maintain these invariants.
    ZX_DEBUG_ASSERT(offset <= AsBytes(zbi).size());
    ZX_DEBUG_ASSERT(data.size() <= AsBytes(zbi).size() - offset);
    memcpy(&AsBytes(zbi)[offset], data.data(), data.size());
    return fitx::ok();
  }
};

}  // namespace zbitl

#endif  // LIB_ZBITL_MEMORY_H_
