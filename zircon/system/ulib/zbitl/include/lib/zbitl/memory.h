// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ZBITL_MEMORY_H_
#define LIB_ZBITL_MEMORY_H_

#include <cstring>

#include <fbl/alloc_checker.h>
#include <fbl/array.h>
#include <fbl/span.h>

#include "storage_traits.h"

namespace zbitl {

// fbl::Array<T> works like std::span<T> + std::unique_ptr<T[]>.

template <typename T>
class StorageTraits<fbl::Array<T>> {
 public:
  using Storage = fbl::Array<T>;

  // An instance represents a failure mode of being out of memory.
  struct error_type {};

  using payload_type = fbl::Span<T>;

  static std::string_view error_string(error_type error) { return "out of memory"; }

  static fbl::Span<std::byte> AsBytes(const Storage& storage) {
    return {reinterpret_cast<std::byte*>(storage.data()), storage.size() * sizeof(T)};
  }

  static fitx::result<error_type, uint32_t> Capacity(const Storage& storage) {
    return fitx::ok(static_cast<uint32_t>(AsBytes(storage).size()));
  }

  static fitx::result<error_type> EnsureCapacity(Storage& storage, uint32_t capacity_bytes) {
    if (size_t current = AsBytes(storage).size(); current < capacity_bytes) {
      const size_t n = (capacity_bytes + sizeof(T) - 1) / sizeof(T);

      fbl::AllocChecker ac;
      Storage new_storage(new (&ac) T[n], n);
      if (!ac.check()) {
        return fitx::error{error_type{}};
      }

      memcpy(new_storage.data(), storage.data(), current);
      storage.swap(new_storage);
    }
    return fitx::ok();
  }

  static fitx::result<error_type, std::reference_wrapper<const zbi_header_t>> Header(
      const Storage& storage, uint32_t offset) {
    return fitx::ok(std::ref(*reinterpret_cast<const zbi_header_t*>(
        AsBytes(storage).subspan(offset, sizeof(zbi_header_t)).data())));
  }

  static fitx::result<error_type, payload_type> Payload(const Storage& storage, uint32_t offset,
                                                        uint32_t length) {
    auto payload = AsBytes(storage).subspan(offset, length);
    ZX_DEBUG_ASSERT(payload.size() == length);
    ZX_ASSERT_MSG(payload.size() % sizeof(T) == 0,
                  "payload size not a multiple of storage fbl::Array element_type size");
    return fitx::ok(payload_type{reinterpret_cast<T*>(payload.data()), payload.size() / sizeof(T)});
  }

  static fitx::result<error_type, ByteView> Read(Storage& zbi, payload_type payload,
                                                 uint32_t length) {
    auto payload_bytes = fbl::as_bytes(payload);
    ByteView bytes{payload_bytes.data(), payload_bytes.size()};
    ZX_DEBUG_ASSERT(bytes.size() == length);
    return fitx::ok(bytes);
  }

  template <typename S = T, typename = std::enable_if_t<!std::is_const_v<S>>>
  static fitx::result<error_type> Write(Storage& zbi, uint32_t offset, ByteView data) {
    memcpy(Write(zbi, offset, static_cast<uint32_t>(data.size())).value(), data.data(),
           data.size());
    return fitx::ok();
  }

  template <typename S = T, typename = std::enable_if_t<!std::is_const_v<S>>>
  static fitx::result<error_type, void*> Write(Storage& zbi, uint32_t offset, uint32_t length) {
    // The caller is supposed to maintain these invariants.
    ZX_DEBUG_ASSERT(offset <= AsBytes(zbi).size());
    ZX_DEBUG_ASSERT(length <= AsBytes(zbi).size() - offset);
    return fitx::ok(&AsBytes(zbi)[offset]);
  }

  static fitx::result<error_type, Storage> Create(Storage& old, size_t size) {
    const size_t n = (size + sizeof(T) - 1) / sizeof(T);
    fbl::AllocChecker ac;
    Storage new_storage(new (&ac) T[n], n);
    if (!ac.check()) {
      return fitx::error{error_type{}};
    }
    return fitx::ok(std::move(new_storage));
  }
};

}  // namespace zbitl

#endif  // LIB_ZBITL_MEMORY_H_
