// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_ZBITL_INCLUDE_LIB_ZBITL_MEMORY_H_
#define SRC_LIB_ZBITL_INCLUDE_LIB_ZBITL_MEMORY_H_

#include <lib/stdcompat/span.h>
#include <zircon/assert.h>

#include <cstring>

#include <fbl/alloc_checker.h>
#include <fbl/array.h>

#include "storage-traits.h"

namespace zbitl {

// fbl::Array<T> works like std::span<T> + std::unique_ptr<T[]>.
template <typename T>
class StorageTraits<fbl::Array<T>> {
 public:
  using Storage = fbl::Array<T>;
  using SpanTraits = StorageTraits<cpp20::span<T>>;

  // An instance represents a failure mode of being out of memory.
  struct error_type {};

  using payload_type = cpp20::span<T>;

  static std::string_view error_string(error_type error) { return "out of memory"; }

  static fit::result<error_type, uint32_t> Capacity(const Storage& storage) {
    auto span = AsSpan<T>(storage);
    return SpanTraits::Capacity(span).take_value();
  }

  static fit::result<error_type> EnsureCapacity(Storage& storage, uint32_t capacity_bytes) {
    if (size_t current = AsBytes(storage).size(); current < capacity_bytes) {
      const size_t n = (capacity_bytes + sizeof(T) - 1) / sizeof(T);

      fbl::AllocChecker ac;
      Storage new_storage(new (&ac) T[n], n);
      if (!ac.check()) {
        return fit::error{error_type{}};
      }
      if (current) {
        memcpy(new_storage.data(), storage.data(), current);
      }
      storage.swap(new_storage);
    }
    return fit::ok();
  }

  static fit::result<error_type, payload_type> Payload(const Storage& storage, uint32_t offset,
                                                       uint32_t length) {
    auto span = AsSpan<T>(storage);
    return SpanTraits::Payload(span, offset, length).take_value();
  }

  template <typename U, bool LowLocality>
  static std::enable_if_t<(alignof(U) <= kStorageAlignment),
                          fit::result<error_type, cpp20::span<const U>>>
  Read(const Storage& storage, payload_type payload, uint32_t length) {
    auto span = AsSpan<T>(storage);
    return SpanTraits::template Read<U, LowLocality>(span, payload, length).take_value();
  }

  template <typename S = T, typename = std::enable_if_t<!std::is_const_v<S>>>
  static fit::result<error_type> Write(Storage& storage, uint32_t offset, ByteView data) {
    auto span = AsSpan<T>(storage);
    auto result = SpanTraits::Write(span, offset, data);
    ZX_DEBUG_ASSERT(result.is_ok());
    return fit::ok();
  }

  template <typename S = T, typename = std::enable_if_t<!std::is_const_v<S>>>
  static fit::result<error_type, void*> Write(Storage& storage, uint32_t offset, uint32_t length) {
    auto span = AsSpan<T>(storage);
    return SpanTraits::Write(span, offset, length).take_value();
  }

  static fit::result<error_type, Storage> Create(Storage& old, uint32_t size,
                                                 uint32_t initial_zero_size) {
    const size_t n = (size + sizeof(T) - 1) / sizeof(T);
    fbl::AllocChecker ac;
    Storage new_storage(new (&ac) T[n], n);
    if (!ac.check()) {
      return fit::error{error_type{}};
    }
    if (initial_zero_size) {
      ZX_DEBUG_ASSERT(initial_zero_size <= size);
      memset(new_storage.data(), 0, initial_zero_size);
    }
    return fit::ok(std::move(new_storage));
  }
};

}  // namespace zbitl

#endif  // SRC_LIB_ZBITL_INCLUDE_LIB_ZBITL_MEMORY_H_
